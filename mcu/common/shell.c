/* shell.c - line editor, tokenizer, prompt.
 * Not bash yet: no variables, no control flow. Quotes group words; '#' starts a comment line. */
#include <string.h>
#include "mcu.h"

#define LINE_MAX 64
#define ARGV_MAX 8

static char line[LINE_MAX];
static char gt[] = ">";                         /* the redirect token (compared by address) */

/* Returns the length of the line; Ctrl-C gives an empty line. */
static uint8_t readline(void)
{
    uint8_t n = 0, esc = 0;
    int c, last = 0;

    for (;;) {
        c = hal_getc();
        if (esc) {                               /* swallow ESC [ x (arrow keys) */
            if (esc == 1 && c == '[')
                esc = 2;
            else if (esc == 2 && c >= 0x40 && c <= 0x7e)
                esc = 0;
            else if (esc == 1)
                esc = 0;
            continue;
        }
        if (c == '\n' && last == '\r') {         /* CR LF from the terminal */
            last = 0;
            continue;
        }
        last = c;
        if (c == '\r' || c == '\n') {
            mputc('\n');
            line[n] = 0;
            return n;
        }
        if (c == 0x1b) {
            esc = 1;
        } else if (c == 0x03) {                  /* Ctrl-C */
            mputs_P(P("^C\n"));
            line[0] = 0;
            return 0;
        } else if (c == 0x08 || c == 0x7f) {     /* backspace */
            if (n) {
                n--;
                mputs_P(P("\b \b"));
            }
        } else if (c >= 0x20 && c < 0x7f && n < LINE_MAX - 1) {
            line[n++] = (char)c;
            mputc((char)c);
        }
    }
}

/* Splits s in place. Returns argc, or <0 on error. */
static int tokenize(char *s, char **argv)
{
    int argc = 0;
    char q;

    for (;;) {
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s)
            break;
        if (argc == ARGV_MAX)
            return -1;
        if (*s == '>') {
            if (s[1] == '>')
                return -3;
            argv[argc++] = gt;
            s++;
            continue;
        }
        if (*s == '"' || *s == '\'') {
            q = *s++;
            argv[argc++] = s;
            while (*s && *s != q)
                s++;
            if (!*s)
                return -2;
            *s++ = 0;
        } else {
            argv[argc++] = s;
            while (*s && *s != ' ' && *s != '\t' && *s != '>')
                s++;
            if (*s)
                *s++ = 0;
        }
    }
    return argc;
}

/* run a command; "cmd args > /dev/..." sends what the command prints to that node */
static void run_line(int argc, char **argv)
{
    int i;

    for (i = 0; i < argc; i++)
        if (argv[i] == gt) {
            if (i != argc - 2 || i == 0) {
                mputs_P(P("sh: syntax error near >\n"));
                return;
            }
            cmd_run_to(i, argv, argv[i + 1]);
            return;
        }
    cmd_run(argc, argv);
}

void shell_run(void)
{
    char *argv[ARGV_MAX];
    int argc;

    mputs_P(P("\ntizix-mcu "));
    mputs_P(hal_banner);
    mputs_P(P("\n"));
    for (;;) {
        mputs_P(P("[/]# "));
        if (readline() == 0)
            continue;
        if (line[0] == '#')
            continue;
        argc = tokenize(line, argv);
        if (argc == -1)
            mputs_P(P("sh: too many words\n"));
        else if (argc == -2)
            mputs_P(P("sh: unterminated quote\n"));
        else if (argc == -3)
            mputs_P(P("sh: >> is not supported\n"));
        else if (argc > 0)
            run_line(argc, argv);
    }
}
