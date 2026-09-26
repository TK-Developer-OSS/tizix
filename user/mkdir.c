/* user/mkdir.c - 外部コマンド mkdir(VFS 一本化 Step 9)
 *   mkdir DIR : drv_tbl[24] kfs_mkdir(カーネルで vfs_resolve + f_mkdir、
 *               非 FAT = /dev 配下は拒否)。
 *   #28 以降 sh は絶対化しない ── 相対パスはカーネル入口(kpath)が解決する。
 *   argc は常に 1 / argv[0] にパス全体(crt0cmd 規約)。
 *   掟: 符号付き比較を避け、戻り値は == で判別(FS_DENIED=0xFF 番兵)。
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
        puts("mkdir: usage: mkdir <dir>");
        return 1;
    }
    r = mkdir(path);
    if (r == 0)
        return 0;
    prs("mkdir: ");
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
