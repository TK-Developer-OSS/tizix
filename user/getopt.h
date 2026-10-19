/* user/getopt.h - tizix 外部コマンド用 再入可能 getopt
 *
 *   この環境のコマンドは書込み可能な file-scope static を持てない
 *   (iy_reg 未変換 + crt0cmd が gsinit を走らせない)。よって getopt の
 *   状態は呼び出し側がスタックに確保する getopt_t に置き、全アクセスを
 *   ポインタ経由にする(DEVELOP.md「グローバルはポインタ経由で触る」)。
 *
 *   ★この環境の argv[] には progname が無い(sh はコマンド名を argv に入れず、
 *     argv[0] が最初の引数)。よって optind は 0 起点。エラーメッセージ用の
 *     プログラム名は getopt_r に明示で渡す。
 *
 *   使い方:
 *     getopt_t go;
 *     int c;
 *     opt_init(&go);
 *     while ((c = getopt_r(&go, "ls", argc, argv, "lh")) != -1) {
 *         switch (c) {
 *         case 'l': ...; break;
 *         case 'h': ...; break;
 *         default:  usage(); return 1;
 *         }
 *     }
 *     first = go.optind;      // 非オプション引数の開始インデックス(0 起点)
 *
 *   optstring: "x" = フラグ / "x:" = 引数を取るオプション。
 *   先頭 ':' を置くと、引数不足時に '?' でなく ':' を返す。
 *   掟: 比較は unsigned。argc は呼び出し側 int のまま渡してよい(内部で cast)。
 */
#ifndef GETOPT_H
#define GETOPT_H

typedef struct {
    unsigned optind;    /* 次に処理する argv インデックス(opt_init で 0) */
    unsigned optpos;    /* 内部: 連結オプション文字列内の走査位置       */
    int      optopt;    /* エラーを起こしたオプション文字               */
    int      opterr;    /* 非0 でエラーメッセージを表示(opt_init で 1)  */
    char    *optarg;    /* 引数付きオプションの引数。無ければ 0          */
} getopt_t;

void opt_init(getopt_t *g);
int  getopt_r(getopt_t *g, const char *prog, int argc, char **argv,
              const char *optstring);

#endif /* GETOPT_H */
