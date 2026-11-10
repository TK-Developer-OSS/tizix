/* tzsh_core.c - tzsh for small targets: a statement-at-a-time interpreter that runs straight off
 * the script text (kept in constant memory), with no syntax tree. C-like control around
 * shell-like commands:
 *
 *     echo 500 > /dev/timer/0/limit;
 *     if (/dev/gpio/13 == "0") { echo 1 > /dev/gpio/13; } else { echo 0 > /dev/gpio/13; }
 *     while (n < 5) { n = n + 1; }
 *     setup { echo 500 > /dev/timer/0/limit; }
 *     intr  { echo "the timer finished"; }
 *     loop  { n = n + 1; }
 *
 * Statements: NAME = VALUE [+|- VALUE];   if (COND) {..} [else {..}]   while (COND) {..}
 *             break;   exit [N];   or a command: WORD... [> /dev/PATH];
 *             setup {..} runs once; intr {..} runs when the host reports an interrupt event;
 *             loop {..} repeats until break or Ctrl-C (it is while (1), so write it last).
 * VALUE is a number, "string", 'string', a variable (bare name) or a /dev path (its current value).
 * COND is VALUE, or VALUE ==, !=, <, <=, >, >= VALUE. Inside command words, $name is a variable.
 * A command ends at ';' or at the end of the line. Comments start with # or //.
 *
 * Values are strings. == and != compare strings; < <= > >= and + - turn the strings into 16 bit
 * numbers with a simple atoi: digits (one leading '-') are the number, an empty string is 0, any
 * other text is 1. A number or result that does not fit in 16 bits is an error that stops the
 * script with status 255.
 *
 * The host supplies what tzsh_host.h declares. The state is static: one interpreter at a time. */
#include "tzsh_host.h"
#include "tzsh_core.h"

#define NVARS    16
#define NAMELEN  8              /* names up to 7 characters */
#define VALLEN   12             /* values up to 11 characters */
#define WBUF     96             /* the words of one command, expanded */
#define MAXARGV  8

static const char *src;
static uint16_t pos;
static uint8_t brk;             /* a break is pending: skip to the end of the loop body */
static uint8_t quit;            /* exit or a fatal error: stop everything */
static uint8_t depth;           /* loops being run */
static uint8_t busy;
static uint8_t in_intr;
static uint16_t intr_pos;       /* where the intr block starts (its '{'); 0 = none */
static int status;

static char vname[NVARS][NAMELEN];
static char vval[NVARS][VALLEN];
static char wbuf[WBUF];
static uint8_t wlen;
static uint8_t argc_;
static char *argv_[MAXARGV];

static void parse_stmt(uint8_t ex);
static void parse_block(uint8_t ex);

static void die(const char *msg, int code)
{
    if (quit)
        return;
    tz_puts_P(TZ_P("tzsh: "));
    tz_puts_P(msg);
    tz_puts_P(TZ_P(" @"));
    tz_putu(pos);
    tz_puts_P(TZ_P("\n"));
    status = code;
    quit = 1;
}

static int pk(void)
{
    return (uint8_t)tz_getb(src + pos);
}

static int pk1(void)
{
    return pk() ? (uint8_t)tz_getb(src + pos + 1) : 0;
}

static uint8_t is_id0(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static uint8_t is_id(int c)
{
    return is_id0(c) || (c >= '0' && c <= '9');
}

static uint8_t is_cmt(void)
{
    return pk() == '#' || (pk() == '/' && pk1() == '/');
}

/* spaces, newlines and comments */
static void skip_sp(void)
{
    int c;

    for (;;) {
        c = pk();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            pos++;
        else if (is_cmt())
            while ((c = pk()) != 0 && c != '\n')
                pos++;
        else
            return;
    }
}

/* ... and the ';' that ends a statement */
static void skip_ws(void)
{
    for (;;) {
        skip_sp();
        if (pk() != ';')
            return;
        pos++;
    }
}

static void expect(int c)
{
    if (pk() != c)
        die(TZ_P("syntax error"), 2);
    else
        pos++;
}

static uint8_t kw_is(const char *k)
{
    uint16_t p = pos;
    char c;

    while ((c = (char)tz_getb(k++)) != 0) {
        if ((char)tz_getb(src + p) != c)
            return 0;
        p++;
    }
    return !is_id((uint8_t)tz_getb(src + p));
}

static uint8_t kw_take(const char *k)
{
    if (!kw_is(k))
        return 0;
    pos += tz_strlen_P(k);
    return 1;
}

/* ---- variables --------------------------------------------------------- */
static char nul;                /* an empty string that costs one byte, not a literal in RAM */

static const char *getvar(const char *n)
{
    uint8_t i;
    for (i = 0; i < NVARS; i++)
        if (vname[i][0] && strcmp(vname[i], n) == 0)
            return vval[i];
    return &nul;
}

static void setvar(const char *n, const char *v)
{
    uint8_t i;

    for (i = 0; i < NVARS; i++)
        if (vname[i][0] && strcmp(vname[i], n) == 0)
            break;
    if (i == NVARS)
        for (i = 0; i < NVARS && vname[i][0]; i++)
            ;
    if (i == NVARS) {
        die(TZ_P("too many variables"), 255);
        return;
    }
    strcpy(vname[i], n);
    strcpy(vval[i], v);
}

/* ---- numbers ----------------------------------------------------------- */
static int16_t to_int(const char *s)
{
    uint16_t v = 0;
    uint8_t neg = 0;
    const char *p;

    if (*s == 0)
        return 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    if (*s == 0)
        return 1;
    for (p = s; *p; p++)
        if (*p < '0' || *p > '9')
            return 1;
    for (; *s; s++) {
        if (v > 3276 || (v = (uint16_t)(v * 10 + (uint16_t)(*s - '0'))) > (neg ? 32768u : 32767u)) {
            die(TZ_P("arithmetic overflow"), 255);
            return 0;
        }
    }
    return neg ? (int16_t)(0u - v) : (int16_t)v;
}

static void put_int(int16_t r, char *out)
{
    char t[6];
    uint8_t i = 0, n = 0;
    uint16_t m = r < 0 ? (uint16_t)(0u - (uint16_t)r) : (uint16_t)r;

    do {
        t[i++] = (char)('0' + m % 10);
        m /= 10;
    } while (m);
    if (r < 0)
        out[n++] = '-';
    while (i)
        out[n++] = t[--i];
    out[n] = 0;
}

/* out = a op b, op is '+' or '-' */
static void arith(const char *a, char op, const char *b, char *out)
{
    int16_t x = to_int(a), y = to_int(b);

    if (quit)
        return;
    if (op == '+') {
        if ((y > 0 && x > 32767 - y) || (y < 0 && x < -32768L - y)) {
            die(TZ_P("arithmetic overflow"), 255);
            return;
        }
        put_int((int16_t)(x + y), out);
    } else {
        if ((y < 0 && x > 32767 + y) || (y > 0 && x < -32768L + y)) {
            die(TZ_P("arithmetic overflow"), 255);
            return;
        }
        put_int((int16_t)(x - y), out);
    }
}

/* ---- values and conditions --------------------------------------------- */
/* One VALUE into out (VALLEN bytes). In skip mode (ex == 0) it is only scanned. */
static void operand(uint8_t ex, char *out)
{
    int c;
    uint8_t n = 0;
    char q, path[24], nm[NAMELEN];

    skip_sp();
    c = pk();
    out[0] = 0;
    if (c == '"' || c == '\'') {
        q = (char)c;
        pos++;
        while ((c = pk()) != 0 && c != q) {
            pos++;
            if (ex) {
                if (n < VALLEN - 1)
                    out[n++] = (char)c;
                else
                    die(TZ_P("value too long"), 255);
            }
        }
        if (c == 0)
            die(TZ_P("unterminated quote"), 2);
        else
            pos++;
        out[n] = 0;
    } else if (c == '/') {                          /* a /dev path: the value it has now */
        while ((c = pk()) == '/' || is_id(c)) {
            pos++;
            if (n < sizeof(path) - 1)
                path[n++] = (char)c;
        }
        path[n] = 0;
        if (ex && !quit && tz_dev_read(path, out, VALLEN) != 0)
            die(TZ_P("no such device"), 255);
    } else if (is_id0(c)) {                         /* a variable */
        while (is_id(c = pk())) {
            pos++;
            if (n < NAMELEN - 1)
                nm[n++] = (char)c;
            else
                die(TZ_P("name too long"), 255);
        }
        nm[n] = 0;
        if (ex && !quit)
            strcpy(out, getvar(nm));
    } else if ((c >= '0' && c <= '9') || c == '-') {    /* a number */
        if (c == '-') {
            pos++;
            if (ex && n < VALLEN - 1)
                out[n++] = '-';
        }
        while (is_id(c = pk())) {
            pos++;
            if (c < '0' || c > '9')
                die(TZ_P("bad number"), 2);
            else if (ex) {
                if (n < VALLEN - 1)
                    out[n++] = (char)c;
                else
                    die(TZ_P("value too long"), 255);
            }
        }
        out[n] = 0;
    } else {
        die(TZ_P("expected a value"), 2);
    }
}

static uint8_t parse_cond(uint8_t ex)
{
    char a[VALLEN], b[VALLEN], op;
    int c;
    int16_t x, y;

    operand(ex, a);
    skip_sp();
    c = pk();
    if (c == '=' && pk1() == '=') {
        op = 'e';
        pos += 2;
    } else if (c == '!' && pk1() == '=') {
        op = 'n';
        pos += 2;
    } else if (c == '<' || c == '>') {
        pos++;
        if (pk() == '=') {
            pos++;
            op = c == '<' ? 'l' : 'g';
        } else {
            op = (char)c;
        }
    } else {
        return ex && !quit && a[0] && !(a[0] == '0' && a[1] == 0);     /* a bare VALUE: not "" and not "0" */
    }
    operand(ex, b);
    if (!ex || quit)
        return 0;
    if (op == 'e')
        return strcmp(a, b) == 0;
    if (op == 'n')
        return strcmp(a, b) != 0;
    x = to_int(a);
    y = to_int(b);
    if (quit)
        return 0;
    return op == '<' ? x < y : op == '>' ? x > y : op == 'l' ? x <= y : x >= y;
}

/* ---- commands ---------------------------------------------------------- */
static void put(uint8_t ex, int c)
{
    if (!ex)
        return;
    if (wlen >= WBUF - 1) {
        die(TZ_P("command too long"), 255);
        return;
    }
    wbuf[wlen++] = (char)c;
}

/* after a '$' in a command word */
static void expand(uint8_t ex)
{
    char nm[NAMELEN];
    uint8_t n = 0;
    const char *v;
    int c = pk();

    if (!is_id0(c)) {
        put(ex, '$');
        return;
    }
    while (is_id(c = pk())) {
        pos++;
        if (n < NAMELEN - 1)
            nm[n++] = (char)c;
        else
            die(TZ_P("name too long"), 255);
    }
    nm[n] = 0;
    if (ex)
        for (v = getvar(nm); *v; v++)
            put(ex, *v);
}

static uint8_t is_wend(int c)
{
    return c == 0 || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';' || c == '}' || c == '>';
}

/* One word of a command. 0: end of the command; 1: a word was added to argv_; 2: a word was
 * consumed but vanished (skip mode, or an unquoted $name that came out empty); 3: the '>' mark. */
static uint8_t read_word(uint8_t ex)
{
    int c;
    uint8_t quoted = 0, start = wlen;

    while (pk() == ' ' || pk() == '\t')
        pos++;
    c = pk();
    if (c == 0 || c == '\n' || c == '\r' || c == ';' || c == '}')
        return 0;
    if (is_cmt()) {
        while ((c = pk()) != 0 && c != '\n')
            pos++;
        return 0;
    }
    if (c == '>') {
        pos++;
        return 3;
    }
    while (!is_wend(c = pk())) {
        pos++;
        if (c == '\'') {
            quoted = 1;
            while ((c = pk()) != 0 && c != '\'') {
                pos++;
                put(ex, c);
            }
            if (c == 0)
                die(TZ_P("unterminated quote"), 2);
            else
                pos++;
        } else if (c == '"') {
            quoted = 1;
            for (;;) {
                c = pk();
                if (c == 0) {
                    die(TZ_P("unterminated quote"), 2);
                    break;
                }
                pos++;
                if (c == '"')
                    break;
                if (c == '$')
                    expand(ex);
                else
                    put(ex, c);
            }
        } else if (c == '$') {
            expand(ex);
        } else {
            put(ex, c);
        }
    }
    if (!ex || quit || (wlen == start && !quoted))
        return 2;
    if (argc_ >= MAXARGV) {
        die(TZ_P("too many words"), 255);
        return 2;
    }
    put(1, 0);
    argv_[argc_++] = wbuf + start;
    return 1;
}

static int run_simple(int argc, char **argv)
{
    int r;

    if (tz_strcmp_P(argv[0], TZ_P("exit")) == 0) {
        r = argc > 1 ? to_int(argv[1]) : status;
        if (!quit) {
            status = r & 0xFF;
            quit = 1;
        }
        return status;
    }
    return tz_run(argc, argv);
}

static void run_cmd(uint8_t ex)
{
    uint8_t rv, redir = 0, ri = 0;

    wlen = 0;
    argc_ = 0;
    while ((rv = read_word(ex)) != 0)
        if (rv == 3) {
            if (redir)
                die(TZ_P("syntax error near >"), 2);
            redir = 1;
            ri = argc_;
        }
    if (!ex || quit)
        return;
    if (redir) {
        if (ri == 0 || argc_ != ri + 1)
            die(TZ_P("syntax error near >"), 2);
        else
            status = tz_run_to(ri, argv_, argv_[ri]);
    } else if (argc_) {
        status = run_simple(argc_, argv_);
    }
}

/* ---- statements -------------------------------------------------------- */
/* the script's intr block, run between statements when the host reports an interrupt event */
static void dispatch_intr(uint8_t ex)
{
    uint16_t saved;
    uint8_t d;

    if (!ex || quit || in_intr || !intr_pos || !tz_pending())
        return;
    saved = pos;
    d = depth;
    depth = 0;                                      /* a break inside intr does not leave a loop of the main script */
    in_intr = 1;
    pos = intr_pos;
    parse_block(1);
    in_intr = 0;
    depth = d;
    pos = saved;
}

static void parse_block(uint8_t ex)
{
    int c;

    skip_sp();
    if (pk() != '{') {
        die(TZ_P("expected {"), 2);
        return;
    }
    pos++;
    for (;;) {
        skip_ws();
        if (quit)
            return;
        c = pk();
        if (c == '}') {
            pos++;
            return;
        }
        if (c == 0) {
            die(TZ_P("missing }"), 2);
            return;
        }
        dispatch_intr(ex && !brk);
        parse_stmt(ex && !brk);
    }
}

static void parse_stmt(uint8_t ex)
{
    char nm[NAMELEN], a[VALLEN], b[VALLEN];
    uint16_t p, q;
    uint8_t n, run, t, inf = 0;
    int c;

    if (kw_take(TZ_P("setup"))) {
        skip_sp();
        parse_block(ex);
        return;
    }
    if (kw_take(TZ_P("if"))) {
        skip_sp();
        expect('(');
        t = parse_cond(ex);
        skip_sp();
        expect(')');
        parse_block(ex && t);
        if (quit)
            return;
        skip_sp();
        if (kw_take(TZ_P("else"))) {
            skip_sp();
            if (kw_is(TZ_P("if")))
                parse_stmt(ex && !t);
            else
                parse_block(ex && !t);
        }
        return;
    }
    if (kw_take(TZ_P("while")) || (inf = kw_take(TZ_P("loop")))) {
        p = pos;
        run = ex;
        depth++;
        for (;;) {
            pos = p;
            if (run) {
                if (tz_intr()) {
                    status = 130;
                    quit = 1;
                }
                dispatch_intr(1);
            }
            t = 1;
            if (!inf) {                             /* loop has no condition */
                skip_sp();
                expect('(');
                t = parse_cond(run);
                skip_sp();
                expect(')');
            }
            t = run && t && !quit;
            parse_block(t);
            if (quit || !t)
                break;
            if (brk) {
                brk = 0;
                break;
            }
        }
        depth--;
        return;
    }
    if (kw_take(TZ_P("break"))) {
        if (ex && depth)
            brk = 1;
        return;
    }
    if (kw_take(TZ_P("intr"))) {
        skip_sp();
        if (ex) {
            intr_pos = pos;
            tz_pending();                           /* an old event from before the handler existed is not for it */
        }
        parse_block(0);
        return;
    }

    /* NAME = VALUE [+|- VALUE] */
    p = pos;
    n = 0;
    while (is_id(c = (uint8_t)tz_getb(src + p))) {
        if (n < NAMELEN)
            nm[n] = (char)c;
        n++;
        p++;
    }
    q = p;
    while (tz_getb(src + q) == ' ' || tz_getb(src + q) == '\t')
        q++;
    if (n > 0 && n < NAMELEN && is_id0((uint8_t)nm[0]) && tz_getb(src + q) == '=' &&
        tz_getb(src + q + 1) != '=') {
        nm[n] = 0;
        pos = q + 1;
        operand(ex, a);
        while (pk() == ' ' || pk() == '\t')
            pos++;
        c = pk();
        if (c == '+' || c == '-') {
            pos++;
            operand(ex, b);
            if (ex && !quit)
                arith(a, (char)c, b, a);
        }
        if (ex && !quit)
            setvar(nm, a);
        return;
    }
    run_cmd(ex);
}

int tzsh_run(const char *script)
{
    uint8_t i;
    int c;

    if (busy) {
        tz_puts_P(TZ_P("tzsh: already running\n"));
        return 126;
    }
    busy = 1;
    for (i = 0; i < NVARS; i++)
        vname[i][0] = 0;
    src = script;
    pos = 0;
    brk = quit = depth = in_intr = 0;
    intr_pos = 0;
    status = 0;
    while (!quit) {
        skip_ws();
        c = pk();
        if (c == 0)
            break;
        if (c == '}') {
            die(TZ_P("unexpected }"), 2);
            break;
        }
        dispatch_intr(1);
        parse_stmt(1);
    }
    while (!quit && intr_pos) {                     /* an intr block keeps the script alive: Ctrl-C leaves */
        if (tz_intr()) {
            status = 130;
            break;
        }
        dispatch_intr(1);
    }
    busy = 0;
    return status;
}
