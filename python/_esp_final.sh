#!/bin/sh
# _esp_final.sh - #62 最終確認: 再ビルド(コメント修正後のソース)→ 通し試験
set -x
exec > "$HOME/tmp/out.txt" 2>&1
rm -f "$HOME/tmp/DONE"
PATH=$HOME/z80pack/sdcc/4.5.0/bin:$PATH
export PATH
cd $HOME/z80pack/tizix || exit 1
make ARCH=z80board 2>&1 | grep -E "error|Error|warning 2[0-9][0-9]|ROM image end" | head -20
ls -l build/arch/z80board/user/net.bin build/arch/z80board/user/wifi.bin
pkill -f at_modem.py; pkill -f z80boardsim; sleep 1
timeout 240 python3 python/test_esp_net.py 2>&1 | tail -10
pkill -f at_modem.py; pkill -f z80boardsim
echo ALLDONE
touch "$HOME/tmp/DONE"
