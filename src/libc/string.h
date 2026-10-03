/* src/libc/string.h  --  freestanding 版 string.h(gcc 系アーキのカーネル共通)
 *   ツールチェインの libc ヘッダを引かない(m68k-elf-gcc は --without-headers、
 *   xtensa-esp-elf は newlib を持つが ABI が合わずリンクしない)。実体は隣の libc.c。
 *   src/ 直下ではなく libc/ に置いてあるのは、z80 のビルド(-I src)で SDCC 自身の
 *   <string.h> を隠さないため。gcc 系は arch/common-gcc.mk(x86-ia16 は自分の Makefile)が
 *   -I で足す。
 */
#ifndef _TZ_LIBC_STRING_H
#define _TZ_LIBC_STRING_H

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef __SIZE_TYPE__ size_t;
#endif

void  *memcpy (void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset (void *d, int c, size_t n);
int    memcmp (const void *a, const void *b, size_t n);

size_t strlen (const char *s);
int    strcmp (const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy (char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
char  *strcat (char *d, const char *s);
char  *strchr (const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr (const char *hay, const char *needle);

#endif
