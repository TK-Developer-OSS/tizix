/* user/touch.c - 外部コマンド touch(VFS 一本化 Step 10)
 *   touch FILE : 無ければ空ファイルを作る。既にあれば何もしない
 *                (mtime 更新は FatFs f_utime が要るので当面やらない)。
 *   #28 以降 sh は絶対化しない ── 相対パスはカーネル入口(kpath)が解決する。
 *   argc は常に 1 / argv[0] にパス全体(crt0cmd 規約)。
 *   ※ fopen(path,"w") は CREATE_ALWAYS で既存を切り詰めるため、まず "r" で
 *     存在確認してから、無いときだけ "w" で作る。
 *   /dev 配下は drv_open が非 FAT を弾く(= cannot create)。
 *   #31: printf(546B)をやめ libtzc の prs(共有)で組む。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    char *path;
    FILE *fp;

    (void)argc;
    path = (argv && argv[0] && argv[0][0]) ? argv[0] : 0;
    if (path == 0) {
        puts("touch: usage: touch <file>");
        return 1;
    }

    fp = fopen(path, "r");
    if (fp != 0) {                      /* 既存 → 何もしない */
        fclose(fp);
        return 0;
    }

    fp = fopen(path, "w");              /* 新規作成(空)*/
    if (fp == 0) {
        prs("touch: ");
        prs(path);
        puts(": cannot create");
        return 1;
    }
    fclose(fp);
    return 0;
}
