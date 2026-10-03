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
#   共有 src/ カーネルと共有 user/ のコマンドを、arch 側の起動・例外入口
#   (crt0.s)・コンソール・ディスクに結線する。どのアーキでも同じ部分
#   (オブジェクト一覧、コンパイル規則、コマンド一覧、ディスク像)は
#   ../common-gcc.mk にあり、ここにはこのボード固有のものだけを書く。

CC      = m68k-elf-gcc
LD      = m68k-elf-ld
OBJCOPY = m68k-elf-objcopy
HOSTCC  = gcc

OBJ  = ../../build/arch/m68k-mega/obj

# TICK_HZ: レベル6タイマ割込みの周期。カーネルと m68ksim の両方がこの 1 つの値に従う。
#   既定 25(#85、2026-09-26)。実機の Mega のファーム(arduino/…/MEGA2560_68000_DTACK_TEST の
#   Timer5、OCR5A=9999)と一致させること。実機で 100/50/25/12/6Hz を測り、応答が一番速かった
#   25Hz にした(10ms ごとの割込み処理が遅いバス代行の上では重い)。100 を割り切れる値にすること
#   (コマンド側 user/stdio.h の getticks が 100 / TICK_HZ を掛ける)。
#   以前は 1Hz 固定でプロセス切り替えが 1 秒に 1 回しか起きなかった(#83)。
#   値を変えたら `make clean` してから(フラグは依存に入らない)。SD のコマンドも作り直しが要る
#   (a / b / rx / sh / sleep / uptime / vi は TICK_HZ で中身が変わる)。
TICK_HZ ?= 25
# -include include/plat.h: 共有 src/ を SDCC 前提の属性(__sdcccall 等)ごと
#   m68k-elf-gcc に通すための移植シム。このポートの素性(PLAT_FLAT32 / スロット数 /
#   割込みマスクの命令)もそこで名乗る。
CFLAGS = -mcpu=68000 -ffreestanding -fomit-frame-pointer -Wall -Wextra \
         -Os -DARCH_M68K_MEGA -DTICK_HZ=$(TICK_HZ) $(KINC) \
         -include include/plat.h
# コマンド用。-mpcrel で PC 相対コードを吐かせる(下の .bin の規則を参照)。
# -include include/plat.h: 共有 user/*.c(a.c/sleep.c 等)が SDCC 前提の
#   呼出規約属性(__sdcccall 等)を自前で書いていることがあり、カーネル側
#   (CFLAGS)と同じシムを通す(#47 で sleep 追加時に __sdcccall 未定義で
#   パースエラーになって発覚)。
UCFLAGS = -mcpu=68000 -ffreestanding -fomit-frame-pointer -O2 -Wall -Wextra \
          -mpcrel -DARCH_M68K_MEGA -DTICK_HZ=$(TICK_HZ) -Iinclude -include include/plat.h
# tzsh(#111)は作業域を小さく、コードも -Os に(像 + .bss が 1 スロットの IMG_BUDGET 24KB に入るように。
# -O2 のままだとコードだけで 17.6KB、-Os で 10.8KB)
UCFLAGS += -DTZSH_SMALL

# ソフト先行検証用ディスクイメージ(FAT16、32MB)
DISK_IMG = $(OBJ)/disk.img
DISK_KB  = 32768

.PHONY: all run clean
all: $(OBJ)/kernel.bin rom.h m68ksim $(DISK_IMG)

include ../common-gcc.mk

# tzsh だけ -Os(上の TZSH_SMALL の注釈。規則が定義された後に置く = 既定の目標を奪わない)
$(OBJ)/ucmd_tzsh.o: UCFLAGS += -Os

LIBGCC = $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

ROCKET68_DIR = rocket68
ROCKET68_LIB = $(ROCKET68_DIR)/lib/librocket68.a

# arch 固有(起動 + 例外入口 / 物理コンソール / ディスク / コマンドのロード)
AOBJ = $(OBJ)/crt0.o $(OBJ)/kmain.o $(OBJ)/console.o $(OBJ)/diskio.o $(OBJ)/loader.o
KOBJ = $(AOBJ) $(LOBJ) $(SOBJ) $(FSBACKEND)

$(OBJ)/kernel.elf: $(KOBJ) link-kernel.ld
	$(LD) -T link-kernel.ld -o $@ $(KOBJ) $(LIBGCC)

$(OBJ)/kernel.bin: $(OBJ)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@sz=$$(stat -c%s $@); echo "kernel.bin = $$sz bytes (RAM budget 0x100000 = 1MB)"; \
	 [ $$sz -le 32768 ] || { echo "*** kernel.bin が 32KB(slot1 の先頭 0x8000)を超過 ***"; \
	   echo "*** include/plat.h の PLAT_SLOT_ADDR(1) と衝突する ***"; exit 1; }

# rom.h は Mega のスケッチ(arduino/MEGA2560_68000_DTACK_TEST、#95)が #include する。
# スケッチのフォルダにも同じものを置く(生成物なので git には入れない)。
ARDUINO_SKETCH = arduino/MEGA2560_68000_DTACK_TEST
rom.h: $(OBJ)/kernel.bin tools/bin2romh.py
	python3 tools/bin2romh.py $(OBJ)/kernel.bin $@
	cp $@ $(ARDUINO_SKETCH)/rom.h

# ---- 外部コマンド(一覧とコンパイルは ../common-gcc.mk) ----
# #48: argv_init.o(空白で割り直すトークナイザ)は廃止。sh が argpack で渡し、
#   kexec_argv がトークンごとの argv[] を作るので、crt0cmd はそれをそのまま使う。
UCRT  = $(OBJ)/crt0cmd.o
ULIB  = $(OBJ)/usyscall.o

$(OBJ)/crt0cmd.o: user/crt0cmd.s | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<
$(OBJ)/usyscall.o: user/usyscall.s | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<

# #61: PIC 化により **コマンドごとに .bin は 1 本**。以前はスロット数ぶん
# 再リンクしていた(ls1.bin/ls2.bin/…、21 コマンド x 31 スロット = 651 本)。
# -mpcrel で PC 相対コードを吐かせ、VMA=0 でリンクする(user/cmd.ld)。
# ロード先はカーネル(loader.c)が実行時に決めるので、Makefile とカーネルの間で
# アドレスを同期させる約束事そのものが消えた。
$(OBJ)/%.bin: $(OBJ)/ucmd_%.o $(UCRT) $(ULIB) user/cmd.ld
	$(LD) -T user/cmd.ld -o $@ $(UCRT) $< $(ULIB)

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
