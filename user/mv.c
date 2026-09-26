/* user/mv.c - 外部コマンド mv(VFS 一本化 Step 9)
 *   mv SRC DST : drv_tbl[26] kfs_rename(カーネルで src/dst 両方 vfs_resolve +
 *                f_rename、いずれか非 FAT なら拒否)。
 *   sh がトークン化し argv[0]=SRC / argv[1]=DST を渡す(argv[] ABI)。
 *   #28 以降 sh は絶対化しない ── 相対パスはカーネル入口(kpath)が解決する。
 *   掟: argc は符号付き int なので unsigned へ写してから比較する。
 *   #31: printf(546B)をやめ libtzc の prs/prnum(共有)で組む。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    char *src;
    char *dst;
    int r;

    if ((unsigned)argc < 2u || argv[0] == 0 || argv[1] == 0 ||
        argv[0][0] == '\0' || argv[1][0] == '\0') {
        puts("mv: usage: mv <src> <dst>");
        return 1;
    }
    src = argv[0];
    dst = argv[1];

    r = rename(src, dst);
    if (r == 0)
        return 0;
    prs("mv: ");
    prs(src);
    prs(" -> ");
    prs(dst);
    if (r == FS_DENIED) {
        puts(": permission denied");
    } else {
        prs(": error ");
        prnum((unsigned)r);
        putchar(10);
    }
    return 1;
}
