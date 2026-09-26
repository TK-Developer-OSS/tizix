/* user/ovlsub.c - #36 オーバーレイの実証・オーバーレイ側
 *
 *   親(ovlmain)の空間へ読み込まれて呼ばれるコード片。プロセスではない。
 *   `make tizixovl CMD=ovlsub OVLADDR=0x1600` で **読み込み先の番地向けに**
 *   リンクされる ── tzcc の PIC は IY(親のベース)+ リンク時オフセット を
 *   実アドレスにするので、番地がずれると親の別の場所を触って壊す。
 *
 *   main(arg) の arg は親のバッファの絶対番地。同じプロセス(同じ IY)なので
 *   普通のポインタとして参照できる ── ここも確かめたい点のひとつ。
 *
 *   自分の static(tag[])を読むことで **オーバーレイ内のデータ参照が
 *   IY 相対で正しく解決されているか**も同時に確かめている。tag[] は
 *   オーバーレイ自身の _DATA(= 0x1600 以降)に置かれる。
 *
 *   掟(tzcc): 関数間で同名ローカル禁止 / printf 不使用 / 比較は符号なし。
 *   ★libtzc の重い関数は引かないこと(オーバーレイが太ると領域からはみ出す)。
 */
#include "stdio.h"

/* core 側ベクタ。ovlmain.c と **決め打ちで一致**させること
 * (シンボル共有はしない。表はオーバーレイ領域の直下に固定)。 */
#define OVL_ADDR  0x1600
#define OVL_VEC   (OVL_ADDR - 24)

static char tag[10];
static int  ncall;

int main(int oarg)
{
    char *os_p;
    unsigned os_rc;
    unsigned os_i;

    /* 自分の _DATA を初期化して読む(IY 相対のデータ参照が効いている証拠)*/
    tag[0] = 'O'; tag[1] = 'V'; tag[2] = 'L'; tag[3] = '-';
    tag[4] = 'O'; tag[5] = 'K'; tag[6] = 0;

    ncall++;                       /* 呼ばれた回数(オーバーレイ内の状態)*/

    /* 親のバッファ(同じ IY のプロセス空間)へ書く */
    os_p = (char *)oarg;
    for (os_i = 0; tag[os_i]; os_i++)
        os_p[os_i] = tag[os_i];
    os_p[os_i] = (char)('0' + ncall);
    os_p[os_i + 1] = 0;

    /* #36 続き: **core の関数を呼ぶ**。prs / prnum はこのオーバーレイに存在
     * しない ── core 側の実体が動いていれば "host: ovl_svc arg=1234" が出る。
     * 引数 2 個目以降は引数ブロックへ置いてから callovl する。 */
    pokew(getbase() + OVL_VEC + 8, 1234);
    os_rc = callovl(getbase() + OVL_VEC, 1);

    return (int)os_rc;             /* 期待値 1235 = core が +1 して返した */
}
