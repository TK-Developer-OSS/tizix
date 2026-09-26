#!/bin/bash
cd $HOME/z80pack/tizix || exit 1
echo "=== kernel size ==="
make size 2>&1 | grep -E 's__INITIALIZER|load limit'
echo
echo "=== final smoke: ovf / repeat / builtin-pipe / big tail ==="
timeout 120 python3 python/_s27_smoke.py \
  'echo abcdefghij0123456789 > /root/b.txt' \
  'cat /root/b.txt >> /root/b.txt' 'cat /root/b.txt >> /root/b.txt' \
  'cat /root/b.txt >> /root/b.txt' 'cat /root/b.txt >> /root/b.txt' \
  'cat /root/b.txt >> /root/b.txt' 'cat /root/b.txt >> /root/b.txt' \
  'cat /root/b.txt >> /root/b.txt' 'cat /root/b.txt >> /root/b.txt' \
  'wc /root/b.txt' \
  'cat /root/b.txt | tail -3' \
  'cat /root/b.txt | wc' \
  'echo one > /root/s.txt' 'echo two >> /root/s.txt' 'echo three >> /root/s.txt' \
  'cat /root/s.txt | tail -1' \
  'cat /root/s.txt | tail -1' \
  'cat /root/s.txt | tail -1' \
  'cat /root/s.txt | grep two' \
  'ls /bin | grep tail' \
  'ps | grep sh' \
  'cat /root/s.txt | wc' \
  'pwd' \
  2>&1 | sed -n '/tizix/,$p'
