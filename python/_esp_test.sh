#!/bin/sh
# _esp_test.sh - #62 の通し試験(z80boardsim + at_modem.py)。2 回連続で回して
#   ブート flake(「LOAD MBR...」で止まる既知の揺れ)の影響を見る。
set -x
exec > "$HOME/tmp/out.txt" 2>&1
rm -f "$HOME/tmp/DONE"
PATH=$HOME/z80pack/sdcc/4.5.0/bin:$PATH
export PATH
cd $HOME/z80pack/tizix || exit 1
i=1
while [ $i -le 2 ]; do
  echo "########## RUN $i ##########"
  pkill -f at_modem.py
  pkill -f z80boardsim
  sleep 1
  timeout 240 python3 python/test_esp_net.py 2>&1 | tail -12
  echo "rc=$?"
  i=$((i+1))
done
pkill -f at_modem.py
pkill -f z80boardsim
echo ALLDONE
touch "$HOME/tmp/DONE"
