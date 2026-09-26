/* user/cp.c - ファイルコピー: cp <src> <dst>
 *
 *   引数機構(crt0cmd.s / kexec.c): sh がトークン化し argv[0]=src / argv[1]=dst
 *   を渡す(argv[] ABI)。#28 以降 sh は絶対化しない ── 相対パスはカーネル入口
 *   (kpath)が cwd 起点で解決する。
 *
 *   dst の扱い(VFS 一本化 Step 10):
 *     ・dst が既存ディレクトリ(opendir が開ける)なら dst/basename(src) へ。
 *     ・dst == "." もその一般則で通る(opendir(".") はカレントを開き、
 *       "./basename" もカーネルが解決する)。
 *     ・それ以外は dst をそのままファイル名として扱う(従来どおり)。
 *
 *   #31: **逐次コピーに変更**(旧: 512B の RAM バッファへ全読み → 全書き)。
 *     旧版が「同時に 2 ファイルを開かない」設計だったのは FF_FS_TINY=1 の
 *     共有セクタ窓を壊す懸念から。しかし dd が in/out を同時に開いた
 *     fgetc/fputc の逐次コピーで動作しており(#26 以降 実機検証済み)、
 *     現行 FatFs では窓の再ロードが噛むだけで壊れないことが分かっている。
 *     逐次化で得たもの:
 *       ・**512B 上限が消えた**(任意サイズをコピーできる)
 *       ・buf[512] が _DATA から消え、cp.bin が 1 ブロックに収まったうえで
 *         実効スタック(= 3776 - サイズ)が確保できる。#31 で PIC グルーを
 *         削った結果 cp が 4190→3585B = 2 ブロックから 1 ブロックへ落ち、
 *         スタックが 191B しか残らず FatFs 呼び出しがイメージ末尾の文字列
 *         リテラルを踏んで printf が無言になっていた([[kexec-block-headroom]])。
 *
 *   掟: unsigned 統一(size_t)/ 除算乗算(/ % *)不使用 /
 *       条件式中の代入を書かない(tzcc が黙って誤コンパイルする)。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    char *p;
    char *src;
    char *dst;
    char *base;
    char dstbuf[48];
    FILE *ifp;
    FILE *ofp;
    unsigned total;
    int c;
    unsigned char i;

    if ((unsigned)argc < 2u || argv[0] == 0 || argv[1] == 0) {
        puts("cp: usage: cp <src> <dst>");
        return 1;
    }
    src = argv[0];
    dst = argv[1];

    if (src[0] == '\0' || dst[0] == '\0') {
        puts("cp: usage: cp <src> <dst>");
        return 1;
    }

    /* basename(src) = 最後の '/' の次から */
    base = src;
    for (p = src; *p; p++)
        if (*p == '/') base = p + 1;
    if (base[0] == '\0') {              /* src が "/" 等で終わる = ディレクトリ指定 */
        prs("cp: ");
        prs(src);
        puts(": not a regular file");
        return 1;
    }

    /* dst が既存ディレクトリなら dst/basename(src) を実 dst にする */
    if (opendir(dst) == 0) {
        unsigned char k = 0;
        closedir();
        for (i = 0; dst[i] && k < 40; i++)
            dstbuf[k++] = dst[i];
        if (k == 0 || dstbuf[k - 1] != '/')
            dstbuf[k++] = '/';
        for (i = 0; base[i] && k < 47; i++)
            dstbuf[k++] = base[i];
        dstbuf[k] = '\0';
        dst = dstbuf;
    }

    ifp = fopen(src, "r");
    if (ifp == 0) {
        prs("cp: cannot open ");
        puts(src);
        return 1;
    }
    ofp = fopen(dst, "w");              /* "w" = CREATE_ALWAYS(切詰め作成) */
    if (ofp == 0) {
        fclose(ifp);
        prs("cp: cannot create ");
        puts(dst);
        return 1;
    }

    total = 0;
    for (;;) {
        c = fgetc(ifp);
        if (c == EOF) break;
        fputc(c, ofp);
        total++;
    }

    fclose(ifp);
    fclose(ofp);

    prs("cp: ");
    prs(src);
    prs(" -> ");
    prs(dst);
    prs(" (");
    prnum(total);
    puts(" bytes)");
    return 0;
}
