/* arch/x86-ia16/user/stdio.h : x86 ユーザーコマンド用の最小 stdio + ファイル API
 *   実体は ulibc.c(INT 80h syscall)。
 */
#ifndef _X86_IA16_STDIO_H
#define _X86_IA16_STDIO_H

#ifndef NULL
#define NULL ((void *)0)
#endif
#define EOF (-1)

/* 端末 */
int      putchar(int c);
int      getchar(void);
int      puts(const char *s);            /* 改行は付けない */
int      printf(const char *fmt, ...);   /* %s %d %u %c %x %% */
unsigned getticks(void);
void     _exit(void);

/* ファイル(低レベル。fd は 0.. の小整数。失敗は -1)
 *   open flags: O_RDONLY=0(読み) / O_WRONLY=1(新規作成して書き) */
#define O_RDONLY 0
#define O_WRONLY 1

int open(const char *path, int flags);
int close(int fd);
int read(int fd, void *buf, unsigned n);
int write(int fd, const void *buf, unsigned n);
int unlink(const char *path);

/* ディレクトリ: opendir -> dh、readdir で 8.3 名(NUL 終端 <=12)を name へ、
 *   終端で 0 を返す。closedir で解放。 */
int opendir(const char *path);
int readdir(int dh, char *name13);
int closedir(int dh);

#endif
