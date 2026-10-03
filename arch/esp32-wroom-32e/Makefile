# tizix arch/esp32-wroom-32e
#   ESP32 単体(ESP32-WROOM-32E 等)で tizix を動かすポート(task.md #108)。
#   ツールチェイン(rocky9 の ~/bin、実体は ~/bin/esp32/):
#     xtensa-esp32-elf-gcc  Espressif crosstool-NG(GCC 16.1.0)
#     esptool               ELF → ROM ローダ用イメージ、flash 書き込み
#     qemu-system-xtensa    Espressif QEMU(-machine esp32)でソフト先行検証
#   使い方: make -C arch/esp32-wroom-32e        … build/arch/esp32-wroom-32e/obj/flash.bin(4MB、QEMU/実機共通)
#           make -C arch/esp32-wroom-32e run    … QEMU で起動(抜けるのは Ctrl-A x)
#           make -C arch/esp32-wroom-32e test   … QEMU でコマンドを打って出力を見る(tools/qrun.py)
#           make ARCH=esp32-wroom-32e …         … ルートからでも同じ
#
#   flash の配置(4MB):
#     0x001000  kernel.img  ROM ローダが読む esptool 形式(2 段目ブートローダの席)
#     0x100000  disk.img    FAT16 3MB(include/plat.h の DISK_FLASH_OFF/SIZE と一致)
#
#   共有 src/ カーネルと共有 user/ のコマンドを、arch 側の起動・例外入口(crt0.S)・
#   コンソール・ディスク・ローダに結線する。どのアーキでも同じ部分(オブジェクト一覧、
#   コンパイル規則、コマンド一覧、ディスク像)は ../common-gcc.mk にあり、ここには
#   このチップ固有のものだけを書く。
#
#   ABI: カーネルは windowed(#109。WiFi のバイナリと ROM が windowed なので揃えた。
#   文脈切替は crt0.S が窓を吐き出してから行う)。外部コマンドは call0 のまま
#   (syscall 命令の例外で入るので混ざらない)。カーネルは libgcc(windowed)だけリンクする。

CROSS   = xtensa-esp32-elf-
CC      = $(CROSS)gcc
ESPTOOL = esptool
QEMU    = qemu-system-xtensa

OBJ  = ../../build/arch/esp32-wroom-32e/obj

# TICK_HZ: タイマ割込みの周期(kmain.c が CCOMPARE0 に仕掛ける)。100 を割り切れる値にすること
#   (コマンド側 user/stdio.h の getticks が 100 / TICK_HZ を掛ける)。
#   値を変えたら `make clean` してから(フラグは依存に入らない)。
TICK_HZ ?= 100

# -fno-tree-loop-distribute-patterns: バイトやワードを写すループを memcpy 呼び出しに
#   畳ませない(IRAM へはワードでしか書けない。loader.c)。
COMMONFLAGS = -ffreestanding -fno-builtin -fno-strict-aliasing \
           -fno-tree-loop-distribute-patterns -Os -Wall -Wextra
CFLAGS   = -mabi=windowed -mlongcalls $(COMMONFLAGS) -DARCH_ESP32 -DTICK_HZ=$(TICK_HZ) $(KINC) -include include/plat.h
LDFLAGS  = -nostdlib -Wl,--gc-sections -T link-kernel.ld
KLIBS    = -lgcc
# -Wno-unused-function: user/stdio.h は全関数を static で持つので、使わないぶんが毎回警告になる。
UCFLAGS  = -mabi=call0 $(COMMONFLAGS) -Wno-unused-function -Wno-comment \
           -DARCH_ESP32 -DTICK_HZ=$(TICK_HZ) -Iinclude -include include/plat.h

FLASH_SIZE = 4MB
DISK_OFF   = 0x100000
DISK_IMG   = $(OBJ)/disk.img
DISK_KB    = 3072
MKFS_OPTS  = -s 1

.PHONY: all run test flash clean
all: $(OBJ)/flash.bin

# ネットワークのコマンド(#109)。共有 user/ の telnet・nettest は z80 と同じソースで、
# user/netcli.h の gcc 側がカーネルの TCP(net/knet.c)を呼ぶ。WiFi 無しのビルドでも
# 載せる(繋がらないと言って終わるだけ)。ネットを持たない m68k-mega には載せない。
ARCH_UCMDS = telnet nettest ifconfig netstat ping dig curl arp nc bigbss mcpd

# 周辺機器のコマンド(#112)。名前は esp32-gpio のように接頭辞付きで、/bin/esp32-gpio.bin に置く
# (gcc 系は長いファイル名が使える。#114。以前は 8.3 のため /bin/esp32/gpio.bin と「-」の読み替え)。
# ソースは arch 側(cmd/)。ハードを持つのは常駐の esp32d(/bin/esp32d.bin。gpio.c などの
# dev_xxx をまとめたもの)で、esp32-* は郵便受けで依頼するだけ(どれも cmd/client.c を
# -DDEV="gpio" などで作り分けたもの。cmd/esp32d.h)。
E32CMDS = gpio pwm adc i2c spi rgb
ARCH_DIRBINS = $(foreach c,$(E32CMDS),$(OBJ)/bin/esp32-$(c).bin) $(OBJ)/bin/esp32d.bin

include ../common-gcc.mk

# arch 固有(起動 + 例外入口 / タイマ / 物理コンソール / ディスク / コマンドのロード)
AOBJ = $(OBJ)/crt0.o $(OBJ)/kmain.o $(OBJ)/console.o $(OBJ)/diskio.o $(OBJ)/loader.o
KOBJ = $(AOBJ) $(LOBJ) $(SOBJ) $(FSBACKEND)

# ---- WiFi(#109)----
#   Espressif のバイナリと ESP-IDF のビルド済み部品・ヘッダ(Arduino-ESP32 3.3.2 の
#   esp32-arduino-libs idf-release_v5.5 から写したもの。リポジトリには入れない)が
#   WIFI_BLOBS にあれば WiFi 入りで作る。無ければ今までどおり WiFi 無し(QEMU の回帰もこれで回る)。
#   WIFI=0 で明示的に外せる。
WIFI_BLOBS ?= $(HOME)/tools/esp32-wifi-blobs
WIFI ?= $(if $(wildcard $(WIFI_BLOBS)/lib/libnet80211.a),1,0)
ifeq ($(WIFI),1)
CFLAGS += -DPLAT_WIFI
WINC  = -I$(WIFI_BLOBS)/config \
        -I$(WIFI_BLOBS)/include/esp_rom/esp32/include/esp32 \
        -I$(WIFI_BLOBS)/include/freertos/config/include/freertos \
        -I$(WIFI_BLOBS)/include/freertos/config/xtensa/include/freertos \
        -I$(WIFI_BLOBS)/include/freertos/FreeRTOS-Kernel/portable/xtensa/include/freertos \
        -I$(WIFI_BLOBS)/include/freertos/FreeRTOS-Kernel/include/freertos \
        $(shell find $(WIFI_BLOBS)/include -type d \( -name include -o -name platform_include -o -path '*esp32/include' -o -path '*esp32/register' \) 2>/dev/null | grep -v -E 'esp32(c|s|h|p)|riscv|linux|/test|/include/lwip/' | sed 's/^/-I/')
NOBJ  = $(OBJ)/net/osal.o $(OBJ)/net/osi.o $(OBJ)/net/stubs.o $(OBJ)/net/wifi.o $(OBJ)/net/ctx.o $(OBJ)/net/netif_wifi.o $(OBJ)/net/knet.o $(OBJ)/net/kquery.o $(OBJ)/net/ktls.o
# lwIP(NO_SYS、raw API)。素の lwIP 2.2.0 をリポジトリの外に置き(LWIP_DIR)、設定は net/lwip-port。
# ESP-IDF の lwip ヘッダ(上の WINC から外した)とは混ぜない。liblwip.a は flash(.flash.text)へ行く。
LWIP_DIR ?= $(HOME)/tools/lwip-2.2.0
LWINC = -Inet/lwip-port -I$(LWIP_DIR)/src/include
LWSRC = $(wildcard $(LWIP_DIR)/src/core/*.c) $(wildcard $(LWIP_DIR)/src/core/ipv4/*.c) $(LWIP_DIR)/src/netif/ethernet.c \
        $(LWIP_DIR)/src/apps/sntp/sntp.c
LWOBJ = $(patsubst $(LWIP_DIR)/src/%.c,$(OBJ)/lwip/%.o,$(LWSRC))
LWHDR = net/lwip-port/lwipopts.h net/lwip-port/arch/cc.h
$(OBJ)/lwip/%.o: $(LWIP_DIR)/src/%.c $(LWHDR) | $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-unused-parameter -Wno-sign-compare -Wno-address $(LWINC) -c -o $@ $<
$(OBJ)/liblwip.a: $(LWOBJ)
	rm -f $@
	$(CROSS)ar rcs $@ $^
# lwIP を呼ぶもの(netif と TCP クライアント)は lwIP のヘッダを ESP-IDF のより先に見る
$(OBJ)/net/netif_wifi.o $(OBJ)/net/knet.o $(OBJ)/net/kquery.o $(OBJ)/net/ktls.o: $(OBJ)/net/%.o: net/%.c net/osal.h $(LWHDR) $(HDRS) | $(OBJ)
	mkdir -p $(OBJ)/net
	$(CC) $(CFLAGS) $(LWINC) $(WINC) $(MBEDCFG) -Wno-unused-parameter -c -o $@ $<
# mbedtls のヘッダは ESP-IDF のビルド済みライブラリと同じ設定で読む(net/ktls.c)
MBEDCFG = -DMBEDTLS_CONFIG_FILE='"mbedtls/esp_config.h"'
WLIBS = $(addprefix $(WIFI_BLOBS)/lib/, libnet80211.a libpp.a libcore.a libphy.a librtc.a libesp_phy.a \
          libesp_wifi.a libesp_hw_support.a libsoc.a libhal.a libesp_rom.a liblog.a libesp_common.a \
          libefuse.a libwpa_supplicant.a libmbedcrypto.a libmbedtls.a libmbedx509.a \
          libmbedtls_2.a)
# libmbedtls_2.a = mbedtls の TLS 本体 / libmbedtls.a = 証明書バンドル(esp_crt_bundle と CA 一式。net/ktls.c)
KOBJ += $(NOBJ)
KLIBS = -Wl,--start-group $(OBJ)/liblwip.a $(WLIBS) -lc -lgcc -Wl,--end-group
KDEPS = $(OBJ)/liblwip.a
# 周辺レジスタへの memcpy を語単位にする(net/stubs.c の __wrap_memcpy に理由)
KLIBS += -Wl,--wrap=memcpy -Wl,--wrap=memset
# ROM の関数の番地(ESP-IDF の ld。ets_printf / キャッシュ / 割込みマトリクス / PHY が使う ROM 関数)
LDFLAGS += -L$(WIFI_BLOBS)/ld -T esp32.rom.ld -T esp32.rom.api.ld -T esp32.rom.libgcc.ld -T esp32.peripherals.ld
# kmain.c は PLL の設定(soc/rtc.h)を読むので ESP-IDF のヘッダも要る
$(OBJ)/kmain.o: kmain.c $(HDRS) | $(OBJ)
	$(CC) $(CFLAGS) $(WINC) -c -o $@ $<
$(OBJ)/net/%.o: net/%.c net/osal.h $(HDRS) | $(OBJ)
	mkdir -p $(OBJ)/net
	$(CC) $(CFLAGS) $(WINC) -Wno-unused-parameter -Wno-cast-function-type -Wno-missing-field-initializers -Wno-sign-compare -c -o $@ $<
$(OBJ)/net/%.o: net/%.S | $(OBJ)
	mkdir -p $(OBJ)/net
	$(CC) $(CFLAGS) -c -o $@ $<
endif

$(OBJ)/kernel.elf: $(KOBJ) $(KDEPS) link-kernel.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(KOBJ) $(KLIBS) -Wl,-Map,$(OBJ)/kernel.map
	$(CROSS)size -A $@ | grep -E '^\.(text|data|bss|flash)'

# ROM ローダが読む esptool 形式(flash 0x1000 に置く。2 段目ブートローダの席)。
# flash から直接走らせる部分(.flash.*)は像から外し、生の bin にして別の位置へ書く。
$(OBJ)/kernel.img: $(OBJ)/kernel.elf
	$(CROSS)objcopy -R .flash.text -R .flash.rodata $< $(OBJ)/kernel.ram.elf 2>/dev/null
	$(ESPTOOL) --chip esp32 elf2image --flash-mode dio --flash-freq 40m \
	    --flash-size $(FLASH_SIZE) -o $@ $(OBJ)/kernel.ram.elf
	$(CROSS)objcopy -O binary -j .flash.rodata $< $(OBJ)/drom.bin
	$(CROSS)objcopy -O binary -j .flash.text $< $(OBJ)/irom.bin
DROM_OFF = 0x40000
IROM_OFF = 0x70000

# ---- 外部コマンド(一覧とコンパイルは ../common-gcc.mk) ----
#   コードとデータは別の枠に入る(loader.c 冒頭)。枠が変わっても動くよう、同じ .o を
#   番地を変えて 3 回リンクし、tools/mkcmd.py が変わった語から再配置表を作る。
#   基準の番地は slot1 の枠(include/plat.h の PROC_TEXT_BASE / PROC_DATA_BASE と
#   一致させること)。
#   UTSIZE / UDSIZE は mkcmd.py が止める上限。16KB を超える像は連続した複数スロットで載る(#113、
#   loader.c の plat_need)。上限は再配置表の語番号(14 ビット = 64KB)で決まる 4 スロットぶん。
UTBASE  = 0x4008C000
UDBASE  = 0x3FFCC000
UTSIZE  = 0x10000
UDSIZE  = 0x10000
UTSHIFT = 0x00100000
UDSHIFT = 0x00200000

ULINK   = $(CC) -mabi=call0 -nostdlib -Wl,-T,user/cmd.ld
UCRT    = $(OBJ)/crt0cmd.o

$(OBJ)/crt0cmd.o: user/crt0cmd.S | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(OBJ)/%.bin: $(OBJ)/ucmd_%.o $(UCRT) user/cmd.ld tools/mkcmd.py
	$(ULINK) -Wl,--defsym=TBASE=$(UTBASE) -Wl,--defsym=DBASE=$(UDBASE) \
	    -o $(OBJ)/$*.elf $(UCRT) $<
	$(ULINK) -Wl,--defsym=TBASE=$(UTBASE)+$(UTSHIFT) -Wl,--defsym=DBASE=$(UDBASE) \
	    -o $(OBJ)/$*.t.elf $(UCRT) $<
	$(ULINK) -Wl,--defsym=TBASE=$(UTBASE) -Wl,--defsym=DBASE=$(UDBASE)+$(UDSHIFT) \
	    -o $(OBJ)/$*.d.elf $(UCRT) $<
	python3 tools/mkcmd.py $(OBJ)/$*.elf $(OBJ)/$*.t.elf $(OBJ)/$*.d.elf $@ \
	    $(UTSHIFT) $(UDSHIFT) $(UTSIZE) $(UDSIZE)

# 番地を変えて 3 回リンクし、mkcmd.py で .bin にする($(1) = 中間物の名前、$(2) = .o の並び)
define LINK3
	$(ULINK) -Wl,--defsym=TBASE=$(UTBASE) -Wl,--defsym=DBASE=$(UDBASE) \
	    -o $(OBJ)/$(1).elf $(UCRT) $(2)
	$(ULINK) -Wl,--defsym=TBASE=$(UTBASE)+$(UTSHIFT) -Wl,--defsym=DBASE=$(UDBASE) \
	    -o $(OBJ)/$(1).t.elf $(UCRT) $(2)
	$(ULINK) -Wl,--defsym=TBASE=$(UTBASE) -Wl,--defsym=DBASE=$(UDBASE)+$(UDSHIFT) \
	    -o $(OBJ)/$(1).d.elf $(UCRT) $(2)
	python3 tools/mkcmd.py $(OBJ)/$(1).elf $(OBJ)/$(1).t.elf $(OBJ)/$(1).d.elf $@ \
	    $(UTSHIFT) $(UDSHIFT) $(UTSIZE) $(UDSIZE)
endef

E32HDRS = cmd/esp32io.h cmd/esp32d.h $(UHDRS)

# esp32d の中の装置の処理(dev_xxx)と本体
$(OBJ)/e32_%.o: cmd/%.c $(E32HDRS) | $(OBJ)
	$(CC) $(UCFLAGS) -I$(TZSRC) -Icmd -c -o $@ $<
E32DOBJ = $(OBJ)/e32_esp32d.o $(foreach c,$(E32CMDS),$(OBJ)/e32_$(c).o)
$(OBJ)/bin/esp32d.bin: $(E32DOBJ) $(UCRT) user/cmd.ld tools/mkcmd.py
	@mkdir -p $(dir $@)
	$(call LINK3,e32_esp32d,$(E32DOBJ))

# esp32-xxx: cmd/client.c を装置名だけ変えて作る
$(OBJ)/e32c_%.o: cmd/client.c $(E32HDRS) | $(OBJ)
	$(CC) $(UCFLAGS) -I$(TZSRC) -Icmd -DDEV='"$*"' -c -o $@ $<
$(OBJ)/bin/esp32-%.bin: $(OBJ)/e32c_%.o $(UCRT) user/cmd.ld tools/mkcmd.py
	@mkdir -p $(dir $@)
	$(call LINK3,e32c_$*,$<)

.SECONDARY:

# ---- flash 像(カーネル + ディスク。ディスク像の作り方は ../common-gcc.mk) ----
$(OBJ)/flash.bin: $(OBJ)/kernel.img $(DISK_IMG)
	$(ESPTOOL) --chip esp32 merge-bin --pad-to-size $(FLASH_SIZE) -o $@ \
	    0x1000 $(OBJ)/kernel.img $(DROM_OFF) $(OBJ)/drom.bin $(IROM_OFF) $(OBJ)/irom.bin $(DISK_OFF) $(DISK_IMG)

run: $(OBJ)/flash.bin
	$(QEMU) -nographic -machine esp32 -drive file=$<,if=mtd,format=raw

# QEMU でコマンドを打って端末出力を見る(対話なし)。例:
#   make ARCH=esp32-wroom-32e test CMDS="'ls /bin' 'echo hi | cat' ps"
CMDS ?= 'ls /' 'echo hello | cat' ps free
test: $(OBJ)/flash.bin
	python3 -u tools/qrun.py $< $(CMDS)

# 実機へ書く(**未検証**: 実機は M5。ボードの USB シリアルを PORT= で指定)。
PORT ?= /dev/ttyUSB0
flash: $(OBJ)/kernel.img $(DISK_IMG)
	$(ESPTOOL) --chip esp32 -p $(PORT) -b 460800 write-flash --flash-mode dio --flash-freq 40m \
	    --flash-size $(FLASH_SIZE) 0x1000 $(OBJ)/kernel.img $(DROM_OFF) $(OBJ)/drom.bin \
	    $(IROM_OFF) $(OBJ)/irom.bin $(DISK_OFF) $(DISK_IMG)

clean:
	rm -rf $(OBJ)
