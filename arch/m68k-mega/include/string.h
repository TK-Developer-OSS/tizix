/* arch/m68k-mega/include/string.h  --  freestanding 版(newlib を引かない)
 *   arch/x86-ia16 の同名シムを m68k-elf-gcc(--without-headers)向けに転用。
 *   実体は arch/m68k-mega/libc.c 。
 */
#ifndef _M68K_MEGA_STRING_H
#define _M68K_MEGA_STRING_H

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
