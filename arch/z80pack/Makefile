#===================================================================
# tizix - arch/z80pack (cpmsim, フロッピー FDC)
#   drive A = boot + kernel、drive B = FAT。実機 ROM ではなくフロッピー
#   ブート前提(boot セクタ + カーネルをセクタ列に並べる)。
#   ルート Makefile から `make ARCH=z80pack` として叩かれる他、
#   このディレクトリで直接 `make` してもよい。
#   中間物は obj/ 、最終イメージは disks/ 。
#===================================================================
ARCH    = z80pack
ROOTDIR = $(abspath ../..)
SRCDIR  = $(ROOTDIR)/src
USERDIR = $(ROOTDIR)/user
OBJDIR  = $(ROOTDIR)/build/arch/z80pack/obj

# アーキ識別マクロ(src/ の #if defined(ARCH_*) 分岐用)。
# sdasz80 は -D 非対応 & asm は arch/ に居るので PLAT_DEF_AS は空。
PLAT_DEF    = -DARCH_Z80PACK
PLAT_DEF_AS =

CODE_LOC = 0x0120
DATA_LOC = 0x8000
NSEC     = 254          # kernel image sectors。boot.s の NSEC と一致必須。
                       # VFS 一本化 5a+5b(wait/wake + カーネルパイプ)でカーネルがピーク。
                       # 254 = ハード上限(限界 0x8000 = DATA_LOC)。Step 7 以降のコマンド
                       # 外部化で縮むので、その後 NSEC を戻せる。

# arch 固有カーネルオブジェクト。
#   #32 で dev_fda.c は不要になった。生ブロック転送は diskio.c の
#   disk_raw_rw()(ドライブ番号を明示で取る 512B セクタ転送)へ一本化し、
#   src/dev.c がそれを直接 extern する。旧 dev_fda.c は dev_ops(関数ポインタ表)
#   前提で、しかも pdrv を無視して常に drive B を読んでいた ── 名前は fda なのに
#   実体は B、かつ誰からも呼ばれていなかった。
#   console.c = コンソールの物理層(層1。約束は src/console.h)。2026-09-30 に
#   ここへ足した ── 旧 arch.mk の時代も含めて一度もリンクされておらず、同じ中身が
#   src/io.c に直書きされていた(カーネル +45 バイト)。
KOBJ_ARCH = console.rel

DISK   = disks/drivea.dsk
FATDSK = disks/driveb.dsk

include ../common-sdcc.mk

# ---- boot セクタ ----
$(OBJDIR)/boot.bin: $(OBJDIR)/boot.rel
	sdldz80 -n -i $(OBJDIR)/boot.ihx $<
	makebin -s 128 $(OBJDIR)/boot.ihx $@

# ---- カーネルイメージ(セクタ列へ載せる生バイナリ)----
#   makebin -s 32768 で 0x0000..0x7FFF を吐き、先頭 256B(=2x128, 0x0000-0x00FF)を
#   落として 0x0100 起点にする。count=NSEC 分だけ切り出す。
#
#   ★サイズガード: ロード窓 = 0x0100 + NSEC*128。コード系エリア
#   (_CODE/_HOME/_GSINIT/_GSFINAL/_INITIALIZER)の終端がこれを超えると、
#   dd がイメージ尻をサイレント切り詰め → boot 後に未ロード番地へ落ちて
#   Op-code trap。ここで検出してビルドを止める。
$(OBJDIR)/kernel.bin: $(OBJDIR)/kernel.ihx
	makebin -s 32768 $< $(OBJDIR)/kernel.img
	@end=`awk '/^_(CODE|HOME|GSINIT|GSFINAL|INITIALIZER) +[0-9A-Fa-f]+ +[0-9A-Fa-f]+ *=/ { a=strtonum("0x" $$2); s=strtonum("0x" $$3); if (a+s > m) m = a+s } END { printf "%d", m+0 }' $(OBJDIR)/kernel.map`; \
	lim=`expr 256 + $(NSEC) \* 128`; \
	if [ "$$end" -gt "$$lim" ]; then \
	  awk -v e=$$end -v l=$$lim 'BEGIN{printf "*** kernel image end 0x%04X > load limit 0x%04X (NSEC=%d). NSEC を上げるかコードを削れ ***\n", e, l, (l-256)/128}' >&2; \
	  exit 1; \
	fi; \
	awk -v e=$$end -v l=$$lim 'BEGIN{printf "kernel image end 0x%04X / load limit 0x%04X  (free %d B)\n", e, l, l-e}'
	dd if=$(OBJDIR)/kernel.img of=$@ bs=128 skip=2 count=$(NSEC) 2>/dev/null

# ---- drive A (boot + kernel) ----
$(DISK): $(OBJDIR)/boot.bin $(OBJDIR)/kernel.bin
	mkdir -p disks
	rm -f $(DISK)
	dd if=/dev/zero of=$(DISK) bs=128 count=2002 2>/dev/null
	dd if=$(OBJDIR)/boot.bin   of=$(DISK) bs=128 seek=0 conv=notrunc 2>/dev/null
	dd if=$(OBJDIR)/kernel.bin of=$(DISK) bs=128 seek=1 conv=notrunc 2>/dev/null

# 実行ターゲット(cpmsim 上で tizix を起動)
run: $(DISK) $(FATDSK)
	./cpmsim $(DISK) $(FATDSK)
