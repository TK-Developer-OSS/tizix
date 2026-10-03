/* user/ls.c - 外部コマンド ls
 *   ls [-l] [-h] [path]
 *     -l : 1 行 1 エントリ、サイズ表示(ディレクトリは <DIR>)
 *     -h : -l と併用でサイズを K 単位表示
 *   -lh / -hl の連結も可。sh がトークン化した argv[] を渡してくる
 *   (progname 無し、argv[0] が最初の引数)。#28 以降 sh は絶対化しない ──
 *   相対パスはカーネル入口(kpath)が cwd 起点で解決する。無引数 ls は "."
 *   を既定にしてカレントを列挙する。
 *
 *   サイズは readdir_size()(drv_tbl[38])から得る。反復中に fopen すると
 *   FF_FS_TINY=1 の共有セクタ窓が壊れる(user/cp.c の注記参照)ため。
 *
 *   #26: tzcc ビルド(iy_reg 卒業)。引数解析は getopt を使わず素の走査。
 *   掟: 比較は unsigned。サイズは全コマンド 64KB 未満なので 16bit に畳む。
 *   #31: printf(546B)をやめ libtzc の prnum/prs(共有)で組む。
 */
#include "stdio.h"
#ifndef TZ_NAME_MAX
#define TZ_NAME_MAX 16   /* z80: 8.3(gcc 側は stdio.h が長いファイル名の 65 にする。#114) */
#endif

/* -l のサイズ欄を出力(末尾に空白 2 個)。t==2(ディレクトリ)は <DIR>。
 * #31: 仮引数を廃止し、ファイルスコープの t / hflag を直接見る。 */
static int t;
static int hflag;
static void put_size(void)
{
    unsigned long sz;
    unsigned b;

    if (t == 2) { prs(" <DIR>  "); return; }

    sz = readdir_size();
    b  = (unsigned)sz;                      /* 64KB 未満前提 */
    if (!hflag) { prnum(b); prs("  "); return; }
    if (b < 1024u) { prnum(b); prs("B  "); return; }
    prnum((unsigned)((b + 512u) >> 10));    /* 四捨五入 KB */
    prs("K  ");
}

int main(int argc, char **argv)
{
    char *path = 0;
    char name[TZ_NAME_MAX];
    char *q;

    int  lflag = 0;
    int  i, j;

    for (i = 0; i < argc; i++) {
        char *a = argv[i];
        if (a == 0 || a[0] == 0) continue;
        if (a[0] == '-' && a[1] != 0) {
            for (j = 1; a[j]; j++) {
                if (a[j] == 'l')      lflag = 1;
                else if (a[j] == 'h') hflag = 1;
                else { puts("usage: ls [-l] [-h] [path]"); return 1; }
            }
            continue;
        }
        if (path == 0) path = a;               /* 最初の非オプション = path */
    }

    if (path == 0) path = ".";           /* 無引数 = カレント(kpath が解決) */

    if (opendir(path) != 0) {
        /* ディレクトリでなければファイルとして開き、UNIX の ls FILE と同じく
         * その名前(-l ならサイズも)を出す。サイズは読み進めて数える
         * (巻き戻しの fseek は使わない掟)。以前は "cannot open" になっていた。 */
        FILE *lf;
        unsigned lsz;
        int lgot;

        lf = fopen(path, "r");
        if (lf == NULL) {
            prs("ls: ");
            prs(path);
            puts(": cannot open");
            return 1;
        }
        if (lflag) {
            lsz = 0;
            for (;;) {
                lgot = fread(name, 1, 16, lf);
                if (lgot <= 0) break;
                lsz = lsz + (unsigned)lgot;
            }
            prnum(lsz);
            prs("  ");
        }
        fclose(lf);
        puts(path);
        return 0;
    }
    for (;;) {
        t = readdir(name);
        if (t == 0)
            break;
        if (lflag)
            put_size();
        for (q = name; *q; q++)
            putchar(*q);
        if (t == 2)
            putchar('/');
        putchar('\n');
    }
    closedir();
    /* ルートの dev/ はカーネルの kdir_read が列挙の末尾で返す(#76)。
     * 以前はここで引数文字列が "/" のときだけ足していたので、`ls ..` や
     * cwd=/ での無引数 ls のようにルートへ別表記で来ると漏れていた。 */
    return 0;
}
