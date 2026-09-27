## Tizix Trial Environment
================

Run prebuilt tizix binaries across three different simulators.

  tizix              z80pack (cpmsim, Z80)        To quit: Ctrl+\
  tizix z80board     Homebrew Z80 board (z80boardsim) To quit: Ctrl+\
  tizix m68k         68000 (m68ksim)              To quit: Ctrl+]
  tizix reset        Restore disk images to their default state

Upon booting, you will see the shell prompt `[/root]#`. Here are a few things you can try:

  ls /bin            List available commands
  echo hello | cat   Test piping
  ps                 List running processes
  sleep 30 &         Run in the background (check with `ps` and terminate with `kill`)
  vi memo.txt        Full-screen editor (save and exit with `:wq`)
  cat /etc/rc        View the startup script

*Note: Running `exit` in the shell will only restart the shell, it will not close the simulator. Please use the quit hotkeys listed above.*
*Note: Any file modifications will persist on the disk images. Run `tizix reset` to start fresh with a clean slate.*

The source code is located in `~/tizix` (does not include git repository metadata).
Please note that this environment does not include a compiler, so you cannot run `make`.
