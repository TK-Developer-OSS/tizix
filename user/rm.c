/* user/rm.c - 外部コマンド rm(VFS 一本化 Step 9)
 *   rm FILE... : drv_tbl[25] kfs_unlink(カーネルで vfs_resolve + f_unlink、
 *             非 FAT = /dev 配下は拒否)。ディレクトリ削除も FatFs 的には
 *             f_unlink(空なら成功)。
 *   #28 以降 sh は絶対化しない ── 相対パスはカーネル入口(kpath)が解決する。
 *   #31: printf(546B)をやめ libtzc の prs/prnum(共有、約 100B)で組む。
 *   #81: 複数ファイルを受ける。以前は argv[0] だけを消していて、`rm a b` の b が
 *     黙って残った(sh が引数をトークンに割る前の「argc は常に 1」の名残)。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    char *path;
    int r;
    int i;
    int rc = 0;

    if (argc <= 0 || argv == 0) {
        puts("rm: usage: rm <file>...");
        return 1;
    }
    for (i = 0; i < argc; i++) {
        path = argv[i];
        if (path == 0 || path[0] == 0) continue;
        r = unlink(path);
        if (r == 0) continue;
        rc = 1;
        prs("rm: ");
        prs(path);
        if (r == FS_DENIED) {
            puts(": permission denied");
        } else {
            prs(": error ");
            prnum((unsigned)r);
            putchar(10);
        }
    }
    return rc;
}
