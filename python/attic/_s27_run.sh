#!/bin/bash
# #27 helper: (opt cleandisk) + build + regression + tail smoke.
cd $HOME/z80pack/tizix || exit 1
if [ "$1" = "clean" ]; then echo "=== cleandisk ==="; make cleandisk >/dev/null 2>&1; fi
echo "=== make ==="
make 2>&1 | tail -3 || exit 1
echo "=== sizes ==="
stat -c '%s %n' arch/z80pack/user/sh.bin arch/z80pack/user/tail.bin
grep -E '_start|_main ' arch/z80pack/user/sh.map | head -3 || true
echo
echo "=== tail / pipe smoke ==="
timeout 90 python3 python/_s27_smoke.py \
  'echo l1 > /root/m.txt' 'echo l2 >> /root/m.txt' 'echo l3 >> /root/m.txt' \
  'echo l4 >> /root/m.txt' 'echo l5 >> /root/m.txt' \
  'cat /root/m.txt | tail' \
  'cat /root/m.txt | tail -2' \
  'cat /root/m.txt | tail -n 3' \
  'tail /root/m.txt' \
  'tail -2 /root/m.txt' \
  'tail < /root/m.txt' \
  'cat /root/m.txt | wc' \
  'ls | grep sh' \
  'pwd' \
  2>&1 | sed -n '/tizix/,$p'
echo
echo "=== regression suite ==="
for t in test_5b_pipe test_pwd_cd test_vfs_step8 test_vfs_step9 test_vfs_step10 test_cmds_all; do
  printf '%-22s ' "$t"
  if timeout 150 python3 "python/$t.py" >/tmp/$t.log 2>&1; then
    echo PASS
  else
    echo "FAIL (rc=$?)"; grep -E 'FAIL|result|Error|trap' /tmp/$t.log | head -10 | sed 's/^/    /'
  fi
done
