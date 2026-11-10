# tzsh conformance script, one file. Each check prints "ok ..." or "FAIL ..."; the last line is
# "ALL OK" or "FAILED: n".   Run:  bash test.sh   |   tzsh test.sh
# Sections run core -> optional -> undecided -> tzsh only, so the first construct tzsh rejects marks
# the edge of what it supports. Everything before the "tzsh only" marker is valid bash, with the same
# result (bash is the oracle). Not tested on purpose (dropped): compound arithmetic precedence,
# ${#x} and substrings, arrays, here-documents, $( ) embedded in a word.
# Undecided, needs a peripheral: v=$(adc 0)   then   if [ $v -gt 400 ]; then gpio 13 1; fi
fail=0
echo "== core"
x=abc
if [ "$x" = abc ]; then echo "ok   assign"; else echo "FAIL assign"; fail=$((fail + 1)); fi
y=$x$x
if [ "$y" = abcabc ]; then echo "ok   concat"; else echo "FAIL concat"; fail=$((fail + 1)); fi
z="a b"
if [ "$z" = "a b" ]; then echo "ok   dquote space"; else echo "FAIL dquote space"; fail=$((fail + 1)); fi
u='lit $x'
if [ "$u" = 'lit $x' ]; then echo "ok   squote literal"; else echo "FAIL squote literal"; fail=$((fail + 1)); fi
d="v=$x"
if [ "$d" = "v=abc" ]; then echo "ok   dquote expand"; else echo "FAIL dquote expand"; fail=$((fail + 1)); fi
x=1 # trailing comment
if [ "$x" = 1 ]; then echo "ok   trailing comment"; else echo "FAIL trailing comment"; fail=$((fail + 1)); fi
x=
if [ "$x" = "" ]; then echo "ok   empty value"; else echo "FAIL empty value"; fail=$((fail + 1)); fi
if [ "$unset" = "" ]; then echo "ok   unset is empty"; else echo "FAIL unset is empty"; fail=$((fail + 1)); fi
n=5
if [ $n -eq 5 ]; then echo "ok   -eq"; else echo "FAIL -eq"; fail=$((fail + 1)); fi
if [ $n -ne 6 ]; then echo "ok   -ne"; else echo "FAIL -ne"; fail=$((fail + 1)); fi
if [ $n -lt 9 ]; then echo "ok   -lt"; else echo "FAIL -lt"; fail=$((fail + 1)); fi
if [ $n -le 5 ]; then echo "ok   -le"; else echo "FAIL -le"; fail=$((fail + 1)); fi
if [ $n -gt 1 ]; then echo "ok   -gt"; else echo "FAIL -gt"; fail=$((fail + 1)); fi
if [ $n -ge 5 ]; then echo "ok   -ge"; else echo "FAIL -ge"; fail=$((fail + 1)); fi
if [ "$n" = 5 ]; then echo "ok   string ="; else echo "FAIL string ="; fail=$((fail + 1)); fi
if [ "$n" != 6 ]; then echo "ok   string !="; else echo "FAIL string !="; fail=$((fail + 1)); fi
r=
if [ $n -gt 9 ]; then r=big; else r=small; fi
if [ "$r" = small ]; then echo "ok   else branch"; else echo "FAIL else branch"; fail=$((fail + 1)); fi
a=7
b=3
r=$((a + b))
if [ "$r" = 10 ]; then echo "ok   add"; else echo "FAIL add"; fail=$((fail + 1)); fi
r=$((a - b))
if [ "$r" = 4 ]; then echo "ok   sub"; else echo "FAIL sub"; fail=$((fail + 1)); fi
r=$((a + 1))
if [ "$r" = 8 ]; then echo "ok   add literal"; else echo "FAIL add literal"; fail=$((fail + 1)); fi
i=0
while [ $i -lt 5 ]; do i=$((i + 1)); done
if [ $i -eq 5 ]; then echo "ok   while"; else echo "FAIL while"; fail=$((fail + 1)); fi
i=0
while true; do
  i=$((i + 1))
  if [ $i -ge 3 ]; then break; fi
done
if [ $i -eq 3 ]; then echo "ok   while true + break"; else echo "FAIL while true + break"; fail=$((fail + 1)); fi
cnt=0
i=0
while [ $i -lt 3 ]; do
  j=0
  while [ $j -lt 2 ]; do cnt=$((cnt + 1)); j=$((j + 1)); done
  i=$((i + 1))
done
if [ $cnt -eq 6 ]; then echo "ok   nested while"; else echo "FAIL nested while"; fail=$((fail + 1)); fi
baud=115200
if [ "$baud" = 115200 ]; then echo "ok   big value stays a string"; else echo "FAIL big value stays a string"; fail=$((fail + 1)); fi
if [ "baud=$baud" = baud=115200 ]; then echo "ok   big value in dquote"; else echo "FAIL big value in dquote"; fail=$((fail + 1)); fi
echo "== optional"
r=
if [ $n -gt 9 ]; then r=big; elif [ $n -gt 3 ]; then r=mid; else r=small; fi
if [ "$r" = mid ]; then echo "ok   elif"; else echo "FAIL elif"; fail=$((fail + 1)); fi
if [ -z "" ]; then echo "ok   -z"; else echo "FAIL -z"; fail=$((fail + 1)); fi
if [ -n "$n" ]; then echo "ok   -n"; else echo "FAIL -n"; fail=$((fail + 1)); fi
r=
true && r=a
if [ "$r" = a ]; then echo "ok   &&"; else echo "FAIL &&"; fail=$((fail + 1)); fi
false && r=b
if [ "$r" = a ]; then echo "ok   && short-circuit"; else echo "FAIL && short-circuit"; fail=$((fail + 1)); fi
false || r=c
if [ "$r" = c ]; then echo "ok   ||"; else echo "FAIL ||"; fail=$((fail + 1)); fi
! false && r=d
if [ "$r" = d ]; then echo "ok   !"; else echo "FAIL !"; fail=$((fail + 1)); fi
r=
for w in a b c; do r=$r$w; done
if [ "$r" = abc ]; then echo "ok   for"; else echo "FAIL for"; fail=$((fail + 1)); fi
i=0
until [ $i -ge 2 ]; do i=$((i + 1)); done
if [ $i -eq 2 ]; then echo "ok   until"; else echo "FAIL until"; fail=$((fail + 1)); fi
i=0
s=0
while [ $i -lt 5 ]; do
  i=$((i + 1))
  if [ $i -eq 2 ]; then continue; fi
  s=$((s + i))
done
if [ $s -eq 13 ]; then echo "ok   continue"; else echo "FAIL continue"; fail=$((fail + 1)); fi
x=b
r=
case $x in
  a) r=A;;
  b|c) r=BC;;
  *) r=other;;
esac
if [ "$r" = BC ]; then echo "ok   case"; else echo "FAIL case"; fail=$((fail + 1)); fi
add() { r=$(( $1 + $2 )); }
add 2 3
if [ "$r" = 5 ]; then echo "ok   function with args"; else echo "FAIL function with args"; fail=$((fail + 1)); fi
f() { return 4; }
f
if [ $? -eq 4 ]; then echo "ok   return status"; else echo "FAIL return status"; fail=$((fail + 1)); fi
set -- a "b c"
if [ $# -eq 2 ]; then echo "ok   $#"; else echo "FAIL $#"; fail=$((fail + 1)); fi
if [ "$1" = a ]; then echo "ok   $1"; else echo "FAIL $1"; fail=$((fail + 1)); fi
if [ "$2" = "b c" ]; then echo "ok   $2"; else echo "FAIL $2"; fail=$((fail + 1)); fi
shift
if [ $# -eq 1 ] && [ "$1" = "b c" ]; then echo "ok   shift"; else echo "FAIL shift"; fail=$((fail + 1)); fi
true
if [ $? -eq 0 ]; then echo "ok   $? after true"; else echo "FAIL $? after true"; fail=$((fail + 1)); fi
false
if [ $? -eq 1 ]; then echo "ok   $? after false"; else echo "FAIL $? after false"; fail=$((fail + 1)); fi
r=$((0x10 + 1))
if [ "$r" = 17 ]; then echo "ok   hex"; else echo "FAIL hex"; fail=$((fail + 1)); fi
a=7
b=3
r=$((a * b))
if [ "$r" = 21 ]; then echo "ok   mul"; else echo "FAIL mul"; fail=$((fail + 1)); fi
r=$((a / b))
if [ "$r" = 2 ]; then echo "ok   div"; else echo "FAIL div"; fail=$((fail + 1)); fi
r=$((a % b))
if [ "$r" = 1 ]; then echo "ok   mod"; else echo "FAIL mod"; fail=$((fail + 1)); fi
r=${a}x
if [ "$r" = 7x ]; then echo "ok   ${a}"; else echo "FAIL ${a}"; fail=$((fail + 1)); fi
unset v
r=${v:-def}
if [ "$r" = def ]; then echo "ok   ${v:-d} unset"; else echo "FAIL ${v:-d} unset"; fail=$((fail + 1)); fi
v=1
r=${v:-def}
if [ "$r" = 1 ]; then echo "ok   ${v:-d} set"; else echo "FAIL ${v:-d} set"; fail=$((fail + 1)); fi
r=$((3 < 5))
if [ "$r" = 1 ]; then echo "ok   arith <"; else echo "FAIL arith <"; fail=$((fail + 1)); fi
r=$((3 > 5))
if [ "$r" = 0 ]; then echo "ok   arith >"; else echo "FAIL arith >"; fail=$((fail + 1)); fi
r=$((3 == 3))
if [ "$r" = 1 ]; then echo "ok   arith =="; else echo "FAIL arith =="; fail=$((fail + 1)); fi
r=$((3 != 3))
if [ "$r" = 0 ]; then echo "ok   arith !="; else echo "FAIL arith !="; fail=$((fail + 1)); fi
echo "== undecided: signed"
r=$((3 - 5))
if [ "$r" = -2 ]; then echo "ok   negative result"; else echo "FAIL negative result"; fail=$((fail + 1)); fi
x=-5
r=$((x + 1))
if [ "$r" = -4 ]; then echo "ok   negative operand"; else echo "FAIL negative operand"; fail=$((fail + 1)); fi

# ==== tzsh only (bash behaves differently; stripped when bash runs this file)
echo "== tzsh only"
x=abc
r=$((x + 1))
if [ "$r" = 2 ]; then echo "ok   non-numeric string is 1 in arithmetic"; else echo "FAIL non-numeric string is 1 in arithmetic"; fail=$((fail + 1)); fi
if [ "$x" -eq 1 ]; then echo "ok   non-numeric string is 1 in a numeric test"; else echo "FAIL non-numeric string is 1 in a numeric test"; fail=$((fail + 1)); fi
y=12abc
r=$((y + 0))
if [ "$r" = 1 ]; then echo "ok   12abc is 1"; else echo "FAIL 12abc is 1"; fail=$((fail + 1)); fi

if [ $fail -eq 0 ]; then echo "ALL OK"; else echo "FAILED: $fail"; fi
# The script must stop here with exit status 255 (arithmetic overflow aborts). Other aborting cases
# (115200 - 100, 1 / 0) need their own run, because an abort ends the script.
echo "abort next"
r=$((32767 + 1))
echo "FAIL not aborted"
