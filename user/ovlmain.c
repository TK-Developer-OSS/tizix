/* user/ovlmain.c - #36 オーバーレイ(自前で半ロード)の実証・親側
 *
 *   #35 の「子プロセスに出す」方式は、実行の瞬間に空きブロックが 1 個要る。
 *   vi の `:w` をそれでやると「起動はできるが保存できない」状態を作りうる。
 *   オーバーレイなら **ブロックを 1 個も余分に食わない** ── 自分の空間の
 *   空き番地へコード片を読み込んで呼ぶだけ。1980 年代の常套手段。
 *
 *   確かめること:
 *     (1) 自分の空間へ .bin を fread できるか
 *     (2) 実行時に決まる番地を呼べるか(tzcc は関数ポインタ非対応 → callovl)
 *     (3) **オーバーレイ内の IY 相対 PIC が親の IY で正しく解決されるか**
 *         (= リンク番地と読み込み番地が一致していれば動くか)
 *     (4) オーバーレイが親のバッファを更新できるか
 *
 *   レイアウト(nblk=2 のとき top=0x2000, SP=top-0x140=0x1EC0):
 *     0x0000..       ovlmain 自身のコード + データ(pad で 0x1600 未満に収める)
 *     0x1600..0x1A00 オーバーレイ領域(OVL_ADDR)
 *     0x1A00..0x1EC0 スタック(下へ伸びる。約 1.2KB)
 *   pad[] は **nblk=2 を確保する**ためのもの(像が 3777B を超えないと 1 ブロック
 *   になり、0x1600 という番地自体がプロセス空間の外になる)。
 *
 *   掟(tzcc): 関数間で同名ローカル禁止 / 乗除算を書かない / printf 不使用 /
 *             比較は符号なし / **配列アドレスを変数へ代入しない**(#35 のバグ)。
 */
#include "stdio.h"

#define OVL_ADDR  0x1600
#define OVL_MAX   1024
/* #36 続き: オーバーレイ → core 呼び出しベクタ。**オーバーレイ領域の直下**に
 * 置く(tzcc/ovlvec.s、リンク時 -b _OVLVEC=)。+0 サンク 8B / +8 引数 16B。 */
#define OVL_VEC   (OVL_ADDR - 24)

static char pad[2600];      /* nblk=2 を確保するための詰め物(内容は使わない)*/
static char buf[40];

int main(int argc, char **argv)
{
    unsigned mo_at;
    unsigned mo_rc;
    int mo_n;
    FILE *mo_fp;

    (void)argc;
    (void)argv;
    pad[0] = 0;                          /* pad を「使っている」ことにする */

    buf[0] = 'e'; buf[1] = 'm'; buf[2] = 'p'; buf[3] = 't'; buf[4] = 'y'; buf[5] = 0;

    mo_at = getbase() + OVL_ADDR;
    prs("ovl: base=");
    prnum(getbase());
    prs(" load-at=");
    prnum(mo_at);
    putchar('\n');

    mo_fp = fopen("/bin/ovlsub.bin", "r");
    if (mo_fp == NULL) {
        puts("ovl: /bin/ovlsub.bin not found");
        return 1;
    }
    /* ★自分の空間の空き番地へ直接読み込む(= 半ロード) */
    mo_n = fread((char *)mo_at, 1, OVL_MAX, mo_fp);
    fclose(mo_fp);

    prs("ovl: loaded ");
    prnum((unsigned)mo_n);
    prs(" bytes\n");
    if (mo_n == 0) {
        puts("ovl: read failed");
        return 1;
    }

    /* ★実行時に決まる番地を呼ぶ。引数は親のバッファの絶対番地。
     * 配列アドレスは **引数の位置で** 渡すこと(#35 の tzcc バグ回避)。 */
    mo_rc = callovl(mo_at, (unsigned)buf);

    prs("ovl: rc=");
    prnum(mo_rc);
    putchar('\n');
    prs("ovl: buf=");
    puts(buf);

    /* 2 回呼べること(オーバーレイは常駐しない = 何度でも読み直せる) */
    mo_rc = callovl(mo_at, (unsigned)buf);
    prs("ovl: rc2=");
    prnum(mo_rc);
    putchar('\n');
    return 0;
}

/* ------------------------------------------------------------------
 * ovl_svc: オーバーレイから呼ばれる core 側の窓口(#36 続き)。
 *   ovlvec.s のサンク経由で入ってくる。sel は callovl() の唯一の引数、
 *   2 個目以降は引数ブロック(base+OVL_VEC+8)から読む。
 *
 *   ★これが通ると何が変わるか: オーバーレイが core のヘルパーを呼べるので、
 *     切り出したコードが prs/prnum のような共有関数を **自前で抱え込まなくて
 *     よくなる**。#37 で core が 639B しか減らないのにオーバーレイが 2672B
 *     かかったのは、これが無かったため。
 * ------------------------------------------------------------------ */
int ovl_svc(int ov_sel)
{
    unsigned ov_arg;

    if (ov_sel == 1) {
        ov_arg = peekw(getbase() + OVL_VEC + 8);
        prs("host: ovl_svc arg=");      /* prs はオーバーレイ側に無い */
        prnum(ov_arg);
        putchar('\n');
        return (int)(ov_arg + 1);
    }
    return -1;
}
