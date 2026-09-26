/* user/rmdir.c - 外部コマンド rmdir(VFS 一本化 Step 9)
 *   rmdir DIR : drv_tbl[25] kfs_unlink(カーネルで vfs_resolve + f_unlink、
 *               非 FAT = /dev 配下は拒否)。ディレクトリ削除も FatFs 的には
 *               f_unlink(空なら成功)。
 *   #28 以降 sh は絶対化しない ── 相対パスはカーネル入口(kpath)が解決する。
 *   argc は常に 1 / argv[0] にパス全体。
 *   #31: printf(546B)をやめ libtzc の prs/prnum(共有、約 100B)で組む。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    char *path;
    int r;

    (void)argc;
    path = (argv && argv[0] && argv[0][0]) ? argv[0] : 0;
    if (path == 0) {
        puts("rmdir: usage: rmdir <dir>");
        return 1;
    }
    r = unlink(path);
    if (r == 0)
        return 0;
    prs("rmdir: ");
    prs(path);
    if (r == FS_DENIED) {
        puts(": permission denied");
    } else {
        prs(": error ");
        prnum((unsigned)r);
        putchar(10);
    }
    return 1;
}
