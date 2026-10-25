/* arch/x86-i386-unreal/user/reboot.c -- 再起動(syscall 62)。カーネルが開いているファイルの目録とディスクの窓を書き出してから落とす。
 *   reboot      BIOS の POST を通らない再起動(INT 19h で起動ディスクを読み直す)。CMOS の電池切れで F1 待ちになる機械でも止まらない
 *   reboot -h   マザーボードごとのリセット(8042 のリセット線、効かなければトリプルフォールト)。POST を通る
 *   実機ハーネス(T105)をシリアルから回すためのもの。 */
#include "tzstdio.h"

int main(int argc, char **argv)
{
    unsigned hard = argc > 0 && argv[0][0] == '-' && argv[0][1] == 'h';   /* tizix の argv[0] は最初の引数 */
    puts(hard ? "rebooting (board reset)..." : "rebooting...");
    _syscall5_asm(62, hard, 0, 0, 0, 0);
    puts("reboot: failed");                  /* ここへは戻らないはず */
    return 1;
}
