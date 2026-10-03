/* src/loader.h - PLAT_FLAT32 のポートが arch/<arch>/loader.c で実装する、コマンドのロードの入口
 *
 *   kexec_argv(src/kexec.c の PLAT_FLAT32 節)の流れのうち、CPU とボードに依るのは
 *   次の 2 点だけで、それを arch 側が出す:
 *     plat_load  像をスロットのどこへどう置くか(番地、コードとデータの置き分け、再配置)
 *     plat_ctx   最初に「復帰」させる文脈の形(arch の例外入口が降ろすフレーム)
 *   空きスロット探し、argv[] と引数文字列の組み立て、ps 用の名前表、プロセス表への
 *   登録は src/kexec.c が全アーキ共通でやる。
 */
#ifndef LOADER_H
#define LOADER_H

#include "ff.h"

#define KEXEC_AV_MAX     32                /* argv[] 枠数(末尾 NULL 込み) */
#define KEXEC_POOL_MAX   0x2C0             /* 引数文字列(NUL 区切り)の合計の上限 */
/* argv[] + 文字列に要る大きさ。文字列が上限で打ち切られても、各トークンの終端 NUL は
 * 書かれるので、そのぶん(最大 KEXEC_AV_MAX 個)を足してある。 */
#define KEXEC_ARGV_BYTES (KEXEC_AV_MAX * 4 + KEXEC_POOL_MAX + KEXEC_AV_MAX)

struct kimage {                 /* plat_load が埋める */
    unsigned long entry;        /* 実行を始める番地 */
    unsigned long argv;         /* argv[] と引数文字列を置く場所。4 バイト境界で、
                                 * KEXEC_ARGV_BYTES ぶん空いていること。スタック(top から
                                 * 下へ伸びる)に踏まれない位置 = 像の直後に取る */
    unsigned long top;          /* スタックの頂上。偽コンテキストはこの直下に積む */
};

/* 開いてある fp の像が何スロット要るか(1..)。見出しを読んだら fp を先頭へ戻しておくこと。
 * 戻り: 1.. = スロット数 / 0xFF = 像が壊れている・読めない(#113。z80 の連続 N ブロックと同じ考え方) */
unsigned char plat_need(FIL *fp);

/* 開いてある fp の像をスロット n..n+k-1(連続、k = plat_need の値)へ載せ、im を埋める。
 * 戻り: 1 = 載せた / 0 = 枠に入らない / 0xFF = 像が壊れている・読めない */
unsigned char plat_load(FIL *fp, unsigned char n, unsigned char k, struct kimage *im);

/* im の像を argc / argv で起動する偽コンテキストを積み、そのときの SP を返す。
 * スケジューラがこの SP へ切り替えて例外から「復帰」すると、entry から走り出す。
 * フレームの形は arch の例外入口(crt0)と一致させること。 */
unsigned long plat_ctx(const struct kimage *im, unsigned long argc, unsigned long argv);

#endif
