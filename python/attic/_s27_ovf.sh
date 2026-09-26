#!/bin/bash
cd $HOME/z80pack/tizix || exit 1
echo "=== kernel size ==="
make size 2>&1 | grep -E 's__INITIALIZER|load limit|_CODE '
echo
echo "=== smoke: 4KB overflow + pipe variety ==="
# make a >4KB file: 500 lines of 'abcdefghij' -> ~5500 bytes
timeout 90 python3 python/_s27_smoke.py \
  'echo aaaaaaaaaa > /root/big.txt' \
  'cat /root/big.txt >> /root/big.txt' \
  'cat /root/big.txt >> /root/big.txt' \
  'cat /root/big.txt >> /root/big.txt' \
  'cat /root/big.txt >> /root/big.txt' \
  'cat /root/big.txt >> /root/big.txt' \
  'cat /root/big.txt >> /root/big.txt' \
  'cat /root/big.txt >> /root/big.txt' \
  'cat /root/big.txt >> /root/big.txt' \
  'cat /root/big.txt >> /root/big.txt' \
  'wc /root/big.txt' \
  'cat /root/big.txt | wc' \
  'echo small > /root/s.txt' \
  'cat /root/s.txt | wc' \
  'ptx 40 | prx' \
  'ps | grep sh' \
  2>&1 | sed -n '/tizix/,$p'
