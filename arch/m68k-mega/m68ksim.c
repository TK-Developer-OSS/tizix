/* ============================================================================
 * m68ksim.c ? arch/m68k-mega の実機(生 MC68000 + Mega2560 バスホスト)を、
 *   実ハードの完成を待たずにソフトだけで先行検証するための最小ハーネス。
 *   rocket68(CPU コアのみのライブラリ)にボード(RAM + Mega が実装する
 *   UART MMIO + ソフト検証専用のディスク DMA)をこのファイルで被せる。
 *   cpmsim (z80pack) の m68k 版に相当。
 *
 *   ボードマップは include/plat.h と一致させること(実機の
 *   MEGA2560_68000_DTACK_TEST_stepd_4mhz_stable.ino_bk loop() が原典)。
 *   ・A20=0 側: 実機は SRAM 直結。ここでは cpu->memory をそのまま使う。
 *   ・A20=1 側: 実機は Mega が UART をエミュレートする(UART_DATA_ADDR/
 *     UART_STATUS_ADDR の2バイトを read8/write8 コールバックで横取りし、
 *     ホストの stdin/stdout に relay)。
 *   ・SD_*(plat.h)は実機と共通のバイトストリーム型レジスタ。ここでは
 *     read8/write8 コールバックで横取りし、ホストのディスクイメージ
 *     ファイルとの間で 1 バイトずつ中継する(SD_DATA の DATA_RDY は
 *     常に即 1 を返す ── 実機は SPI クロック待ちの遅延があるが diskio.c は
 *     どちらもポーリングで待つだけなので同じコードで動く)。
 *   ・レベル6の周期割込み(実機は Mega Timer5)はホストの単調時計で
 *     1/TICK_HZ 秒ごとに m68k_set_irq(6) で模擬する(オートベクタなので
 *     int_ack 不要)。TICK_HZ は Makefile の 1 つの値(既定 100、#83)。
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>

#include "rocket68/include/rocket68.h"
#include "include/plat.h"

/* ホスト側メモリは RAM 本体 + 各デバイスの MMIO 番地が収まる分だけ
 * 確保すればよい。途中の未使用域は触られない想定(実機は A20/A22/A23 と
 * A1-A4 しか見ていないので、その外のアドレスにアクセスする
 * コードを書かないこと)。 */
#define SIM_MEM_SIZE (((UART_STATUS_ADDR > SD_DATA) ? UART_STATUS_ADDR : SD_DATA) + 1)

static struct termios g_orig_termios;
static int g_termios_saved = 0;

static void restore_stdin(void)
{
    if (g_termios_saved) tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
}

/* atexit は正常な exit() 経路でしか走らない。`pkill m68ksim`(既定 SIGTERM)や
 * 端末を閉じたときの SIGHUP はプロセスを即終了させ atexit を素通りするので、
 * raw モードの端末設定が復元されないまま残り「文字入力を受け付けない」
 * 事故になる(実際に踏んだ)。シグナルでも確実に復元するようハンドラを積む。 */
static void on_fatal_signal(int sig)
{
    restore_stdin();
    _exit(128 + sig);
}

static void setup_raw_stdin(void)
{
    if (!isatty(STDIN_FILENO)) return;
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) != 0) return;
    g_termios_saved = 1;
    atexit(restore_stdin);
    signal(SIGTERM, on_fatal_signal);
    signal(SIGHUP, on_fatal_signal);
    signal(SIGINT, on_fatal_signal);   /* raw で ISIG 無効だが保険で */
    raw = g_orig_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

/* raw モード(ISIG 無効)では Ctrl+C はゲスト(tizix の con_break)へ渡る
 * だけでホスト側は止まらない。抜け道として Ctrl+] (0x1D、QEMU のシリアル
 * コンソール離脱キーと同じ)を m68ksim 自身の終了キーとして横取りする。
 *
 * ★実際にフリーズした生セッションへ gdb アタッチして発見した、今回の
 *   本当の直接原因: 以前は 1 バイトぶんの先読みスロット(g_pending)しか
 *   無く、**そこに1バイトでも未消費のまま残っていると、それ以降永遠に
 *   新しい入力を見に行かなくなる**(Ctrl+] すら検出しない)バグがあった
 *   (実測で g_pending=27=ESC が居座っていた ── ヒストリ機能の矢印キー
 *   ESC シーケンスの取りこぼし等、何らかの理由で1バイト残ると即座に
 *   この詰み状態になる)。shutdown 後はゲスト側が二度と
 *   stdin_take_byte() を呼ばない(kgetchar() する前景プロセスが居ない)
 *   ため、一度そのスロットが埋まると**未来永劫**誰もそれを空にしない。
 *   このバグは今回の SIGALRM 化やスケジューリング変更とは無関係に
 *   m68ksim.c に元からあったもので、「shutdown 後 Ctrl+] が効かない」の
 *   もう一つの(むしろ本命の)直接原因だった。
 *
 *   最初の修正案(先読みが埋まっていても常に poll+read する)は、SIGALRM
 *   ハンドラが 30ms ごとに動くこと自体は正しいが、先読みスロットが
 *   1バイトしか無いままだと**ゲスト行きのバイトを読んでは捨てる**形に
 *   なり、`ls`/`date` 等の通常操作でエコーが欠けたり文字化けする regression
 *   を作ってしまった(実測: "ls" が "l" だけしかエコーされない等)。
 *   そこで単一スロットではなく **リングバッファ(64B)** にした。新しい
 *   バイトは常に読んで Ctrl+] かどうかをまず判定し(バッファの空き状況に
 *   関係なく Ctrl+] は必ず即応答)、Ctrl+] でなければキューへ積む。
 *   64B もあれば通常のタイピング/エコー速度でオーバーフローすることは
 *   まず無く、万一積み過ぎても捨てるのは Ctrl+] 以外のバイトだけ。
 *   → rx(xmodem)の移植で 64B では足りないと分かった: ホストが 1 パケット
 *   (133B、1K モードなら 1029B)を一度に送るので、ゲストが読み切る前に
 *   あふれて中身が欠け、block 1 から NAK になった。4KB にする(pty の
 *   バッファと同程度。常に読んで Ctrl+] を即判定する方針はそのまま)。 */
#define PENDING_CAP 4096
static unsigned char g_pending_buf[PENDING_CAP];
static unsigned g_pending_head = 0, g_pending_tail = 0;   /* head==tail: 空 */

static int pending_empty(void) { return g_pending_head == g_pending_tail; }
static int pending_full(void)  { return ((g_pending_tail + 1) % PENDING_CAP) == g_pending_head; }

static void pending_push(unsigned char c)
{
    if (pending_full()) return;           /* 64B 分すら消費されない異常時のみ捨てる */
    g_pending_buf[g_pending_tail] = c;
    g_pending_tail = (g_pending_tail + 1) % PENDING_CAP;
}

static int pending_pop(void)
{
    int c;
    if (pending_empty()) return -1;
    c = g_pending_buf[g_pending_head];
    g_pending_head = (g_pending_head + 1) % PENDING_CAP;
    return c;
}

/* check_quit_key: Ctrl+] を見つけたら終了、それ以外はリングへ積む。
 * 通常のメインループ(stdin_fill 経由)からも、下の SIGALRM ハンドラからも
 * 呼ばれるため、**async-signal-safe な関数だけ**を使う(poll/read/write/
 * tcsetattr/_exit)。fprintf や exit() のような stdio バッファリングを
 * 経由するものはシグナルハンドラ内で使うと再入でデッドロックし得るので
 * 使わない。 */
static void check_quit_key(void)
{
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN)) return;
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return;
    if (c == 0x1D) {                      /* Ctrl+] : m68ksim を終了 */
        restore_stdin();
        write(STDERR_FILENO, "\nm68ksim: quit (Ctrl+])\n", 24);
        _exit(0);
    }
    pending_push(c);
}

/* ★rocket68 の m68k_step()(1 命令実行)の内部で、特定のオペコード
 * (`bra.s $-2` の自己分岐 = src/init.c の shutdown 後 `for(;;);` が
 * コンパイルされる形)に対し、単発呼出から永遠に戻らないバグがあることを
 * 実測(gdb アタッチ)で確認した(#47)。メインループを m68k_execute の
 * サイクル予算方式から m68k_step の命令数ループへ変えても直らなかった
 * ── つまり無限ループは m68k_step() の**呼出 1 回の内側**に閉じており、
 * ホスト側がどれだけ頻繁に「戻ってきたら stdin を見る」をやっても、
 * そもそも戻ってこない限り原理的に届かない。
 *
 * そこで OS のリアルタイムシグナル(SIGALRM + setitimer)で**強制的に**
 * 割り込む。シグナルはユーザー空間のコードがどこで詰まっていても(純粋な
 * 無限ループの最中でも)配送時点で必ずハンドラへ制御を渡す ── `kill`
 * (SIGTERM)でこの手のハングしたプロセスを終了できていたのと同じ原理を
 * Ctrl+] の検出そのものに使う。ハンドラは check_quit_key() だけを呼ぶ。 */
/* #80: メイン側が入力を扱っている最中は 1。ハンドラはこの間何もしない。
 * check_quit_key() はメインとハンドラの両方から呼ばれ、どちらもリングの tail を
 * 書く。メインが read() で 1 文字取ってから pending_push() するまでの間に
 * ハンドラが割り込むと、ハンドラが次の文字を先に積んで**順序が入れ替わる**
 * (1 行まとめて送ると末尾の文字が Enter の後に回り、次の行の頭に付いた:
 * `wc -l /etc/rc` → `/etc/r` + 次行 `cgrep`)。tail の同時書き込みで文字が
 * 消えることもある。ハンドラの目的は「メインが戻ってこないときの Ctrl+]」
 * なので、メインが入力を見ている間は任せてよい。 */
static volatile sig_atomic_t g_in_input = 0;

static void on_heartbeat(int sig)
{
    (void)sig;
    if (g_in_input)
        return;
    check_quit_key();
}

static void setup_heartbeat(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_heartbeat;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                      /* SA_RESTART を付けない */
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval it;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 30000;       /* 30ms ごとに Ctrl+] を強制チェック */
    it.it_value = it.it_interval;
    setitimer(ITIMER_REAL, &it, NULL);
}

static int stdin_fill(void)
{
    int r;

    g_in_input = 1;
    check_quit_key();
    r = !pending_empty();
    g_in_input = 0;
    return r;
}

static int stdin_has_byte(void)
{
    return stdin_fill();
}

static int stdin_take_byte(void)
{
    int c;

    g_in_input = 1;
    check_quit_key();
    c = pending_pop();
    g_in_input = 0;
    return c;
}

/* ---- SD カード状態(m68ksim 専用のホスト側実装、plat.h SD_* 参照) ----
 * 実機は Mega が SPI で 1 バイトずつ SD と往復するので遅延があるが、
 * m68ksim はホストのディスクイメージへの fread/fwrite を CMD トリガ時に
 * 一括で済ませてしまい、以降の DATA_RDY は常に 1 を返す(guest 側の
 * ポーリングループはそれでも正しく動く ── 実機より速いだけ)。 */
static FILE         *g_diskimg = NULL;
static unsigned char g_sd_buf[512];
static unsigned      g_sd_idx = 0;
static unsigned char g_sd_lba_hi = 0, g_sd_lba_mid = 0, g_sd_lba_lo = 0;
static unsigned char g_sd_op = 0;      /* 0=idle 1=read 2=write */
static unsigned char g_sd_status = 0;  /* bit0=SD_ERROR bit1=SD_DATA_RDY */

#define SECTOR_SIZE 512

static void sd_trigger(u8 cmd)
{
    unsigned long lba = ((unsigned long)g_sd_lba_hi << 16) |
                        ((unsigned long)g_sd_lba_mid << 8) | g_sd_lba_lo;

    g_sd_op = cmd;
    g_sd_idx = 0;
    g_sd_status = 0;
    if (!g_diskimg || (cmd != 1 && cmd != 2)) {
        g_sd_status = SD_ERROR;
        g_sd_op = 0;
        return;
    }
    if (fseek(g_diskimg, (long)(lba * SECTOR_SIZE), SEEK_SET) != 0) {
        g_sd_status = SD_ERROR;
        g_sd_op = 0;
        return;
    }
    if (cmd == 1 && fread(g_sd_buf, 1, SECTOR_SIZE, g_diskimg) != SECTOR_SIZE)
        g_sd_status |= SD_ERROR;
    g_sd_status |= SD_DATA_RDY;
}

static u8 sim_read8(M68kCpu *cpu, u32 addr)
{
    if (addr == UART_STATUS_ADDR) {
        u8 st = UART_TXRDY; /* ホスト stdout は常に書ける想定 */
        if (stdin_has_byte()) st |= UART_RXRDY;
        return st;
    }
    if (addr == UART_DATA_ADDR) {
        int c = stdin_take_byte();
        return (c >= 0) ? (u8)c : 0;
    }
    if (addr == SD_STATUS) return g_sd_status;
    if (addr == SD_DATA) {
        if (g_sd_op == 1 && g_sd_idx < SECTOR_SIZE) return g_sd_buf[g_sd_idx++];
        return 0xFF;
    }
    if (addr < cpu->memory_size) return cpu->memory[addr];
    return 0xFF; /* 未接続バス */
}

static void sim_write8(M68kCpu *cpu, u32 addr, u8 value)
{
    if (addr == UART_DATA_ADDR) {
        /* stdio(putchar+fflush)を使わない。SIGALRM は SA_RESTART 無し
         * (setup_heartbeat)なので、pty が詰まって write が待たされている
         * ところへ 30ms の心拍が来ると EINTR で戻り、glibc はそのバイトを
         * 捨てる。実測: `ptx 40 | prx` の "line-29" が "line-9" に化けた。 */
        ssize_t w;
        do {
            w = write(STDOUT_FILENO, &value, 1);
        } while (w < 0 && errno == EINTR);
        return;
    }
    if (addr == UART_STATUS_ADDR) return; /* 読み専用 */
    if (addr == SD_LBA_HI)  { g_sd_lba_hi = value;  return; }
    if (addr == SD_LBA_MID) { g_sd_lba_mid = value; return; }
    if (addr == SD_LBA_LO)  { g_sd_lba_lo = value;  return; }
    if (addr == SD_CMD)     { sd_trigger(value); return; }
    if (addr == SD_DATA) {
        if (g_sd_op == 2 && g_sd_idx < SECTOR_SIZE) {
            g_sd_buf[g_sd_idx++] = value;
            if (g_sd_idx == SECTOR_SIZE) {
                if (fwrite(g_sd_buf, 1, SECTOR_SIZE, g_diskimg) != SECTOR_SIZE)
                    g_sd_status |= SD_ERROR;
                fflush(g_diskimg);
                g_sd_op = 0;
            }
        }
        return;
    }
    if (addr < cpu->memory_size) cpu->memory[addr] = value;
}

/* 16/32bit アクセスは SD_* に無いので常に素通しでメモリへ(rocket68 の
 * 既定実装と同じビッグエンディアン合成)。 */
static u16 sim_read16(M68kCpu *cpu, u32 addr)
{
    if (addr + 1 < cpu->memory_size)
        return (u16)((cpu->memory[addr] << 8) | cpu->memory[addr + 1]);
    return 0xFFFF;
}

static void sim_write16(M68kCpu *cpu, u32 addr, u16 value)
{
    if (addr + 1 < cpu->memory_size) {
        cpu->memory[addr] = (value >> 8) & 0xFF;
        cpu->memory[addr + 1] = value & 0xFF;
    }
}

static u32 sim_read32(M68kCpu *cpu, u32 addr)
{
    if (addr + 3 < cpu->memory_size) {
        return ((u32)cpu->memory[addr] << 24) | ((u32)cpu->memory[addr + 1] << 16) |
               ((u32)cpu->memory[addr + 2] << 8) | (u32)cpu->memory[addr + 3];
    }
    return 0xFFFFFFFFu;
}

static void sim_write32(M68kCpu *cpu, u32 addr, u32 value)
{
    if (addr + 3 < cpu->memory_size) {
        cpu->memory[addr] = (value >> 24) & 0xFF;
        cpu->memory[addr + 1] = (value >> 16) & 0xFF;
        cpu->memory[addr + 2] = (value >> 8) & 0xFF;
        cpu->memory[addr + 3] = value & 0xFF;
    }
}

/* 4MHz 実機を想定し、この呼出単位ぶんの経過時間で壁時計と足並みを揃える
 * (単純に「1回のスライス=INSTRS_PER_SLICE 命令」を回し続け、都度 1 秒経過を
 * 見て level6 を上げるだけ。速度制限はしない=実機より速く進む。実機速度に
 * 合わせたい場合は nanosleep で調整すること)。
 *
 * ★m68k_execute(cpu, cycles) ではなく m68k_step(cpu) を INSTRS_PER_SLICE 回
 *   呼ぶ形にしてある。rocket68 の m68k_execute はサイクル予算をライブラリ
 *   内部の累積カウンタで管理しているが、`bra.s $-2`(自分自身への無条件分岐、
 *   src/init.c の `for(;;) ;` が shutdown 後にコンパイルされる形そのもの)を
 *   実行すると、この累積が進まず **1回の m68k_execute 呼出が永遠に返って
 *   来ない**バグを実測で確認した(gdb アタッチで
 *   start_cycles_run=338908930 と、要求した 4000 の 8 万倍以上を確認)。
 *   m68k_execute が戻らない限り、この main loop の stdin_fill()(Ctrl+] 検知)
 *   も m68k_set_irq(level6 タイマ)も一切呼ばれなくなり、**shutdown 後に
 *   level6 がまだ届いていない/届いても消費し切ったタイミングで `bra.s $-2`
 *   に入ると、ホスト側が Ctrl+] にも一切反応しない永久ハングになる**
 *   (level6 が偶然すぐ届けば例外処理のサイクル消費で救われるだけの、
 *   タイミング依存のバグだったため自動テストでは再現しにくかった)。
 *   m68k_step() は「命令 1 個」単位で必ず戻るので、rocket68 側のサイクル
 *   会計バグに関係なく INSTRS_PER_SLICE 命令ごとに確実にホストへ制御が
 *   戻り、stdin_fill() が呼ばれ続ける。 */
#define INSTRS_PER_SLICE 4000

static long g_trace_n = 0;
static void trace_hook(M68kCpu *cpu, u32 pc)
{
    if (g_trace_n++ < 2000000)
        fprintf(stderr, "pc=%06x d0=%08x a0=%08x sr=%04x\n",
                pc, cpu->d_regs[0].l, cpu->a_regs[0].l, cpu->sr);
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: %s kernel.bin [disk.img]\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }

    u8 *mem = calloc(1, SIM_MEM_SIZE);
    if (!mem) { fprintf(stderr, "out of memory\n"); return 1; }

    size_t n = fread(mem, 1, RAM_SIZE, f);
    fclose(f);
    fprintf(stderr, "m68ksim: loaded %zu bytes (RAM budget %luB)\n", n, (unsigned long)RAM_SIZE);

    if (argc == 3) {
        g_diskimg = fopen(argv[2], "r+b");
        if (!g_diskimg) { perror(argv[2]); return 1; }
        fprintf(stderr, "m68ksim: disk image %s attached\n", argv[2]);
    } else {
        fprintf(stderr, "m68ksim: no disk image given (FAT mount will fail)\n");
    }

    M68kCpu cpu;
    m68k_init(&cpu, mem, (u32)SIM_MEM_SIZE);
    if (getenv("M68KSIM_TRACE")) m68k_set_instr_hook_callback(&cpu, trace_hook);
    m68k_set_read8_callback(&cpu, sim_read8);
    m68k_set_write8_callback(&cpu, sim_write8);
    m68k_set_read16_callback(&cpu, sim_read16);
    m68k_set_write16_callback(&cpu, sim_write16);
    m68k_set_read32_callback(&cpu, sim_read32);
    m68k_set_write32_callback(&cpu, sim_write32);
    m68k_reset(&cpu);

    setup_raw_stdin();
    setup_heartbeat();
    fprintf(stderr, "m68ksim: Ctrl+] to quit (Ctrl+C goes to the guest shell)\n");

    /* #83: レベル6タイマを TICK_HZ(Makefile の 1 つの値。カーネルも同じ値で
     * ビルドされる)で上げる。以前は time() の秒単位で 1Hz 固定だった。
     * rocket68 の割込み要求は level で、受理されると irq_level が 0 に戻る。
     * syscall(trap0)の間は割込み禁止なので、前の要求がまだ受理されていない
     * うちに次の周期が来ることがある。そのまま上げ直すと 1 回分消え、時計が
     * 遅れる(実測: sleep 2 が 4.5 秒)。受理待ちの間に来た tick は missed に
     * 数えておき、受理されたら続けて上げ直す。 */
#ifndef TICK_HZ
#define TICK_HZ 100
#endif
    const long long period_ns = 1000000000LL / TICK_HZ;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long next_tick = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec + period_ns;
    int missed = 0;                        /* 上げるべきなのにまだ上げていない tick 数 */
    for (;;) {
        int i;
        for (i = 0; i < INSTRS_PER_SLICE; i++)
            m68k_step(&cpu);
        /* ゲストが UART を叩かなくても(ハング/無限ループ中でも)Ctrl+] は
         * 常に効くようにする(実際には SIGALRM ハートビートが主に効く。
         * ここは通常時の応答性のための保険)。stdin_fill 自身が終了キーを
         * 検出して抜ける。ゲスト側の実データ読み(sim_read8/UART_DATA)は
         * リングバッファ(pending_pop)経由で拾う。 */
        stdin_fill();
        clock_gettime(CLOCK_MONOTONIC, &ts);
        long long now = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
        while (now >= next_tick) {
            if (missed < TICK_HZ)          /* 最大 1 秒分まで溜める(長い停止の後は捨てる) */
                missed++;
            next_tick += period_ns;
        }
        if (missed > 0 && cpu.irq_level == 0) {   /* 前の要求は受理済み */
            m68k_set_irq(&cpu, 6);
            missed--;
        }
    }
    return 0;
}
