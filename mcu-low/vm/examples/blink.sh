// Blink gpio4 every 0.5 s (the same shape as mcu/scripts/blink.sh, for a 12F683).
setup {
  echo 1 > /dev/timer/0/repeat;
  echo 500 > /dev/timer/0/limit;
  n = 0;
}
intr {
  echo 0 > /dev/timer/0/finish;
  if (/dev/gpio/4 == "0") { echo 1 > /dev/gpio/4; }
  else { echo 0 > /dev/gpio/4; }
}
loop {
  n = n + 1;
  if (n == 1000) {
    echo "loop: 1000";
    n = 0;
  }
}
