/* user/dd.c - 外部コマンド dd
 *   dd if=INPUT of=OUTPUT [bs=N] [count=N] [skip=N] [seek=N]
 *   dd INPUT OUTPUT       : 位置引数でも可(後方互換)。
 *
 *   #32: bs= / count= / skip= / seek= を実装し、生ブロックデバイス
 *   (/dev/fda = cpmsim drive A、/dev/fdb = drive B = FAT ボリューム)を
 *   読み書きできるようにした。
 *     dd if=/dev/fdb of=VBR.IMG bs=512 count=1     … VBR を 1 セクタ取る
 *     dd if=VBR.IMG of=/dev/fdb bs=512 count=1     … 書き戻す
 *
 *   **生デバイスは 512B セクタ粒度**。bs は 512 の倍数でなければならない
 *   (カーネル側にバウンスバッファを置かないための割り切り。src/dev.h)。
 *   通常ファイル同士なら bs は任意(既定 128)。
 *   skip= / seek= の単位は bs ブロック(本家 dd と同じ)。
 *
 *   #26: tzcc ビルド(iy_reg 卒業)。argv[] は progname 無し・argv[0] が最初の引数。
 *   掟: fread の nmemb に sizeof(配列) を使わない(tzcc の sizeof は 2 を返す)。
 *       除算・剰余を使わない。printf を使わない(prs / prnum)。
 *       関数間で同名ローカルを作らない(スコープレス変数モデル)。
 */
#include "stdio.h"

#define DD_BUFMAX 512

static char ddbuf[DD_BUFMAX];

/* s が "pfx" で始まれば pfx の直後を返す。違えば 0。 */
static char *match_kv(char *s, char *pfx)
{
    int k = 0;
    while (pfx[k]) {
        if (s[k] != pfx[k]) return 0;
        k++;
    }
    return &s[k];
}

/* 10 進の非負整数を読む。数字が無ければ BADNUM。
 *   掟 1: 乗算を書かない → 10x = 8x + 2x を加算だけで組む。
 *   掟 2: **tzcc の比較は符号なし**。負値を番兵に使うと `x < 0` が常に偽に
 *         なる(実測: count=1 が `count < -1` を真にして弾かれた)。
 *         番兵は 0xFFFF 側に取り、判定は == で行う。 */
#define BADNUM 0xFFFFu

static unsigned parse_num(char *ns)
{
    unsigned nv = 0;
    unsigned nk = 0;
    unsigned n2;
    if (ns[0] == 0) return BADNUM;
    while (ns[nk]) {
        if (ns[nk] < '0') return BADNUM;
        if (ns[nk] > '9') return BADNUM;
        n2 = nv + nv;            /* 2x */
        nv = n2 + n2;            /* 4x */
        nv = nv + nv;            /* 8x */
        nv = nv + n2;            /* 10x */
        nv = nv + (unsigned)(ns[nk] - '0');
        nk++;
    }
    return nv;
}

/* bcnt ブロック × bsz バイトのバイトオフセット。32bit 乗算を避けて加算で積む
 * (bcnt はセクタ数程度なのでループで十分)。 */
static long block_off(unsigned bcnt, unsigned bsz)
{
    long bo = 0;
    unsigned bi = 0;
    while (bi < bcnt) {
        bo = bo + (long)bsz;
        bi++;
    }
    return bo;
}

int main(int argc, char **argv)
{
    char *ifname = 0;
    char *ofname = 0;
    FILE *ifp;
    FILE *ofp;
    unsigned i;
    unsigned bs = 128;
    unsigned count = 0;          /* has_count==0 なら無視(最後まで) */
    unsigned has_count = 0;
    unsigned skip = 0;
    unsigned seek = 0;
    unsigned blocks = 0;
    unsigned bad = 0;
    int got;
    long off;

    for (i = 0; i < (unsigned)argc; i++) {
        char *a = argv[i];
        char *v;
        if (a == 0 || a[0] == 0) continue;

        v = match_kv(a, "if=");
        if (v != 0) { ifname = v; continue; }
        v = match_kv(a, "of=");
        if (v != 0) { ofname = v; continue; }
        v = match_kv(a, "bs=");
        if (v != 0) { bs = parse_num(v); continue; }
        v = match_kv(a, "count=");
        if (v != 0) { count = parse_num(v); has_count = 1; continue; }
        v = match_kv(a, "skip=");
        if (v != 0) { skip = parse_num(v); continue; }
        v = match_kv(a, "seek=");
        if (v != 0) { seek = parse_num(v); continue; }

        if (ifname == 0)      ifname = a;   /* 位置: 1個目 */
        else if (ofname == 0) ofname = a;   /* 位置: 2個目 */
    }

    if (ifname == 0 || ofname == 0) {
        puts("usage: dd if=IN of=OUT [bs=N] [count=N] [skip=N] [seek=N]");
        return 1;
    }
    if (bs == 0)          bad = 1;
    if (bs > DD_BUFMAX)   bad = 1;          /* BADNUM(0xFFFF)もここで落ちる */
    if (count == BADNUM)  bad = 1;
    if (skip  == BADNUM)  bad = 1;
    if (seek  == BADNUM)  bad = 1;
    if (bad) {
        /* どれが壊れたか判るように解釈結果をそのまま出す(bs は 1..DD_BUFMAX)。 */
        prs("dd: bad operand: bs=");
        prnum(bs);
        prs(" count=");
        prnum(count);
        prs(" skip=");
        prnum(skip);
        prs(" seek=");
        prnum(seek);
        putchar('\n');
        return 1;
    }

    ifp = fopen(ifname, "r");
    if (ifp == NULL) {
        prs("dd: cannot open ");
        puts(ifname);
        return 1;
    }
    ofp = fopen(ofname, "w");
    if (ofp == NULL) {
        prs("dd: cannot create ");
        puts(ofname);
        fclose(ifp);
        return 1;
    }

    /* skip= / seek= は bs ブロック単位(本家と同じ)。生デバイスでは
     * 512 の倍数でないと fseek が弾く ── そこで黙って先へ進むと位置が
     * ずれたまま転送してしまうので、エラーにして降りる。 */
    if (skip > 0) {
        off = block_off(skip, bs);
        if (fseek(ifp, off, SEEK_SET) != 0) {
            puts("dd: cannot skip on input");
            fclose(ifp);
            fclose(ofp);
            return 1;
        }
    }
    if (seek > 0) {
        off = block_off(seek, bs);
        if (fseek(ofp, off, SEEK_SET) != 0) {
            puts("dd: cannot seek on output");
            fclose(ifp);
            fclose(ofp);
            return 1;
        }
    }

    for (;;) {
        if (has_count && blocks >= count) break;
        got = fread(ddbuf, 1, bs, ifp);
        if (got == 0) break;              /* EOF / 読めない(符号なし比較の掟) */
        if (fwrite(ddbuf, 1, got, ofp) != got) {
            puts("dd: write error");
            fclose(ifp);
            fclose(ofp);
            return 1;
        }
        blocks++;
        if ((unsigned)got < bs) break;    /* 端数 = 入力終端 */
    }

    fclose(ifp);
    fclose(ofp);

    prnum(blocks);
    puts(" blocks copied");
    return 0;
}
