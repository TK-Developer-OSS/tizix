#include <stdio.h>

/* ファイル I/O 書き込み -> 読み戻し 往復テスト（ローカル配列 + fgets 版）。
 *
 * io.txt に "File IO Test\n" を fputs で書き、fclose 後 fopen("r") で開き直し、
 * ローカル配列 buf[32] へ fgets で 1 行読み込んで puts で表示する。
 * ローカル配列がアドレスとして正しく fgets へ渡ること、書いた内容を
 * 読み戻せることの確認を兼ねる。
 * 期待コンソール出力: File IO Test + 改行
 */
int main(int argc, char *argv[]) {
    char buf[32];

    char *fw = fopen("io.txt", "w");
    fputs("File IO Test\n", fw);
    fclose(fw);

    char *fr = fopen("io.txt", "r");
    fgets(buf, 32, fr);
    puts(buf);
    fclose(fr);

    return 0;
}

// RUN: skip file-io(BDOS)
