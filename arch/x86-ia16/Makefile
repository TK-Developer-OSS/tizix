# tizix x86-ia16 (8086 リアルモード / PC-AT / QEMU)
#   ツールチェイン: ia16-elf-gcc 6.3.0 + binutils, qemu-system-i386, mtools, mkfs.fat
#   ルート Makefile から `make ARCH=x86-ia16` として叩かれる他、
#   このディレクトリで直接 `make` してもよい。
#
#   M2: 共通 src/ を ia16-gcc で通し sh プロンプトへ。
#   M4: 二段ブート(VBR + gap stage2)+ INT 13h diskio + FAT12 フロッピー。

# The default build is the tzcc kernel with the resident DRIVER (Makefile.tz: obj/tz, disk/floppy_tz.img), so that
# `make ARCH=x86-ia16 clean all run` from the top builds and runs it. GCC=1 builds the ia16-gcc kernel (obj,
# disk/floppy.img) with the rules below. Makefile.tz includes this file with UCMD_DRV set, which also takes them.
ifeq ($(UCMD_DRV)$(GCC),)
.PHONY: all clean run test
all clean run test:
	$(MAKE) -f Makefile.tz OBJ=obj/tz IMG=disk/floppy_tz.img $@
else

CC  = ia16-elf-gcc
LD  = ia16-elf-ld
SRC = ../../src

# -mcmodel=tiny: text+data を単一 64KB セグメントに(crt0 で CS=DS=SS=ES 済み)。
# -O2 必須: ia16-gcc 6.3.0 は -Os/-O1/-Og だと近ポインタ引数を隣の 32bit 引数と
#   融合して les(far ロード)を吐く。ff.c の move_window(FATFS*) が壊れ、
#   fs アクセスがセグメント 0(低位 RAM)へ化ける。-O2 だけこのバグを回避。
# -I$(SRC)/libc: freestanding の <string.h>。mem* / str* の実体(libc.o)も
#   そこの libc.c を共用する(以前は同じ中身を arch 側に持っていた。m68k-mega /
#   esp32-wroom-32e と共通。リンク順を変えないよう libc.o は AOBJ の同じ位置に残す)。
# FDD: フロッピーの種類(環境変数でも make の引数でも指定できる。既定 pcat)。幾何を stage2.s / bios13.s へ
#   -Wa,--defsym,FDD_SPT= で渡し、ディスク像の大きさ(mkfs.fat -C の KB)を決める。
#     pcat = 1.44MB(PC/AT 以降の 3.5" 2HD、IBM 純正 USB FDD など。80 cyl・2 head・18 spt)  ← 既定。ここは変えない
#     pcxt = 360KB(PC/XT の 5.25" 2D。40 cyl・2 head・9 spt)。ネット系コマンドは入れない(容量)
#            (xt は pcxt の別名)
FDD ?= pcat
ifeq ($(FDD),xt)
override FDD := pcxt
endif
ifeq ($(FDD),pcat)
FDD_SPT = 18
FDD_KB  = 1440
else ifeq ($(FDD),pcxt)
FDD_SPT = 9
FDD_KB  = 360
else
$(error FDD は pcat か pcxt(別名 xt)を指定する(FDD=$(FDD)))
endif

# カーネルのサイズ削減(TK 2026-10-23「z80pack 並みにシビアに」): -O2 のまま、膨らみやすい最適化だけ止める(約 -15%)。
#   -Os は ia16-gcc 6.3.0 の far ポインタ融合バグがあるので使わない(上記)。-fno-inline / -fno-gcse / -fno-reorder-blocks /
#   -fno-caller-saves / -fno-optimize-sibling-calls。
# FATFS=1: the shared FatFs (src/ff.c, as on the z80); 0: the minimal FAT12 (fat12min.c, -DFAT12_MIN)
FATFS ?= 1
ifeq ($(FATFS),1)
FATDEF =
FATOBJ = $(OBJ)/ff.o
else
FATDEF = -DFAT12_MIN
FATOBJ = $(OBJ)/fat12min.o
endif
KOPT = -O2 -fno-inline -fno-gcse -fno-reorder-blocks -fno-caller-saves -fno-optimize-sibling-calls
CFLAGS = -Wa,--defsym,FDD_SPT=$(FDD_SPT) -mcmodel=tiny -ffreestanding -fno-pic -fno-stack-protector $(KOPT) \
         -Wall -Wextra -Wno-unused-parameter \
         -DARCH_X86_IA16 $(FATDEF) $(EXTSH_DEF) -I. -I$(SRC) -I$(SRC)/libc -include plat.h
OBJ = obj
IMG = disk/floppy.img

# arch 固有(console phys / storage 下位層 / ブート / syscall ディスパッチ /
#   カーネルパイプ非対応の穴埋め stub / /dev/fda 実体)+ 共有の libc.o
# コンソールは arch/x86-ia16/console.c 1 本: VGA テキスト(B800:0 直書き)+ PS/2 キーボード(8042 直読み)
# + COM1(16550 を起動時に検出し、無ければ使わない)。以前の COM1 専用版と画面版の分離は廃止(統合)。
# プロセススロットの最大数(plat.h の X86_MAXSLOT)。crt0.s へ -Wa,--defsym,X86_N= で渡す。
X86_MAXSLOT := $(shell sed -n 's/^\#define X86_MAXSLOT *\([0-9][0-9]*\).*/\1/p' plat.h)
X86_SCSTK_SHIFT := $(shell sed -n 's/^\#define X86_SCSTK_SHIFT *\([0-9][0-9]*\).*/\1/p' plat.h)
# PERSLOT: syscall スタックをスロットごとに持つ(1、既定)/ 従来の単一スタック(0)。1 だとパイプ
# (src/pipe.c)が使える。※以前 CONSOLE=vga + PERSLOT=1 が停止していた(#182)のは、bios13_rw が ES を
# 壊して戻り、ia16-gcc が ES に置いたポインタ(f_write の bw)がカーネル 0x1000 番地を書き換えて
# いたため(配置次第でそこが除算コードになり暴走)。bios13.s で ES を保存して解消。
PERSLOT ?= 1
ifeq ($(PERSLOT),1)
PIPEOBJ = $(OBJ)/pipe.o
else
PIPEOBJ = $(OBJ)/pipestub.o
endif

AOBJ = $(OBJ)/crt0.o $(OBJ)/kmain.o $(OBJ)/console.o $(OBJ)/libc.o \
       $(OBJ)/libgcc.o $(OBJ)/diskio.o $(OBJ)/bios13.o $(OBJ)/proc.o \
       $(OBJ)/sysfile.o $(OBJ)/dev_fda.o $(PIPEOBJ)
# 共有 src/ カーネル中核(FS 非依存にしていく対象。doc「8/31 ディスカッション」)
#   dev.o = デバイステーブル(dev_lookup)。vfs.o が vfs_init() から参照するため必須。
# EXTSH=1(既定): シェルは外部コマンド(/bin/sh.bin = 共有 user/sh.c)で、カーネルから外す(約 4.6KB)。
#   init が PID 1 として起動し、終了したら respawn する(z80 / m68k-mega / esp32 と同じ)。EXTSH=0 で従来のカーネル内蔵。
EXTSH ?= 1
ifeq ($(EXTSH),1)
SHOBJ =
EXTSH_DEF = -DX86_EXTSH
else
SHOBJ = $(OBJ)/sh.o
EXTSH_DEF =
endif
SOBJ = $(OBJ)/init.o $(OBJ)/kernel.o $(OBJ)/io.o $(OBJ)/vfs.o $(OBJ)/dev.o \
       $(SHOBJ) $(OBJ)/builtin.o $(OBJ)/kexec.o
# FS backend(条件 D: arch ごとに差し替え可能な分離。今は FAT のみ。
#   将来 MSX=host FAT12 / PC-8001=RAM ディスク 等に置き換える単位)
#   fsbackend_fat.o = fsbackend.h(fsb_*)界面の FAT 実装。
# FatFs 本体(ff.c、14.6KB)は載せず、最小の FAT12(fat12min.c)にする(カーネルの 64KB セグメントが窮屈なため)
FSBACKEND = $(OBJ)/fsbackend_fat.o $(OBJ)/fatcmd.o $(FATOBJ)
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

HDRS = $(wildcard ../../include/*.h) $(wildcard $(SRC)/*.h) $(wildcard $(SRC)/libc/*.h) $(wildcard *.h)
UHDRS = $(wildcard ../../include/*.h) $(wildcard user/*.h)

.PHONY: all run test clean
all: $(IMG)

$(OBJ):
	mkdir -p $(OBJ)

$(OBJ)/%.o: %.s | $(OBJ)
	$(CC) $(CFLAGS) -Wa,--defsym,PERSLOT=$(PERSLOT) -Wa,--defsym,X86_N=$(X86_MAXSLOT) -Wa,--defsym,SCSTK_SHIFT=$(X86_SCSTK_SHIFT) -c -o $@ $<
$(OBJ)/%.o: %.c $(HDRS) | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJ)/%.o: $(SRC)/%.c $(HDRS) | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJ)/%.o: $(SRC)/libc/%.c $(HDRS) | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- カーネル (フラットバイナリ, 0 起点) ---
$(OBJ)/kernel.bin: $(KOBJ) link-kernel.ld
	$(LD) -T link-kernel.ld -o $@ $(KOBJ)
	@sz=$$(stat -c%s $@); lim=$$(( ($(FAT_RSVD) - $(KERNEL_LBA)) * 512 )); \
	 echo "kernel.bin = $$sz bytes ($$(( ($$sz+511)/512 )) sectors) / 予算 $$lim bytes"; \
	 [ $$sz -le $$lim ] || { echo "*** kernel.bin が FAT_RSVD 予算を超過(FAT 領域を dd で破壊する) ***"; \
	   echo "*** Makefile の FAT_RSVD を上げること ***"; exit 1; }

# --- stage2 が読む実セクタ数を kernel.bin から生成 ---
#   _layout.inc は OBJ の外にあり、OBJ を変えたビルド(Makefile.tz の obj/tz)と共有になる。日付だけで判断すると
#   別のビルドが書いた値が残る(tz 版のカーネルの後ろ側が読み込まれず起動しなかった)ので、毎回中身を照らし、
#   変わったときだけ書き換える(書き換えたら stage2 が作り直される)。
_layout.inc: $(OBJ)/kernel.bin FORCE
	@sz=$$(stat -c%s $<); ks=$$(( ($$sz + 511) / 512 )); \
	 if [ "$$(cat $@ 2>/dev/null)" != "KERNEL_SECS = $$ks" ]; then \
	   echo "KERNEL_SECS = $$ks" > $@; echo "generated $@ : KERNEL_SECS = $$ks"; fi
.PHONY: FORCE
FORCE:

# --- ユーザーコマンド(C。crt0cmd + ulibc とリンク、pseg:0x0100 起点)---
#   コマンドはカーネルとは別プログラム。-ffreestanding -nostdlib、
#   syscall は INT 80h(ulibc.c)。
UCFLAGS = -mcmodel=tiny -ffreestanding -fno-pic -fno-stack-protector -O2 \
          -Wall -Wextra -Iuser -I$(SRC)/libc
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
# Commands that want memory above bss + stack when the pool has room (the .BIN header word __xwant, see crt0cmd.s).
# vi: its text buffer is the rest of its own blocks (tzstdio.h getxbase / getxsize), 8KB when it can get it.
XWANT_CMDS = vi
XWANT_vi = 8192
# UCMD_DRV (Makefile.tz): commands use the resident DRIVER and Makefile.tz links them; this rule is the gcc kernel's.
ifndef UCMD_DRV
$(OBJ)/%.bin: $(OBJ)/ucmd_%.o $(UCRT) $(ULIB) $(OBJ)/libc.o user/cmd.ld
	$(LD) -T user/cmd.ld $(if $(filter $*,$(XWANT_CMDS)),--defsym=__xwant=$(XWANT_$*)) -o $@ $(UCRT) $(OBJ)/libc.o $< $(if $(filter $*,$(NET_CMDS)),$(NETOBJ) $(OBJ)/nstd16.o $(OBJ)/libgcc.o) $(ULIB)
endif

# ネットワーク系コマンド(共有 user/<cmd>.c、netcli.h の口を使う)は、NET の下回り(netmodem16.o / netcli16.o)をリンクする。
# NET: ネットワークの下回り(環境変数でも make の引数でも指定できる。既定 modem)。
#     modem = RS-232C モデム(Hayes AT、ATDT host:port)  ← 原則(TK 2026-10-23)。netmodem16.c
#     esp   = ESP-AT(AT+CIPSTART ほか。dig / ping / ifconfig / mcpd も使える)  ← 一時的な代用。netcli16.c
NET ?= modem
ifeq ($(NET),modem)
NETOBJ = $(OBJ)/netmodem16.o
else ifeq ($(NET),esp)
NETOBJ = $(OBJ)/netcli16.o
else
$(error NET は modem か esp を指定する(NET=$(NET)))
endif

# コマンド名は esp32-wroom-32e の ARCH_UCMDS(telnet nettest ifconfig netstat ping dig curl arp nc)に合わせる。
# ほかに ntpdate(起動時の時計合わせ。TK 指示)・wifi(AT コマンド)と、z80 由来の atcli・tzftp。
NET_CMDS = telnet nettest atcli tzftp dig ping curl nc ifconfig netstat arp ntpdate wifi
# mcpd(MCP サーバ)は「外部へサービスを提供する」ためのもので、ESP32 のポート(内蔵 WiFi を外へ使わせる側)の用途。
# ia16 のネットワークは機械自身が内部で使う(クライアント側)ので、モデム版には載せない。ESP-AT 版(暫定)だけに残す。
ifeq ($(NET),esp)
NET_CMDS += mcpd
endif
$(addprefix $(OBJ)/,$(addsuffix .bin,$(NET_CMDS))): $(NETOBJ) $(OBJ)/nstd16.o $(OBJ)/libgcc.o
$(OBJ)/netcli16.o $(OBJ)/netmodem16.o $(OBJ)/nstd16.o: $(OBJ)/%.o: user/%.c user/tzstdio.h ../../user/netcli.h | $(OBJ)
	$(CC) $(UCFLAGS) -I../../user -c -o $@ $<

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
# コマンド一覧は全アーキ共通の arch/common-ucmds.mk(UCMDS_BASE)から取る(#181)。x86 でビルドしないもの:
#   sh     = カーネル内蔵(src/sh.c が SOBJ。外部 sh.bin は不要)
#   tzsh   = 外部 sh(tzsh)向け syscall 22〜34 が未実装
#   mbtest = 郵便受け syscall 47〜51 が未実装
#   dd     = /dev/fda 読みの #165 が未検証(Makefile から外れていた。LOW)
include ../common-ucmds.mk
ifeq ($(EXTSH),1)
X86_SKIP = tzsh mbtest dd
else
X86_SKIP = sh tzsh mbtest dd
endif
# x86 専用のコマンド(arch/x86-ia16/user/<名前>.c)。ネット PoC(#187)など。
X86_ARCH_UCMDS = netpoc $(NET_CMDS)
# NETCMDS: ネット系コマンド(各 12KB 前後)をイメージに入れるか。既定 0(ネットワークは後回し、#243)。1 で入れる。
#   360KB(FDD=pcxt)には常に入れない。
NETCMDS ?= 0
ifneq ($(or $(filter pcxt,$(FDD)),$(filter 0,$(NETCMDS))),)
TZPORT_CMDS = $(filter-out $(X86_SKIP),$(UCMDS_BASE))
else
TZPORT_CMDS = $(filter-out $(X86_SKIP),$(UCMDS_BASE)) $(X86_ARCH_UCMDS)
endif

# 共有 user/<cmd>.c を直接コンパイルする(#181)。共通 user/stdio.h が __ia16__ のとき
# user/tzstdio.h を読むので、以前の obj/tzport へのコピーは不要になった。
$(OBJ)/ucmd_%.o: $(TZSRC)/%.c $(TZSRC)/stdio.h user/tzstdio.h $(UHDRS) | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<
# ntpdate だけは arch/x86-ia16/user/ntpdate.c(ESP-AT の SNTP 版)を使う。共有 user/ntpdate.c は z80 の at_modem 中継前提。
$(OBJ)/ucmd_ntpdate.o: user/ntpdate.c user/tzstdio.h $(UHDRS) | $(OBJ)
	$(CC) $(UCFLAGS) -I$(TZSRC) -c -o $@ $<
$(OBJ)/ucmd_wifi.o: user/wifi.c user/tzstdio.h $(UHDRS) | $(OBJ)
	$(CC) $(UCFLAGS) -I$(TZSRC) -c -o $@ $<
$(OBJ)/ucmd_%.o: user/%.c user/tzstdio.h $(UHDRS) | $(OBJ)
	$(CC) $(UCFLAGS) -I$(TZSRC) -c -o $@ $<

TZPORT_CMDS += $(if $(filter 1,$(GCC)),,ttyecho)
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
	mkfs.fat -F 12 -R $(FAT_RSVD) -S 512 -f 2 -r 224 --invariant -n TIZIX -C $@ $(FDD_KB) >/dev/null
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

endif
