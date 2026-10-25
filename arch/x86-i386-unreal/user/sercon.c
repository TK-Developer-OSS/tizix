/* arch/x86-i386-unreal/user/sercon.c -- コンソールが COM1 を使うかどうか(syscall 64)。
 *   sercon off   COM1 をコンソールから外す(画面とキーボードは残る)。COM が 1 つの機械(T105)で COM1 をモデムに回すとき。
 *                   /etc/rc に書けば、起動したら COM1 はモデム用になる(シリアルからの操作はできなくなる)
 *   sercon on    COM1 をコンソールに戻す
 *   sercon       今どちらかを表示
 *   コンソールが COM1 を使っている間は、カーネルがモデム(syscall 56)の COM1 を拒否する
 *   (モデムの応答がシェルへの入力として流れ込み、押し付け合いになるため)。 */
#include "tzstdio.h"

int main(int argc, char **argv)
{
    unsigned r;
    if (argc > 0 && argv[0][0] == 'o' && argv[0][1] == 'f')
        r = _syscall5_asm(64, 0, 0, 0, 0, 0);
    else if (argc > 0 && argv[0][0] == 'o' && argv[0][1] == 'n')
        r = _syscall5_asm(64, 1, 0, 0, 0, 0);
    else if (argc > 0) {
        puts("usage: sercon [on|off]");
        return 1;
    } else
        r = _syscall5_asm(64, 2, 0, 0, 0, 0);
    puts(r == 1 ? "serial console: COM1" : r == 3 ? "serial console: output only (telnetd call in progress)"
                                       : "serial console: off (COM1 is free for a modem)");
    return 0;
}
