#===================================================================
# arch/common-gcc.mk - gcc 系(PLAT_FLAT32)アーキの共通ビルド規則
#
#   m68k-mega / esp32-wroom-32e の Makefile が include する(z80 系の
#   common-sdcc.mk と同じ役)。ポートを足すたびに各 Makefile へ同じ行を
#   写さずに済むよう、「どのアーキでも同じもの」だけをここに置く:
#     ・共有カーネル(src/)のオブジェクト一覧とコンパイル規則
#     ・外部コマンドの一覧(UCMDS)とコンパイル規則(共有 user/<cmd>.c をそのまま使う)
#     ・ディスク像(FAT)の作り方
#   リンク(カーネル像、コマンドの .bin の形)と実行・書き込みは CPU とボードで
#   違うので、各 arch の Makefile に残す。
#
#   include する側が **先に** 決めておくもの:
#     CC        クロス gcc
#     OBJ       中間物の置き場(../../build/arch/<arch>/obj)
#     CFLAGS    カーネル用。$(KINC) と -include include/plat.h を含めること
#     UCFLAGS   コマンド用。-include include/plat.h を含めること
#     DISK_IMG / DISK_KB / MKFS_OPTS   ディスク像の出力先・大きさ(KB)・mkfs.fat への追加指定
#   既定のゴール(all)は include より前に書くこと(ここにも規則があるため)。
#
#   x86-ia16 は対象外(セグメント越しの別実装を arch 側に持つ。休止中)。
#===================================================================

SRC   = ../../src
TZSRC = ../../user

# カーネルの include パス。src/libc は freestanding 版の string.h
# (src/ 直下に置かないのは、z80 のビルドで SDCC 自身の <string.h> を隠さないため)。
KINC  = -Iinclude -I$(SRC) -I$(SRC)/libc

# ---- 共有カーネルのオブジェクト(並べる順は各 arch の KOBJ が決める) ----
# freestanding libc と syscall のディスパッチャ(番地にも CPU にも依らない素の C)
LOBJ = $(OBJ)/libc.o $(OBJ)/sysfile.o
# カーネル中核。sh は入れない ── user/sh.c を /bin/sh.bin として作り、init が起動する。
SOBJ = $(OBJ)/init.o $(OBJ)/kernel.o $(OBJ)/io.o $(OBJ)/vfs.o $(OBJ)/dev.o \
       $(OBJ)/builtin.o $(OBJ)/kexec.o $(OBJ)/pipe.o $(OBJ)/mbox.o
# FatFs バックエンド
FSBACKEND = $(OBJ)/fsbackend_fat.o $(OBJ)/fatcmd.o $(OBJ)/ff.o $(OBJ)/ffunicode_min.o

$(OBJ):
	mkdir -p $(OBJ)

# 共有ヘッダ(kmem.h 等)の変更で obj が腐って再ビルドされない事故が
# 実際に起きた(#47: KW_VTREE のオフセット修正が反映されず起動直後に
# ハング)。ヘッダ依存を個別追跡せず、全 .h に一括で依存させて対策する。
HDRS = $(wildcard include/*.h) $(wildcard $(SRC)/*.h) $(wildcard $(SRC)/libc/*.h)

# arch 側のソース(同名が src/ にあっても arch 側が優先される)
$(OBJ)/%.o: %.c $(HDRS) | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJ)/%.o: %.s | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJ)/%.o: %.S $(HDRS) | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<
# 共有 src/
$(OBJ)/%.o: $(SRC)/%.c $(HDRS) | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJ)/%.o: $(SRC)/libc/%.c $(HDRS) | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- 外部コマンド ----
#   共有 user/<cmd>.c を **そのまま** コンパイルする。以前は obj/tzport/ へ写して
#   arch 専用の stdio.h を添えていたが、user/stdio.h・shvec.h・string.h が
#   コンパイラを見て中身を切り替えるようになり(gcc では syscall5 経由の側)、
#   date.c / free.c も共有になったので、写す理由が無くなった。
#   コマンドを足すときはここに名前を 1 つ足す(全 gcc 系アーキに載る)。
UCMDS = ls echo cat head tail rm touch cp wc hello grep sed uniq tee whoami uname \
        date du mkdir mv rmdir sleep id a b dd vi rsyslog df uptime free history \
        ptx prx sh rx mbtest tzsh $(ARCH_UCMDS)
# ARCH_UCMDS: そのアーキだけに載せるコマンド(include より前に決めておく。esp32 のネット系など)

# コマンドはヘッダ 3 本と plat.h(TICK_HZ の倍率や枠の大きさ)に依存する。
UHDRS = $(TZSRC)/stdio.h $(TZSRC)/string.h $(TZSRC)/shvec.h $(TZSRC)/mbox.h $(wildcard include/*.h)

$(OBJ)/ucmd_%.o: $(TZSRC)/%.c $(UHDRS) | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<

USERBINS = $(foreach c,$(UCMDS),$(OBJ)/$(c).bin)
# tzsh の例と回帰用スクリプト(#111)。ディスクの /usr/share/tzsh/ に置く
TZSH_SCRIPTS = $(wildcard $(TZSRC)/tzsh/*.sh)

# ---- ディスク像(FAT16。mtools で直接書く) ----
# ARCH_DIRBINS: アーキ固有の置き場所を持つコマンド($(OBJ)/bin/<path> → ::bin/<path>)。include より前に決める。
#   esp32 の esp32-gpio.bin / esp32d.bin など(gcc 系は長いファイル名が使える。#114)。
$(DISK_IMG): $(USERBINS) $(ARCH_DIRBINS) etc/rc $(TZSH_SCRIPTS) | $(OBJ)
	rm -f $@
	mkfs.fat -F 16 -S 512 $(MKFS_OPTS) -n TIZIX -C $@ $(DISK_KB) >/dev/null
	mmd -i $@ ::root
	mmd -i $@ ::bin
	mmd -i $@ ::etc
	mcopy -o -i $@ etc/rc ::etc/rc
	mmd -i $@ ::var
	mmd -i $@ ::var/log
	mmd -i $@ ::usr
	mmd -i $@ ::usr/share
	mmd -i $@ ::usr/share/tzsh
	@for s in $(TZSH_SCRIPTS); do mcopy -o -i $@ $$s ::usr/share/tzsh/; done
	@for b in $(USERBINS); do \
	   n=$$(basename $$b .bin); \
	   mcopy -o -i $@ $$b ::bin/$$n.bin; done
	@for b in $(ARCH_DIRBINS); do \
	   r=$${b#$(OBJ)/bin/}; d=$$(dirname $$r); \
	   mmd -i $@ ::bin/$$d 2>/dev/null || true; \
	   mcopy -o -i $@ $$b ::bin/$$r; done
