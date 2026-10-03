// 条件コンパイル: #ifdef / #ifndef / #if / #elif / #else / #endif / #undef。
// 採用しない枝は字句解析せずに読み飛ばす(中に tzcc が解釈できない構文や、
// コメント・文字列の中の "#endif" があってもよい)。
// tizix の共有ソースを gcc / SDCC と 1 本で持つための機能。
// EXPECT: ABCDEFGHIJKL
#include <stdio.h>

#define FEATURE 1
#define LEVEL 3

#ifdef __TZCC__
#define C1 'A'
#else
#define C1 'x'
this branch is never lexed: { [ (
#endif

#ifndef NOT_DEFINED_ANYWHERE
#define C2 'B'
#else
#define C2 'x'
#endif

#ifdef NOT_DEFINED_ANYWHERE
#define C3 'x'
#elif defined(FEATURE)
#define C3 'C'
#else
#define C3 'y'
#endif

#if defined(FEATURE) && !defined(NOT_DEFINED_ANYWHERE)
#define C4 'D'
#endif

#if LEVEL >= 3 && (LEVEL != 4 || 0)
#define C5 'E'
#else
#define C5 'x'
#endif

#if 0
#ifdef FEATURE
#define C6 'x'
#else
#define C6 'y'
#endif
/* コメントの中の
#endif は数えない */
static char *skipped = "#endif も 'x' も数えない";
#else
#define C6 'F'
#endif

#ifdef FEATURE
#ifdef NOT_DEFINED_ANYWHERE
#define C7 'x'
#else
#define C7 'G'
#endif
#endif

#undef FEATURE
#ifdef FEATURE
#define C8 'x'
#else
#define C8 'H'
#endif

/* 組み込みの NULL は #ifndef から見ると未定義(ヘッダ側の定義を採用させるため) */
#ifndef NULL
#define C9 'I'
#else
#define C9 'x'
#endif

static void two(void) {
#if LEVEL == 3
    putchar('J');
#elif LEVEL == 4
    putchar('x');
#endif
#ifdef NOT_DEFINED_ANYWHERE
    putchar('x');
#endif
    putchar('K');
}

int main(void) {
    putchar(C1); putchar(C2); putchar(C3); putchar(C4); putchar(C5);
    putchar(C6); putchar(C7); putchar(C8); putchar(C9);
    two();
#if !defined(__TZCC__)
    putchar('x');
#else
    putchar('L');
#endif
    putchar('\n');
    return 0;
}
