#===================================================================
# arch/common-sdcc.mk -- z80 (sdcc) 系アーキ共通ビルドロジック
#   z80pack / z80board の各 arch/<ARCH>/Makefile から include される。
#   include 元(呼び出し側)が先に以下を定義しておくこと:
#     ARCH        アーキ名(自分のディレクトリ名と一致させる)
#     ROOTDIR     tizix リポジトリルートへの絶対パス
#     SRCDIR      共通カーネル src/ への絶対パス
#     USERDIR     共通 user/ への絶対パス
#     OBJDIR      中間生成物ディレクトリ(build/arch/<arch>/obj)
#     PLAT_DEF / PLAT_DEF_AS   -D 定義 (C / asm)
#     CODE_LOC / DATA_LOC / NSEC
#     KOBJ_ARCH   arch 固有カーネルオブジェクト(無ければ空)
#     DISK / FATDSK            最終イメージ(このディレクトリからの相対パス)
#   呼び出し側は本ファイルを include した後に、$(DISK) を組み立てる
#   arch 固有ルール(boot/ROM イメージ生成など)を追加で書くこと。
#===================================================================
CC     = sdcc
AS     = sdasz80
AR     = sdar
MZ80   = -mz80

.DEFAULT_GOAL := all

# --max-allocs-per-node: SDCC のレジスタ割付探索を深くする。
#   意味論は不変。コードサイズが数%縮むことが多い代わりに
#   ff.c のコンパイルが目に見えて遅くなる。重ければ 3000(既定)へ戻す。
# ABI 統一: 全域 --sdcccall 0(全スタック渡し)。ドライバ/ユーザーコマンドと一致。
#   標準ライブラリ(sdcccall 1)と衝突する div/mul/mod/memcmp/strcmp は
#   src/ivthelpers.c で自前供給し、z80.lib の該当モジュールを引かせない。
CFLAGS  = $(MZ80) --reserve-regs-iy -c --sdcccall 0 --opt-code-size --max-allocs-per-node 25000 -I$(SRCDIR) $(PLAT_DEF)
ASFLAGS = $(PLAT_DEF_AS)

# ---- リンク対象 ----
#   sh.rel は z80 では外部コマンド化(user/sh.c → /bin/sh.bin)したのでカーネル
#   像から外す(~2KB 減。NSEC=254 の天井対策)。
KOBJ_BASE = crt0.rel init.rel kernel.rel builtin.rel kexec.rel ff.rel diskio.rel fatcmd.rel vfs.rel pipe.rel dev.rel
KOBJ = $(KOBJ_BASE) $(KOBJ_ARCH)
KRELS   = $(addprefix $(OBJDIR)/, $(KOBJ))
IVT_REL = $(OBJDIR)/ivthelpers.rel

.PHONY: all disk size cleandisk user user-bins tzcc-cmds install zip clean

all: $(DISK) $(FATDSK)

disk: $(FATDSK)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# 中間物は全部 $(OBJDIR)/ へ。sdcc は .asm/.lst/.sym も -o と同じディレクトリに吐く。
$(OBJDIR)/%.rel: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $<

# arch 固有ソース(このディレクトリ直下の .c / .s)
$(OBJDIR)/%.rel: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.rel: %.s | $(OBJDIR)
	$(AS) $(ASFLAGS) -o $@ $<

# crt0.s は ../common-z80/ の断片(宣言と定数 / RAM とスケジューラ表の初期化 /
# スケジューラ本体と _kexit / エリア順と gsinit)を .include する。z80pack と
# z80board で同じだった部分を 1 か所に寄せたもの。sdasz80 は -D を持たないが
# .include と .if / .ifdef は使える(下の sdcc -MM のような依存の自動生成は無いので、
# ここで明示する)。
$(OBJDIR)/crt0.rel: $(wildcard ../common-z80/*.inc)

$(OBJDIR)/ivthelpers.rel: $(SRCDIR)/ivthelpers.c | $(OBJDIR)
	$(CC) $(MZ80) --reserve-regs-iy -c --sdcccall 0 --opt-code-size -o $@ $<

# ---- ヘッダ依存は sdcc -MM で自動生成する(#55)----
#   以前は下のような手書きの依存を並べていたが、漏れがあった
#   (dev.rel / ff.rel / ivthelpers.rel / builtin.rel が kmem.h を知らず、
#   KYIELD / IRQ_ON を変えても再コンパイルされずに古いマクロのまま ROM に
#   入った ── z80board 実機ブリングアップで実害)。
#   .c ごとに $(OBJDIR)/<name>.d を作り、`<rel> <d>: <c> <全ヘッダ>` を持たせる。
#   .d 自身もヘッダに依存させるので、#include を増やせば次回に追従する。
#   アセンブラ(.s)は対象外(crt0.s の .include だけは上で手書きしている)。
DEP_CFLAGS = $(filter-out -c,$(CFLAGS))
KDEPS = $(foreach r,$(KOBJ) io.rel ivthelpers.rel,\
          $(if $(wildcard $(SRCDIR)/$(r:.rel=.c) $(r:.rel=.c)),$(OBJDIR)/$(r:.rel=.d)))

$(OBJDIR)/%.d: $(SRCDIR)/%.c | $(OBJDIR)
	@$(CC) -MM $(DEP_CFLAGS) $< | sed 's|^[^:]*:|$(OBJDIR)/$*.rel $@:|' > $@.tmp && mv $@.tmp $@

$(OBJDIR)/%.d: %.c | $(OBJDIR)
	@$(CC) -MM $(DEP_CFLAGS) $< | sed 's|^[^:]*:|$(OBJDIR)/$*.rel $@:|' > $@.tmp && mv $@.tmp $@

ifeq ($(filter clean,$(MAKECMDGOALS)),)
-include $(KDEPS)
endif

$(OBJDIR)/io.lib: $(OBJDIR)/io.rel
	rm -f $@
	$(AR) -rc $@ $(OBJDIR)/io.rel

kernel.ihx: $(OBJDIR)/kernel.ihx

$(OBJDIR)/kernel.ihx: $(KRELS) $(OBJDIR)/io.lib $(IVT_REL)
	$(CC) $(MZ80) --no-std-crt0 --code-loc $(CODE_LOC) --data-loc $(DATA_LOC) \
	      $(KRELS) $(IVT_REL) -L$(OBJDIR) -lio -o $@

size: $(OBJDIR)/kernel.ihx
	@grep -E "s__CODE|s__DATA|s__GSINIT|s__INITIALIZER" $(OBJDIR)/kernel.map
	@if [ -n "$(NSEC)" ]; then printf "load limit = 0x%04X (NSEC=$(NSEC))\n" $$((0x0100 + $(NSEC)*128)); fi

$(FATDSK): user-bins
	sh mkfatdisk.sh

# cleandisk: driveb.dsk を捨てて mkfs から作り直す(通常の make は /bin を
#   上書きするだけで永続。テストが溜めた /root の中身ごと消したいとき用)。
cleandisk:
	rm -f $(FATDSK)
	$(MAKE) $(FATDSK)

# coreutils(#26)は tzcc でビルドする。iy_reg 卒業。
#   tizix リポジトリ同居の tzcc/(既定 $(ROOTDIR)/tzcc)の tizixcmds が
#   build/arch/$(ARCH)/user/*.bin を吐き、mkfatdisk.sh がそれを glob する。
#   TIZIX_USERBIN/TIZIX_DRIVEB は tzcc 側で ?= 上書き可能な変数
#   (既定は z80pack 決め打ち)なので、ARCH= に応じて明示的に渡す。
#   user/ のサブ make は sh + 開発用スクラッチ(hello/a/b/…)+ DRIVER.BIN のみ。
#   別クローンで検証したい場合は TZCC_DIR=/path/to/tzcc で上書き可能。
TZCC_DIR ?= $(ROOTDIR)/tzcc

tzcc-cmds:
	@test -d $(TZCC_DIR) || { echo "TZCC_DIR=$(TZCC_DIR) が無い。git clone 先を TZCC_DIR= で指定"; exit 1; }
	$(MAKE) -C $(TZCC_DIR) tizixcmds TIZIX_ROOT=$(ROOTDIR) \
		TIZIX_USERBIN=$(ROOTDIR)/build/arch/$(ARCH)/user TIZIX_DRIVEB=$(CURDIR)/$(FATDSK)

# user/ の sh・スクラッチ・DRIVER.BIN を先に、その後 coreutils を tzcc で
# 上書き生成(user clean が build/arch/$(ARCH)/user を消すため順序が重要)。
# user/Makefile 自体も ARCH= を受けて出力先を build/arch/$(ARCH)/user へ向ける。
user-bins: $(OBJDIR)/kernel.ihx
	$(MAKE) -C $(USERDIR) ARCH=$(ARCH)
	$(MAKE) tzcc-cmds

user: $(OBJDIR)/kernel.ihx
	$(MAKE) -C $(USERDIR) ARCH=$(ARCH) clean all
	$(MAKE) tzcc-cmds

clean:
	rm -rf $(OBJDIR)
	rm -f $(ROOTDIR)/fatimg.raw $(DISK) $(FATDSK)
	$(MAKE) -C $(USERDIR) ARCH=$(ARCH) clean

install:
	$(MAKE) -C $(USERDIR) install

zip:
	cd $(ROOTDIR) && zip -r tizix_z80.zip src/ arch/ python/ user/*.c user/*.h Makefile user/Makefile doc/
