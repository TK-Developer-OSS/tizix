#!/bin/bash
cd $HOME/z80pack/tizix || exit 1
timeout 70 python3 python/_s27_smoke.py \
  'echo l1 > /root/m.txt' \
  'echo l2 >> /root/m.txt' \
  'echo l3 >> /root/m.txt' \
  'echo l4 >> /root/m.txt' \
  'echo l5 >> /root/m.txt' \
  'cat /root/m.txt | tail' \
  'pwd' \
  'cat /root/m.txt | tail -2' \
  'pwd' \
  'tail /root/m.txt' \
  'tail -2 /root/m.txt' \
  'tail < /root/m.txt' \
  'pwd' \
  2>&1 | sed -n '/tizix/,$p'
