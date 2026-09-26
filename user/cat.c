/* user/cat.c - 外部コマンド cat(VFS 一本化 Step 8)
 *   cat FILE : fopen/fread/fclose(cp.c と同じ実績経路)
 *   cat      : stdin を EOF / ^D まで。`cat < FILE` やパイプ後段は sh /
 *              カーネル側の in_on / pipe_is_reader ルーティングで getchar が透過。
 *   #28 以降 sh は絶対化しない ── 相対パスはカーネル入口(kpath)が解決する。
 *   掟: unsigned 統一(size_t)/ 除算乗算不使用 / fread は size=1。
 *   #31: printf(546B)をやめ libtzc の prs(共有)で組む。
 *
 *   旧・既知の制限(#33 で解消): 端末直結の対話 `cat`(引数・< ・パイプ
 *   いずれも無し)は前景プロセスなので、sh の待ちループが Ctrl+C 検出で回す
 *   con_break() と 1 バイト単位で標準入力を取り合っていた。
 *   いまは con_break が Ctrl+C 以外を捨てずカーネルの戻しバッファへ退避し、
 *   kgetchar がそれを物理層より先に返すので取り合いは起きない
 *   (src/io.c の con_ung。詳細は task.md #33)。
 */
#include "stdio.h"

#define CAT_BUFSZ 128

int main(int argc, char **argv)
{
    char *path;
    FILE *fp;
    unsigned char buf[CAT_BUFSZ];
    size_t n, i;
    int c;

    (void)argc;

    path = (argv && argv[0] && argv[0][0]) ? argv[0] : 0;

    if (path == 0) {                       /* stdin(< FILE / パイプ後段 / 端末)*/
        for (;;) {
            c = getchar();
            if (c == EOF) break;           /* < FILE / パイプの終端 */
            if (c == 0x04) break;          /* ^D(端末直結時)*/
            putchar(c);
        }
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == 0) {
        prs("cat: ");
        prs(path);
        puts(": cannot open");
        return 1;
    }
    for (;;) {
        n = fread(buf, 1, (size_t)CAT_BUFSZ, fp);
        if (n == 0) break;                 /* EOF */
        for (i = 0; i < n; i++)
            putchar(buf[i]);
    }
    fclose(fp);
    return 0;
}
