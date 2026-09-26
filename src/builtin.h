#ifndef _BUILTIN_H
#define _BUILTIN_H

/* builtin_try() の戻り値 */
#define BUILTIN_NONE  0   /* 組み込みではない。呼び出し側でフォールスルー */
#define BUILTIN_OK    1   /* 実行した。REPL 継続                        */
#define BUILTIN_EXIT  2   /* 実行した。シェルを終了する                 */

/* cwd 等のセッション状態を初期化する。
   crt0(gsinit)は BSS をゼロクリアしないため、sh 起動時に明示的に呼ぶこと。 */
void builtin_init(void);

/* cmd が組み込みなら実行して BUILTIN_OK / BUILTIN_EXIT を返す。
   組み込みでなければ BUILTIN_NONE。
   arg は cmd 以降の残り文字列(先頭の空白は除去済み、無ければ "")。 */
int  builtin_try(const char *cmd, const char *arg);

/* cmd が builtin(表 + cd/pwd)なら 1。実行はしない。sh のパイプ判定用。 */
int  builtin_is(const char *cmd);

#endif
