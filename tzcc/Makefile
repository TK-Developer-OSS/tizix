# ビルド成果物はすべて arch/z80pack/ 以下に出す（src を散らかさない）。
#   ルートに残すのは実行ファイル tzcc のみ。
#   ディレクトリ作成は order-only 依存 "| $(BUILD)" で行う（parse 時 mkdir だと
#   `make clean all` で clean 後に消えたまま all が走り失敗するため）。
BUILD := arch/z80pack

CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = tzcc
SRCS = main.c lexer.c parser.c generator.c tizix.c gen_x86.c
OBJS = $(SRCS:%.c=$(BUILD)/%.o)

# tzcc 自体をスタティックリンクし、任意の x86 Linux で動くようにする。
# glibc-static が無い環境では -static がリンクエラーになるので、
# 実際にリンクできる場合だけ -static を付ける（自動フォールバック）。
STATIC := $(shell printf 'int main(void){return 0;}' | $(CC) -static -x c - -o /dev/null 2>/dev/null && echo -static)

# #21: SDCC(sdasz80 等)は PATH から探す(tizix 本体の arch/common-sdcc.mk と同じ)。
#   別の場所を使うなら make SDCC_BIN=/path/to/sdcc/bin。以前は開発機の絶対パスを直書きしていた。
#   見つからなければコマンド名だけで呼ぶ(使う場面でだけ失敗し、tzcc 本体のビルドや clean は通る)。
SDCC_BIN ?= $(patsubst %/,%,$(dir $(shell command -v sdasz80 2>/dev/null)))
SDCC_PFX = $(if $(strip $(SDCC_BIN)),$(SDCC_BIN)/,)
AS = $(SDCC_PFX)sdasz80
LD = $(SDCC_PFX)sdldz80
MAKEBIN = $(SDCC_PFX)makebin
# cpmsim は tizix のツリーの arch/z80pack/cpmsim(tests/run_rt.sh と同じ)。
CPMSIM ?= ../arch/z80pack/cpmsim

# --tizix-user のアセンブルはこちらを使う。tizix.c がセグメント内 jp を jr へ
# 落とすので、変換で挿入されたバイト分だけ ±127 を超えた jr が出うる。jrfix.py が
# sdasz80 を回して範囲外と報告された jr **だけ** を間接列へ戻し、収束まで反復する。
# 素の z80 / CP/M 経路は PIC 変換が無く jp のままなので $(AS) を直に使う。
PYTHON  ?= python3
ASTZ     = $(PYTHON) jrfix.py

# make com の転送先(CP/M 2.2 のディスク)。tizix 統合後のツリーには CP/M ディスクが
# 無いので、無ければ .COM を作るだけで止める(make com DISK=path/to/cpm.dsk で転送)。
DISK    ?= disks/drivea.dsk
DISKFMT = ibm-3740
CPMCP   = cpmcp
# make tizix の転送先は tizix 本体の driveb.dsk(tizixcmd と同じ。旧 tzcc 単独時代の
# disks/driveb.dsk は統合後のツリーに無く、mcopy が "Cannot initialize 'X:'" で落ちていた)。
DRIVEB  = $(TIZIX_DRIVEB)
MCOPY   = mcopy
# make test の [[ ]] は bash 構文(/bin/sh が dash の環境で落ちないように)。
SHELL   = /bin/bash

# 指定テストの中間 .s / 最終 .BIN 名（すべて $(BUILD)/ 以下）
TSRC_S  = $(BUILD)/$(notdir $(TEST_SRC:.c=.s))

.PHONY: all static build_test run com cpm tizix tizixrun clean test runtest x86 x86test
.DEFAULT_GOAL := all

all: $(TARGET)

# ビルドディレクトリ（order-only 依存の対象）
$(BUILD):
	@mkdir -p $(BUILD)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(STATIC) -o $(TARGET) $(OBJS)
	@file $(TARGET) | grep -q 'statically linked' \
	  && echo ">>> $(TARGET): statically linked (portable)" \
	  || echo ">>> $(TARGET): dynamically linked (install glibc-static for a fully portable build)"

# 明示的にスタティックリンクを要求する（glibc-static が無ければ失敗する）
static: $(OBJS)
	$(CC) $(CFLAGS) -static -o $(TARGET) $(OBJS)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- Z80 スタートアップ ----
# sdasz80 は .lst / .sym をソースと同じディレクトリに吐くので、
# ソースを $(BUILD)/ にコピーしてからアセンブルし、副産物も全部 arch/z80pack/ に集める。
$(BUILD)/crt0.rel: crt0.s | $(BUILD)
	cp $< $(BUILD)/crt0.s
	$(AS) -g -o $@ $(BUILD)/crt0.s

$(BUILD)/crt0_tizix.rel: crt0_tizix.s | $(BUILD)
	cp $< $(BUILD)/crt0_tizix.s
	$(AS) -g -o $@ $(BUILD)/crt0_tizix.s

# #26: string/mem/ctype/stdlib を crt0_tizix から分離し、参照するコマンドだけが
#   リンクするライブラリに。iy 相対肥大で 1 ブロックを超えてパイプが
#   「no free block」になっていた対策(head/tail/grep/uniq/wc を 1 ブロックへ)。
AR = $(SDCC_PFX)sdar
# tzctramp は **オーバーレイ専用**。通常コマンドは crt0_tizix.rel(明示リンク)で
# 全部解決されるので、ライブラリからは引かれない = 既存コマンドは 1 バイトも
# 変わらない。オーバーレイは crt0_tizix を持たないのでここから引く。
#   tzcshare … #35/#36 の共有メモリ / オーバーレイ補助。**参照したものだけ**が
#               引かれるようにライブラリへ置く(crt0_tizix に置くと全コマンドが
#               +134B 太り、そのぶん実効スタックが減る)。
#   tzctramp  … オーバーレイ専用の DRIVER トランポリン。通常コマンドは
#               crt0_tizix.rel(明示リンク)で全部解決されるので引かれない
#               = 既存コマンドは 1 バイトも変わらない。
#   tzcovl    … #69 関数単位オーバーレイのローダ。ovlsplit.py で割ったコマンドだけが引く。
TZLIB_MODS = tzcstr tzcstrn tzcstrstr tzcmem tzcctype tzcstdlib tzcprintf tzcout tzcshare tzcpeek tzcblk tzctramp tzcovl tzcmuldiv tzcdf
TZLIB_RELS = $(addprefix $(BUILD)/,$(addsuffix .rel,$(TZLIB_MODS)))

$(BUILD)/%.rel: %.s | $(BUILD)
	cp $< $(BUILD)/$*.s
	$(AS) -g -o $@ $(BUILD)/$*.s

$(BUILD)/libtzc.lib: $(TZLIB_RELS)
	rm -f $@
	$(AR) -rc $@ $(TZLIB_RELS)

# crt0.s から CP/M .COM 用スタートアップを自動生成（エントリ 0x0100、終了はウォームブート）
$(BUILD)/crt0_cpm.s: crt0.s | $(BUILD)
	sed '/\.area _HEADER (ABS)/,/\.area _CODE/c\
    .area _HEADER (ABS)\
    .org 0x0100\
_start::\
    ld hl, #dummy_argv\
    push hl\
    ld hl, #1\
    push hl\
    call _main\
    pop af\
    pop af\
    jp 0x0000\
\
    .area _CODE' crt0.s > $@

$(BUILD)/crt0_cpm.rel: $(BUILD)/crt0_cpm.s
	$(AS) -g -o $@ $<

# ---- ベアメタル（cpmsim -z -x）ビルド ----
# 使用例: make build_test TEST_SRC=tests/ok_13.c
build_test: $(TARGET) $(BUILD)/crt0.rel
	@if [ -z "$(TEST_SRC)" ]; then echo "Error: TEST_SRC is not set"; exit 1; fi
	./$(TARGET) -o $(TSRC_S) $(TEST_SRC)
	$(AS) -g -o $(BUILD)/output.rel $(TSRC_S)
	$(LD) -i -b _HEADER=0x0000 -b _CODE=0x0020 $(BUILD)/output.ihx $(BUILD)/crt0.rel $(BUILD)/output.rel
	$(MAKEBIN) $(BUILD)/output.ihx $(BUILD)/output.bin

# 直近の build_test 成果物をベアメタル実行（TEST_SRC 指定時はビルドしてから実行）
run:
	@if [ -n "$(TEST_SRC)" ]; then $(MAKE) build_test TEST_SRC=$(TEST_SRC); fi
	$(CPMSIM) -z -x $(BUILD)/output.ihx

# ---- CP/M 2.2 .COM ビルド ----
# 使用例: make com TEST_SRC=tests/ok_01.c  → NAME.COM を disks/drivea.dsk へ転送
com: $(TARGET) $(BUILD)/crt0_cpm.rel
	@if [ -z "$(TEST_SRC)" ]; then echo "Error: TEST_SRC is not set (例: make com TEST_SRC=tests/ok_01.c)"; exit 1; fi
	./$(TARGET) -o $(TSRC_S) $(TEST_SRC)
	$(AS) -g -o $(BUILD)/prog.rel $(TSRC_S)
	$(LD) -i -b _HEADER=0x0100 -b _CODE=0x0120 $(BUILD)/prog.ihx $(BUILD)/crt0_cpm.rel $(BUILD)/prog.rel
	$(MAKEBIN) -p $(BUILD)/prog.ihx $(BUILD)/prog.full.bin
	dd if=$(BUILD)/prog.full.bin of=$(BUILD)/prog.com bs=1 skip=256 status=none
	$(eval COMNAME := $(shell basename $(TEST_SRC) .c | tr -cd 'A-Za-z0-9' | cut -c1-8 | tr a-z A-Z))
	@if [ ! -f "$(DISK)" ]; then \
		echo ">>> $(BUILD)/prog.com を作った(CP/M ディスク $(DISK) が無いので転送は省略。make com DISK=… で指定)"; \
		exit 0; \
	fi; \
	$(CPMCP) -f $(DISKFMT) $(DISK) $(BUILD)/prog.com 0:$(COMNAME).COM && \
	echo "" && \
	echo ">>> $(COMNAME).COM を $(DISK) に転送しました" && \
	echo ">>> 'make cpm' で CP/M にログインし、A> プロンプトで  $(COMNAME)  と入力"

# CP/M 2.2 にログイン (対話)
cpm:
	$(CPMSIM) -z

# ================= Tizix ターゲット =================
# 使用例: make tizix TEST_SRC=tests/ok_tz1.c  → name.bin を tizix の driveb.dsk の /bin へ転送
#   その後 'make tizixrun' で Tizix 起動 → シェルで name と入力。
# カーネル低位ベクタ(実行時カーネルが設置)を固定番地に束縛。
#   注: -g は「どこかで .globl(import) された symbol」にしか効かない。
#       未参照のものを並べると "No definition of symbol"。
TZVEC = -g ___sdcc_call_hl=0x0050 -g _kexit=0x003B -g _kputchar=0x003E -g _kgetchar=0x0041 -g _getticks=0x0044

tizix: $(TARGET) $(BUILD)/crt0_tizix.rel $(BUILD)/libtzc.lib
	@if [ -z "$(TEST_SRC)" ]; then echo "Error: TEST_SRC is not set (例: make tizix TEST_SRC=tests/ok_tz1.c)"; exit 1; fi
	./$(TARGET) --tizix-user -o $(TSRC_S) $(TEST_SRC)
	$(ASTZ) $(TSRC_S) $(BUILD)/prog.rel --as $(AS)
	$(LD) -n -i -b _CODE=0x0000 $(TZVEC) $(BUILD)/prog.ihx $(BUILD)/crt0_tizix.rel $(BUILD)/prog.rel -k $(BUILD) -l libtzc.lib
	$(MAKEBIN) -p $(BUILD)/prog.ihx $(BUILD)/prog.bin
	$(eval BINNAME := $(shell basename $(TEST_SRC) .c | tr -cd 'A-Za-z0-9' | cut -c1-8 | tr A-Z a-z))
	@printf 'drive x: file="%s" exclusive\n' "$(abspath $(DRIVEB))" > /tmp/tzcc_mtoolsrc
	MTOOLS_SKIP_CHECK=1 MTOOLSRC=/tmp/tzcc_mtoolsrc $(MCOPY) -o $(BUILD)/prog.bin x:bin/$(BINNAME).bin
	@echo ""
	@echo ">>> $(BINNAME).bin ($$(stat -c %s $(BUILD)/prog.bin) bytes) を $(DRIVEB) の /bin に転送しました"
	@echo ">>> 'make tizixrun' で Tizix 起動 → シェルで  $(BINNAME)  と入力"

# Tizix 起動 (対話)。tizix 本体の make run(arch/z80pack で cpmsim を起動)。
tizixrun:
	$(MAKE) -C $(TIZIX_ROOT) run

# ---- tizix の user/*.c をそのまま tzcc でビルドして tizix の driveb.dsk へ ----
# 使用例: make tizixcmd CMD=cp        （= ../user/cp.c を CP.BIN 化して
#         ../arch/z80pack/disks/driveb.dsk の /bin/cp.bin に上書き）
#   tizix の user/xxx.c は #include "stdio.h" を持つが、それは関数ポインタで
#   drv_tbl を叩く SDCC 用。ここでは include/tizix.h を stdio.h として横に
#   置き、素のプロトタイプ + crt0_tizix.s のトランポリンで解決する。
#   FS 関数は drv_tbl(0x9000) 実体が要るので、対象 driveb.dsk のカーネル/DRIVER
#   が現行 tizix ビルドであること。
# tzcc は tizix/tzcc/ に同居しているので既定は親ディレクトリ ".."。
TIZIX_ROOT   ?= ..
TIZIX_USER   ?= $(TIZIX_ROOT)/user
TIZIX_DRIVEB ?= $(TIZIX_ROOT)/arch/z80pack/disks/driveb.dsk
TZCMD_DIR     = $(BUILD)/tzcmd

# EXTRA … TIZIX_USER 内の追加 .c ベース名（例: ls は EXTRA=getopt）。
#          crt0_tizix と一緒にリンクする。
# NOCOPY … 非空なら driveb.dsk への mcopy を省略（tizixcmds が一括で行う）。
EXTRA  ?=
NOCOPY ?=

tizixcmd: $(TARGET) $(BUILD)/crt0_tizix.rel $(BUILD)/libtzc.lib $(if $(OVLVEC),$(BUILD)/ovlvec.rel)
	@if [ -z "$(CMD)" ]; then echo "Error: CMD is not set (例: make tizixcmd CMD=cp)"; exit 1; fi
	@if [ ! -f "$(TIZIX_USER)/$(CMD).c" ]; then echo "not found: $(TIZIX_USER)/$(CMD).c"; exit 1; fi
	@mkdir -p $(TZCMD_DIR)
	cp $(TIZIX_USER)/$(CMD).c $(TZCMD_DIR)/$(CMD).c
	@for h in $(TIZIX_USER)/*.h; do cp "$$h" $(TZCMD_DIR)/ 2>/dev/null || true; done
	cp include/tizix.h        $(TZCMD_DIR)/stdio.h
	cp include/string.h       $(TZCMD_DIR)/string.h
	cp include/stdlib.h       $(TZCMD_DIR)/stdlib.h
	cp include/ctype.h        $(TZCMD_DIR)/ctype.h
	./$(TARGET) --tizix-user -o $(TZCMD_DIR)/$(CMD).s $(TZCMD_DIR)/$(CMD).c
	$(ASTZ) $(TZCMD_DIR)/$(CMD).s $(TZCMD_DIR)/$(CMD).rel --as $(AS)
	@rm -f $(TZCMD_DIR)/$(CMD).extra.rels
	@for e in $(EXTRA); do \
		cp $(TIZIX_USER)/$$e.c $(TZCMD_DIR)/$$e.c; \
		./$(TARGET) --tizix-user -o $(TZCMD_DIR)/$$e.s $(TZCMD_DIR)/$$e.c || exit 1; \
		$(ASTZ) $(TZCMD_DIR)/$$e.s $(TZCMD_DIR)/$$e.rel --as $(AS) || exit 1; \
		echo "$(TZCMD_DIR)/$$e.rel" >> $(TZCMD_DIR)/$(CMD).extra.rels; \
	done
	$(LD) -n -i -b _CODE=0x0000 $(if $(OVLVEC),-b _OVLVEC=$(OVLVEC)) $(TZVEC) $(TZCMD_DIR)/$(CMD).ihx $(BUILD)/crt0_tizix.rel $(TZCMD_DIR)/$(CMD).rel $(if $(OVLVEC),$(BUILD)/ovlvec.rel) `cat $(TZCMD_DIR)/$(CMD).extra.rels 2>/dev/null` -k $(BUILD) -l libtzc.lib
	$(MAKEBIN) -p $(TZCMD_DIR)/$(CMD).ihx $(TZCMD_DIR)/$(CMD).bin
	@# #38: XBLK=n を指定したら .BIN 先頭ヘッダへ「追加ブロック数」を刻む。
	@#   0x10 'T' / 0x11 'Z' / 0x12 n。kexec がこれを見て像とは別に n ブロック
	@#   確保する(SP と argv は像側の上端に置かれるので丸ごと作業領域になる)。
	@#   crt0 の `.ds 0x20` = 誰も使っていない死に領域なので後から刻んでよい。
	@if [ -n "$(XBLK)" ] && [ "$(XBLK)" != "0" ]; then \
		printf 'TZ' | dd of=$(TZCMD_DIR)/$(CMD).bin bs=1 seek=16 conv=notrunc 2>/dev/null; \
		printf "$$(printf '\\%03o' $(XBLK))" | dd of=$(TZCMD_DIR)/$(CMD).bin bs=1 seek=18 conv=notrunc 2>/dev/null; \
		echo ">>> $(CMD).bin: 追加ブロック $(XBLK) を宣言"; \
	fi
	@if [ -n "$(TIZIX_USERBIN)" ]; then mkdir -p $(TIZIX_USERBIN); cp $(TZCMD_DIR)/$(CMD).bin $(TIZIX_USERBIN)/$(CMD).bin; \
		echo ">>> $(CMD).bin ($$(stat -c %s $(TZCMD_DIR)/$(CMD).bin) bytes) -> $(TIZIX_USERBIN)/$(CMD).bin"; fi
	@if [ -z "$(NOCOPY)" ]; then \
		printf 'drive x: file="%s" exclusive\n' "$(abspath $(TIZIX_DRIVEB))" > /tmp/tzcc_mtoolsrc; \
		MTOOLS_SKIP_CHECK=1 MTOOLSRC=/tmp/tzcc_mtoolsrc $(MCOPY) -o $(TZCMD_DIR)/$(CMD).bin x:bin/$(CMD).bin; \
		echo ">>> $(CMD).bin ($$(stat -c %s $(TZCMD_DIR)/$(CMD).bin) bytes) -> $(TIZIX_DRIVEB) /bin/$(CMD).bin"; \
	fi

# ---- coreutils 一括ビルド（#26: iy_reg 卒業） ----
# 使用例: make tizixcmds TIZIX_ROOT=/abs/path/to/tizix
#   TIZIX_USERBIN が指す先（既定 $(TIZIX_ROOT)/arch/z80pack/user）へ .bin を置く。
#   tizix 側 mkfatdisk.sh がそこを glob して driveb.dsk /bin へ載せる。
TIZIX_USERBIN ?= $(TIZIX_ROOT)/arch/z80pack/user
TIZIX_CMDS    ?= calli cat cp date dd df du echo free grep head history id ls mkdir mv rm rmdir rsyslog sed sleep spawn spawnc tail tee touch uname uniq uptime wc whoami
# オーバーレイ → core ベクタ (#36 続き) を持つコマンド。「名前:OVLVEC 番地」。
#   番地はオーバーレイ領域の直下(OVLADDR - 24)。a43eb6c で ovlmain にベクタを
#   足したが通常ビルドへ配線されておらず、test_ovl が `make` 経由だと落ちていた。
TIZIX_VCMDS   ?= ovlmain:0x15E8
# 追加ブロック (#38) を宣言するコマンドは別枠。「名前:ブロック数」。
#   像とは別に n ブロックを確保させる(大きな作業領域を像に持たないため)。
TIZIX_XCMDS   ?= xblk:1
# 関数単位オーバーレイ (#69) で割るコマンド。「名前:追加ブロック数」。
#   割り方は ovl/<名前>.ovl に書く(C のソースは割らない = m68k と共有のまま)。
TIZIX_OVLCMDS ?= vi:1
# オーバーレイ (#36) は通常コマンドと違う番地でリンクするので別枠。
#   名前:番地 の形。tizixcmds の後に tizixovl で作る。
TIZIX_OVLS    ?= ovlsub:0x1600

# ---- オーバーレイ (#36): 親プロセスの空間へ後から読み込んで呼ぶコード片 ----
# 使用例: make tizixovl CMD=ovlsub OVLADDR=0x1600
#   ・**リンク番地 = 親が読み込む先のオフセット**。tzcc の --tizix-user は
#     「実行時に IY を足す」PIC を吐くので、IY(親のベース)+ リンク時オフセット
#     が実アドレスになる。ずらすと親の別の場所を触って壊す。
#   ・crt0_tizix ではなく crt0_ovl(IY 相対で _main へ飛ぶだけの 8 バイト)を使う。
#     _start / SP 張替 / kexit は持たない ── kexit したら親ごと終わる。
#   ・makebin は 0 番地から詰めた像を作るので、先頭 OVLADDR バイトを dd で捨てて
#     「OVLADDR から始まる実体」だけを .bin にする(DRIVER.BIN と同じ流儀)。
OVLADDR ?= 0x1600

tizixovl: $(TARGET) $(BUILD)/crt0_ovl.rel $(BUILD)/libtzc.lib
	@if [ -z "$(CMD)" ]; then echo "Error: CMD is not set (例: make tizixovl CMD=ovlsub OVLADDR=0x1600)"; exit 1; fi
	@if [ ! -f "$(TIZIX_USER)/$(CMD).c" ]; then echo "not found: $(TIZIX_USER)/$(CMD).c"; exit 1; fi
	@mkdir -p $(TZCMD_DIR)
	cp $(TIZIX_USER)/$(CMD).c $(TZCMD_DIR)/$(CMD).c
	@for h in $(TIZIX_USER)/*.h; do cp "$$h" $(TZCMD_DIR)/ 2>/dev/null || true; done
	cp include/tizix.h        $(TZCMD_DIR)/stdio.h
	cp include/string.h       $(TZCMD_DIR)/string.h
	cp include/stdlib.h       $(TZCMD_DIR)/stdlib.h
	cp include/ctype.h        $(TZCMD_DIR)/ctype.h
	./$(TARGET) --tizix-user -o $(TZCMD_DIR)/$(CMD).s $(TZCMD_DIR)/$(CMD).c
	$(ASTZ) $(TZCMD_DIR)/$(CMD).s $(TZCMD_DIR)/$(CMD).rel --as $(AS)
	@# TZVEC は渡さない。低位ベクタは crt0_ovl.s が実体として定義している
	@# (sdld の -g は「参照されている未定義シンボル」にしか値を与えられず、
	@#  オーバーレイが使わないベクタを -g で渡すとリンクが落ちるため)。
	$(LD) -n -i -b _CODE=$(OVLADDR) $(TZCMD_DIR)/$(CMD).ihx $(BUILD)/crt0_ovl.rel $(TZCMD_DIR)/$(CMD).rel -k $(BUILD) -l libtzc.lib
	$(MAKEBIN) -p $(TZCMD_DIR)/$(CMD).ihx $(TZCMD_DIR)/$(CMD).img
	dd if=$(TZCMD_DIR)/$(CMD).img of=$(TZCMD_DIR)/$(CMD).bin bs=$$(printf '%d' $(OVLADDR)) skip=1 2>/dev/null
	@rm -f $(TZCMD_DIR)/$(CMD).img
	@if [ -n "$(TIZIX_USERBIN)" ]; then mkdir -p $(TIZIX_USERBIN); cp $(TZCMD_DIR)/$(CMD).bin $(TIZIX_USERBIN)/$(CMD).bin; \
		echo ">>> [overlay @$(OVLADDR)] $(CMD).bin ($$(stat -c %s $(TZCMD_DIR)/$(CMD).bin) bytes) -> $(TIZIX_USERBIN)/$(CMD).bin"; fi

# ---- 関数単位オーバーレイ (#69): 1 本の C を core + <CMD>NN.ovl に割る ----
# 使用例: make tizixovlcmd CMD=vi XBLK=1
#   ・user/<CMD>.c はそのまま 1 本でコンパイルする(m68k と共有のため割らない)。
#   ・ovlsplit.py が ovl/<CMD>.ovl の指定に従って関数を .area _OVk へ移し、
#     core → 入口の呼び出しを tzcovl.s の ___ovlcall 経由に書き換える。
#   ・ovllink.py が area の番地だけ変えて何度かリンクし直し、core .bin と
#     各 .ovl を切り出す。core の同一性と「領域 + 最大オーバーレイ <= imgtop」
#     をそこで検査する(破っていればビルドが落ちる)。
#   ・.ovl は TIZIX_USERBIN へ置き、mkfatdisk.sh が /bin へ載せる。
tizixovlcmd: $(TARGET) $(BUILD)/crt0_tizix.rel $(BUILD)/libtzc.lib
	@if [ -z "$(CMD)" ]; then echo "Error: CMD is not set (例: make tizixovlcmd CMD=vi XBLK=1)"; exit 1; fi
	@if [ ! -f "ovl/$(CMD).ovl" ]; then echo "not found: ovl/$(CMD).ovl"; exit 1; fi
	@mkdir -p $(TZCMD_DIR)
	cp $(TIZIX_USER)/$(CMD).c $(TZCMD_DIR)/$(CMD).c
	@for h in $(TIZIX_USER)/*.h; do cp "$$h" $(TZCMD_DIR)/ 2>/dev/null || true; done
	cp include/tizix.h        $(TZCMD_DIR)/stdio.h
	cp include/string.h       $(TZCMD_DIR)/string.h
	cp include/stdlib.h       $(TZCMD_DIR)/stdlib.h
	cp include/ctype.h        $(TZCMD_DIR)/ctype.h
	./$(TARGET) --tizix-user -o $(TZCMD_DIR)/$(CMD).s $(TZCMD_DIR)/$(CMD).c
	$(PYTHON) ovlsplit.py $(TZCMD_DIR)/$(CMD).s $(TZCMD_DIR)/$(CMD).split.s --cmd $(CMD) --conf ovl/$(CMD).ovl > $(TZCMD_DIR)/$(CMD).novl
	mv $(TZCMD_DIR)/$(CMD).split.s $(TZCMD_DIR)/$(CMD).s
	$(ASTZ) $(TZCMD_DIR)/$(CMD).s $(TZCMD_DIR)/$(CMD).rel --as $(AS)
	rm -f $(TZCMD_DIR)/$(CMD)[0-9][0-9].ovl
	$(PYTHON) ovllink.py --ld $(LD) --dir $(TZCMD_DIR) --cmd $(CMD) --novl `cat $(TZCMD_DIR)/$(CMD).novl` \
		--xblk $(if $(XBLK),$(XBLK),0) --crt0 $(BUILD)/crt0_tizix.rel --libdir $(BUILD) -- $(TZVEC)
	@if [ -n "$(TIZIX_USERBIN)" ]; then mkdir -p $(TIZIX_USERBIN); \
		rm -f $(TIZIX_USERBIN)/$(CMD)[0-9][0-9].ovl; \
		cp $(TZCMD_DIR)/$(CMD).bin $(TIZIX_USERBIN)/$(CMD).bin; \
		cp $(TZCMD_DIR)/$(CMD)[0-9][0-9].ovl $(TIZIX_USERBIN)/; \
		echo ">>> $(CMD).bin ($$(stat -c %s $(TZCMD_DIR)/$(CMD).bin) bytes) + $$(ls $(TZCMD_DIR)/$(CMD)[0-9][0-9].ovl | wc -l) overlays -> $(TIZIX_USERBIN)/"; fi

$(BUILD)/crt0_ovl.rel: crt0_ovl.s
	@mkdir -p $(BUILD)
	$(AS) -o $@ $<

# #36 続き: オーバーレイ → core 呼び出しベクタ。tizixcmd で OVLVEC=<番地> を
#   与えたときだけ core へリンクする(番地は「オーバーレイ領域の直下」)。
$(BUILD)/ovlvec.rel: ovlvec.s
	@mkdir -p $(BUILD)
	$(AS) -o $@ $<

tizixcmds: $(TARGET) $(BUILD)/crt0_tizix.rel $(BUILD)/libtzc.lib
	@rc=0; for c in $(TIZIX_CMDS); do \
		echo "=== tzcc build: $$c ==="; \
		$(MAKE) --no-print-directory tizixcmd CMD=$$c NOCOPY=1 \
			TIZIX_ROOT=$(TIZIX_ROOT) TIZIX_USERBIN=$(TIZIX_USERBIN) || rc=1; \
	done; \
	for c in $(TIZIX_VCMDS); do \
		cx=`echo $$c | cut -d: -f1`; va=`echo $$c | cut -d: -f2`; \
		echo "=== tzcc build (ovlvec @$$va): $$cx ==="; \
		$(MAKE) --no-print-directory tizixcmd CMD=$$cx NOCOPY=1 OVLVEC=$$va \
			TIZIX_ROOT=$(TIZIX_ROOT) TIZIX_USERBIN=$(TIZIX_USERBIN) || rc=1; \
	done; \
	for c in $(TIZIX_XCMDS); do \
		cx=`echo $$c | cut -d: -f1`; xb=`echo $$c | cut -d: -f2`; \
		echo "=== tzcc build (追加ブロック $$xb): $$cx ==="; \
		$(MAKE) --no-print-directory tizixcmd CMD=$$cx NOCOPY=1 XBLK=$$xb \
			TIZIX_ROOT=$(TIZIX_ROOT) TIZIX_USERBIN=$(TIZIX_USERBIN) || rc=1; \
	done; \
	for o in $(TIZIX_OVLS); do \
		oc=`echo $$o | cut -d: -f1`; oa=`echo $$o | cut -d: -f2`; \
		echo "=== tzcc build (overlay @$$oa): $$oc ==="; \
		$(MAKE) --no-print-directory tizixovl CMD=$$oc OVLADDR=$$oa \
			TIZIX_ROOT=$(TIZIX_ROOT) TIZIX_USERBIN=$(TIZIX_USERBIN) || rc=1; \
	done; \
	for c in $(TIZIX_OVLCMDS); do \
		cx=`echo $$c | cut -d: -f1`; xb=`echo $$c | cut -d: -f2`; \
		echo "=== tzcc build (関数単位オーバーレイ, 追加ブロック $$xb): $$cx ==="; \
		$(MAKE) --no-print-directory tizixovlcmd CMD=$$cx XBLK=$$xb \
			TIZIX_ROOT=$(TIZIX_ROOT) TIZIX_USERBIN=$(TIZIX_USERBIN) || rc=1; \
	done; \
	echo "=== tizixcmds done (rc=$$rc) ==="; exit $$rc

clean:
	rm -rf $(BUILD)
	rm -f $(TARGET) ast.txt

# コンパイルが通る/エラー検出できる、だけを見る軽いテスト
test: $(TARGET)
	@for f in tests/*.c; do \
		echo -n "Testing $$f... "; \
		sr=`sed -n 's#.*//[[:space:]]*TEST:[[:space:]]*skip[[:space:]]\{1,\}##p' $$f | tr -d '\r' | head -1`; \
		if [ -n "$$sr" ]; then echo "SKIP ($$sr)"; continue; fi; \
		timeout 10 ./$(TARGET) -X $$f > /dev/null 2>&1; \
		rc=$$?; \
		if [ $$rc -eq 124 ]; then echo "FAIL (timeout - possible infinite loop)"; exit 1; fi; \
		if [ $$rc -eq 0 ]; then \
			if [[ $$f == *ok_* || $$f == *test_* ]]; then echo "OK"; else echo "FAIL (expected error)"; exit 1; fi; \
		else \
			if [[ $$f == *err_* ]]; then echo "OK"; else echo "FAIL (unexpected error)"; exit 1; fi; \
		fi \
	done

# tests/ok_*.c を cpmsim ベアメタルで実行し、// EXPECT: と実出力を比較する
#   使用例: make runtest            全 ok_*.c
#           make runtest T="ok_18 ok_20"   指定分のみ
runtest: $(TARGET)
	@bash tests/run_rt.sh $(T)

# ---- x86-64 バックエンド ----
# 使用例: make x86 TEST_SRC=tests/ok_01.c   → ネイティブ実行
x86: $(TARGET)
	@if [ -z "$(TEST_SRC)" ]; then echo "Error: TEST_SRC is not set"; exit 1; fi
	./$(TARGET) --march=x86 -o $(BUILD)/x86.s $(TEST_SRC)
	gcc -no-pie -o $(BUILD)/x86prog $(BUILD)/x86.s
	@echo "--- run ---"
	@$(BUILD)/x86prog; echo "(exit $$?)"

# tests/ok_*.c を tzcc --march=x86 → gcc リンク → ネイティブ実行、// EXPECT: と照合
x86test: $(TARGET)
	@bash tests/run_x86.sh $(T)
