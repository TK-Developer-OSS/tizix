/* user/df.c - 外部コマンド df(#56。旧ビルトインの外部化)
 *
 *   df : FAT ボリュームの総容量と空き容量を KB で表示する。
 *        "total NNNKB free NNNKB"
 *
 *   容量はカーネルの kfs_df()(drv_tbl[47])から受け取る。外部コマンドは
 *   FatFs を直に叩かない([[commands-access-via-syscall]])── f_getfree は
 *   カーネルの中で呼び、ここは表示だけ。
 *
 *   掟(tzcc): printf 不使用(prs / prnuml)。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    unsigned long df_tot;
    unsigned long df_fre;

    (void)argc;
    (void)argv;
    df_tot = kfs_df(0);
    if (df_tot == 0xFFFFFFFFUL) {
        puts("df: no volume");
        return 1;
    }
    df_fre = kfs_df(1);
    prs("total ");
    prnuml(df_tot);
    prs("KB free ");
    prnuml(df_fre);
    puts("KB");
    return 0;
}
