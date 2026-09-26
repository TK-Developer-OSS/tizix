/* user/stdlib.h - tizix ユーザーコマンド用 stdlib.h
 *
 *   実体は stdlib.c → stdlib.rel。コマンドが自プロセス内へリンクして使う。
 *   (旧版は driver.c 常駐関数を drv_tbl[] 経由で叩くマクロ群。driver 4KB 超過対策で
 *    純粋関数をコマンド側へ移設し、通常のプロトタイプ宣言に戻した。)
 *   ABI は全域 --sdcccall 0 に統一。
 */
#ifndef _STDLIB_H
#define _STDLIB_H

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned int size_t;
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#define RAND_MAX 32767

int   atoi(const char *s) __sdcccall(0);
long  atol(const char *s) __sdcccall(0);
int   abs(int j) __sdcccall(0);
long  labs(long j) __sdcccall(0);
int   rand(void) __sdcccall(0);
void  srand(unsigned int seed) __sdcccall(0);
char *itoa(int value, char *str, int radix) __sdcccall(0);

#endif
