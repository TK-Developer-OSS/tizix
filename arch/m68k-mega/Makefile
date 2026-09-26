# tizix arch/m68k-mega
#   ツールチェイン: m68k-elf-gcc 8.3.0 (freestanding, -mcpu=68000)
#   実機: 生 MC68000(4MHz) + Arduino Mega2560(バスホスト: RESET/HALT/DTACK/
#         クロック駆動、UART を MMIO でエミュレート)。ROM は無く、Mega が
#         SRAM へ起動前に書き込む(このディレクトリの成果物がその「ROM」)。
#   ルート Makefile から `make ARCH=m68k-mega` として叩かれる他、
#   このディレクトリで直接 `make` してもよい。
#   使い方: make -C arch/m68k-mega            … kernel.bin + rom.h + m68ksim + disk.img
#           make -C arch/m68k-mega run        … m68ksim でソフト先行検証(対話)
#
#   S2: 共有 src/ カーネル(init/kernel/io/vfs/dev/sh/builtin + FatFs)を結線。
#   外部コマンド実行(kexec)はまだ無い(src/kexec.c の ARCH_M68K_MEGA 分岐は
#   常に「無い」を返すスタブ)。sh の builtin だけが動く段階。

CC      = m68k-elf-gcc
LD      = m68k-elf-ld
OBJCOPY = m68k-elf-objcopy
HOSTCC  = gcc

OBJ  = obj
SRC  = ../../src

# TICK_HZ: レベル6タイマ割込みの周期。カーネルと m68ksim の両方がこの 1 つの値に従う。
#   既定 25(#85、2026-09-26)。実機の Mega のファーム(arduino/…/MEGA2560_68000_DTACK_TEST の
#   Timer5、OCR5A=9999)と一致させること。実機で 100/50/25/12/6Hz を測り、応答が一番速かった
#   25Hz にした(10ms ごとの割込み処理が遅いバス代行の上では重い)。100 を割り切れる値にすること
#   (コマンド側 tzstdio の getticks が 100 / TICK_HZ を掛ける)。
#   以前は 1Hz 固定でプロセス切り替えが 1 秒に 1 回しか起きなかった(#83)。
#   値を変えたら `make clean` してから(フラグは依存に入らない)。SD のコマンドも作り直しが要る
#   (a / b / rx / sh / sleep / uptime / vi は TICK_HZ で中身が変わる)。
TICK_HZ ?= 25
# -include include/plat.h: 共有 src/ を SDCC 前提の属性(__sdcccall 等)ごと
#   m68k-elf-gcc に通すための移植シム(arch/x86-ia16 と同じ手口)。
CFLAGS = -mcpu=68000 -ffreestanding -fomit-frame-pointer -Wall -Wextra \
         -Os -DARCH_M68K_MEGA -DTICK_HZ=$(TICK_HZ) -Iinclude -I$(SRC) \
         -include include/plat.h
LIBGCC = $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

ROCKET68_DIR = rocket68
ROCKET68_LIB = $(ROCKET68_DIR)/lib/librocket68.a

# arch 固有(起動/物理コンソール/ディスクDMA/freestanding libc/syscall dispatch)
AOBJ = $(OBJ)/crt0.o $(OBJ)/kmain.o $(OBJ)/console.o $(OBJ)/diskio.o $(OBJ)/libc.o \
       $(OBJ)/sysfile.o
# 共有 src/ カーネル中核(x86-ia16 の SOBJ と同じ発想)。#82 でカーネルパイプ
# (src/pipe.c)も共有にした(以前は pipestub.c で常に失敗を返していた)。
SOBJ = $(OBJ)/init.o $(OBJ)/kernel.o $(OBJ)/io.o $(OBJ)/vfs.o $(OBJ)/dev.o \
       $(OBJ)/builtin.o $(OBJ)/kexec.o $(OBJ)/pipe.o
# sh はカーネルに入れない。z80 と同じ user/sh.c を /bin/sh.bin として作り、
# init(src/init.c)が起動・respawn する。
# FatFs バックエンド
FSBACKEND = $(OBJ)/fsbackend_fat.o $(OBJ)/fatcmd.o $(OBJ)/ff.o
KOBJ = $(AOBJ) $(SOBJ) $(FSBACKEND)

DISK_IMG = $(OBJ)/disk.img

.PHONY: all run clean
all: $(OBJ)/kernel.bin rom.h m68ksim $(DISK_IMG)

$(OBJ):
	mkdir -p $(OBJ)

# 共有ヘッダ(kmem.h 等)の変更で obj が腐って再ビルドされない事故が
# 実際に起きた(#47: KW_VTREE のオフセット修正が反映されず起動直後に
# ハング)。ヘッダ依存を個別追跡せず、全 .h に一括で依存させて対策する。
HDRS = $(wildcard include/*.h) $(wildcard $(SRC)/*.h)

$(OBJ)/%.o: %.c $(HDRS) | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJ)/%.o: %.s | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJ)/%.o: $(SRC)/%.c $(HDRS) | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ)/kernel.elf: $(KOBJ) link-kernel.ld
	$(LD) -T link-kernel.ld -o $@ $(KOBJ) $(LIBGCC)

$(OBJ)/kernel.bin: $(OBJ)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@sz=$$(stat -c%s $@); echo "kernel.bin = $$sz bytes (RAM budget 0x100000 = 1MB)"; \
	 [ $$sz -le 32768 ] || { echo "*** kernel.bin が 32KB(PROC_BASE=0x8000)を超過 ***"; \
	   echo "*** src/kexec.c の PROC_BASE と衝突する ***"; exit 1; }

# rom.h は Mega のスケッチ(arduino/MEGA2560_68000_DTACK_TEST、#95)が #include する。
# スケッチのフォルダにも同じものを置く(生成物なので git には入れない)。
ARDUINO_SKETCH = arduino/MEGA2560_68000_DTACK_TEST
rom.h: $(OBJ)/kernel.bin tools/bin2romh.py
	python3 tools/bin2romh.py $(OBJ)/kernel.bin $@
	cp $@ $(ARDUINO_SKETCH)/rom.h

# ---- 外部コマンド(#47 kexec、#50 で複数スロット化)。reloc 無し方針の
#   ままスロットごとに固定リンクアドレスを変えるため、同じ .o をスロット
#   数(SLOTS、src/kexec.c PROC_NSLOT と一致させること)だけ再リンクして
#   "<cmd><slot>.bin" を作る(再コンパイルは1回、リンクだけ複数回)。
#   tzport 手口(x86-ia16 と同じ): 共有 user/<cmd>.c を obj/tzport/ へ
#   コピーし、この arch 専用 stdio.h(tzstdio.h)を添えてビルド。
# -include include/plat.h: 共有 user/*.c(a.c/sleep.c 等)が SDCC 前提の
#   呼出規約属性(__sdcccall 等)を自前で書いていることがあり、カーネル側
#   (CFLAGS)と同じシムを通す(#47 で sleep 追加時に __sdcccall 未定義で
#   パースエラーになって発覚)。
UCFLAGS = -mcpu=68000 -ffreestanding -fomit-frame-pointer -O2 -Wall -Wextra \
          -mpcrel -DARCH_M68K_MEGA -DTICK_HZ=$(TICK_HZ) -Iuser -Iinclude -include include/plat.h
# #48: argv_init.o(空白で割り直すトークナイザ)は廃止。sh が argpack で渡し、
#   kexec_argv がトークンごとの argv[] を作るので、crt0cmd はそれをそのまま使う。
UCRT  = $(OBJ)/crt0cmd.o
ULIB  = $(OBJ)/usyscall.o

$(OBJ)/crt0cmd.o: user/crt0cmd.s | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<
$(OBJ)/usyscall.o: user/usyscall.s | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<

TZSRC      = ../../user
TZPORT_DIR = $(OBJ)/tzport
TZPORT_CMDS = ls echo cat head tail rm touch cp wc hello grep sed uniq tee whoami uname date du mkdir mv rmdir sleep id a b dd vi rsyslog df uptime free history ptx prx sh rx

$(TZPORT_DIR):
	mkdir -p $(TZPORT_DIR)
$(TZPORT_DIR)/stdio.h: user/tzstdio.h | $(TZPORT_DIR)
	cp $< $@
# date.c は共有 user/date.c ではなく arch 側の専用実装を使う(共有
# ソースに #ifdef ARCH_M68K_MEGA を足すと tzcc がプリプロセッサ分岐を
# 除去できず z80pack ビルドが壊れるため)。明示ターゲットはパターン
# ルールより優先される。
$(TZPORT_DIR)/date.c: user/date.c | $(TZPORT_DIR)
	cp $< $@
# free.c も arch 側専用(z80 版は pid 表の番地 0x8400 と 4KB ブロック 2..7 が前提。
# m68k はスロット 1..NSLOT-1 が 32KB ずつで、状態は syscall 20 で読む)。
$(TZPORT_DIR)/free.c: user/free.c | $(TZPORT_DIR)
	cp $< $@
# sh: z80 の user/shvec.h(drv_tbl の関数ポインタ)の代わりに arch 側の syscall 版を写す。
$(TZPORT_DIR)/shvec.h: user/shvec.h | $(TZPORT_DIR)
	cp $< $@
$(OBJ)/ucmd_sh.o: $(TZPORT_DIR)/shvec.h
$(TZPORT_DIR)/%.c: $(TZSRC)/%.c | $(TZPORT_DIR)
	cp $< $@
$(OBJ)/ucmd_%.o: $(TZPORT_DIR)/%.c $(TZPORT_DIR)/stdio.h | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<

# #61: PIC 化により **コマンドごとに .bin は 1 本**。以前はスロット数ぶん
# 再リンクしていた(ls1.bin/ls2.bin/…、21 コマンド x 31 スロット = 651 本)。
# -mpcrel で PC 相対コードを吐かせ、VMA=0 でリンクする(user/cmd.ld)。
# ロード先は src/kexec.c が実行時に決めるので、Makefile と kexec.c の間で
# アドレスを同期させる約束事そのものが消えた。
$(OBJ)/%.bin: $(OBJ)/ucmd_%.o $(UCRT) $(ULIB) user/cmd.ld
	$(LD) -T user/cmd.ld -o $@ $(UCRT) $< $(ULIB)

USERBINS = $(foreach c,$(TZPORT_CMDS),$(OBJ)/$(c).bin)

# ---- ソフト先行検証用ディスクイメージ(FAT12、1.44MB、mtools で直接書く) ----
$(DISK_IMG): $(USERBINS) etc/rc | $(OBJ)
	rm -f $@
	mkfs.fat -F 16 -S 512 -n TIZIX -C $@ 32768 >/dev/null
	mmd -i $@ ::root
	mmd -i $@ ::bin
	mmd -i $@ ::etc
	mcopy -o -i $@ etc/rc ::etc/rc
	mmd -i $@ ::var
	mmd -i $@ ::var/log
	@for b in $(USERBINS); do \
	   n=$$(basename $$b .bin); \
	   mcopy -o -i $@ $$b ::bin/$$n.bin; done

# ---- ホスト側ソフト先行検証ハーネス(rocket68 埋め込み、cpmsim 相当) ----
m68ksim: m68ksim.c $(ROCKET68_LIB)
	$(HOSTCC) -O2 -Wall -Wextra -I. -DTICK_HZ=$(TICK_HZ) -o $@ m68ksim.c $(ROCKET68_LIB)
# rocket68(github.com/habedi/rocket68、MIT)は tizix のリポジトリに入っていない。
# 置いてあれば静的ライブラリだけ作る(以前はビルド済みの lib/ に頼っていて、
# 新しく置いた環境では m68ksim が作れなかった)。
$(ROCKET68_LIB):
	$(MAKE) -C $(ROCKET68_DIR) static CC=$(HOSTCC)

run: m68ksim $(OBJ)/kernel.bin $(DISK_IMG)
	./m68ksim $(OBJ)/kernel.bin $(DISK_IMG)

clean:
	rm -rf $(OBJ) rom.h m68ksim $(ARDUINO_SKETCH)/rom.h $(ARDUINO_SKETCH)/build
