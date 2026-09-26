#===================================================================
# tizix - external command build (function pointer table version)
#===================================================================
#
#   ビルド生成物は **すべて** $(OBJ) = arch/$(ARCH)/user/ に集約する。
#   user/ にはソース(.c / .h / .s / .py)だけを置き、汚さない。
#   (サブ make は user/ で実行されるので $(OBJ) は相対パス。
#    DRIVEB = ../arch/$(ARCH)/disks/... と同じ流儀)
#   ARCH はルート Makefile から ARCH=$(ARCH) で渡る(単体実行時は既定 z80pack)。
#
#   カーネルの中間物 arch/$(ARCH)/obj/ とは別ディレクトリ。
#   `make clean` は $(OBJ) を丸ごと消す(次ビルドで作り直す)。

# ---- toolchain ----
SDCC    := sdcc
SDAS    := sdasz80
SDLD    := sdldz80
MAKEBIN := makebin
PYTHON  := python3

# ---- ARCH (ルート Makefile から ARCH=$(ARCH) で渡される。単体実行時は既定 z80pack) ----
ARCH ?= z80pack

# ---- 出力ディレクトリ ----
OBJ := ../arch/$(ARCH)/user

# ---- flags ----
# ユーザーコマンドは crt0cmd.s 側のスタック受け渡し仕様との互換性のため --sdcccall 0
# DRIVER.BIN (driver.c) はカーネル (ff.c) のレジスタ引数渡しを叩くためデフォルト (--sdcccall 1)
# gcse/loop/label 最適化は引き続き禁止(素直なコード生成を保つ)。
#
# ★ peephole (--no-peep) は CFLAGS_CMD から外した。
#   理由: peephole は届く範囲の jp を jr へ畳む。jr は相対分岐なので base 非依存 =
#   iy_reg のグルーが 0 バイトで済む。--no-peep を付けると sh.c の分岐は
#   jp 405 / jr 0 になり、その 405 個すべてに 1 個 14-16B のグルーが付いていた
#   (実測 sh.bin 11589B)。peephole を戻すと jp 35 / jr 198 になり、
#   iy_reg の変換数は 492 -> 122、sh.bin は 6507B(3 ブロック -> 2 ブロック)。
#   gcse 等の他の最適化は入れても更なる改善が無かった(jp 35 / jr 192)ので
#   変数を 1 つだけ動かす意味でも据置とする。
#
#   以前 --no-peep が必要だったのは iy_reg が jr を間接化しており、その変換が
#   壊れていたため。今は iy_reg が jr を素通しし、範囲外になった少数だけを
#   iy_jrfix.py が後段で直す(user/iy_reg_claude.py [2] / user/iy_jrfix.py 参照)。
#
# DRIVER.BIN (driver.c) は 0x9000 固定リンクで iy_reg を通さない = 上の話と無関係。
#   #32 で **--no-peep を DRIVER からも外した**。据置していたのは「変数を 2 つ
#   同時に動かさない」ためだけで、正しさの理由は無かった(iy_reg を通さないので
#   jr/jp の話は最初から無関係)。外す理由ができた: DRIVER は 0x9000 から
#   **ちょうど 4096B しかロードされない**(src/kexec.c kload_driver)。/dev 対応で
#   枠を 40B 超え、末尾の drv_printf が載らず sh が無言でハングした。peephole は
#   意味論を変えずにこの分を作る。溢れたかどうかは driver.map の l__CODE が
#   0x1000 以下かで判る(超えると **ビルドは通り、実行時に飛ぶ**ので要注意)。
OPTOFF     := --nogcse --noinvariant --nolabelopt --noloopreverse --nooverlay
CFLAGS_CMD := -mz80 --reserve-regs-iy --sdcccall 0 $(OPTOFF) -S -I../src
CFLAGS_DRV := -mz80 --reserve-regs-iy --sdcccall 0 $(OPTOFF) -S -I../src \
              $(if $(filter z80board,$(ARCH)),-DARCH_Z80BOARD)
LDBASE     := -b _CODE=0x0000

# ---- 固定シンボル（すべてのコマンドで共通） ----
# カーネルベクタ + DRIVER 関数テーブル先頭
FIXED_SYMS := \
    -g _kexit=0x003B \
    -g _getticks=0x0044 \
    -g _time_get=0x004A \
    -g _time_set=0x004D \
    -g ___sdcc_call_hl=0x0050 \
    -g _drv_tbl=0x9000

# ---- commands ----
#   coreutils(cat/cp/ls/echo/head/tail/wc/... 21 本)は #26 で tzcc ビルドへ移行。
#   tizix トップ Makefile の `tzcc-cmds` が tzcc/ の tizixcmds を叩き、
#   arch/z80pack/user/*.bin を直接吐く。ここは sh + 開発スクラッチ + DRIVER のみ。
CRT0     := crt0cmd
#   wifi は z80board 専用(ESP-WROOM-02 へ AT コマンドを 1 行通すだけの小物。
#   cpmsim の port 50/51 には AT パーサが居ないので z80pack では意味が無い)。
COMMANDS := hello a b date_dbg rx cp_dbg test1 t2 blk wak ptx prx sh telnet net nettest tzftp atcli ntpdate \
            $(if $(filter z80board,$(ARCH)),wifi)
BINS := $(addprefix $(OBJ)/,$(addsuffix .bin,$(COMMANDS)))

# ---- コマンド側リンクライブラリ（driver 常駐に依存しない純粋関数）----
#   string.h -> string.rel, stdlib.h -> stdlib.rel（ヘッダ同名対応）。
#   driver 実体 4KB 超過対策で drv_tbl[19..37] から分離。使うコマンドだけがリンクする。
LIBRELS_test1 := $(OBJ)/string.rel $(OBJ)/stdlib.rel

# driveb(FAT) 配置
DRIVEB := ../arch/$(ARCH)/disks/driveb.dsk
MTOOLSRC_FILE := /tmp/tizix_mtoolsrc

.PHONY: all clean install disk reinstall rebuild test-iy
.SECONDARY:

# 途中でコケた recipe の出力（壊れた .bin/.rel など）を必ず消す。
.DELETE_ON_ERROR:

# ---- ターゲット ----
all: test-iy $(BINS) $(OBJ)/driver.bin

# 生成物ディレクトリ(order-only 依存)
$(OBJ):
	mkdir -p $(OBJ)

rebuild:
	$(MAKE) clean
	$(MAKE) all

reinstall:
	$(MAKE) clean
	$(MAKE) install

# driveb.dsk が無ければ初期作成、存在すれば /bin 以下を mcopy -o で上書き転送。
# コマンド類は全部 x:bin/ 、DRIVER.BIN も x:bin/DRIVER.BIN(launch / kload_driver
# はともに /bin から開く)。
install: test-iy $(BINS) $(OBJ)/driver.bin
	@test -f $(DRIVEB) || { $(MAKE) disk; }
	@printf 'drive x: file="%s" exclusive\n' "$(abspath $(DRIVEB))" > $(MTOOLSRC_FILE)
	@MTOOLS_SKIP_CHECK=1 MTOOLSRC=$(MTOOLSRC_FILE) sh -c '\
		mmd x:bin 2>/dev/null || true; mmd x:root 2>/dev/null || true; \
		for f in $(BINS) $(OBJ)/driver.bin; do \
			mcopy -o $$f x:bin/`basename $$f`; \
		done; \
		echo "=== driveb 内容 ==="; mdir x:; mdir x:bin'
	@echo "=== read-back verify (disk == source / Gate C) ==="
	@MTOOLS_SKIP_CHECK=1 MTOOLSRC=$(MTOOLSRC_FILE) sh -c '\
		rc=0; tmp=; \
		for f in $(BINS) $(OBJ)/driver.bin; do \
			bn=`basename $$f`; \
			tmp=$$(mktemp); rm -f $$tmp; \
			mcopy -o -m x:bin/$$bn $$tmp; \
			if cmp -s $$f $$tmp; then echo "  [OK] bin/$$bn == $$f"; \
			else echo "  [NG] bin/$$bn != $$f  (ディスク上の実体がソースと不一致!)"; rc=1; fi; \
		done; \
		rm -f $$tmp; \
		if [ $$rc -ne 0 ]; then echo "*** read-back FAILED ***"; exit 1; fi'
	@echo "installed to $(DRIVEB)"

# disk はディスク初期作成時またはクリーン再作成時のみ使用
disk:
	cd .. && sh arch/$(ARCH)/mkfatdisk.sh

# ---- DRIVER.BIN : block1(0x9000) 常駐 I/O ドライバ ----
# 固定番地(0x9000)リンク、再配置なし。関数ポインタテーブルもここに含む。
$(OBJ)/driver.asm: driver.c | $(OBJ)
	$(SDCC) $(CFLAGS_DRV) -o $@ driver.c

$(OBJ)/driver.rel: $(OBJ)/driver.asm
	$(SDAS) -g -o $@ $<

$(OBJ)/drvvec.rel: drvvec.s | $(OBJ)
	$(SDAS) -o $@ drvvec.s

# kernel.map から DRIVER が -g で解決するカーネルシンボルのアドレスを抽出。
#   f_open/f_close/f_read/f_write/f_lseek/f_sync : FatFs 低レベル
#   vfs_resolve                                  : VFS 一本化(コマンドの FS 操作も VFS 経由)
#   proc_block/proc_wake                         : プロセス wait/wake(drv_tbl[19..20])
#   kdir_open/read/close                         : ディレクトリ反復(drv_tbl[21..23])
#   kfs_mkdir/unlink/rename                       : FS 書込み rm/mkdir/mv(drv_tbl[24..26])
#   kexec_argv                                    : コマンド起動(drv_tbl[27]、外部 sh 用)
#   builtin_try/builtin_is                        : 常駐 builtin 呼び出し(drv_tbl[28..29])
#   redir_begin/redir_end/in_begin/in_end         : > / < リダイレクト(drv_tbl[30..33])
#   pipe_setup/attach_writer/teardown/note_exit   : カーネルパイプ(drv_tbl[34..37])
#   klog_write                                    : rsyslog、/var/log/message 追記(drv_tbl[46])
#   kfs_df                                        : /bin/df の容量(drv_tbl[47]、#56)
# ※ _f_opendir, _f_closedir, _f_readdir などへの部分一致を防ぐため完全一致でマッチ
# ※★ここに並べてよいのは **driver.c / drvvec.s が実際に参照しているシンボルだけ**。
#    sdld の `-g sym=val` は「参照されている未定義シンボルに値を与える」ものなので、
#    誰も参照していない名前を渡すと `No definition of symbol ...` でリンクが落ちる。
#    (#32 で driver.c が vfs_resolve を呼ばなくなった際にこれを踏んだ)
FS_SYMS = $(shell awk '$$2=="__f_open"||$$2=="_f_open"{print "-g _f_open=0x"$$1} $$2=="__f_close"||$$2=="_f_close"{print "-g _f_close=0x"$$1} $$2=="__f_read"||$$2=="_f_read"{print "-g _f_read=0x"$$1} $$2=="__f_write"||$$2=="_f_write"{print "-g _f_write=0x"$$1} $$2=="__f_lseek"||$$2=="_f_lseek"{print "-g _f_lseek=0x"$$1} $$2=="__f_sync"||$$2=="_f_sync"{print "-g _f_sync=0x"$$1} $$2=="__proc_block"||$$2=="_proc_block"{print "-g _proc_block=0x"$$1} $$2=="__proc_wake"||$$2=="_proc_wake"{print "-g _proc_wake=0x"$$1} $$2=="__kdir_open"||$$2=="_kdir_open"{print "-g _kdir_open=0x"$$1} $$2=="__kdir_read"||$$2=="_kdir_read"{print "-g _kdir_read=0x"$$1} $$2=="__kdir_close"||$$2=="_kdir_close"{print "-g _kdir_close=0x"$$1} $$2=="__kfs_mkdir"||$$2=="_kfs_mkdir"{print "-g _kfs_mkdir=0x"$$1} $$2=="__kfs_unlink"||$$2=="_kfs_unlink"{print "-g _kfs_unlink=0x"$$1} $$2=="__kfs_rename"||$$2=="_kfs_rename"{print "-g _kfs_rename=0x"$$1} $$2=="_kexec_argv"{print "-g _kexec_argv=0x"$$1} $$2=="_builtin_try"{print "-g _builtin_try=0x"$$1} $$2=="_builtin_is"{print "-g _builtin_is=0x"$$1} $$2=="_redir_begin"{print "-g _redir_begin=0x"$$1} $$2=="_redir_end"{print "-g _redir_end=0x"$$1} $$2=="_in_begin"{print "-g _in_begin=0x"$$1} $$2=="_in_end"{print "-g _in_end=0x"$$1} $$2=="_pipe_setup"{print "-g _pipe_setup=0x"$$1} $$2=="_pipe_attach_writer"{print "-g _pipe_attach_writer=0x"$$1} $$2=="_pipe_teardown"{print "-g _pipe_teardown=0x"$$1} $$2=="_pipe_note_exit"{print "-g _pipe_note_exit=0x"$$1} $$2=="_pipe_ovf"{print "-g _pipe_ovf=0x"$$1} $$2=="_pipe_tail"{print "-g _pipe_tail=0x"$$1} $$2=="_krun_pipe"{print "-g _krun_pipe=0x"$$1} $$2=="_kdir_size"{print "-g _kdir_size=0x"$$1} $$2=="_kpath"{print "-g _kpath=0x"$$1} $$2=="_kchdir"{print "-g _kchdir=0x"$$1} $$2=="_kgetcwd"{print "-g _kgetcwd=0x"$$1} $$2=="_con_break"{print "-g _con_break=0x"$$1} $$2=="_con_pending"{print "-g _con_pending=0x"$$1} $$2=="_krun_wait"{print "-g _krun_wait=0x"$$1} $$2=="_klog_write"{print "-g _klog_write=0x"$$1} $$2=="_kfs_df"{print "-g _kfs_df=0x"$$1} $$2=="__kdev_open"||$$2=="_kdev_open"{print "-g _kdev_open=0x"$$1} $$2=="__kdev_stream"||$$2=="_kdev_stream"{print "-g _kdev_stream=0x"$$1} $$2=="__kdev_seek"||$$2=="_kdev_seek"{print "-g _kdev_seek=0x"$$1} $$2=="__kdev_tell"||$$2=="_kdev_tell"{print "-g _kdev_tell=0x"$$1}' ../arch/$(ARCH)/obj/kernel.map 2>/dev/null)

$(OBJ)/driver.ihx: $(OBJ)/drvvec.rel $(OBJ)/driver.rel Makefile ../arch/$(ARCH)/obj/kernel.map
	$(SDLD) -n -i $@ -b _CODE=0x9000 \
		-g _kputchar=0x003E -g _kgetchar=0x0041 -g _getticks=0x0044 \
		$(FS_SYMS) \
		-m -w $(OBJ)/drvvec.rel $(OBJ)/driver.rel

$(OBJ)/driver.bin: $(OBJ)/driver.ihx
	$(MAKEBIN) -s 49152 $< $(OBJ)/driver.img
	dd if=$(OBJ)/driver.img of=$@ bs=256 skip=144 count=16 2>/dev/null
	rm -f $(OBJ)/driver.img

# ---- crt0 ----
$(OBJ)/$(CRT0).rel: $(CRT0).s | $(OBJ)
	$(SDAS) -o $@ $<

# sh 専用 crt0(SP を top-0x10 に。3 ブロック sh のスタック確保。詳細は crt0sh.s)
$(OBJ)/crt0sh.rel: crt0sh.s | $(OBJ)
	$(SDAS) -o $@ $<

# ---- C -> asm -> object ----
#   sdcc の -o で .asm を $(OBJ)/ へ直接吐かせる(user/ に .asm を残さない)。
#   iy_reg_claude.py の入出力・map も全部 $(OBJ)/ 側。
#   iy_jrfix.py がアセンブルを担当する(単なる $(SDAS) の置き換えではない):
#   iy_reg は jr を素通しするので、挿入を跨いだ jr が ±127 を超えることがある。
#   sdasz80 の Branching Range Exceeded を捕まえ、その jr だけを間接化して
#   再アセンブルする。範囲外以外のエラーはそのまま表示して落ちる。
$(OBJ)/%.rel: %.c | $(OBJ)
	$(SDCC) $(CFLAGS_CMD) -o $(OBJ)/$*.asm $<
	$(PYTHON) iy_reg_claude.py $(OBJ)/$*.asm -o $(OBJ)/$*.iy.asm --map $(OBJ)/$*.iymap
	$(PYTHON) iy_jrfix.py $(OBJ)/$*.iy.asm $@ --as $(SDAS)

# ---- コマンド側 libc: 手書き PIC アセンブラ（iy_reg を通さない）----
#   string.h -> string.rel (lstr.s) / stdlib.h -> stdlib.rel (lstd.s)
#   jr/djnz のみ・IY/IX 不破壊で位置独立。iy_reg グルー(実測 +94%)を回避。
#   明示ルールなので上の %.rel: %.c パターンより優先される。
$(OBJ)/string.rel: lstr.s | $(OBJ)
	$(SDAS) -o $@ lstr.s

$(OBJ)/stdlib.rel: lstd.s | $(OBJ)
	$(SDAS) -o $@ lstd.s

# ---- link (固定シンボルはすべてのコマンドで共通) ----

# string.rel をリンクするコマンドの明示的ルール
$(OBJ)/test1.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/test1.rel $(LIBRELS_test1) Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/test1.rel $(LIBRELS_test1)

$(OBJ)/t2.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/t2.rel $(LIBRELS_test1) Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/t2.rel $(LIBRELS_test1)

$(OBJ)/sh.ihx: $(OBJ)/crt0sh.rel $(OBJ)/sh.rel $(OBJ)/string.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/crt0sh.rel $(OBJ)/sh.rel $(OBJ)/string.rel

$(OBJ)/ls.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/ls.rel $(OBJ)/getopt.rel $(OBJ)/string.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/ls.rel $(OBJ)/getopt.rel $(OBJ)/string.rel

$(OBJ)/grep.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/grep.rel $(OBJ)/string.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/grep.rel $(OBJ)/string.rel

$(OBJ)/uniq.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/uniq.rel $(OBJ)/string.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/uniq.rel $(OBJ)/string.rel

$(OBJ)/wc.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/wc.rel $(OBJ)/string.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/wc.rel $(OBJ)/string.rel

$(OBJ)/tee.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/tee.rel $(OBJ)/string.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/tee.rel $(OBJ)/string.rel

# telnet: net.bin常駐 + netcli.h 経由(旧: NET_STAT/NET_DATA 直叩きで net.c と
# ポート衝突していたのを書き直し。tzftp/atcli と同じ土台)。
$(OBJ)/telnet.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/telnet.rel $(OBJ)/netcli.rel $(OBJ)/string.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/telnet.rel $(OBJ)/netcli.rel $(OBJ)/string.rel

# net: 中継デーモン。**arch で実体が違う**(バイナリ名は両方 net.bin):
#   z80pack  … net.c    cpmsim の port 50/51(ATD dial-on-demand)を叩く
#   z80board … netesp.c + arch/z80board/espuart.s(#62: SPI → bit-bang UART)
#               ラッチ 0x80 の bit-bang UART(9600bps + CTS)で ESP-WROOM-02 の純正 ESP-AT を話す
#   (memory: tzcc-no-preprocessor-branching / date.c と同じ「明示ターゲットで
#    丸ごと差し替える」流儀。共有 user/*.c 側に #ifdef を増やさない。)
ifeq ($(ARCH),z80board)
NETOBJS := $(OBJ)/netesp.rel $(OBJ)/espuart.rel $(OBJ)/string.rel $(OBJ)/stdlib.rel

# espuart.s は arch 固有なので arch/z80board/ に置く(手書き PIC。iy_reg は通さない)
$(OBJ)/espuart.rel: ../arch/$(ARCH)/espuart.s | $(OBJ)
	$(SDAS) -o $@ $<
else
NETOBJS := $(OBJ)/net.rel
endif

$(OBJ)/net.ihx: $(OBJ)/$(CRT0).rel $(NETOBJS) Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(NETOBJS)

# wifi: ESP へ AT コマンドを 1 行通す(z80board 専用。COMMANDS も条件付き)
$(OBJ)/wifi.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/wifi.rel $(OBJ)/string.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/wifi.rel $(OBJ)/string.rel

# nettest: net.bin/netcli の実証用。受信側の既知バグ(DEVELOP.md 5.6)の
# 再現手段としてしばらく残す。
$(OBJ)/nettest.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/nettest.rel $(OBJ)/netcli.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/nettest.rel $(OBJ)/netcli.rel

# tzftp: net.bin常駐 + python/tzftp_srv.py(host) 用の簡易ファイル転送クライアント
# (本物のFTPではないことを明示するため ftp から改名)。
$(OBJ)/tzftp.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/tzftp.rel $(OBJ)/netcli.rel $(OBJ)/string.rel $(OBJ)/stdlib.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/tzftp.rel $(OBJ)/netcli.rel $(OBJ)/string.rel $(OBJ)/stdlib.rel

# atcli: net.bin常駐 + python/at_modem.py(host, ESP-AT テストベッド) 用の
# 実証コマンド。at_modem.py へダイヤル後、本物の AT コマンド列を流す。
$(OBJ)/atcli.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/atcli.rel $(OBJ)/netcli.rel $(OBJ)/string.rel $(OBJ)/stdlib.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/atcli.rel $(OBJ)/netcli.rel $(OBJ)/string.rel $(OBJ)/stdlib.rel

# ntpdate: atcli と同じ at_modem.py 経由ダイヤルで python/ntpdate_srv.py から
# JST epoch を1行受信し、time_set() でシステム時計を書き換える。
$(OBJ)/ntpdate.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/ntpdate.rel $(OBJ)/netcli.rel $(OBJ)/string.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/ntpdate.rel $(OBJ)/netcli.rel $(OBJ)/string.rel


# その他のコマンド用パターンルール (明示的ルールがない場合にのみ使用される)
$(OBJ)/%.ihx: $(OBJ)/$(CRT0).rel $(OBJ)/%.rel Makefile
	$(SDLD) -n -i $@ $(LDBASE) $(FIXED_SYMS) -m -w $(OBJ)/$(CRT0).rel $(OBJ)/$*.rel

# ---- final binary ----
$(OBJ)/%.bin: $(OBJ)/%.ihx
	$(MAKEBIN) -p $< $@

# ---- test ----
test-iy:

# ---- clean ----
#   生成物は全部 $(OBJ) 配下。旧レイアウトで user/ に残った物も念のため掃除する。
clean:
	rm -rf $(OBJ)
	rm -f *.asm *.rel *.ihx *.bin *.raw.bin *.iy.asm *.map *.lst *.sym *.noi *.o *.img *.iymap
