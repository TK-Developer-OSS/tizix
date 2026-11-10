/* cmds.c - builtin command table and the small builtins.
 * A command name is just a key in this table (no paths). */
#include <string.h>
#include "mcu.h"
#include "tzsh_core.h"

struct cmd {
    const char *name;
    int (*fn)(int, char **);
    const char *help;
};

static int cmd_help(int argc, char **argv);
static int cmd_ls(int argc, char **argv);
static int cmd_cat(int argc, char **argv);
static int find_script(const char *name, struct tz_script *sc);
static int cmd_echo(int argc, char **argv);
static int cmd_sleep(int argc, char **argv);
static int cmd_free(int argc, char **argv);

static const char n_help[]  MPROGMEM = "help";
static const char n_ls[]    MPROGMEM = "ls";
static const char n_echo[]  MPROGMEM = "echo";
static const char n_sleep[] MPROGMEM = "sleep";
static const char n_free[]  MPROGMEM = "free";
static const char n_cat[]   MPROGMEM = "cat";
static const char n_tzsh[]  MPROGMEM = "tzsh";

static const char h_help[]  MPROGMEM = "list the commands";
static const char h_ls[]    MPROGMEM = "list everything below a path, default /: ls [path]";
static const char h_echo[]  MPROGMEM = "print the arguments";
static const char h_sleep[] MPROGMEM = "sleep SECONDS (Ctrl-C stops)";
static const char h_free[]  MPROGMEM = "show unused RAM";
static const char h_cat[]   MPROGMEM = "print a device value or a script: cat /dev/... | cat /NAME.sh";
static const char h_tzsh[]  MPROGMEM = "run a script baked into flash: tzsh NAME";

static const struct cmd cmds[] MPROGMEM = {
    { n_help,  cmd_help,  h_help  },
    { n_ls,    cmd_ls,    h_ls    },
    { n_echo,  cmd_echo,  h_echo  },
    { n_sleep, cmd_sleep, h_sleep },
    { n_free,  cmd_free,  h_free  },
    { n_cat,   cmd_cat,   h_cat   },
    { n_tzsh,  cmd_tzsh,  h_tzsh  },
};
#define NCMDS (sizeof(cmds) / sizeof(cmds[0]))

int cmd_run(int argc, char **argv)
{
    struct cmd c;
    uint8_t i;

    for (i = 0; i < NCMDS; i++) {
        mmemcpy_P(&c, &cmds[i], sizeof(c));
        if (mstrcmp_P(argv[0], c.name) == 0)
            return c.fn(argc, argv);
    }
    msink_end();                            /* the message goes to the console, not into a redirect */
    mputs_P(P("sh: "));
    mputs(argv[0]);
    mputs_P(P(": not found\n"));
    return 127;
}

int cmd_name(uint8_t i, char *buf)
{
    struct cmd c;

    if (i >= NCMDS)
        return 0;
    mmemcpy_P(&c, &cmds[i], sizeof(c));
    mstrcpy_P(buf, c.name);
    return 1;
}

int script_name(uint8_t i, char *buf)
{
    struct tz_script sc;
    uint8_t k;

    for (k = 0; k <= i; k++) {
        mmemcpy_P(&sc, &tz_scripts[k], sizeof(sc));
        if (!sc.name)
            return 0;
    }
    mstrcpy_P(buf, sc.name);
    return 1;
}

/* run a command and send what it prints to a /dev node ("cmd > /dev/..."), as the shell and tzsh both need */
int cmd_run_to(int argc, char **argv, const char *path)
{
    char out[12];
    int i, st;

    msink_begin(out, sizeof(out));
    st = cmd_run(argc, argv);
    msink_end();
    for (i = (int)strlen(out); i > 0 && (out[i - 1] == '\n' || out[i - 1] == ' '); i--)
        out[i - 1] = 0;
    if (st != 0) {
        mputs(out);                         /* the command failed: show its message instead */
        return st;
    }
    return dev_write(path, out);
}

static int cmd_help(int argc, char **argv)
{
    struct cmd c;
    uint8_t i, n;
    (void)argc; (void)argv;

    for (i = 0; i < NCMDS; i++) {
        mmemcpy_P(&c, &cmds[i], sizeof(c));
        mputs_P(c.name);
        for (n = (uint8_t)strlen_P(c.name); n < 8; n++)
            mputc(' ');
        mputs_P(c.help);
        mputc('\n');
    }
    return 0;
}

/* ls and cat only know the /dev tree (see dev.c); there is no file system. ls is recursive. */
static int cmd_ls(int argc, char **argv)
{
    char root[] = "/";                      /* on the stack: no permanent RAM */

    return dev_ls(argc > 1 ? argv[1] : root);
}

static int cmd_cat(int argc, char **argv)
{
    struct tz_script sc;

    if (argc != 2) {
        mputs_P(P("cat: usage: cat /dev/... | cat /SCRIPT.sh\n"));
        return 1;
    }
    if (find_script(argv[1], &sc)) {
        mputs_P(sc.text);
        return 0;
    }
    return dev_cat(argv[1]);
}

static int cmd_echo(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (i > 1)
            mputc(' ');
        mputs(argv[i]);
    }
    mputc('\n');
    return 0;
}

static int cmd_sleep(int argc, char **argv)
{
    unsigned s;
    uint32_t t0;

    if (argc != 2 || mparse_uint(argv[1], &s) != 0) {
        mputs_P(P("sleep: usage: sleep SECONDS\n"));
        return 1;
    }
    t0 = hal_ms();
    while ((uint32_t)(hal_ms() - t0) < (uint32_t)s * 1000UL)
        if (hal_poll() == 0x03)
            return 130;
    return 0;
}

static int cmd_free(int argc, char **argv)
{
    (void)argc; (void)argv;
    mputu(hal_free());
    mputs_P(P(" bytes free\n"));
    return 0;
}

/* find a baked-in script by the name ls shows (/blink.sh) or by its bare name; 0 when there is none */
static int find_script(const char *name, struct tz_script *sc)
{
    uint8_t i;

    if (*name == '/')
        name++;
    for (i = 0; ; i++) {
        mmemcpy_P(sc, &tz_scripts[i], sizeof(*sc));
        if (!sc->name)
            return 0;
        if (mstrcmp_P(name, sc->name) == 0)
            return 1;
    }
}

int cmd_tzsh(int argc, char **argv)
{
    struct tz_script sc;
    uint8_t i;

    if (argc == 2 && find_script(argv[1], &sc))
        return tzsh_run(sc.text);
    mputs_P(P("tzsh: usage: tzsh NAME, one of:"));
    for (i = 0; ; i++) {
        mmemcpy_P(&sc, &tz_scripts[i], sizeof(sc));
        if (!sc.name)
            break;
        mputc(' ');
        mputs_P(sc.name);
    }
    mputc('\n');
    return 1;
}
