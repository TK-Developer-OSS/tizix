#===================================================================
# tizix - top-level dispatcher
#   実体は arch/$(ARCH)/Makefile。ここでは ARCH= の解決と委譲のみ行う
#   (ARCH ごとにツールチェイン/ビルド手順が全く異なるため、共通ロジックを
#    ここに書かない。z80 系(sdcc)の共通部分は arch/common-sdcc.mk)。
#   ARCH=z80pack (既定, cpmsim) / z80board (実機ROM) / x86-ia16 / x86-i386 /
#        m68k-mega / esp32-wroom-32e
#   例: make ARCH=z80board / make ARCH=x86-ia16 run
#===================================================================
ARCH ?= z80pack

# ./configure(#73、リリース版ツリー)が書くツールの場所。無ければ PATH 任せ。
-include config.mk

.DEFAULT_GOAL := all

.PHONY: all sims
all:
	$(MAKE) -C arch/$(ARCH) ARCH=$(ARCH) all

# cpmsim / z80boardsim を z80pack-tizix フォークから作って arch/ へ置く。
# フォークの場所は ./configure --with-z80pack=DIR か make sims Z80PACK_DIR=DIR。
# cpmsim は起動時に補助ポート用の `receive` を ./receive か PATH から exec し、
# 見つからないとプロセスグループごと SIGQUIT で落ちる(z80pack 1.37 の仕様)。
# なので receive も cpmsim の隣へ置く(tzcc の runtest のように別の場所から
# cpmsim を起動する経路は、config.mk が srctools を PATH に足して拾う)。
sims:
	@test -n "$(Z80PACK_DIR)" || { echo "Z80PACK_DIR が未設定(./configure --with-z80pack=DIR か make sims Z80PACK_DIR=DIR)"; exit 1; }
	$(MAKE) -C $(Z80PACK_DIR)/cpmsim/srcsim
	$(MAKE) -C $(Z80PACK_DIR)/cpmsim/srctools receive
	cp $(Z80PACK_DIR)/cpmsim/cpmsim arch/z80pack/cpmsim
	cp $(Z80PACK_DIR)/cpmsim/srctools/receive arch/z80pack/receive
	$(MAKE) -C $(Z80PACK_DIR)/z80boardsim/srcsim
	cp $(Z80PACK_DIR)/z80boardsim/z80boardsim arch/z80board/z80boardsim

# 下の「何でも委譲」に Makefile 自身と config.mk を拾わせない。
Makefile config.mk: ;

# all 以外の全ゴール(clean/disk/user/install/run/test/size/…)は
# そのまま arch/$(ARCH)/Makefile へ委譲する。
%:
	$(MAKE) -C arch/$(ARCH) ARCH=$(ARCH) $@
