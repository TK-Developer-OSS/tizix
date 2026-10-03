/* arch/esp32-wroom-32e loader.c - 外部コマンドのロード(src/loader.h の plat_load / plat_ctx。task.md #108)
 *
 *   空きスロット探し・argv[] の組み立て・プロセス表への登録は全アーキ共通の
 *   src/kexec.c にあり、ここにはこのチップに依る 2 点(像の置き方、最初の文脈の形)
 *   だけを置く。
 *
 *   ESP32 はコードとデータを同じ場所に置けない: DRAM は実行できず、IRAM は
 *   32bit アクセスしか効かない(バイトで読む .rodata を置けない)。そこで 1 プロセスに
 *   枠を 2 つ持たせる(番地は include/plat.h):
 *     コード枠  PROC_TEXT_BASE + (n-1) * PROC_TEXT_SIZE   IRAM、16KB
 *     データ枠  PLAT_SLOT_ADDR(n)(PROC_DATA_BASE〜)       DRAM、16KB
 *                 [見出し 32B][.rodata+.data][.bss][argv[] と文字列][ … スタック ↓ ]
 *   像が 16KB に収まらなければ、連続した k スロットを使う(コード枠もデータ枠も k 個ずつ続けて。
 *   plat_need が k を決める。#113)。
 *
 *   Xtensa のコードは絶対番地を命令には埋めず、リテラル(32bit の語)に置いて
 *   l32r で引く。関数呼び出しと分岐は PC 相対。だから枠を変えて動かすには
 *   「番地の入っている語」に枠の差を足せば足りる。その語の場所は tools/mkcmd.py が
 *   ビルド時に割り出して .bin の末尾に表で持たせてある(番地を変えて 3 回リンクし、
 *   変わった語を拾う)。ここはその表のとおりに足すだけで、命令は書き換えない。
 *
 *   .bin の形(リトルエンディアン。tools/mkcmd.py と一致させること):
 *     +0  u32 magic 'TZX1'      +4  u32 entry(コード先頭から)
 *     +8  u32 コードのバイト数   +12 u32 .rodata+.data のバイト数(どちらも 4 の倍数)
 *     +16 u32 .bss のバイト数    +20 u32 再配置の個数
 *     続けて コード、データ、再配置表(u16 × 個数):
 *       bit15 = 直す語がデータ側にある / bit14 = 語の中身がデータ枠の番地 /
 *       bit13-0 = その側の先頭から数えた語の番号
 *   リンク時の番地は slot1 の枠(PROC_TEXT_BASE / PROC_DATA_BASE)。
 */
#include "loader.h"
#include "kmem.h"

#define CMD_MAGIC    0x31585A54UL          /* 'T','Z','X','1' */
#define STACK_MIN    0x800                 /* これだけスタックが残らない像は載せない。
                                            * tools/mkcmd.py も同じ値でビルド時に止める */

struct cmdhdr {
    unsigned long magic, entry, tsize, dsize, bsize, nrel;
};

/* 読み込みの作業域(512B、4 バイト境界)。IRAM へはここからワードで写す。 */
static unsigned long kx_buf[128];

/* 見出しを読んで確かめる。戻り: 1 = この像 / 0 = 違う・読めない */
static int read_hdr(FIL *fp, struct cmdhdr *h)
{
    UINT br;

    if (f_read(fp, kx_buf, sizeof *h, &br) != FR_OK || br != sizeof *h)
        return 0;
    *h = *(struct cmdhdr *)kx_buf;
    return h->magic == CMD_MAGIC && !((h->tsize | h->dsize) & 3UL) && h->entry < h->tsize;
}

/* 何スロット要るか(#113)。命令は tsize、データは .rodata+.data+.bss + argv + スタックの最小。
 * どちらかが 16KB を超えれば、連続したスロットを複数取る(命令ブロックとデータブロックは対で
 * 取るので、多い方に合わせる)。大きな静的配列(.bss)を持つコマンドはこれで自動的に広い枠を得る。 */
unsigned char plat_need(FIL *fp)
{
    struct cmdhdr h;
    unsigned long dneed, kt, kd;

    if (!read_hdr(fp, &h))
        return 0xFF;
    f_lseek(fp, 0);
    dneed = ((h.dsize + h.bsize + 3UL) & ~3UL) + KEXEC_ARGV_BYTES + STACK_MIN;
    kt = (h.tsize + PROC_TEXT_SIZE - 1) / PROC_TEXT_SIZE;
    kd = (dneed + PROC_DATA_SIZE - 1) / PROC_DATA_SIZE;
    if (kt < kd) kt = kd;
    return kt > PLAT_NSLOT - 1 ? (unsigned char)PLAT_NSLOT : (unsigned char)kt;   /* 入らない数なら kexec が断る */
}

unsigned char plat_load(FIL *fp, unsigned char n, unsigned char k, struct kimage *im)
{
    struct cmdhdr h;
    unsigned long tbase, dbase, avbase, left, got, i;
    volatile unsigned long *tw;
    UINT br;

    if (!read_hdr(fp, &h))
        return 0xFF;                       /* このアーキの像ではない */
    avbase = (h.dsize + h.bsize + 3UL) & ~3UL;
    if (h.tsize > k * PROC_TEXT_SIZE
        || avbase + KEXEC_ARGV_BYTES + STACK_MIN > k * PROC_DATA_SIZE)
        return 0;                          /* 枠に入らない */

    tbase = PROC_TEXT_BASE + ((unsigned long)n - 1UL) * PROC_TEXT_SIZE;
    dbase = PLAT_SLOT_ADDR(n);

    /* コード: 512B ずつ読んで IRAM へワードで書く */
    tw = (volatile unsigned long *)tbase;
    for (left = h.tsize; left; left -= got) {
        got = left > sizeof kx_buf ? sizeof kx_buf : left;
        if (f_read(fp, kx_buf, (UINT)got, &br) != FR_OK || br != got)
            return 0xFF;
        for (i = 0; i < got / 4; i++)
            *tw++ = kx_buf[i];
    }

    /* データ: そのまま枠へ。続く .bss はゼロに */
    if (h.dsize && (f_read(fp, (void *)dbase, (UINT)h.dsize, &br) != FR_OK || br != h.dsize))
        return 0xFF;
    for (i = h.dsize; i < avbase; i++)
        ((unsigned char *)dbase)[i] = 0;

    /* 再配置: 表の語に「この枠 - slot1 の枠」を足す */
    for (left = h.nrel; left; left -= got) {
        const unsigned short *e = (const unsigned short *)kx_buf;

        got = left > sizeof kx_buf / 2 ? sizeof kx_buf / 2 : left;
        if (f_read(fp, kx_buf, (UINT)(got * 2), &br) != FR_OK || br != got * 2)
            return 0xFF;
        for (i = 0; i < got; i++) {
            unsigned long idx = e[i] & 0x3FFFUL;
            volatile unsigned long *w;

            if (idx * 4 >= ((e[i] & 0x8000) ? h.dsize : h.tsize))
                return 0xFF;
            w = (volatile unsigned long *)(((e[i] & 0x8000) ? dbase : tbase) + idx * 4);
            *w += (e[i] & 0x4000) ? dbase - PROC_DATA_BASE : tbase - PROC_TEXT_BASE;
        }
    }

    im->entry = tbase + h.entry;
    im->argv  = dbase + avbase;            /* .bss の直後。スタックは頂上から下へ伸びるので離れている */
    im->top   = dbase + k * PROC_DATA_SIZE;   /* k スロットぶんの頂上(#113) */
    return 1;
}

/* 偽コンテキストは crt0.S の exc_restore が降ろす形(plat.h の CTX_*)。
 * a2 = argc、a3 = argv(call0 ABI の第 1・第 2 引数)、割込み可で entry へ。 */
unsigned long plat_ctx(const struct kimage *im, unsigned long argc, unsigned long argv)
{
    unsigned long *ctx = (unsigned long *)(im->top - CTX_SIZE);
    unsigned k;

    for (k = 0; k < CTX_SIZE / 4; k++)
        ctx[k] = 0;
    ctx[CTX_A(2) / 4] = argc;
    ctx[CTX_A(3) / 4] = argv;
    ctx[CTX_PC / 4]   = im->entry;
    ctx[CTX_PS / 4]   = PS_UM;
    return (unsigned long)ctx;
}
