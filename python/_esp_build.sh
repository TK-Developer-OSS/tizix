#!/bin/sh
# _esp_build.sh - #62 (z80board ESP-WROOM-02 WiFi) のビルド一式。
#   ssh を 1 回しか張れない前提(memory: rocky9-ssh-flaky)なので、
#   ここに全部まとめて nohup で走らせ、out.txt/DONE をポーリングする。
set -x
exec > "$HOME/tmp/out.txt" 2>&1
rm -f "$HOME/tmp/DONE"
PATH=$HOME/z80pack/sdcc/4.5.0/bin:$PATH
export PATH

echo "=== 1. tizix (ARCH=z80board) ==="
cd $HOME/z80pack/tizix || exit 1
make ARCH=z80board 2>&1 | tail -40

echo "=== 2. 生成物のサイズ ==="
ls -l arch/z80board/user/net.bin arch/z80board/user/wifi.bin 2>&1
ls -l arch/z80board/boot.rom arch/z80board/sdcard.img 2>&1
grep -E "l__CODE|_CODE " arch/z80board/user/net.map 2>/dev/null | head -3

echo ALLDONE
touch "$HOME/tmp/DONE"
