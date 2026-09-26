#include <stdio.h>
#include <socket.h>

/* ソケット対応の疎通テスト。
   _socket は crt0.s のダミー実装 (I/O ポート1番の UART に直結) を使う。
   socket() で fd を取得し、send() で UART へ書き出す。 */
int main(int argc, char *argv[]) {
    int fd;
    fd = socket(2, 1, 0);
    send(fd, "Socket OK\n", 10);
    return 0;
}

// EXPECT: Socket OK
// RUN: skip-x86 socket/send は libc の実ネット関数（z80 ダミー専用）
