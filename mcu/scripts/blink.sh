// Blink D13 every 0.5 s. setup runs once, intr runs each time the timer finishes,
// loop repeats forever (Ctrl-C stops). One statement per if/else keeps the toggle easy to read.
setup {
  echo 1 > /dev/timer/0/repeat;
  echo 500 > /dev/timer/0/limit;
  n = 0;
}
intr {
  echo 0 > /dev/timer/0/finish;
  if (/dev/gpio/13 == "0") { echo 1 > /dev/gpio/13; }
  else { echo 0 > /dev/gpio/13; }
}
// loop does something else meanwhile: every 1000th round it says so (several statements in a block)
loop {
  n = n + 1;
  if (n == 1000) {
    echo "loop: 1000 rounds";
    n = 0;
  }
}
