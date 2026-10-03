#!/bin/bash
# WiFi 実験のビルド(引数・ライブラリの並びをここに固定)
export PATH=$HOME/bin:$PATH
cd $HOME/tmp/20261002/wt || exit 1
make EXTRA_OBJS='osal.o osi.o wifi.o stubs.o' \
     BLIBS='libnet80211.a libpp.a libcore.a libphy.a librtc.a libesp_phy.a libesp_wifi.a libesp_hw_support.a libsoc.a libhal.a libesp_rom.a liblog.a libesp_common.a libefuse.a libwpa_supplicant.a libmbedcrypto.a libmbedtls.a libmbedx509.a' 2>&1 \
  | grep -v '^xtensa' | grep -v 'is not implemented and will always fail' \
  | grep -E 'undefined|error|ERROR|^\.|multiple|warning: ' | sed 's/.*undefined reference to/undef/' | sort | uniq -c | sort -rn | head -40
ls -la wt.img drom.bin irom.bin 2>&1
xtensa-esp32-elf-objdump -h wt.elf | awk 'NR>5 && /^ *[0-9]/ && $4 != "00000000" {print $2}' | grep -v -E '^\.(iram|dram|bss|flash\.text|flash\.rodata)$' | head
