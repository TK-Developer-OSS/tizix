/* user/stdlib.c - tizix ユーザーコマンド用 stdlib.h 実体ライブラリ
 *
 *   driver.c から分離したデバイス非依存の純粋関数群(string.c と同じ理由・同じ経路)。
 *   コマンドは stdlib.rel をリンクして自プロセスの 4KB 内で実行する。
 *
 *   掟(README):
 *     1. 符号付き int の比較を使わない
 *        SDCC は符号比較で overflow 補正の jp p / jp m / jp po / jp pe を吐く。
 *        iy_reg_claude.py はこれらのスキップ分岐を絶対 jp のまま残すため base!=0 で
 *        暴走する。よって全ての大小比較は unsigned 一発比較(→ jr c/nc)へ落とす。
 *     2. 除算・乗算(/ % *)を使わない
 *        標準ライブラリ(__divuint 等)を引けないため。減算ループの udiv / umul で代用。
 */
#include "stdlib.h"

/* ---- 除算・乗算の自前実装(libc 非依存) ---- */
static unsigned int udiv(unsigned int num, unsigned int den)
{
    unsigned int quot = 0;
    if (den == 0) return 0;
    while (num >= den) {
        num -= den;
        quot++;
    }
    return quot;
}

static unsigned int umul(unsigned int a, unsigned int b)
{
    unsigned int res = 0;
    while (b > 0) {
        if (b & 1) res += a;
        a <<= 1;
        b >>= 1;
    }
    return res;
}

/* '0'..'9' 判定を unsigned 一発比較で(符号比較を出さない) */
#define IS_DIGIT(ch)  ((unsigned char)((ch) - '0') <= 9u)
#define IS_SPACE(ch)  ((ch) == ' ' || (ch) == '\t' || (ch) == '\n' || (ch) == '\r')

int atoi(const char *s) __sdcccall(0)
{
    unsigned int res = 0;
    unsigned char neg = 0;

    while (IS_SPACE(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    while (IS_DIGIT(*s)) {
        res = (res << 3) + (res << 1) + (unsigned char)(*s - '0');
        s++;
    }
    return neg ? -(int)res : (int)res;
}

long atol(const char *s) __sdcccall(0)
{
    unsigned long res = 0;
    unsigned char neg = 0;

    while (IS_SPACE(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    while (IS_DIGIT(*s)) {
        res = (res << 3) + (res << 1) + (unsigned char)(*s - '0');
        s++;
    }
    return neg ? -(long)res : (long)res;
}

int abs(int j) __sdcccall(0)
{
    unsigned int u = (unsigned int)j;
    if (u & 0x8000u) u = (unsigned int)(-(int)u);
    return (int)u;
}

long labs(long j) __sdcccall(0)
{
    unsigned long u = (unsigned long)j;
    if (u & 0x80000000UL) u = (unsigned long)(-(long)u);
    return (long)u;
}

static unsigned int rand_seed = 1;

int rand(void) __sdcccall(0)
{
    rand_seed = (unsigned int)(umul(rand_seed, 25173U) + 13849U);
    return (int)(rand_seed & 0x7FFF);
}

void srand(unsigned int seed) __sdcccall(0)
{
    rand_seed = seed;
}

char *itoa(int value, char *str, int radix) __sdcccall(0)
{
    char buf[18];
    unsigned int i = 0;
    unsigned int v;
    char *out = str;
    unsigned int r;

    /* radix < 2 || radix > 16 を unsigned 一発比較で */
    if ((unsigned int)(radix - 2) > 14u) {
        *str = '\0';
        return str;
    }
    r = (unsigned int)radix;

    if (((unsigned int)value & 0x8000u) && r == 10u) {
        *out++ = '-';
        v = (unsigned int)(-value);
    } else {
        v = (unsigned int)value;
    }

    if (v == 0) {
        buf[i++] = '0';
    } else {
        while (v != 0) {
            unsigned int q = udiv(v, r);
            unsigned int rem = v - umul(q, r);
            buf[i++] = (rem <= 9u) ? (char)('0' + rem) : (char)('a' + rem - 10u);
            v = q;
        }
    }

    while (i != 0) {
        *out++ = buf[--i];
    }
    *out = '\0';
    return str;
}
