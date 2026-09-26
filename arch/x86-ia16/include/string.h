/* arch/x86-ia16/include/string.h  --  freestanding 版(newlib を引かない)
 *   -I arch/x86-ia16/include を system より前に置いて <string.h> をこれに差し替える。
 *   実体は arch/x86-ia16/libc.c 。
 */
#ifndef _X86_IA16_STRING_H
#define _X86_IA16_STRING_H

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
