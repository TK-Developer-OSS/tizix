/* arch/x86-ia16/user/drvpsp.h : the per-process area at pseg:0000..007F (below the arguments at 0080, the image at 0100),
 *   as DOS keeps its PSP there. kexec zeroes it and writes the DRIVER's segment. The DRIVER keeps no data of its own
 *   (it runs with CS = its segment and DS = SS = the caller's), so whatever it must remember per process lives here.
 *   Plain #defines: read by C (kernel, driver), assembler (drvc.S, drv16e.S).
 */
#define DRV_PSP_ENTRY  0x00     /* far pointer to the DRIVER's entry: offset (always 0) ... */
#define DRV_PSP_SEG    0x02     /* ... and segment (kexec writes it) */
#define DRV_PSP_RIP    0x04     /* the DRIVER entry keeps the far return address here while a call runs */
#define DRV_PSP_RCS    0x06
#define DRV_PSP_DH     0x08     /* open directory handle + 1 (0 = none; the area starts zeroed) */
#define DRV_PSP_FILES  0x10     /* FILE[DRV_NFILE], 12 bytes each: fd, eof, err, used, pos (lo, hi) */
#define DRV_NFILE      4
#define DRV_FILE_SIZE  12
#define DRV_PSP_END    0x80     /* 0x40..0x7F free */
