# tz/ - kernel pieces compiled by tzcc (--march=ia16)

Goal: boot the shell with the kernel C compiled by tzcc, one module at a time, from the boot loader outward.
The existing sources are not touched; each step replaces one object in a separate build (`obj/tz/`).

    tz/check.sh                       build (Makefile.tz) + boot in QEMU, expect the shell prompt
    MEMCAP_KB=192 TZ_QEMU_M=1 tz/check.sh   same with a software memory limit (see below)
    python3 tz/session.py IMG CMD...  send shell commands, print the transcript (compare disk/floppy.img vs disk/floppy_tz.img)
    python3 tz/screen.py IMG KEY...   send keys through the QEMU monitor (8042 path), print the VGA text screen

Pipeline per module: `ia16-elf-gcc -E -P` (tzcc has no preprocessor) -> `tzcc --march=ia16` -> `as`.
Data shared with asm / gcc objects is not possible (tzcc defines every global as `var_NAME`); use the tz_* functions in glue.s.

## Steps (kernel.bin must fit the 64KB segment; the kernel stack lives above `__bss_end`)

Every step: boots to the shell prompt; `tz/session.py` transcript and `tz/screen.py` screen equal the ia16-gcc kernel
(differences: `extended ...MB` instead of KB, and the stack headroom numbers).

| step | module (tzcc) | text+data gcc -> tzcc | kernel.bin | kernel stack headroom |
|---|---|---|---|---|
| 0 | (none, all ia16-gcc) | | 24660 | 5832 |
| 1 | kmain | 468 -> 947 | | |
| 2 | console (VGA, 8042, COM1) | 2573 -> 3536 | | |
| 3 | init (+ backend size pass) | 158 -> 246 | 26314 | 4218 |
| 4 | kernel (tick, time, block/wake) | 528 -> 601 | 26514 | 4014 |
| 5 | builtin | 649 -> 1138 | 27000 | |
| 6 | diskio | 239 -> 545 | 27320 | |
| 7 | kexec (loader + block pool) | 956 -> 1871 | 28454 (with the memory limit code) | 2038 |
| 8 | `--gc-sections` (ELF link + objcopy; one section per function) | | **25744** | **4730** |
| 9 | **all kernel C**: fs (fat12min + fsbackend_fat + fatcmd + vfs + dev + dev_fda), io, pipe, sysfile; libc -> glue.S; minimal configuration `TZ_MAXSLOT=8` | 18479 -> about 24000 | **33898** | **14984** |

Step 9 (2026-10-24): no C object of the kernel is compiled by ia16-gcc any more (ia16-gcc is only the preprocessor; as / ld
stay). `tools/test_interactive.py` with `TIZIX_IMG=disk/floppy_tz.img`: FAIL 0, same as the gcc image.
`__bss_end` = 0xC476 with 8 slots (8 x 1KB syscall stacks). `TZ_MAXSLOT=N` changes the number (the user commands are still ia16-gcc).

Not converted (gcc text+data, bytes): vfs 1111, dev 94, io 823, fsbackend_fat 1090, fatcmd 3350, fat12min 6288,
sysfile 2488, pipe 1815, dev_fda 158, libc 585, libgcc 677 = 18479. At the ratio seen so far (x1.4 .. x2.3) that is
+8KB .. +20KB, against 2.1KB of kernel stack headroom: the whole kernel does not fit as long as the syscall stacks
(24 slots x 1KB in .bss) stay in the kernel segment. See X86IA16.md (P1, case E): running the kernel C on the process
stack removes those 24KB, and tzcc already compiles with SS != DS.

## Software memory limit

QEMU cannot go below 1MB and the BIOS always reports 639KB, so the limit is software: `MEMCAP_KB=N` (any value; 0 = off)
makes tz/kmain.c write min(BIOS value, N) back to 0000:0512, which the block pool, `free` and the banner all read.
Nothing is special-cased; the pool is (N - 128) / 4 blocks of 4KB, slots = 2 + blocks / 4. Measured (QEMU -m 1):

| MEMCAP_KB | blocks | slots | result |
|---|---|---|---|
| 300 | 43 | 12 | shell, all commands |
| 200 | 18 | 6 | shell |
| 160 | 8 | 4 | shell; /etc/rc daemons take all 8 blocks, later commands report `no free block` |
| 144 | 4 | 3 | shell; most commands report `no free block` |
| 140 | 3 | 2 | shell (sh.bin takes 3 blocks) |
| <= 130 | 0 | 2 | `init: cannot start /bin/sh.bin (0)` |

(The table above is for the gcc layout, pool from 0x2000. tz/kexec.c now also uses the boot area below the kernel, see ../BOOTMAP.md: 128KB gives 15 blocks / 5 slots and the shell and pipes work; the floor is now 128KB, where the kernel window itself ends.)
A 64KB machine needs a different layout (kernel and processes in one segment, as on the Z80), not a smaller number.

## Rules learned (tzcc as it is)

- char escapes: only `\n \t \r \0` are right. `'\b'` becomes `'b'`, `'\x1b'` becomes `'x'`: write 8 / 27.
- Initialized scalar globals (`int g = 5;`) and `{..}` initializers of structs / string-pointer arrays are dropped silently: assign at boot.
- A `char` parameter of a function called from ia16-gcc code must be declared `char` (gcc pushes it without extending); `unsigned` would read garbage in the high byte.
- A gcc function that returns a narrow type (FRESULT, char, enum) leaves AH undefined (`f_open()` came back as 0x1E00 with AL = 0). Declare it as returning `char` in the tz source: tzcc (ia16 only, `ia16_is_narrowfunc`) clears AH after the call. A typedef name such as BYTE or FRESULT is not recognised: write `char`.
- 16-bit values are compared / divided as signed; keep counts below 0x8000 or use `>>` (logical).
- A pointer target is always one byte, whatever the pointer's type: `int *f; f[3]` reads the byte at f + 3. Only
  global `int` / `unsigned` arrays are word arrays. Read and write words through an asm helper (tz_kw_w, drv_peekw).
- Parameters are copied into the frame on entry, so `&param` is the copy: it cannot walk to the next argument
  (varargs: let an asm wrapper pass the address, as printf in drv16e.S).
- Only the globals the code refers to get a `var_` (fixed 2026-10-25, #201: every local and parameter used to get
  one in .data as well).
- Hand-written asm in glue.S: read stack arguments with `%ss:` (`movw %ss:2(%bx),%ax`). [bx] defaults to DS, and in the timer ISR DS = kernel but SS = the interrupted process, so a plain `2(%bx)` reads the wrong memory (it silently lost 1/3 of the ticks).
- Pointers to locals must not reach a DS pointer (the backend prints "SS pointer escapes").
- No `do-while`, no `p++` stride on struct pointers, no function-pointer tables, no `void *`.
- `*p` always reads / writes ONE BYTE, whatever the pointer type. `p + 1` on an int pointer adds 1 (not 2). An int
  array is reached only by index (`a[i]`) through a global array / global pointer or a LOCAL pointer variable; an int
  pointer PARAMETER indexed directly is also byte-sized: copy it to a local first (or avoid int pointers).
- `extern` data is not shared: every module gets its own `var_NAME`. Cross-module state goes through functions.
- `/` and `%` are signed (`cwtd; idiv`): use `tz_udiv` (glue.S) or `>>` for values that can reach 0x8000; `ult(a, b)`
  for an unsigned compare.
- `static` is ignored: every function is global, so names must be unique across the whole kernel.
- No 32-bit type: keep a 32-bit value as two words (tz/fs.c keeps file position / size as lo, hi).
- tzcc peephole bug fixed 2026-10-24 (gen_ia16.c a_peep_once): `movw %ax, X / movw X, %cx` lost the second line,
  so `buf[0] = param;` stored garbage (every redirected file came out as 0xFF).
