/* arch/z80-pc8801/pipestub.c  --  カーネルパイプ(src/pipe.c)のスタブ */
#include "pipe.h"

unsigned char pipe_setup(unsigned char rblk)    { (void)rblk; return 0; }
void pipe_attach_writer(unsigned char wblk)     { (void)wblk; }
void pipe_teardown(void)                        { }
int  pipe_putc(int c)                           { return c; }
int  pipe_getc(void)                            { return -1; }
unsigned char pipe_is_reader(unsigned char blk) { (void)blk; return 0; }
unsigned char pipe_ovf(void)                    { return 0; }
unsigned char pipe_tail(unsigned want)          { (void)want; return 0; }
void pipe_note_exit(unsigned char blk)          { (void)blk; }

unsigned char krun_pipe(const char *ln, const char *lp, unsigned char lc,
                        const char *rn, const char *rp, unsigned char rc)
{
	(void)ln; (void)lp; (void)lc; (void)rn; (void)rp; (void)rc;
	return 1;
}
