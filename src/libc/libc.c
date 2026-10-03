/* src/libc/libc.c  --  freestanding な最小 libc プリミティブ(gcc 系アーキのカーネル共通)
 *   番地にも CPU にも依らない素の C。m68k-mega / esp32-wroom-32e(arch/common-gcc.mk)と
 *   x86-ia16 のカーネルがリンクする。-nostdlib のため memcpy/memset は GCC の暗黙呼び出しにも
 *   使われる。z80 は SDCC のライブラリを使うのでリンクしない。宣言は隣の string.h。
 */
#include <string.h>

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *dd = d;
	const unsigned char *ss = s;
	while (n--)
		*dd++ = *ss++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *dd = d;
	const unsigned char *ss = s;
	if (dd <= ss || dd >= ss + n) {
		while (n--)
			*dd++ = *ss++;
	} else {
		dd += n;
		ss += n;
		while (n--)
			*--dd = *--ss;
	}
	return d;
}

void *memset(void *d, int c, size_t n)
{
	unsigned char *dd = d;
	while (n--)
		*dd++ = (unsigned char)c;
	return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *aa = a, *bb = b;
	while (n--) {
		if (*aa != *bb)
			return (int)*aa - (int)*bb;
		aa++;
		bb++;
	}
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	while (n && *a && *a == *b) {
		a++;
		b++;
		n--;
	}
	if (n == 0)
		return 0;
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char *strcpy(char *d, const char *s)
{
	char *r = d;
	while ((*d++ = *s++))
		;
	return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
	char *r = d;
	while (n && (*d = *s)) {
		d++;
		s++;
		n--;
	}
	while (n--)
		*d++ = 0;
	return r;
}

char *strcat(char *d, const char *s)
{
	char *r = d;
	while (*d)
		d++;
	while ((*d++ = *s++))
		;
	return r;
}

char *strchr(const char *s, int c)
{
	for (; *s; s++)
		if (*s == (char)c)
			return (char *)s;
	return (c == 0) ? (char *)s : 0;
}

char *strrchr(const char *s, int c)
{
	const char *last = 0;
	for (; *s; s++)
		if (*s == (char)c)
			last = s;
	if (c == 0)
		return (char *)s;
	return (char *)last;
}
