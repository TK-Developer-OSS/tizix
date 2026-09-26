/* user/string.h - tizix ユーザーコマンド用 string.h
 *
 *   実体は string.c → string.rel。コマンドが自プロセス内へリンクして使う。
 *   (旧版は driver.c 常駐関数を drv_tbl[] 経由で叩くマクロ群だった。driver 4KB 超過
 *    対策として純粋関数をコマンド側へ移設し、通常のプロトタイプ宣言に戻した。)
 *   ABI は全域 --sdcccall 0 に統一。
 */
#ifndef _STRING_H
#define _STRING_H

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned int size_t;
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

size_t strlen(const char *s) __sdcccall(0);
char  *strcpy(char *dest, const char *src) __sdcccall(0);
char  *strncpy(char *dest, const char *src, size_t n) __sdcccall(0);
char  *strcat(char *dest, const char *src) __sdcccall(0);
int    strcmp(const char *s1, const char *s2) __sdcccall(0);
int    strncmp(const char *s1, const char *s2, size_t n) __sdcccall(0);
char  *strchr(const char *s, int c) __sdcccall(0);
char  *strrchr(const char *s, int c) __sdcccall(0);
char  *strstr(const char *haystack, const char *needle) __sdcccall(0);
void  *memset(void *s, int c, size_t n) __sdcccall(0);
void  *memcpy(void *dest, const void *src, size_t n) __sdcccall(0);
int    memcmp(const void *s1, const void *s2, size_t n) __sdcccall(0);

#endif
