# tizix x86-ia16 (8086 リアルモード / PC-AT / QEMU)
#   ツールチェイン: ia16-elf-gcc 6.3.0 + binutils, qemu-system-i386, mtools, mkfs.fat
#   ルート Makefile から `make ARCH=x86-ia16` として叩かれる他、
#   このディレクトリで直接 `make` してもよい。
#
#   M2: 共通 src/ を ia16-gcc で通し sh プロンプトへ。
#   M4: 二段ブート(VBR + gap stage2)+ INT 13h diskio + FAT12 フロッピー。

CC  = ia16-elf-gcc
LD  = ia16-elf-ld
SRC = ../../src

# -mcmodel=tiny: text+data を単一 64KB セグメントに(crt0 で CS=DS=SS=ES 済み)。
# -O2 必須: ia16-gcc 6.3.0 は -Os/-O1/-Og だと近ポインタ引数を隣の 32bit 引数と
#   融合して les(far ロード)を吐く。ff.c の move_window(FATFS*) が壊れ、
#   fs アクセスがセグメント 0(低位 RAM)へ化ける。-O2 だけこのバグを回避。
CFLAGS = -mcmodel=tiny -ffreestanding -fno-pic -fno-stack-protector -O2 \
         -Wall -Wextra -Wno-unused-parameter \
         -DARCH_X86_IA16 -I. -Iinclude -I$(SRC) -include plat.h
OBJ = obj
IMG = disk/floppy.img

# arch 固有(console phys / storage 下位層 / ブート / syscall ディスパッチ /
#   カーネルパイプ非対応の穴埋め stub / /dev/fda 実体)
AOBJ = $(OBJ)/crt0.o $(OBJ)/kmain.o $(OBJ)/console.o $(OBJ)/libc.o \
       $(OBJ)/libgcc.o $(OBJ)/diskio.o $(OBJ)/bios13.o $(OBJ)/proc.o \
       $(OBJ)/sysfile.o $(OBJ)/pipestub.o $(OBJ)/dev_fda.o
# 共有 src/ カーネル中核(FS 非依存にしていく対象。doc「8/31 ディスカッション」)
#   dev.o = デバイステーブル(dev_lookup)。vfs.o が vfs_init() から参照するため必須。
SOBJ = $(OBJ)/init.o $(OBJ)/kernel.o $(OBJ)/io.o $(OBJ)/vfs.o $(OBJ)/dev.o \
       $(OBJ)/sh.o $(OBJ)/builtin.o $(OBJ)/kexec.o
# FS backend(条件 D: arch ごとに差し替え可能な分離。今は FAT のみ。
#   将来 MSX=host FAT12 / PC-8001=RAM ディスク 等に置き換える単位)
#   fsbackend_fat.o = fsbackend.h(fsb_*)界面の FAT 実装。
FSBACKEND = $(OBJ)/fsbackend_fat.o $(OBJ)/fatcmd.o $(OBJ)/ff.o
KOBJ = $(AOBJ) $(SOBJ) $(FSBACKEND)

# フロッピー配置
STAGE2_LBA  = 1
STAGE2_SECS = 8
KERNEL_LBA  = 9
# stage1(1) + stage2(8) + kernel を覆う予約セクタ。kernel 予算 = FAT_RSVD - KERNEL_LBA。
#   ★2026-09-12: カーネルが 58 セクタまで育ち、旧 FAT_RSVD=64(予算55)を超過
#   していた。dd がガード無しで FAT 先頭領域(mkfs.fat -R 分の直後)へ溢れ書き
#   し、FAT のメディアバイトを破壊 → mtools が "Bad media types" で image を
#   開けなくなっていた(z80pack の NSEC 超過と同型のバグ。kernel.bin 側には
#   stage2.bin のような予算チェックが無かったので気づかれずに壊れていた)。
#   160 まで上げて現状比 約2.6倍の余裕を持たせ、下の kernel.bin ルールに
#   予算チェックを追加した。
FAT_RSVD    = 160         # kernel 予算 = (160-9)*512 = 77312B ≈ 151 セクタ

.PHONY: all run test clean
all: $(IMG)

$(OBJ):
	mkdir -p $(OBJ)

$(OBJ)/%.o: %.s | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJ)/%.o: %.c | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- カーネル (フラットバイナリ, 0 起点) ---
$(OBJ)/kernel.bin: $(KOBJ) link-kernel.ld
	$(LD) -T link-kernel.ld -o $@ $(KOBJ)
	@sz=$$(stat -c%s $@); lim=$$(( ($(FAT_RSVD) - $(KERNEL_LBA)) * 512 )); \
	 echo "kernel.bin = $$sz bytes ($$(( ($$sz+511)/512 )) sectors) / 予算 $$lim bytes"; \
	 [ $$sz -le $$lim ] || { echo "*** kernel.bin が FAT_RSVD 予算を超過(FAT 領域を dd で破壊する) ***"; \
	   echo "*** Makefile の FAT_RSVD を上げること ***"; exit 1; }

# --- stage2 が読む実セクタ数を kernel.bin から生成 ---
_layout.inc: $(OBJ)/kernel.bin
	@sz=$$(stat -c%s $<); ks=$$(( ($$sz + 511) / 512 )); \
	 echo "KERNEL_SECS = $$ks" > $@; \
	 echo "generated $@ : KERNEL_SECS = $$ks"

# --- ユーザーコマンド(C。crt0cmd + ulibc とリンク、pseg:0x0100 起点)---
#   コマンドはカーネルとは別プログラム。-ffreestanding -nostdlib、
#   syscall は INT 80h(ulibc.c)。
UCFLAGS = -mcmodel=tiny -ffreestanding -fno-pic -fno-stack-protector -O2 \
          -Wall -Wextra -Iuser -Iinclude
UCRT  = $(OBJ)/crt0cmd.o $(OBJ)/argv_init.o
ULIB  = $(OBJ)/ulibc.o $(OBJ)/usyscall.o

$(OBJ)/crt0cmd.o: user/crt0cmd.s | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<
$(OBJ)/argv_init.o: user/argv_init.c | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<
$(OBJ)/usyscall.o: user/usyscall.s | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<
$(OBJ)/ulibc.o: user/ulibc.c user/stdio.h | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<

# コマンド実体のビルドは下の tzport 経由のみ(arch/x86-ia16/user/ にはコマンド
# ソースを置かない方針。native 用パターンルールは意図的に持たない)。
$(OBJ)/%.bin: $(OBJ)/ucmd_%.o $(UCRT) $(ULIB) user/cmd.ld
	$(LD) -T user/cmd.ld -o $@ $(UCRT) $< $(ULIB)

# ---- tzport: 共有 user/<cmd>.c(tzcc 向け書式)を x86 でもビルドする ----
#   tzcc(tizix/tzcc/)は ia16 の対象外なので、共有ソースへ user/tzstdio.h
#   互換ヘッダを添えて obj/tzport/ へコピーしてからコンパイルする。
#   #include "stdio.h" はクォート付きなので同じディレクトリの tzstdio.h
#   (コピー先では stdio.h という名前)を優先して拾い、user/stdio.h(x86
#   ネイティブコマンド用)には触れない。
#   対応できるのは fopen/fread/fwrite/fgetc/fputc/fgets/fputs/opendir/
#   readdir/closedir/unlink 止まり(tzstdio.h の注記参照)。mkdir/rename/
#   fseek 等を使うコマンドは対象外。
TZSRC       = ../../user
TZPORT_DIR  = $(OBJ)/tzport
#   tail は pipe_tail()(z80 カーネルパイプ専用、x86 未実装)に依存するため対象外。
#
#   ★2026-09-12: arch/x86-ia16/user/ にコマンド実体(ls.c/cp.c/wc.c/hello.c/
#   a.c/b.c)を個別に置くフォーク運用をやめた。ls.c は旧フォークが -l/-h
#   フラグ未対応のまま argv[] トークン化にも追従しておらず、
#   `ls -l /bin` が "-l" をパスとして開こうとして落ちる実害があった。
#   コマンドのソースは必ず共通 user/<cmd>.c を正としてここへコピーする
#   (arch/x86-ia16/user/ に残せるのはビルド基盤 = crt0cmd.s/ulibc.c/
#   usyscall.s/tzstdio.h/argv_init.c/cmd.ld のみ)。
#   xxd は共通 user/ に対応が無い x86 専用デバッグコマンドだったので削除
#   (対応を追加するなら共通 user/xxd.c を新設すること。x86 専用の別実装は作らない)。
TZPORT_CMDS = echo cat head rm touch grep ls cp wc hello a b rsyslog

$(TZPORT_DIR):
	mkdir -p $(TZPORT_DIR)

$(TZPORT_DIR)/stdio.h: user/tzstdio.h | $(TZPORT_DIR)
	cp $< $@

$(TZPORT_DIR)/%.c: $(TZSRC)/%.c | $(TZPORT_DIR)
	cp $< $@

$(OBJ)/ucmd_%.o: $(TZPORT_DIR)/%.c $(TZPORT_DIR)/stdio.h | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<

TZPORT_BINS = $(addprefix $(OBJ)/,$(addsuffix .bin,$(TZPORT_CMDS)))

USERBINS = $(TZPORT_BINS)

# --- stage1 = VBR。コードは file offset 0x3E(BPB を空けて)。---
$(OBJ)/stage1.bin: stage1.s | $(OBJ)
	$(CC) $(CFLAGS) -c -o $(OBJ)/stage1.o $<
	$(LD) -Ttext=0x7c00 -e _start --oformat=binary -o $@ $(OBJ)/stage1.o

# --- stage2 = gap 領域ブートストラップ ---
$(OBJ)/stage2.bin: stage2.s _layout.inc | $(OBJ)
	$(CC) $(CFLAGS) -c -o $(OBJ)/stage2.o $<
	$(LD) -Ttext=0x0000 -e _s2 --oformat=binary -o $@ $(OBJ)/stage2.o
	@sz=$$(stat -c%s $@); lim=$$(( $(STAGE2_SECS) * 512 )); \
	 echo "stage2.bin = $$sz bytes (予約 $$lim)"; \
	 [ $$sz -le $$lim ] || { echo "*** stage2 予約超過 ***"; exit 1; }

# --- 1.44MB FAT12 フロッピーイメージ ---
#   LBA 0        stage1 (VBR。BPB は mkfs、コードは stage1.bin[0x3E..0x1FD])
#   LBA 1 .. 8   stage2
#   LBA 9 ..     kernel blob(予算は FAT_RSVD - KERNEL_LBA セクタ)
#   LBA FAT_RSVD .. FAT12 データ領域(mkfs.fat -R $(FAT_RSVD))
#
#   共有 src/sh.c の launch() は無修飾コマンドを必ず "/bin/<cmd>.bin"(小文字
#   固定)で探す(z80pack と共通のレイアウト規約)。従来はルート直下へ
#   "NAME.BIN"(大文字)で置いていたため、x86 では外部コマンドが軒並み
#   "not found" になっていた。/bin /root を作り、規約どおりに配置する。
$(IMG): $(OBJ)/stage1.bin $(OBJ)/stage2.bin $(OBJ)/kernel.bin $(USERBINS) etc/rc
	mkdir -p disk
	rm -f $@
	mkfs.fat -F 12 -R $(FAT_RSVD) -S 512 -f 2 -r 224 --invariant -n TIZIX -C $@ 1440 >/dev/null
	mmd -i $@ ::bin
	mmd -i $@ ::root
	mmd -i $@ ::etc
	mmd -i $@ ::var
	mmd -i $@ ::var/log
	mcopy -o -i $@ etc/rc ::etc/rc
	@for f in fatfiles/*; do [ -e "$$f" ] && mcopy -o -i $@ "$$f" ::root/ ; done; true
	@for b in $(USERBINS); do \
	   n=$$(basename $$b .bin); \
	   mcopy -o -i $@ $$b ::bin/$$n.bin; done
	# stage1: BPB(0x00..0x3D)と 0x55AA(0x1FE)を残し、コード(0x3E..0x1FD)だけ上書き
	dd if=$(OBJ)/stage1.bin of=$@ bs=1 skip=62 seek=62 count=446 conv=notrunc status=none
	dd if=$(OBJ)/stage2.bin of=$@ bs=512 seek=$(STAGE2_LBA) conv=notrunc status=none
	dd if=$(OBJ)/kernel.bin of=$@ bs=512 seek=$(KERNEL_LBA) conv=notrunc status=none
	@echo "--- image root ---"; mdir -i $@ :: | sed -n '1,20p'
	@echo "--- /bin ---"; mdir -i $@ ::bin | sed -n '1,20p'

QEMU     = qemu-system-i386
QEMUDISK = -drive file=$(IMG),format=raw,if=floppy
QEMUEXIT = -device isa-debug-exit,iobase=0xf4,iosize=0x04
# ★2026-09-12: signal=on だと QEMU の chardev が Ctrl-C を SIGINT として
#   ホスト側で横取りし、ゲストへ生バイトが届かない(前景コマンドを Ctrl-C で
#   止めようとするたびに QEMU ごと終了していた実害)。signal=off でゲストへ
#   素通しし、脱出は console.c 側の Ctrl-](0x1D)専用に統一する
#   (z80pack の cpmsim が Ctrl-\ を脱出キーに使うのと同じ役回り)。
QEMUCON  = -chardev stdio,id=c0,signal=off -serial chardev:c0
QEMUFLAGS = $(QEMUDISK) $(QEMUEXIT) $(QEMUCON) -display none -no-reboot -boot a

# 抜け方: ゲスト内 Ctrl-](0x1D)。Ctrl-C(0x03)はコマンド中断用にゲストへ素通し。
run: $(IMG)
	$(QEMU) $(QEMUFLAGS)

test: $(IMG)
	timeout 6 $(QEMU) $(QEMUFLAGS) < /dev/null || true

clean:
	rm -rf $(OBJ) disk _layout.inc
