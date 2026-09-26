/* arch/x86-ia16/user/ulibc.c : ユーザーコマンド用 libc(INT 80h syscall)
 *   AH=0 exit / 1 putchar(AL) / 2 getchar->AX / 3 getticks->AX
 */
#include "stdio.h"

int putchar(int c)
{
	__asm__ volatile ("int $0x80" : : "a"((unsigned)0x0100 | (unsigned char)c));
	return c;
}

int getchar(void)
{
	unsigned r;
	__asm__ volatile ("int $0x80" : "=a"(r) : "a"((unsigned)0x0200));
	return (int)(unsigned char)r;
}

unsigned getticks(void)
{
	unsigned r;
	__asm__ volatile ("int $0x80" : "=a"(r) : "a"((unsigned)0x0300));
	return r;
}

int puts(const char *s)
{
	while (*s)
		putchar((unsigned char)*s++);
	return 0;
}

/* ---- ファイル syscall(INT 80h AH=4..11)----
 *   引数はレジスタ渡し: AH=func, AL=小引数, BX/CX/SI/DI=残り。
 *   ia16 の inline asm では複数レジスタ拘束が面倒なので、汎用の
 *   syscall5(func, al, bx, cx, si, di) を asm 1 本にまとめる。 */
extern unsigned syscall5(unsigned func, unsigned al, unsigned bx,
                         unsigned cx, unsigned si, unsigned di);

int open(const char *path, int flags)
{
	unsigned r = syscall5(4, (unsigned)flags, 0, 0, (unsigned)path, 0);
	return (r == 0xFFFF) ? -1 : (int)r;
}

int close(int fd)
{
	syscall5(5, (unsigned)fd, 0, 0, 0, 0);
	return 0;
}

int read(int fd, void *buf, unsigned n)
{
	return (int)syscall5(6, 0, (unsigned)fd, n, 0, (unsigned)buf);
}

int write(int fd, const void *buf, unsigned n)
{
	return (int)syscall5(7, 0, (unsigned)fd, n, 0, (unsigned)buf);
}

int unlink(const char *path)
{
	return (syscall5(11, 0, 0, 0, (unsigned)path, 0) == 0xFFFF) ? -1 : 0;
}

int opendir(const char *path)
{
	unsigned r = syscall5(8, 0, 0, 0, (unsigned)path, 0);
	return (r == 0xFFFF) ? -1 : (int)r;
}

int readdir(int dh, char *name13)
{
	return (int)syscall5(9, 0, (unsigned)dh, 0, 0, (unsigned)name13);
}

int closedir(int dh)
{
	syscall5(10, (unsigned)dh, 0, 0, 0, 0);
	return 0;
}

/* string.h(arch/x86-ia16/include/string.h)は宣言だけで実体はカーネル側
 * libc.c にあるが、あちらはユーザーコマンドにはリンクされない。grep 等
 * ユーザーコマンドが strstr を使うためここに実体を置く。 */
char *strstr(const char *hay, const char *needle)
{
	const char *h, *n;

	if (!*needle)
		return (char *)hay;
	for (; *hay; hay++) {
		h = hay;
		n = needle;
		while (*h && *n && *h == *n) { h++; n++; }
		if (!*n)
			return (char *)hay;
	}
	return 0;
}

static void put_u(unsigned v)
{
	char b[6];
	int i = 0;
	do {
		b[i++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (i)
		putchar(b[--i]);
}

int printf(const char *fmt, ...)
{
	__builtin_va_list ap;
	__builtin_va_start(ap, fmt);
	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			putchar((unsigned char)*fmt);
			continue;
		}
		fmt++;
		switch (*fmt) {
		case 's': {
			const char *s = __builtin_va_arg(ap, const char *);
			while (*s)
				putchar((unsigned char)*s++);
			break;
		}
		case 'd': {
			int d = __builtin_va_arg(ap, int);
			if (d < 0) { putchar('-'); d = -d; }
			put_u((unsigned)d);
			break;
		}
		case 'u':
			put_u(__builtin_va_arg(ap, unsigned));
			break;
		case 'c':
			putchar(__builtin_va_arg(ap, int));
			break;
		case '%':
			putchar('%');
			break;
		default:
			putchar('%');
			putchar((unsigned char)*fmt);
			break;
		}
	}
	__builtin_va_end(ap);
	return 0;
}
