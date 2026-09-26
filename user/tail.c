/* user/tail.c - 外部コマンド tail
 *   tail [-n N] [-N] [FILE] : ファイルの末尾 N 行を表示。N の既定は 10(1〜99)。
 *
 *   実装メモ:
 *   - FILE 指定: fseek 巻き戻しは cpmsim FDC で返らないので 2 パス
 *     ((1) '\n' 数える (2) 先頭 total-N 行を読み飛ばして残りを出力)。
 *   - FILE 省略(= パイプ後段 `X | tail`): #27 でカーネルパイプが 4KB ブロック
 *     バッファになった。tail は pipe_tail() で「writer 完了まで待って 4KB 窓を
 *     後方スキャンし末尾 N 行」を得る。スプール不要 → FatFs 非接触・浅い
 *     スタックで済み `cat|tail` が動く。総量が 4KB を超えた場合は先頭 4KB
 *     分だけの窓になる(sh が "out of memory")。
 *   - `tail < file` は sh が `tail file` に書き換える(このコマンドには来ない)。
 *   掟: 除算・剰余なし。continue 前提の while。
 *   #31: tail_file の仮引数(path / want)を廃止しファイルスコープを直接触る。
 *        tzcc はローカルも静的領域なので仮引数は損なうえ、caller と同名だと
 *        同一記憶域を共有してしまう(tzcc 側にこの検出を入れた)。
 */
#include "stdio.h"

/* crt0_tizix.s トランポリン(drv_tbl[40])。writer 完了まで待って 4KB 窓を
 *   後方スキャンし末尾 want 行を出力。戻り 1=処理した / 0=パイプ後段ではない。
 *   後方スキャン本体はカーネル側(iy_reg 税なし)。tail.c は 1 ブロックを保つ。 */
unsigned char pipe_tail(unsigned want);

static char *path;
static int   want;

/* 先頭 1〜2 桁の 10 進を読む(ループなし)。 */
static int num2(char *s)
{
    int n = 0;
    if (s[0] >= '0' && s[0] <= '9') {
        n = s[0] - '0';
        if (s[1] >= '0' && s[1] <= '9')
            n = n * 10 + (s[1] - '0');
    }
    return n;
}

/* path の末尾 want 行を出力。戻り 0=OK / 1=open 失敗。 */
static int tail_file(void)
{
    FILE *fp;
    int c;
    int total = 0;
    int skip = 0;
    int ln = 0;

    fp = fopen(path, "r");
    if (fp == NULL) return 1;
    for (;;) {
        c = fgetc(fp);
        if (c == EOF) break;
        if (c == '\n') total++;
    }
    fclose(fp);

    if (total > want) skip = total - want;

    fp = fopen(path, "r");
    if (fp == NULL) return 1;
    for (;;) {
        c = fgetc(fp);
        if (c == EOF) break;
        if (ln >= skip) putchar(c);
        if (c == '\n') ln++;
    }
    fclose(fp);
    return 0;
}

int main(int argc, char **argv)
{
    int ai = 0;

    path = 0;
    want = 10;

    if (ai < argc && argv[ai][0] == '-') {
        char *o = argv[ai];
        if (o[1] == 'n') {                 /* -n N */
            ai++;
            if (ai < argc) want = num2(argv[ai]);
            ai++;
        } else {                          /* -N */
            want = num2(&o[1]);
            ai++;
        }
    }
    if (ai < argc)
        path = argv[ai];

    if (path == 0) {
        if (want < 1) want = 1;
        if (pipe_tail((unsigned)want) == 0) {
            puts("tail: needs FILE or pipe");
            return 1;
        }
        return 0;
    }

    if (tail_file() != 0) {
        puts("tail: cannot open");
        return 1;
    }
    return 0;
}
