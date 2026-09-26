/* user/string.c - tizix ユーザーコマンド用 string.h 実体ライブラリ
 *
 *   従来は driver.c(0x9000 常駐)に drv_strXXX として同居していたが、driver 実体が
 *   4KB を超え _DATA がプロセス空間へ落ちる問題の恒久対策として、デバイス非依存の
 *   純粋関数群をここへ分離した。ビルドは他のユーザーコマンドと同じ経路
 *   (--sdcccall 0 → iy_reg_claude.py → sdasz80)で、base 相対の再配置コードになる。
 *   コマンドは string.rel をリンクし、自プロセスの 4KB 内で実行する。
 *
 *   掟(README): 符号付き int を使わない / 除算乗算を使わない。
 *               文字範囲判定は unsigned 一発比較に落とす(SDCC が jp p/m を吐かない)。
 */
#include "string.h"

size_t strlen(const char *s) __sdcccall(0)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char *strcpy(char *dest, const char *src) __sdcccall(0)
{
    char *d = dest;
    while ((*d++ = *src++) != '\0') ;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) __sdcccall(0)
{
    char *d = dest;
    while (n > 0 && *src != '\0') {
        *d++ = *src++;
        n--;
    }
    while (n > 0) {
        *d++ = '\0';
        n--;
    }
    return dest;
}

char *strcat(char *dest, const char *src) __sdcccall(0)
{
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++) != '\0') ;
    return dest;
}

int strcmp(const char *s1, const char *s2) __sdcccall(0)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

int strncmp(const char *s1, const char *s2, size_t n) __sdcccall(0)
{
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

char *strchr(const char *s, int c) __sdcccall(0)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return 0;
}

char *strrchr(const char *s, int c) __sdcccall(0)
{
    const char *last = 0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) __sdcccall(0)
{
    if (!*needle) return (char *)haystack;
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n) return (char *)haystack;
        haystack++;
    }
    return 0;
}

void *memset(void *s, int c, size_t n) __sdcccall(0)
{
    unsigned char *p = (unsigned char *)s;
    while (n > 0) {
        *p++ = (unsigned char)c;
        n--;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) __sdcccall(0)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n > 0) {
        *d++ = *s++;
        n--;
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) __sdcccall(0)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n > 0) {
        if (*p1 != *p2) return (int)(*p1 - *p2);
        p1++;
        p2++;
        n--;
    }
    return 0;
}
