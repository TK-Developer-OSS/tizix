#===================================================================
# tizix - arch/z80board (実機 Z80 ボード: Z84C000 + FT245RL + SD/SPI)
#   前半 ROM 0x0000-0x7FFF(XIP)/ 後半 RAM 0x8000-0xFFFF。
#   z80pack と違いブートセクタは無い(フロッピーのような外部ローダが存在しない
#   ROM 直実行)。crt0.s がリセットベクタ(0x0000)を直接持ち、IM1 割り込み
#   ベクタ(0x0038)とソフトベクタ(0x003B〜0x006C)もリンク時に ROM 上へ
#   .org で焼き込む(z80pack の crt0.s のような実行時自己書き換えは ROM では
#   不可能なので採らない)。
#   ルート Makefile から `make ARCH=z80board` として叩かれる他、
#   このディレクトリで直接 `make` してもよい。
#===================================================================
ARCH    = z80board
ROOTDIR = $(abspath ../..)
SRCDIR  = $(ROOTDIR)/src
USERDIR = $(ROOTDIR)/user
OBJDIR  = obj

# アーキ識別マクロ(src/ の #if defined(ARCH_*) 分岐用)。
# sdasz80 は -D 非対応 & asm は arch/ に居るので PLAT_DEF_AS は空。
PLAT_DEF    = -DARCH_Z80BOARD
PLAT_DEF_AS =

# SD 読み書きの詳細ログ(diskio.c の RD/WR lba= を成功時も出す)。既定 OFF。
# `make ARCH=z80board SDDBG=1` で有効。**エラー(SD RD/WR ERR)は SDDBG に
# 関係なく常に出る**。ON にすると ROM 容量を作るため builtin の ps/kill が
# 外れる(builtin.c)。uptime/tree/df は外部コマンド化の方針で常に無効
# (task.md #56)。2026-09-19 の実機ブリングアップ中は既定 ON にしていた。
# ROM は 0x8000 ハード上限(2026-09-19 実タイマ割り込み化(#59)後、OFF 空き 21B。
# 触る前に必ず `ROM image end` の空き数を確認 ── 既存コメントの数値は
# すぐ陳腐化するので鵜呑みにしない)。
# `ROM image end` の表示を必ず確認すること。
# ★ヘッダ(kmem.h 等)を触った後は make clean してから(task.md #55)。
SDDBG ?= 0
ifneq ($(SDDBG),0)
PLAT_DEF += -DSD_DEBUG
endif

# arch 固有カーネルオブジェクト。
#   spi.s   = bit-bang SPI 物理層(実機 ~/z80pack/bios/spi.s を移植)。
#   sdcard.s = SD SPI モードプロトコル(sd_init/sd_data_read/sd_data_write)。
#   diskio.c がこれらを呼ぶ。
#   boot.s はここには含めない ── あれは SD/コンソール単体ブリングアップ用の
#   独立モニタ(自前の reset ベクタ 0x0000 を持つ)で、crt0.s と衝突する。
#   カーネルには要らない。実機の SPI 配線確認だけに単独で焼く用途。
KOBJ_ARCH = spi.rel sdcard.rel

# _HEADER(リセット+IM1+ソフトベクタ、crt0.s が .org で直書き)は 0x0000-0x006C。
# 0x006D 以降が空き(memory: 低位ベクタはハードウェア用)。_CODE はそこから
# 少し余裕を見て 0x0080 から(boot.s の単体モニタも同じ流儀で 0x0080 開始)。
CODE_LOC = 0x0080
DATA_LOC = 0x8000

DISK   = boot.rom
FATDSK = sdcard.img

include ../common-sdcc.mk

# ---- ROM イメージ(XIP。ブートローダ無しでそのまま 0x0000 から実行) ----
#   makebin -s 32768 で 0x0000..0x7FFF を丸ごと切り出す。ROM チップの実容量が
#   32KB(0x8000)未満なら焼き込み側で別途パディング/分割すること
#   (ここでは ROM 上限 = DATA_LOC = 0x8000 をハードガードにする)。
$(DISK): $(OBJDIR)/kernel.ihx
	makebin -s 32768 $< $(OBJDIR)/kernel.img
	@end=`awk '/^_(CODE|HOME|GSINIT|GSFINAL|INITIALIZER) +[0-9A-Fa-f]+ +[0-9A-Fa-f]+ *=/ { a=strtonum("0x" $$2); s=strtonum("0x" $$3); if (a+s > m) m = a+s } END { printf "%d", m+0 }' $(OBJDIR)/kernel.map`; \
	lim=32768; \
	if [ "$$end" -gt "$$lim" ]; then \
	  awk -v e=$$end -v l=$$lim 'BEGIN{printf "*** kernel image end 0x%04X > ROM limit 0x%04X。コードを削れ(DATA_LOC=0x8000 固定) ***\n", e, l}' >&2; \
	  exit 1; \
	fi; \
	awk -v e=$$end -v l=$$lim 'BEGIN{printf "ROM image end 0x%04X / ROM limit 0x%04X  (free %d B)\n", e, l, l-e}'
	cp $(OBJDIR)/kernel.img $@

# boot.s 用のターゲット(SPI/UART 配線確認用の単体モニタ。tizix 本体ではない)
#   出力は monitor.rom($(DISK)=boot.rom とは別名。以前は同名で、make boot が
#   本物のカーネル ROM を上書きしてしまうバグがあったので分離した)。
#   boot.s は crt0.s と同じ sdasz80 方言(equ ではなく =、即値は # 必須、
#   .area (ABS) 宣言必須)。カーネルと同じく spi.s/sdcard.s は #include せず
#   spi.rel/sdcard.rel を別リンクする(z80pack が基準。zasm 方言や #include
#   による1ファイル化は使わない)。boot.s 自体は 0x0000-0x4000+ を .area
#   _HEADER (ABS) で固定配置しているので、spi.rel/sdcard.rel の _CODE
#   (relocatable) は衝突しない安全な余白(0x5000)から --code-loc で置く。
boot: $(OBJDIR)/boot.ihx
	makebin -s 32768 $< monitor.rom

$(OBJDIR)/boot.ihx: $(OBJDIR)/boot.rel $(OBJDIR)/spi.rel $(OBJDIR)/sdcard.rel
	$(CC) $(MZ80) --no-std-crt0 --code-loc 0x5000 --data-loc 0x8000 \
	      $(OBJDIR)/boot.rel $(OBJDIR)/spi.rel $(OBJDIR)/sdcard.rel -o $@

$(OBJDIR)/boot.rel: boot.s
	$(AS) $(ASFLAGS) -o $@ $<

# 実行ターゲット(z80boardsim 上で tizix を起動。他アーキ(z80pack/cpmsim,
# m68k-mega/m68ksim, x86-ia16/QEMU)の run と同じ「即インタラクティブに
# 入れる」体験に揃える。実機には無いタイマ割り込みを z80boardsim が
# 補って動かしている(z80boardsim/srcsim/iosim.c 参照)。
# boot.rom は raw バイナリで -x が読める形式ではないため、-x には
# Intel-HEX である $(OBJDIR)/kernel.ihx をそのまま渡す。
#
# $(FATDSK) を Make の通常の依存関係経由で要求すると、common-sdcc.mk の
# user-bins/tzcc-cmds が .PHONY のため、変更が無くても毎回 tzcc 側の全外部
# コマンド(cat/date/... 約35個)が無条件に再ビルドされ、make run のたびに
# 数十秒〜分オーダーで待たされる(common-sdcc.mk は z80pack 等の他アーキも
# 共有するので、そちらは触らずここだけで対処する)。
# ここでは run 専用に、実際のタイムスタンプを自前でチェックし、ソース
# (tizix/user/*.c)や kernel.ihx が $(FATDSK) より新しいときだけ
# $(MAKE) $(FATDSK)(= common-sdcc.mk の通常の再ビルド経路)を呼ぶ。
run: $(OBJDIR)/kernel.ihx
	@if [ ! -f $(FATDSK) ] \
	    || [ -n "$$(find $(USERDIR) -name '*.c' -newer $(FATDSK) 2>/dev/null)" ] \
	    || [ $(OBJDIR)/kernel.ihx -nt $(FATDSK) ]; then \
		$(MAKE) $(FATDSK); \
	else \
		echo "$(FATDSK) は最新(user/*.c・kernel.ihx に変更なし) — tzcc 再ビルドをスキップ"; \
	fi
	./z80boardsim -x $(OBJDIR)/kernel.ihx

# 実機転送ガイド(ROM ライター/SD カード書き込み手順の表示のみ)
flash: $(DISK) $(FATDSK)
	@echo "--------------------------------------------------------"
	@echo "実機への転送手順:"
	@echo "1. boot.rom を ROM ライターで Z80 ボードの ROM へ書き込む"
	@echo "2. sdcard.img を Rufus の「DD イメージ」で SD カードへ書き込む"
	@echo "--------------------------------------------------------"
