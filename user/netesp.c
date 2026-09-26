/* user/netesp.c - z80board(実機)版のネットワーク中継デーモン。
 *   `net &` でバックグラウンド起動する。**バイナリ名は z80pack と同じ
 *   net.bin**(user/Makefile が ARCH=z80board のときだけこちらをリンクする)
 *   なので、netcli.h を使う既存コマンド(telnet / tzftp / atcli / ntpdate /
 *   nettest)は 1 行も変えずに実機でも動く。
 *
 *   z80pack 版(user/net.c)との違いは「相手の device」だけ:
 *     z80pack : port 50/51 = cpmsim の client socket #1(ATD ダイヤル拡張)
 *     z80board: ラッチ 0x80 の bit-bang UART(9600bps + CTS)ごしの ESP-WROOM-02
 *               に純正 ESP-AT(AT+CIPSTART / AT+CIPSEND / +IPD)を話す
 *   共有リングバッファ(kmem.h の KW_NET*)の使い方は完全に同じ。
 *
 *   物理層は user/espuart.h(arch/z80board/espuart.s)。#62 で SPI(espspi.s +
 *   ESP 側の自作ファーム)から UART(純正 ESP-AT そのまま)へ切り替えた。
 *   ESP-AT の文字列は python/at_modem.py(テストベッド)と同じものなので、
 *   z80boardsim の UART 模擬(ラッチ 0x80 の波形を T ステートで読む)経由で
 *   at_modem.py を相手にそのまま検証できる。
 *
 *   KW_NETCMD の拡張(z80board のみ): NETCMD_ATCMD
 *     TX リングに積んだバイト列を「AT コマンド 1 行」として ESP へ送り、
 *     応答を RX リングへ素通しする。AP 参加(AT+CWJAP)や IP 確認
 *     (AT+CIFSR)に使う。呼び出し側は user/wifi.c。
 *
 *   終了させたい時は sh から `kill <block>`。次に使うときは `net &` を
 *   もう一度起動すればよい(起動のたびに状態を初期化するので、それが
 *   そのまま復旧手順になる)。
 *
 *   ★外部コマンドの掟(memory: external-cmd-authoring-constraints):
 *     モジュールレベルの可変 static を持たない(BSS は 0 clear されない)。
 *     状態は全部 main のスタック上の struct es に置き、ポインタで回す。
 *     除算・剰余は使わない(itoa/atoi は stdlib.rel の手書きアセンブラ)。
 *     文字列リテラルは必ず関数引数として渡す(iy_reg の穴)。
 */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "kmem.h"
#include "espuart.h"

extern unsigned int getticks(void);

#define NB(a)  (*(volatile unsigned char *)(a))

#define LINEMAX     48          /* AT 応答 1 行の上限(超過分は捨てる) */
#define TXCHUNK     64          /* 1 回の AT+CIPSEND で送る最大バイト数 */

/* ---- タイムアウトは「反復回数」と「tick」の両方で見る(expired 参照)----
 *   I_* = 反復回数 / T_* = tick。**両方**を超えたときだけ諦める
 *   (= 実質 max(反復, tick)。どちらかが速すぎても短くならない)。
 *   理由: z80board の TICKS は素直な 1/100 秒ではない。
 *     ・GP5 のハードウェアタイマは実機で出ている(2026-09-21 TK 確認)が、
 *       周期は ≒977Hz/1953Hz 系で TICK_HZ=100 とは一致していない
 *       (memory: z80board-hw-io-map / src/kernel.h の TICK_HZ は暫定値)。
 *     ・さらに getticks() 自身が KYIELD を踏むので **呼ぶたびに +1** される
 *       (src/kernel.c)。待ちループで回すほど tick が自走する。
 *   つまり tick だけを見ると実時間では見込みよりずっと早く諦めてしまい、
 *   WiFi の接続(DNS + TCP)には足りない。逆に反復回数だけを見ると、CPU が
 *   無限速のシミュレータで一瞬で諦める。両方見れば実機/シミュレータ双方で
 *   ちょうど良いところに落ちる。 */
#define I_OK        8000
#define T_OK        200         /* 通常コマンドの応答待ち */
#define I_DIAL      60000
#define T_DIAL      1500        /* AT+CIPSTART(DNS + TCP)の応答待ち */
#define I_PROMPT    20000
#define T_PROMPT    300         /* AT+CIPSEND の '>' 待ち */
#define I_SENDOK    30000
#define T_SENDOK    500         /* SEND OK 待ち */
#define I_ATCMD     60000
#define T_ATCMD     1500        /* AT パススルー(CWJAP は数秒かかる) */
#define I_ERRHOLD   3000
#define T_ERRHOLD   200         /* ERROR を呼び出し側に見せておく時間 */
#define I_STUCK     20000
#define T_STUCK     500         /* RX リングが詰まったまま = 読み捨てへ */
#define I_BOOT      30000
#define T_BOOT      100         /* リセット解除後、ESP の起動を待つ */

/* esp_rxbuf の最初の 1 バイトを待つ回数(1 回 48T)。400 ≒ 2.4ms ≒ 2 バイト分。
 * CTS フロー制御が効いていれば、待っていない間の分は ESP の FIFO に残る。 */
#define RXWAIT      400
#define RXWAIT_BOOT 20000       /* 起動直後(フロー制御を有効にする前)は長めに聞く */

struct es {
    unsigned char rbuf[ESP_RXBUFLEN];   /* esp_rxbuf で読んだ塊 */
    unsigned char rpos, rlen;           /* rbuf の消費位置 / 有効終端 */
    unsigned char line[LINEMAX];        /* AT 応答の行組み立て */
    unsigned char len;
    unsigned int  ipd;                  /* +IPD の残ペイロード長 */
    unsigned char ok, err, sendok, closed, conn, prompt;
    unsigned char echo;                 /* 1 = 受信を全部 RX リングへ流す */
    unsigned char drop;                 /* 1 = +IPD ペイロードを読み捨てる */
    unsigned int  rxwait;               /* esp_rxbuf の最初の待ち(RXWAIT / RXWAIT_BOOT) */
    unsigned int  stuck;                /* リング詰まり開始 tick */
    unsigned int  sit;                  /* 同・反復回数 */
    unsigned int  errt;                 /* NETSTATE_ERROR にした tick */
    unsigned int  eit;                  /* 同・反復回数 */
};

/* 待ちループの共通タイムアウト判定。反復回数と tick の**両方**が上限を
 * 超えたときだけ真を返す(上の I_ と T_ の定数のコメント参照)。
 * iter は呼び出し側が持つカウンタ(0 で初期化して渡す)。 */
static int expired(unsigned int *iter, unsigned int t0,
                   unsigned int imax, unsigned int tmax)
{
    if (*iter < imax) {
        (*iter)++;
        return 0;
    }
    return ((unsigned int)(getticks() - t0) > tmax);
}

/* 時間つぶし(imax 反復 かつ tmax tick 経つまで) */
static void dly(unsigned int imax, unsigned int tmax)
{
    unsigned int t0, it;

    t0 = getticks();
    it = 0;
    while (!expired(&it, t0, imax, tmax))
        ;
}

/* ---------------- UART(espuart.s)---------------- */

/* ESP から 1 バイト。無ければ -1(ノンブロッキング、最大 s->rxwait だけ聞く)。
 * esp_rxbuf は CTS=0 にしてまとめて受ける。聞いていない間は CTS=1 なので、
 * ESP(CTS フロー制御有効)は FIFO に貯めて待つ。 */
static int esp_getc(struct es *s)
{
    int n;

    if (s->rpos < s->rlen)
        return (int)s->rbuf[s->rpos++];

    n = esp_rxbuf(s->rbuf, ESP_RXBUFLEN, (int)s->rxwait);
    s->rpos = 0;
    s->rlen = 0;
    if (n <= 0)
        return -1;
    s->rlen = (unsigned char)n;
    s->rpos = 1;
    return (int)s->rbuf[0];
}

/* 送信は 1 バイトずつそのまま出す(9600bps で 1 バイト ≒ 1ms。割込み禁止は
 * その 1 バイトの間だけ)。esp_flush は SPI 版の名残で、今は何もしない。 */
static void esp_flush(struct es *s)
{
    (void)s;
}

static void esp_putc(struct es *s, int c)
{
    (void)s;
    esp_tx(c);
}

/* ---------------- KW_NET* リングバッファ ---------------- */

static int rx_room(void)
{
    unsigned char nh;

    nh = NB(KW_NETRXH) + 1;
    if (nh >= KW_NETBUF_SIZE)
        nh = 0;
    return (nh != NB(KW_NETRXT));
}

static void rx_put(int c)
{
    unsigned char h, nh;

    h = NB(KW_NETRXH);
    nh = h + 1;
    if (nh >= KW_NETBUF_SIZE)
        nh = 0;
    if (nh == NB(KW_NETRXT))
        return;                         /* 満杯: 捨てる */
    NB(KW_NETRXBUF + h) = (unsigned char)c;
    NB(KW_NETRXH) = nh;
}

static void rx_puts(const char *p)
{
    while (*p) {
        rx_put((int)*p);
        p++;
    }
}

static int tx_count(void)
{
    unsigned char h, t;

    h = NB(KW_NETTXH);
    t = NB(KW_NETTXT);
    if (h >= t)
        return (int)(h - t);
    return (int)(KW_NETBUF_SIZE - t + h);
}

static int tx_get(void)
{
    unsigned char t;
    int c;

    t = NB(KW_NETTXT);
    if (t == NB(KW_NETTXH))
        return -1;
    c = (int)NB(KW_NETTXBUF + t);
    t++;
    if (t >= KW_NETBUF_SIZE)
        t = 0;
    NB(KW_NETTXT) = t;
    return c;
}

static void tx_drain(void)
{
    NB(KW_NETTXT) = NB(KW_NETTXH);
}

/* ---------------- AT 応答のパーサ ---------------- */

static unsigned int dec(const char *p)
{
    unsigned int v;

    v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10;
        v = v + (unsigned int)(*p - '0');
        p++;
    }
    return v;
}

static void classify(struct es *s)
{
    char *l;

    l = (char *)s->line;
    if (!strcmp(l, "OK"))
        s->ok = 1;
    else if (!strcmp(l, "ERROR"))
        s->err = 1;
    else if (!strcmp(l, "FAIL"))
        s->err = 1;
    else if (!strcmp(l, "SEND OK"))
        s->sendok = 1;
    else if (!strcmp(l, "SEND FAIL"))
        s->err = 1;
    else if (!strcmp(l, "CLOSED"))
        s->closed = 1;
    else if (!strcmp(l, "CONNECT"))
        s->conn = 1;
    else if (!strcmp(l, "ALREADY CONNECTED"))
        s->err = 1;
}

/* 受信 1 バイトを行バッファへ。行が閉じたら classify、
 * "+IPD,<len>:" を見たらペイロードモードへ入る。 */
static void feed(struct es *s, int c)
{
    if (s->echo)
        rx_put(c);

    if (c == '\r')
        return;
    if (c == '\n') {
        s->line[s->len] = 0;
        if (s->len)
            classify(s);
        s->len = 0;
        return;
    }
    if (s->len == 0 && c == '>') {
        s->prompt = 1;                  /* AT+CIPSEND の入力プロンプト */
        return;
    }
    if (s->len < LINEMAX - 1) {
        s->line[s->len] = (unsigned char)c;
        s->len++;
        s->line[s->len] = 0;
    }
    /* "+IPD,<len>:" は CRLF で終わらない。':' で判定する。 */
    if (c == ':' && s->line[0] == '+' && s->line[1] == 'I' &&
        s->line[2] == 'P' && s->line[3] == 'D' && s->line[4] == ',') {
        s->ipd = dec((char *)&s->line[5]);
        s->len = 0;
        s->stuck = getticks();
        s->sit = 0;
        s->drop = 0;
    }
}

/* ESP から来ているものを全部さばく(ノンブロッキング)。
 * +IPD のペイロードは RX リングへ。リングが満杯なら**読まずに戻る**
 * (バックプレッシャ)。ただし詰まったままが T_STUCK を超えたら
 * 読み捨てへ切り替える ── 読み手が居なくなった場合にデーモンが
 * 永久に固まらないようにする。 */
static void rx_pump(struct es *s)
{
    int c;

    for (;;) {
        if (s->ipd) {
            if (!s->drop && !rx_room()) {
                if (expired(&s->sit, s->stuck, I_STUCK, T_STUCK))
                    s->drop = 1;
                return;
            }
            c = esp_getc(s);
            if (c < 0)
                return;
            if (!s->drop)
                rx_put(c);
            s->ipd--;
            if (!s->ipd)
                s->drop = 0;
            continue;
        }
        c = esp_getc(s);
        if (c < 0)
            return;
        feed(s, c);
    }
}

/* ---------------- AT コマンド送信 ---------------- */

static void at_line(struct es *s, const char *cmd)
{
    s->ok = 0;
    s->err = 0;
    s->sendok = 0;
    s->prompt = 0;
    s->conn = 0;
    while (*cmd) {
        esp_putc(s, (int)*cmd);
        cmd++;
    }
    esp_putc(s, '\r');
    esp_putc(s, '\n');
    esp_flush(s);
}

/* 戻り: 1 = OK / -1 = ERROR / 0 = タイムアウト */
static int at_wait(struct es *s, unsigned int imax, unsigned int tmax)
{
    unsigned int t0, it;

    t0 = getticks();
    it = 0;
    for (;;) {
        rx_pump(s);
        if (s->ok)
            return 1;
        if (s->err)
            return -1;
        if (expired(&it, t0, imax, tmax))
            return 0;
    }
}

/* ---------------- 各コマンドの処理 ---------------- */

static void set_error(struct es *s)
{
    NB(KW_NETSTATE) = NETSTATE_ERROR;
    s->errt = getticks();
    s->eit = 0;
}

/* KW_NETHOST の "host:port" へ AT+CIPSTART する */
static void do_connect(struct es *s)
{
    char host[KW_NETHOST_MAX + 2];
    char cmd[KW_NETHOST_MAX + 28];
    char num[8];
    char *colon;
    unsigned char i;

    for (i = 0; i < KW_NETHOST_MAX; i++) {
        host[i] = (char)NB(KW_NETHOST + i);
        if (!host[i])
            break;
    }
    host[i] = 0;

    colon = strrchr(host, ':');
    if (colon == 0) {
        set_error(s);
        return;
    }
    *colon = 0;
    itoa(atoi(colon + 1), num, 10);

    strcpy(cmd, "AT+CIPSTART=\"TCP\",\"");
    strcat(cmd, host);
    strcat(cmd, "\",");
    strcat(cmd, num);

    /* 前の接続の残骸を掃除してから繋ぐ */
    NB(KW_NETRXH) = 0; NB(KW_NETRXT) = 0;
    NB(KW_NETTXH) = 0; NB(KW_NETTXT) = 0;
    s->ipd = 0;
    s->len = 0;
    s->closed = 0;

    at_line(s, cmd);
    if (at_wait(s, I_DIAL, T_DIAL) == 1)
        NB(KW_NETSTATE) = NETSTATE_CONNECTED;
    else
        set_error(s);
}

static void do_close(struct es *s)
{
    at_line(s, "AT+CIPCLOSE");
    at_wait(s, I_OK, T_OK);
    s->ipd = 0;
    s->len = 0;
    s->closed = 0;
    NB(KW_NETSTATE) = NETSTATE_IDLE;
}

/* TX リングに溜まった分を AT+CIPSEND で送る */
static void do_tx(struct es *s)
{
    char cmd[16];
    char num[8];
    unsigned int t0, it;
    int n, i, c;

    n = tx_count();
    if (n <= 0)
        return;
    if (n > TXCHUNK)
        n = TXCHUNK;

    itoa(n, num, 10);
    strcpy(cmd, "AT+CIPSEND=");
    strcat(cmd, num);
    at_line(s, cmd);

    t0 = getticks();
    it = 0;
    while (!s->prompt) {
        rx_pump(s);
        if (s->err || s->closed)
            return;
        if (expired(&it, t0, I_PROMPT, T_PROMPT))
            return;                     /* プロンプトが来ない: 次周回で再試行 */
    }

    for (i = 0; i < n; i++) {
        c = tx_get();
        if (c < 0)
            c = 0;                      /* 有り得ないが 0 で埋めて長さを守る */
        esp_putc(s, c);
    }
    esp_flush(s);

    t0 = getticks();
    it = 0;
    while (!s->sendok && !s->err) {
        rx_pump(s);
        if (expired(&it, t0, I_SENDOK, T_SENDOK))
            break;
    }
}

/* TX リングの中身を AT コマンド 1 行として送り、応答を RX リングへ流す */
static void do_atcmd(struct es *s)
{
    char cmd[KW_NETBUF_SIZE];
    int n, i, c;

    n = tx_count();
    if (n > KW_NETBUF_SIZE - 1)
        n = KW_NETBUF_SIZE - 1;
    i = 0;
    while (i < n) {
        c = tx_get();
        if (c < 0)
            break;
        cmd[i] = (char)c;
        i++;
    }
    cmd[i] = 0;
    tx_drain();

    NB(KW_NETRXH) = 0;
    NB(KW_NETRXT) = 0;
    s->len = 0;
    s->echo = 1;
    at_line(s, cmd);
    at_wait(s, I_ATCMD, T_ATCMD);
    s->echo = 0;
}

/* ---------------- 起動時のリンク確認 ---------------- */

static void esp_boot(struct es *s)
{
    int i;

    /* ESP をこちらからリセットして状態を揃える(~RST = ラッチ bit4)。
     * 起動直後の ESP は 74880bps のブートログと "ready" を出すが、
     * 9600bps で読むと化けるので読み捨てる。 */
    s->rxwait = RXWAIT_BOOT;
    esp_rst(1);
    dly(2000, 10);
    esp_rst(0);
    dly(I_BOOT, T_BOOT);                /* ESP のブートを待つ */
    while (esp_getc(s) >= 0)            /* ブートログ / ready を読み捨てる */
        ;
    s->len = 0;

    i = 0;
    while (i < 3) {
        at_line(s, "AT");
        if (at_wait(s, I_OK, T_OK) == 1)
            break;
        i++;
    }
    if (i >= 3) {
        puts("net: ESP が応答しない(UART 配線 / 9600bps 設定 / ~RST を確認)\n");
        return;
    }
    at_line(s, "ATE0");                 /* エコー off(応答解釈を単純に保つ) */
    at_wait(s, I_OK, T_OK);
    /* CTS フロー制御を有効にする(保存しない _CUR)。以後 ESP は CTS=1 の間
     * 送らないので、こちらが聞いていない間の分は ESP の FIFO に残る。 */
    at_line(s, "AT+UART_CUR=9600,8,1,0,2");
    if (at_wait(s, I_OK, T_OK) == 1)
        s->rxwait = RXWAIT;
    else
        puts("net: AT+UART_CUR が通らない(フロー制御なしで続ける)\n");
    at_line(s, "AT+CIPMUX=0");          /* 単一接続モード */
    at_wait(s, I_OK, T_OK);
    puts("net: ESP link ok\n");
}

int main(int argc, char **argv)
{
    struct es st;
    unsigned char cmd, state;

    (void)argc;
    (void)argv;

    memset((void *)&st, 0, sizeof(st));

    /* 起動のたびに状態を初期化する。kill で刈られた後の再実行が
     * そのまま復旧手順になる(kmem.h のコメント参照)。 */
    NB(KW_NETCMD)   = NETCMD_NONE;
    NB(KW_NETSTATE) = NETSTATE_IDLE;
    NB(KW_NETRXH) = 0; NB(KW_NETRXT) = 0;
    NB(KW_NETTXH) = 0; NB(KW_NETTXT) = 0;

    esp_boot(&st);

    for (;;) {
        rx_pump(&st);
        cmd = NB(KW_NETCMD);
        state = NB(KW_NETSTATE);

        if (state == NETSTATE_CONNECTED) {
            if (st.closed) {             /* 相手が切った */
                st.closed = 0;
                st.ipd = 0;
                NB(KW_NETSTATE) = NETSTATE_IDLE;
                NB(KW_NETCMD) = NETCMD_NONE;
                continue;
            }
            if (cmd == NETCMD_CLOSE) {
                do_close(&st);
                NB(KW_NETCMD) = NETCMD_NONE;
                continue;
            }
            if (cmd == NETCMD_ATCMD) {   /* 通信中は受け付けない */
                rx_puts("BUSY\r\n");
                NB(KW_NETCMD) = NETCMD_NONE;
                continue;
            }
            do_tx(&st);
            continue;
        }

        if (state == NETSTATE_ERROR) {
            /* 呼び出し側(netcli の net_connect)が -1 を拾えるだけの時間
             * ERROR を見せてから IDLE へ戻す。戻さないと二度と繋げない。 */
            if (expired(&st.eit, st.errt, I_ERRHOLD, T_ERRHOLD))
                NB(KW_NETSTATE) = NETSTATE_IDLE;
            continue;
        }

        /* NETSTATE_IDLE */
        if (cmd == NETCMD_CONNECT) {
            do_connect(&st);
            NB(KW_NETCMD) = NETCMD_NONE;
        } else if (cmd == NETCMD_ATCMD) {
            do_atcmd(&st);
            NB(KW_NETCMD) = NETCMD_NONE;
        } else if (cmd == NETCMD_CLOSE) {
            NB(KW_NETCMD) = NETCMD_NONE;
        }
    }
    return 0;
}
