/* argv_init.c - crt0cmd.s から呼ぶ argv[] トークナイザ。
 *
 *   旧 crt0cmd.s は argv[0] に「引数文字列まるごと」(スペースを含む)を
 *   1個だけ渡していた(argc は常に 1)。共通 user/*.c(echo.c/cp.c/grep.c
 *   等の tzcc 向けソース)は sh がトークン化した argv[0..argc-1] を前提に
 *   書かれているため、そのままでは動かない(cp.c だけ自前でスペース分割
 *   していた ── 他のコマンドは分割していない)。
 *
 *   ここで pseg:0xFF00 の生文字列を空白区切りでその場 NUL 化して
 *   g_argv[] に積み、crt0cmd.s の main 呼び出しへ渡す。
 */
#define MAXARGS 8

char *g_argv[MAXARGS];

int build_argv(char *raw)
{
        int argc = 0;
        char *p = raw;

        while (*p == ' ')
                p++;
        while (*p && argc < MAXARGS - 1) {
                g_argv[argc++] = p;
                while (*p && *p != ' ')
                        p++;
                if (*p) {
                        *p++ = 0;
                        while (*p == ' ')
                                p++;
                }
        }
        g_argv[argc] = 0;
        return argc;
}
