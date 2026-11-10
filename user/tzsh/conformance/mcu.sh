// tzsh self-test for a board: the language core and the /dev nodes, in one self-contained file.
// Run it with: tzsh mcu.sh. Every check prints "ok ..." or "FAIL ...", then a summary line.
fail = 0;

echo "== values";
x = "abc";
if (x == "abc") { echo "ok   assign"; } else { echo "FAIL assign"; fail = fail + 1; }
y = x;
if (y == "abc") { echo "ok   copy"; } else { echo "FAIL copy"; fail = fail + 1; }
x = "a b";
if (x == "a b") { echo "ok   string with a space"; } else { echo "FAIL string with a space"; fail = fail + 1; }
x = 'lit $x';
if (x == 'lit $x') { echo "ok   single quotes are literal"; } else { echo "FAIL single quotes are literal"; fail = fail + 1; }
x = "";
if (x == "") { echo "ok   empty string"; } else { echo "FAIL empty string"; fail = fail + 1; }
if (nothing == "") { echo "ok   unset is empty"; } else { echo "FAIL unset is empty"; fail = fail + 1; }
x = "abc";
if (x != "abd") { echo "ok   !="; } else { echo "FAIL !="; fail = fail + 1; }
x = "115200";
if (x == "115200") { echo "ok   a big value stays a string"; } else { echo "FAIL a big value stays a string"; fail = fail + 1; }

echo "== numbers";
n = 5;
if (n == 5) { echo "ok   =="; } else { echo "FAIL =="; fail = fail + 1; }
if (n < 9) { echo "ok   <"; } else { echo "FAIL <"; fail = fail + 1; }
if (n <= 5) { echo "ok   <="; } else { echo "FAIL <="; fail = fail + 1; }
if (n > 1) { echo "ok   >"; } else { echo "FAIL >"; fail = fail + 1; }
if (n >= 5) { echo "ok   >="; } else { echo "FAIL >="; fail = fail + 1; }
if (n > 9) { echo "FAIL > is false"; fail = fail + 1; } else { echo "ok   else branch"; }
a = 7;
b = 3;
r = a + b;
if (r == 10) { echo "ok   +"; } else { echo "FAIL +"; fail = fail + 1; }
r = a - b;
if (r == 4) { echo "ok   -"; } else { echo "FAIL -"; fail = fail + 1; }
r = a + 1;
if (r == 8) { echo "ok   + literal"; } else { echo "FAIL + literal"; fail = fail + 1; }
r = 3 - 5;
if (r == -2) { echo "ok   negative result"; } else { echo "FAIL negative result"; fail = fail + 1; }
x = "abc";
r = x + 1;
if (r == 2) { echo "ok   text counts as 1 in arithmetic"; } else { echo "FAIL text counts as 1 in arithmetic"; fail = fail + 1; }
x = "12abc";
r = x + 0;
if (r == 1) { echo "ok   12abc counts as 1"; } else { echo "FAIL 12abc counts as 1"; fail = fail + 1; }

echo "== control";
if (n == 1) { echo "FAIL if"; fail = fail + 1; } else if (n == 5) { echo "ok   else if"; } else { echo "FAIL else if"; fail = fail + 1; }
if (n) { echo "ok   a bare value is true"; } else { echo "FAIL a bare value is true"; fail = fail + 1; }
x = 0;
if (x) { echo "FAIL zero is false"; fail = fail + 1; } else { echo "ok   zero is false"; }
i = 0;
while (i < 5) { i = i + 1; }
if (i == 5) { echo "ok   while"; } else { echo "FAIL while"; fail = fail + 1; }
i = 0;
while (1) {
  i = i + 1;
  if (i >= 3) { break; }
}
if (i == 3) { echo "ok   break"; } else { echo "FAIL break"; fail = fail + 1; }
cnt = 0;
i = 0;
while (i < 3) {
  j = 0;
  while (j < 2) { cnt = cnt + 1; j = j + 1; }
  i = i + 1;
}
if (cnt == 6) { echo "ok   nested while"; } else { echo "FAIL nested while"; fail = fail + 1; }

echo "== /dev";
echo 1 > /dev/gpio/11;
if (/dev/gpio/11 == "1") { echo "ok   write 1, read it"; } else { echo "FAIL write 1, read it"; fail = fail + 1; }
echo 0 > /dev/gpio/11;
if (/dev/gpio/11 == "0") { echo "ok   write 0, read it"; } else { echo "FAIL write 0, read it"; fail = fail + 1; }
echo 1 > /dev/gpio/11;
x = /dev/gpio/11;
if (x == "1") { echo "ok   v = /dev/gpio/11"; } else { echo "FAIL v = /dev/gpio/11"; fail = fail + 1; }
echo in > /dev/gpio/11;
echo up > /dev/gpio/12;
if (/dev/gpio/12) { echo "ok   pull-up reads 1"; } else { echo "FAIL pull-up reads 1"; fail = fail + 1; }
echo in > /dev/gpio/12;
if (/dev/timer/0/limit == 0) { echo "ok   timer idle"; } else { echo "FAIL timer idle"; fail = fail + 1; }
echo 30 > /dev/timer/0/limit;
spin = 0;
while (/dev/timer/0/finish == 0) { spin = spin + 1; if (spin > 20000) { break; } }
if (/dev/timer/0/finish == 1) { echo "ok   the timer finishes"; } else { echo "FAIL the timer finishes"; fail = fail + 1; }
if (/dev/timer/0/limit == 0) { echo "ok   one shot stops by itself"; } else { echo "FAIL one shot stops by itself"; fail = fail + 1; }
echo 0 > /dev/timer/0/finish;
if (/dev/timer/0/finish == 0) { echo "ok   finish is cleared by writing 0"; } else { echo "FAIL finish is cleared by writing 0"; fail = fail + 1; }
echo 1 > /dev/timer/0/repeat;
echo 20 > /dev/timer/0/limit;
k = 0;
spin = 0;
while (k < 3) {
  if (/dev/timer/0/finish == 1) { echo 0 > /dev/timer/0/finish; k = k + 1; }
  spin = spin + 1;
  if (spin > 20000) { break; }
}
if (k == 3) { echo "ok   repeat finishes again and again"; } else { echo "FAIL repeat finishes again and again"; fail = fail + 1; }
echo 0 > /dev/timer/0/limit;
echo 0 > /dev/timer/0/repeat;
echo 0 > /dev/timer/0/finish;

echo "== intr";
hit = 0;
intr {
  hit = hit + 1;
  echo 0 > /dev/timer/0/finish;
}
echo 20 > /dev/timer/0/limit;
spin = 0;
while (hit < 1) { spin = spin + 1; if (spin > 20000) { break; } }
if (hit == 1) { echo "ok   intr runs when the timer finishes"; } else { echo "FAIL intr runs when the timer finishes"; fail = fail + 1; }

echo "== setup / loop";
setup { n = 5; }
if (n == 5) { echo "ok   setup runs once, where it stands"; } else { echo "FAIL setup runs once"; fail = fail + 1; }
n = 0;
loop { n = n + 1; if (n == 7) { break; } }
if (n == 7) { echo "ok   loop repeats until break"; } else { echo "FAIL loop repeats until break"; fail = fail + 1; }

if (fail == 0) { echo "ALL OK"; } else { echo "FAILED: $fail"; }
exit 0;
