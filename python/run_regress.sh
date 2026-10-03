#!/bin/sh
# run_regress.sh - tizix 回帰スイート一括実行
#
#   使い方(tizix のルートから):
#       sh python/run_regress.sh            … 現在のビルドでテストだけ回す
#       sh python/run_regress.sh --build    … make してから回す
#       sh python/run_regress.sh --clean    … make cleandisk(mkfs からやり直し)してから回す
#
#   各テストのログは build/regress/<YYYYMMDD>/<name>.log(日付で古さを判断して捨てる)。
#   最後に PASS/FAIL の一覧と総合結果を出す(全部 PASS で終了コード 0)。
set -u

TIZIX=${TIZIX_ROOT:-$(pwd)}
# #73: ./configure が書いた config.mk があれば、ツールの場所を PATH に足す
#   (ここでは make -C arch/… や tzcc を直に呼ぶので、ルート Makefile の
#   -include config.mk を通らない)。
if [ -f "$TIZIX/config.mk" ]; then
    TZ_EXTRA_PATH=$(sed -n 's/^TZ_EXTRA_PATH *= *//p' "$TIZIX/config.mk")
    [ -n "$TZ_EXTRA_PATH" ] && PATH=$TZ_EXTRA_PATH:$PATH && export PATH
    SDCC_BIN=$(sed -n 's/^SDCC_BIN *= *//p' "$TIZIX/config.mk")
    [ -n "$SDCC_BIN" ] && export SDCC_BIN
fi
LOGDIR=$TIZIX/build/regress/$(date +%Y%m%d)
mkdir -p "$LOGDIR"

case "${1:-}" in
    # ★cleandisk は driveb.dsk(FAT 側)しか作り直さない。**カーネルが載るのは
    #   drivea.dsk** で、そちらは `all: $(DISK) $(FATDSK)` の $(DISK) 側。
    #   cleandisk だけだと kernel.ihx は再ビルドされるのに起動ディスクへ
    #   書かれず、**直したはずのカーネルが載っていない**状態でテストが走る
    #   (#38 でこれに嵌まり、修正済みのはずのバグを追いかけた)。必ず make も回す。
    --clean) echo "=== make cleandisk + make ==="
             make -C "$TIZIX" cleandisk || exit 1
             make -C "$TIZIX"           || exit 1 ;;
    --build) echo "=== make ===";           make -C "$TIZIX" || exit 1 ;;
esac

# cpmsim を使う pty テスト。順序は「速いもの・素性の確かなものから」。
TESTS="test_cmds_all test_pwd_cd test_vfs_step8 test_vfs_step9 test_vfs_step10 \
       test_5b_pipe test_sed test_dev_dd test_spawn test_ovl test_xblk test_vi \
       test_sh_hist test_ls_format test_pipe_kill test_5a_waitwake test_calli test_args test_rx test_rx_cksum"

rc=0
echo "=== regression ==="
for t in $TESTS; do
    printf '%-18s ' "$t"
    if timeout 400 python3 "$TIZIX/python/$t.py" > "$LOGDIR/$t.log" 2>&1; then
        echo "PASS"
    else
        echo "FAIL  ($LOGDIR/$t.log)"
        grep '^\[FAIL' "$LOGDIR/$t.log" | head -5 | sed 's/^/      /'
        rc=1
    fi
done

# ゲスト内では中身を突き合わせられないので、生ブロックデバイスの読み出しは
# ホスト側でイメージとバイト比較する(test_dev_dd の後に走らせること)。
echo "=== /dev raw read: host byte-compare ==="
if sh "$TIZIX/python/run_dev_dd_check.sh"; then :; else rc=1; fi

# DRIVER.BIN は 0x9000 から 4096B しか載らない。溢れてもリンクは通り、
# 実行時に末尾の関数へ飛んだ瞬間に落ちる ── ビルドの度に見ておく。
echo "=== driver size guard (l__CODE must be <= 0x1000) ==="
DMAP="$TIZIX/build/arch/z80pack/user/driver.map"
if [ -f "$DMAP" ]; then
    L=$(awk '$2=="l__CODE"{print $1}' "$DMAP")
    if [ -n "$L" ] && [ "$((0x$L))" -le 4096 ]; then
        echo "  l__CODE=0x$L  OK"
    else
        echo "  l__CODE=0x$L  OVER 4096 -- drv_printf 等が載らず sh が無言でハングする"
        rc=1
    fi
else
    echo "  (driver.map が無い。make してから)"
fi
# #87: z80board は block1 の末尾 0x9F00-0x9FFF を FT245 受信リングに使うので、
# DRIVER は 0xF00 未満でなければならない(越えると受信バイトが DRIVER のコードを壊す)。
ZDMAP="$TIZIX/build/arch/z80board/user/driver.map"
if [ -f "$ZDMAP" ]; then
    L=$(awk '$2=="l__CODE"{print $1}' "$ZDMAP")
    if [ -n "$L" ] && [ "$((0x$L))" -le 3840 ]; then
        echo "  z80board l__CODE=0x$L  OK (<= 0xF00)"
    else
        echo "  z80board l__CODE=0x$L  OVER 0xF00 -- 0x9F00 からの受信リングと重なる"
        rc=1
    fi
fi

# 共有 src/ を少し太らせると、**z80pack より先に z80board の ROM(32KB)が溢れる**
# (z80board は SD/SPI ドライバをカーネルに持つぶん約 1.1KB 重い。task.md #71)。
# 上のテストは z80pack でしか回らないので、ここでカーネル ROM だけ作って見張る。
# user/ のビルドは要らない(boot.rom は kernel.ihx だけに依存)。
echo "=== z80board ROM guard (kernel must fit 0x8000) ==="
if OUT=$(make -C "$TIZIX/arch/z80board" boot.rom 2>&1); then
    # boot.rom が最新だと make は何も言わないので、空きは kernel.ihx から毎回数える
    # (空きが十数バイトしか無い時期があるので、毎回見えるようにしておく)。
    python3 - "$TIZIX/build/arch/z80board/obj/kernel.ihx" <<'PY'
import sys
m = 0
for l in open(sys.argv[1]):
    l = l.strip()
    if l.startswith(":") and l[7:9] == "00":
        m = max(m, int(l[3:7], 16) + int(l[1:3], 16))
print("  ROM image end 0x%04X / ROM limit 0x8000  (free %d B)" % (m, 0x8000 - m))
PY
else
    echo "$OUT" | grep -E "ROM limit|too small|rror" | head -3 | sed 's/^/  /'
    echo "  z80board のカーネルが ROM に入らない(共有 src/ を太らせた?)"
    rc=1
fi

# tzcc 単体の実行テスト(tests/ok_*.c を cpmsim で動かし出力を比較)。tizix の
# コマンドは全部 tzcc でビルドされるので、コンパイラの退行はここで先に見る。
# 以前は誰も回しておらず、cpmsim の場所がずれて全件 FAIL のまま放置されていた(#84)。
echo "=== tzcc runtest ==="
if OUT=$(make -C "$TIZIX/tzcc" runtest 2>&1); then
    echo "$OUT" | grep "^PASS=" | sed 's/^/  /'
else
    echo "$OUT" | grep -E "^PASS=|^failed:|FAIL " | head -6 | sed 's/^/  /'
    rc=1
fi

# z80board(実機の本命)を z80boardsim で。SD / FT245 受信 / ESP(bit-bang UART)の
# 経路は z80pack には無いので、ここで見る。z80boardsim が無ければ飛ばす。
# REGRESS_SKIP_Z80BOARD=1 で省略できる(時間短縮用)。
if [ -x "$TIZIX/arch/z80board/z80boardsim" ] && [ -z "${REGRESS_SKIP_Z80BOARD:-}" ]; then
    echo "=== z80board (z80boardsim) ==="
    if make -C "$TIZIX/arch/z80board" > "$LOGDIR/z80board_make.log" 2>&1; then
        for t in test_cmds_all test_args test_rx test_rx_cksum test_esp_net; do
            printf '  %-16s ' "$t"
            if TIZIX_ARCH=z80board timeout 400 python3 "$TIZIX/python/$t.py" z80board \
                    > "$LOGDIR/zb_$t.log" 2>&1; then
                echo "PASS"
            else
                echo "FAIL  ($LOGDIR/zb_$t.log)"
                rc=1
            fi
        done
    else
        echo "  make ARCH=z80board が失敗($LOGDIR/z80board_make.log)"
        rc=1
    fi
else
    echo "=== z80board (z80boardsim) === SKIP: $([ -n "${REGRESS_SKIP_Z80BOARD:-}" ] && echo "REGRESS_SKIP_Z80BOARD が立っている" || echo "arch/z80board/z80boardsim が無い(make sims)")"
fi

# スロット方式(PLAT_FLAT32)のアーキで回すテスト。m68k-mega と esp32-wroom-32e は
# 共有 src/ の同じコード(kexec・syscall・スケジューラの C 側)を通るので、一覧も 1 つにする。
# z80 と共通のテストで、tzpaths が TIZIX_ARCH を見て各シミュレータを起動する。
#   入れていないもの: test_spawn / test_ovl / test_xblk / test_calli / test_5a_waitwake
#   (z80 の追加ブロック・オーバーレイ・krun_wait・blk/wak を使う。こちらには無い機能)
FLAT32_TESTS="test_args test_ls_format test_ps_m68k test_pwd_cd test_sh_hist \
              test_vfs_step8 test_vfs_step9 test_vfs_step10 test_5b_pipe \
              test_cmds_all test_dev_dd test_sed test_pipe_kill test_vi test_rx test_rx_cksum \
              test_mbox test_lfn test_tzsh"

# m68k-mega(m68ksim)。リリース対象の 3 アーキ目。REGRESS_SKIP_M68K=1 で省略できる。
# m68ksim は下の make が作る(rocket68 のソースがあれば)。以前は「m68ksim が既にある」ことを
# 条件にしていて、新しく clone したツリー(Docker #74 で確認)では区間ごと黙って飛ばしていた。
# 飛ばすときは理由を出す。
M68K_SKIP=
if [ -n "${REGRESS_SKIP_M68K:-}" ]; then
    M68K_SKIP="REGRESS_SKIP_M68K が立っている"
elif ! command -v m68k-elf-gcc > /dev/null 2>&1; then
    M68K_SKIP="m68k-elf-gcc が無い"
elif [ ! -x "$TIZIX/arch/m68k-mega/m68ksim" ] && [ ! -d "$TIZIX/arch/m68k-mega/rocket68/src" ]; then
    M68K_SKIP="m68ksim も rocket68 のソースも無い(arch/m68k-mega/rocket68)"
fi
if [ -n "$M68K_SKIP" ]; then
    echo "=== m68k-mega (m68ksim) === SKIP: $M68K_SKIP"
else
    echo "=== m68k-mega (m68ksim) ==="
    if make -C "$TIZIX/arch/m68k-mega" > "$LOGDIR/m68k_make.log" 2>&1; then
        for t in $FLAT32_TESTS; do
            printf '  %-16s ' "$t"
            if TIZIX_ARCH=m68k-mega timeout 400 python3 "$TIZIX/python/$t.py" m68k-mega \
                    > "$LOGDIR/m68k_$t.log" 2>&1; then
                echo "PASS"
            else
                echo "FAIL  ($LOGDIR/m68k_$t.log)"
                rc=1
            fi
        done
    else
        echo "  make -C arch/m68k-mega が失敗($LOGDIR/m68k_make.log)"
        rc=1
    fi
fi

# esp32-wroom-32e(Espressif QEMU、-machine esp32。#108)。REGRESS_SKIP_ESP32=1 で省略できる。
# m68k-mega と同じ PLAT_FLAT32 のポートなので、同じテストを回す。ツールチェインと QEMU は
# PATH に要る(rocky9 では ~/bin)。無ければ理由を出して飛ばす。
# ★QEMU は境界違反の 32bit アクセスも IRAM へのバイトアクセスも素通しする(実機は例外)。
#   ここが通っても実機の保証にはならない(task.md #108)。
ESP_SKIP=
if [ -n "${REGRESS_SKIP_ESP32:-}" ]; then
    ESP_SKIP="REGRESS_SKIP_ESP32 が立っている"
elif ! command -v xtensa-esp32-elf-gcc > /dev/null 2>&1; then
    ESP_SKIP="xtensa-esp32-elf-gcc が無い"
elif ! command -v qemu-system-xtensa > /dev/null 2>&1; then
    ESP_SKIP="qemu-system-xtensa が無い"
fi
if [ -n "$ESP_SKIP" ]; then
    echo "=== esp32-wroom-32e (QEMU) === SKIP: $ESP_SKIP"
else
    echo "=== esp32-wroom-32e (QEMU) ==="
    if make -C "$TIZIX/arch/esp32-wroom-32e" > "$LOGDIR/esp32_make.log" 2>&1; then
        for t in $FLAT32_TESTS test_esp32d test_multislot; do
            printf '  %-16s ' "$t"
            if TIZIX_ARCH=esp32-wroom-32e timeout 400 python3 "$TIZIX/python/$t.py" esp32-wroom-32e \
                    > "$LOGDIR/esp32_$t.log" 2>&1; then
                echo "PASS"
            else
                echo "FAIL  ($LOGDIR/esp32_$t.log)"
                rc=1
            fi
        done
    else
        echo "  make -C arch/esp32-wroom-32e が失敗($LOGDIR/esp32_make.log)"
        rc=1
    fi
fi

echo "=== result: $([ $rc -eq 0 ] && echo ALL PASS || echo FAIL) ==="
exit $rc
