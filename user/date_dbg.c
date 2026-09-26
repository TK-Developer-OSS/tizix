/* user/date_dbg.c - date のデバッグ版(実機完走・切り分け用)
 *
 *   狙い: kexec の単一 reloc(0x29=call _main)だけで完走するイメージにする
 *   → 内部 call・内部 jp を 1 個も出さない。これで「iy_reg 修正が効いた」
 *   「時刻分解が実機で正しい」を確定し、残る問題を『内部 call/jp の reloc
 *   一般化』の一点に絞り込む。
 *
 *   ★★ このビルドは切り分け試験用(TEST): time_get() 直後に di、main 末尾で
 *      ei を入れ、date_dbg 実行中(≒25ms の減算ループ+printf)だけプリエンプ
 *      ションを止める。実機で回し、化けが消えれば「context switch が犯人」
 *      と確定、消えなければ context switch は無罪(仮説 falsified)。★TEST 行
 *      2 箇所を消せば元の date_dbg に戻る(t の宣言と代入もあわせて戻す)。
 *
 *   ★実測で分かった罠(このデバッグを作る過程で判明。本丸=reloc一般化の
 *     裏付け):
 *     (a) 複数引数 printf は文字列を bc に載せ(ld bc,#___str)、iy_reg が
 *         停止する → printf は「文字列のみ」or「文字列+int1個」に割る。
 *     (b) iy_reg 注入は各 ld hl を7行に膨らませ、タイトなループ内で多用
 *         すると近傍 jr が範囲外に → トレースはループ外(ステージ境界)へ。
 *     (c) ★call が無くてもループ本体が育つと SDCC は jr でなく絶対 jp を
 *         吐き、これも base 加算が要る(iy_reg も kexec も触らない)。
 *         → 「call さえ無ければ reloc 不要」は誤り。ループ本体を極小化して
 *            jr に収める必要がある。
 *
 *   うるう年: 1970-2099 の範囲では非うるう世紀年が無い(2000 は 400 で
 *   割れてうるう)ので leap == (y&3)==0 が厳密。判定を潰して年ループ本体を
 *   最小化し、絶対 jp を出させない(デバッグ限定の割り切り。本番 date.c は
 *   400/100 判定を持つ)。
 *   除算・乗算不使用(全減算)。32bit epoch は %lu 非対応なので分解後に出す。
 */
#include "stdio.h"

extern unsigned long time_get(void);        /* 0x004A */

static const unsigned char mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

void main(void)
{
    unsigned long t;                    /* ★TEST: 初期化子を外し、代入は di 直前の文にする */
    unsigned long r;
    unsigned int days = 0;
    unsigned int y, h = 0, mi = 0, s;
    unsigned char mo, dm;
    unsigned char leap;

    t = time_get();                     /* time_get は内部で di;read;ei する。取得は割込 on で完了 */
    __asm di __endasm;                  /* ★TEST: ここから date_dbg 実行中プリエンプション停止 */

    printf("[0] t.lo=");  printf("%u\n", (unsigned int)t);

    while (t >= 86400UL) { t -= 86400UL; days++; }
    r = t;
    while (r >= 3600UL) { r -= 3600UL; h++; }
    while (r >= 60UL)   { r -= 60UL;   mi++; }
    s = (unsigned int)r;

    printf("[1] days=");   printf("%u", days);
    printf(" h=");         printf("%u", h);
    printf(" mi=");        printf("%u", mi);
    printf(" s=");         printf("%u\n", s);

    y = 1970;
    for (;;) {
        unsigned int dy;
        leap = ((y & 3) == 0);          /* 1970-2099 限定で厳密 */
        dy = leap ? 366 : 365;
        if (days < dy) break;
        days -= dy; y++;
    }
    printf("[3] year=");  printf("%u", y);
    printf(" leap=");     printf("%u", (unsigned)leap);
    printf(" rem=");      printf("%u\n", days);

    mo = 0;
    for (;;) {
        dm = mdays[mo];
        if (mo == 1 && leap) dm = 29;
        if (days < dm) break;
        days -= dm; mo++;
    }
    printf("[4] mo=");    printf("%u", (unsigned)mo);
    printf(" dm=");       printf("%u", (unsigned)dm);
    printf(" rem=");      printf("%u\n", days);

    printf("[5] RESULT ");   printf("%u", y);
    printf("-");             printf("%u", (unsigned)(mo + 1));
    printf("-");             printf("%u", (unsigned)(days + 1));
    printf(" ");             printf("%u", h);
    printf(":");             printf("%u", mi);
    printf(":");             printf("%u\n", s);

    __asm ei __endasm;                  /* ★TEST: プリエンプション復帰(以降は _kexit の restore でも ei される) */
}
