/* user/mcpd.c - MCP サーバ(task.md #110)。gcc 系でネットワークを持つアーキ(esp32-wroom-32e)だけ。
 *
 *   mcpd [PORT]      既定 8000。`mcpd &` で常駐させる(/etc/rc にコメントで置いてある)
 *
 *   AI のクライアント(Claude Code など)から tizix とつないだ機器を操作するための口。
 *   MCP の転送は Streamable HTTP の最小形: POST で JSON-RPC を 1 つ受け、application/json で 1 つ返す
 *   (GET の SSE は 405)。1 依頼ごとに接続を閉じる(Connection: close)。
 *     例: claude mcp add --transport http tizix http://<esp32 の IP>:8000/mcp
 *
 *   ツール:
 *     run           tizix のコマンド(1 つ。| や > は無し)を実行して、出力と終了コードを返す
 *     read_file     ファイルを読む(先頭 3KB まで)       write_file  ファイルに書く(上書き)
 *     list_dir      ディレクトリの一覧
 *     esp32_device  常駐 esp32d へ依頼する("gpio 2 1"、"rgb red"、"adc 34" など。esp32-* と同じ引数)
 *
 *   制約: カーネルの TCP は全体で 1 本(src/knet.h)。mcpd が待ち受けている間、ほかのネットのコマンドは使えない。
 *   run の出力はカーネルのリダイレクト(向け先 1 本)でファイルへ取る。その間のコンソール出力も一緒に入る。
 *   認証は無い(LAN の中で使う前提)。
 */
#include "stdio.h"
#include "string.h"
#define SHVEC_NO_STATE
#include "shvec.h"
#include "mbox.h"
#include "netcli.h"

#define REQ_MAX   2048
#define REP_MAX   5120
#define OUT_MAX   3072
#define OUT_FILE  "/mcpd.out"

/* コマンドは libc を持たない(stdio.h の gcc 側に strn* は無い) */
static int strncmp_(const char *a, const char *b, unsigned n)
{
    while (n--) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
        a++; b++;
    }
    return 0;
}

static int strncmp_ci(const char *a, const char *b, unsigned n)
{
    while (n--) {
        char x = (char)((*a >= 'A' && *a <= 'Z') ? *a + 32 : *a), y = (char)((*b >= 'A' && *b <= 'Z') ? *b + 32 : *b);
        if (x != y) return x - y;
        if (!x) return 0;
        a++; b++;
    }
    return 0;
}

static char req[REQ_MAX + 1];
/* 返事は repbuf の REP_HDR から後ろに積み、HTTP のヘッダはその直前に書いて 1 回で送る
 * (ヘッダと本体を別々に送ると、途中で HTTP を検査する機器が本体を取り違えた例があった。Windows で確認) */
#define REP_HDR   192
static char repbuf[REP_HDR + REP_MAX];
#define rep (repbuf + REP_HDR)
static unsigned rlen;
static char body_hdr[160];

/* ---- 返事を積む ---- */
static void rput(const char *s)
{
    while (*s && rlen < REP_MAX - 1) rep[rlen++] = *s++;
}

static void rnum(long v)
{
    char b[12];
    int i = 0, neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    do { b[i++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg && rlen < REP_MAX - 1) rep[rlen++] = '-';
    while (i && rlen < REP_MAX - 1) rep[rlen++] = b[--i];
}

/* JSON の文字列の中身として積む(" \ と制御文字を逃がす。UTF-8 の多バイトはそのまま) */
static void resc(const char *s, unsigned n)
{
    static const char hx[] = "0123456789abcdef";
    unsigned i;
    for (i = 0; i < n && rlen < REP_MAX - 8; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { rep[rlen++] = '\\'; rep[rlen++] = (char)c; }
        else if (c == '\n') { rep[rlen++] = '\\'; rep[rlen++] = 'n'; }
        else if (c == '\r') { rep[rlen++] = '\\'; rep[rlen++] = 'r'; }
        else if (c == '\t') { rep[rlen++] = '\\'; rep[rlen++] = 't'; }
        else if (c < 0x20) { rput("\\u00"); rep[rlen++] = hx[c >> 4]; rep[rlen++] = hx[c & 15]; }
        else rep[rlen++] = (char)c;
    }
}

/* ---- 依頼の JSON から値を拾う(入れ子は見ずに、最初に見つかった "key" を使う) ---- */
static const char *jfind(const char *js, const char *key)
{
    unsigned k = (unsigned)strlen(key);
    const char *p = js;
    while ((p = strchr(p, '"')) != 0) {
        if (!strncmp_(p + 1, key, k) && p[1 + k] == '"') {
            p += k + 2;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (*p != ':') continue;
            p++;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            return p;
        }
        p++;
    }
    return 0;
}

/* 文字列の値を out へ(逃がしを戻す。\uXXXX は ASCII だけ、ほかは '?')。戻り: 1 = あった */
static int jstr(const char *js, const char *key, char *out, unsigned max)
{
    const char *p = jfind(js, key);
    unsigned o = 0;
    if (!p || *p != '"') return 0;
    for (p++; *p && *p != '"' && o < max - 1; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') out[o++] = '\n';
            else if (*p == 't') out[o++] = '\t';
            else if (*p == 'r') out[o++] = '\r';
            else if (*p == 'u') {
                unsigned v = 0, i;
                for (i = 1; i <= 4 && p[i]; i++)
                    v = v * 16 + (unsigned)(p[i] <= '9' ? p[i] - '0' : (p[i] | 0x20) - 'a' + 10);
                out[o++] = v < 0x80 ? (char)v : '?';
                p += 4;
            } else out[o++] = *p;
        } else out[o++] = *p;
    }
    out[o] = 0;
    return 1;
}

/* 値をそのまま(数か "文字列")。id に使う */
static int jraw(const char *js, const char *key, char *out, unsigned max)
{
    const char *p = jfind(js, key);
    unsigned o = 0;
    if (!p) return 0;
    if (*p == '"') {
        out[o++] = *p++;
        while (*p && *p != '"' && o < max - 2) { if (*p == '\\' && p[1]) out[o++] = *p++; out[o++] = *p++; }
        out[o++] = '"';
    } else
        while (*p && *p != ',' && *p != '}' && *p != ' ' && o < max - 1) out[o++] = *p++;
    out[o] = 0;
    return o > 0;
}

/* ---- ツール ---- */
static int run_cmdline(const char *line, int *code)
{
    static char pk[320];
    char cmd[48], fn[64];
    unsigned o = 0, argc = 0, i = 0;
    unsigned char n;

    while (line[i] == ' ') i++;
    while (line[i] && line[i] != ' ' && o < sizeof cmd - 1) cmd[o++] = line[i++];
    cmd[o] = 0;
    if (!cmd[0]) return -1;
    o = 0;
    while (line[i]) {                                   /* 残りを空白で割って NUL 区切りに('…' "…" は 1 語) */
        while (line[i] == ' ') i++;
        if (!line[i]) break;
        if (line[i] == '"' || line[i] == '\'') {
            char q = line[i++];
            while (line[i] && line[i] != q && o < sizeof pk - 2) pk[o++] = line[i++];
            if (line[i]) i++;
        } else
            while (line[i] && line[i] != ' ' && o < sizeof pk - 2) pk[o++] = line[i++];
        pk[o++] = 0;
        argc++;
    }
    pk[o] = 0;

    if (redir_begin(OUT_FILE, 0) != 0) return -1;
    if (builtin_is(cmd)) {
        builtin_try(cmd, line + (unsigned)(strchr(line, ' ') ? strchr(line, ' ') - line + 1 : (int)strlen(line)));
        *code = 0;
    } else {
        strcpy(fn, cmd[0] == '/' ? "" : "/bin/");
        strcat(fn, cmd);
        strcat(fn, ".bin");
        n = kexec_argv(fn, pk, (unsigned char)argc);
        if (n == 0xFF) { printf("%s: not found\n", cmd); *code = 127; }
        else if (n == 0) { printf("%s: no free slot\n", cmd); *code = 126; }
        else {
            unsigned t0 = getticks();
            while (SH_PID(n) != 0) {
                if ((unsigned)(getticks() - t0) > 3000) { SH_KILL(n); printf("\n(killed after 30 s)\n"); break; }
                ksleep(2);
            }
            *code = (int)syscall5(52, n, 0, 0, 0);
        }
    }
    redir_end();
    return 0;
}

static void put_file(const char *path, unsigned max)
{
    FILE *f = fopen(path, "r");
    char buf[128];
    unsigned total = 0;
    size_t k;
    if (!f) return;
    while (total < max && (k = fread(buf, 1, sizeof buf, f)) > 0) {
        resc(buf, (unsigned)k);
        total += (unsigned)k;
    }
    if (total >= max) rput("\\n(…truncated)");
    fclose(f);
}

static void text_result(void)
{
    rput("{\"content\":[{\"type\":\"text\",\"text\":\"");
}

/* 依頼が読めなかったときの手掛かりを /var/log/message へ(受けたバイト数・Content-Length から決めた長さ・本体の頭) */
static unsigned dbg_n, dbg_need;
static void diag(const char *what, const char *js)
{
    char m[320];
    unsigned i, j;
    strcpy(m, "mcpd: ");
    strcat(m, what);
    i = (unsigned)strlen(m);
    m[i++] = ' ';
    for (j = 100000; j; j /= 10) if (dbg_n >= j || j == 1) m[i++] = (char)('0' + dbg_n / j % 10);
    m[i++] = '/';
    for (j = 100000; j; j /= 10) if (dbg_need >= j || j == 1) m[i++] = (char)('0' + dbg_need / j % 10);
    {   /* カーネルが受けたバイト数(リングへ移した数)と lwIP の tcp_state */
        unsigned long rb = net_info(NI_RXBYTE), ts = net_info(NI_TCPST), ri = net_info(NI_RXIN), pd = net_info(NI_PEND);
        m[i++] = ' '; m[i++] = 'k';
        for (j = 100000; j; j /= 10) if (rb >= j || j == 1) m[i++] = (char)('0' + rb / j % 10);
        m[i++] = ' '; m[i++] = 'i';
        for (j = 100000; j; j /= 10) if (ri >= j || j == 1) m[i++] = (char)('0' + ri / j % 10);
        m[i++] = ' '; m[i++] = 'p';
        for (j = 100000; j; j /= 10) if (pd >= j || j == 1) m[i++] = (char)('0' + pd / j % 10);
        m[i++] = ' '; m[i++] = 's'; m[i++] = (char)('0' + ts % 10);
    }
    m[i++] = ' ';
    for (j = 0; js[j] && i < sizeof m - 1; j++)
        m[i++] = js[j] < ' ' ? '.' : js[j];
    m[i] = 0;
    klog(m);
}

static void tool_call(const char *js)
{
    static char a1[1024], a2[256];
    char name[32];

    if (!jstr(js, "name", name, sizeof name)) {
        diag("no tool name", js);
        rput("{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"no tool name\"}]}");
        return;
    }

    if (!strcmp(name, "run")) {
        int code = 0;
        if (!jstr(js, "command", a2, sizeof a2) || run_cmdline(a2, &code) != 0) {
            rput("{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"cannot run\"}]}");
            return;
        }
        text_result();
        put_file(OUT_FILE, OUT_MAX);
        rput("\\n[exit ");
        rnum(code);
        rput("]\"}]");
        if (code) rput(",\"isError\":true");
        rput("}");
        unlink(OUT_FILE);
        return;
    }
    if (!strcmp(name, "read_file")) {
        FILE *f;
        if (!jstr(js, "path", a2, sizeof a2) || (f = fopen(a2, "r")) == 0) {
            rput("{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"cannot open\"}]}");
            return;
        }
        fclose(f);
        text_result();
        put_file(a2, OUT_MAX);
        rput("\"}]}");
        return;
    }
    if (!strcmp(name, "write_file")) {
        FILE *f;
        if (!jstr(js, "path", a2, sizeof a2) || !jstr(js, "content", a1, sizeof a1) || (f = fopen(a2, "w")) == 0) {
            rput("{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"cannot write\"}]}");
            return;
        }
        fwrite(a1, 1, strlen(a1), f);
        fclose(f);
        text_result();
        rput("wrote ");
        rnum((long)strlen(a1));
        rput(" bytes\"}]}");
        return;
    }
    if (!strcmp(name, "list_dir")) {
        char nm[TZ_NAME_MAX];
        int t;
        if (!jstr(js, "path", a2, sizeof a2)) strcpy(a2, "/");
        if (opendir(a2) != 0) { rput("{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"no such directory\"}]}"); return; }
        text_result();
        for (;;) {
            t = readdir(nm);
            if (t == 0) break;
            resc(nm, (unsigned)strlen(nm));
            if (t == 2) rput("/");
            else { rput("  "); rnum((long)readdir_size()); }
            rput("\\n");
        }
        closedir();
        rput("\"}]}");
        return;
    }
    if (!strcmp(name, "esp32_device")) {
        unsigned i, o = 0;
        int n;
        if (!jstr(js, "request", a2, sizeof a2)) { rput("{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"no request\"}]}"); return; }
        for (i = 0; a2[i] && o < sizeof a1 - 1; i++) {   /* "gpio 2 1" → "gpio\0" "2\0" "1\0" */
            if (a2[i] == ' ') { if (o && a1[o - 1]) a1[o++] = 0; }
            else a1[o++] = a2[i];
        }
        if (o && a1[o - 1]) a1[o++] = 0;
        n = mb_call("esp32d", a1, o, a2, sizeof a2 - 1);
        if (n < 1) { rput("{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"esp32d is not running\"}]}"); return; }
        text_result();
        resc(a2 + 1, (unsigned)(n - 1));
        rput("\"}]");
        if (a2[0]) rput(",\"isError\":true");
        rput("}");
        return;
    }
    rput("{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"unknown tool\"}]}");
}

static const char tools_json[] =
    "{\"tools\":["
    "{\"name\":\"run\",\"description\":\"Run one tizix command (no pipes or redirection) on the ESP32 and return its output and exit code. Examples: ls /, ps, free, ifconfig, date, cat /etc/rc.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\"}},\"required\":[\"command\"]}},"
    "{\"name\":\"read_file\",\"description\":\"Read a file on the tizix FAT disk (first 3 KB).\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
    "{\"name\":\"write_file\",\"description\":\"Write (overwrite) a file on the tizix FAT disk (up to 1 KB).\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}},"
    "{\"name\":\"list_dir\",\"description\":\"List a directory (name and size; directories end with /).\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}},"
    "{\"name\":\"esp32_device\",\"description\":\"Ask the esp32d daemon to drive ESP32 peripherals. Request is a device and its arguments, e.g. 'gpio 2 1', 'gpio 5', 'rgb red', 'rgb 0 0 40', 'pwm 5 1000 50', 'adc 34', 'i2c scan', 'spi 0x9f 0 0'.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"request\":{\"type\":\"string\"}},\"required\":[\"request\"]}}"
    "]}";

/* JSON-RPC 1 つを処理して rep に返事の本体を作る。戻り: 0 = 通知(返事なし) */
static int handle(const char *js)
{
    char method[40], id[40], ver[24];

    rlen = 0;
    if (!jstr(js, "method", method, sizeof method)) {
        rput("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32600,\"message\":\"Invalid Request\"}}");
        return 1;
    }
    if (!jraw(js, "id", id, sizeof id))
        return 0;                                       /* 通知(notifications/initialized など) */
    rput("{\"jsonrpc\":\"2.0\",\"id\":");
    rput(id);
    if (!strcmp(method, "initialize")) {
        if (!jstr(js, "protocolVersion", ver, sizeof ver)) strcpy(ver, "2025-06-18");
        rput(",\"result\":{\"protocolVersion\":\"");
        rput(ver);
        rput("\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"tizix-mcpd\",\"version\":\"0.1\"},"
             "\"instructions\":\"tizix on ESP32. Use run for shell commands, esp32_device for GPIO/RGB/PWM/ADC.\"}}");
    } else if (!strcmp(method, "ping")) {
        rput(",\"result\":{}}");
    } else if (!strcmp(method, "tools/list")) {
        rput(",\"result\":");
        rput(tools_json);
        rput("}");
    } else if (!strcmp(method, "tools/call")) {
        rput(",\"result\":");
        tool_call(js);
        rput("}");
    } else {
        rput(",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
    }
    return 1;
}

/* ---- HTTP ---- */
static void send_all(const char *s, unsigned n)
{
    unsigned t0 = getticks();
    int k;
    while (n) {
        k = net_write(s, (int)n);
        if (k > 0) { s += k; n -= (unsigned)k; t0 = getticks(); }
        else {
            if (net_state() != NET_CONNECTED || (unsigned)(getticks() - t0) > 500) return;
            ksleep(1);
        }
    }
}

static void respond(const char *status, const char *ctype, const char *b, unsigned n)
{
    char num[12];
    unsigned i = 0, v = n;
    char t[12];
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    v = 0;
    while (i) num[v++] = t[--i];
    num[v] = 0;
    strcpy(body_hdr, "HTTP/1.1 ");
    strcat(body_hdr, status);
    strcat(body_hdr, "\r\nContent-Type: ");
    strcat(body_hdr, ctype);
    strcat(body_hdr, "\r\nContent-Length: ");
    strcat(body_hdr, num);
    strcat(body_hdr, "\r\nConnection: close\r\n\r\n");
    v = (unsigned)strlen(body_hdr);
    if (b == rep) {                                   /* 本体の直前にヘッダを置いて 1 回で送る */
        char *h = rep - v;
        for (i = 0; i < v; i++) h[i] = body_hdr[i];
        send_all(h, v + n);
    } else {                                          /* 短い定型の本体: ヘッダの後ろへつなげて 1 回で */
        for (i = 0; i < n && v + i < sizeof body_hdr - 1; i++) body_hdr[v + i] = b[i];
        send_all(body_hdr, v + i);
    }
}

static unsigned clen(const char *h)
{
    const char *p = h;
    unsigned v = 0;
    while ((p = strchr(p, '\n')) != 0) {
        p++;
        if ((p[0] | 0x20) == 'c' && !strncmp_ci(p, "content-length:", 15)) {
            p += 15;
            while (*p == ' ') p++;
            while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned)(*p++ - '0');
            return v;
        }
    }
    return 0;
}

static void serve_one(void)
{
    unsigned n = 0, t0 = getticks(), need = 0;
    char *body = 0;
    int k;

    /* 前の依頼の文字列が req に残っている。最初の読みが 0 バイトのとき、これを空にしておかないと
     * strstr が古いヘッダの終わりを見つけ、長さも本体の位置も取り違える(実機でときどき落ちた原因) */
    req[0] = 0;
    for (;;) {                                          /* ヘッダと本体を読む(5 秒で諦める) */
        k = net_read(req + n, (int)(REQ_MAX - n));
        if (k > 0) { n += (unsigned)k; req[n] = 0; t0 = getticks(); }
        if (!body && (body = strstr(req, "\r\n\r\n")) != 0) {
            body += 4;
            need = (unsigned)(body - req) + clen(req);
            /* Expect: 100-continue(Windows の .NET など)は、こちらが応えるまで本体を送ってこない */
            if (n < need && strstr(req, "100-continue"))
                send_all("HTTP/1.1 100 Continue\r\n\r\n", 25);
        }
        if (body && n >= need) break;
        if (n >= REQ_MAX) { respond("413 Payload Too Large", "text/plain", "", 0); return; }
        if (net_state() != NET_CONNECTED || (unsigned)(getticks() - t0) > 500) {
            dbg_n = n;
            dbg_need = need;
            diag(net_state() != NET_CONNECTED ? "peer closed before the request was complete" : "request timeout", req);
            return;
        }
        if (k <= 0) ksleep(1);
    }
    req[need] = 0;
    dbg_n = n;
    dbg_need = need;
    if (strncmp_(req, "POST ", 5)) {
        respond("405 Method Not Allowed", "text/plain", "use POST\n", 9);
        return;
    }
    if (handle(body)) respond("200 OK", "application/json", rep, rlen);
    else respond("202 Accepted", "text/plain", "", 0);
}

int main(int argc, char **argv)
{
    unsigned port = 8000, i;

    if (argc >= 1) {
        port = 0;
        for (i = 0; argv[0][i] >= '0' && argv[0][i] <= '9'; i++) port = port * 10 + (unsigned)(argv[0][i] - '0');
    }
    klog("mcpd: started");
    for (;;) {
        if (net_listen(port) != 0) { ksleep(100); continue; }
        while (net_state() == NET_LISTEN) ksleep(2);
        if (net_state() == NET_CONNECTED) {
            unsigned t0;
            serve_one();
            /* 閉じるのは相手から(Connection: close を見たクライアントが閉じる)を待つ。こちらが先に閉じると
             * その接続が TIME_WAIT でポートを握り、次の待ち受けが張れないことがあった。2 秒待って閉じなければ閉じる */
            t0 = getticks();
            while (net_state() == NET_CONNECTED && (unsigned)(getticks() - t0) < 200)
                ksleep(2);
        }
        net_close();
    }
}
