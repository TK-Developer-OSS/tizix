#include "plat.h"
#include "pico/stdlib.h"
#include "hardware/irq.h"

void con_putc(char c) {
    putchar_raw(c);
}

// 受信の 1 文字先読み。con_rx_ready は文字を読み捨ててはいけない。
// getchar_timeout_us(0) をそのまま判定に使うと、判定で 1 文字を消費して次の con_getc が
// その次の文字を読むため、入力が 1 文字おきに落ちる(実機で確認)。
static int rx_pending = PICO_ERROR_TIMEOUT;

int con_rx_ready(void) {
    if (rx_pending == PICO_ERROR_TIMEOUT)
        rx_pending = getchar_timeout_us(0);
    return rx_pending != PICO_ERROR_TIMEOUT;
}

unsigned char con_getc(void) {
    int c = rx_pending;
    rx_pending = PICO_ERROR_TIMEOUT;
    // 先読み済みならそれを返す。無ければ待つ
    // タイムアウト 100us で高頻度ポーリング
    while (c == PICO_ERROR_TIMEOUT && (c = getchar_timeout_us(100)) == PICO_ERROR_TIMEOUT) {
        tight_loop_contents();
    }
    return (unsigned char)c;
}

// 複数文字を一括読み取り（キー入力取りこぼし防止）
int con_getc_batch(unsigned char *buf, int max_len) {
    int count = 0;
    int c;
    
    // 最初の文字はブロッキングで取得（待機）
    c = rx_pending;
    rx_pending = PICO_ERROR_TIMEOUT;
    if (c == PICO_ERROR_TIMEOUT)
        c = getchar_timeout_us(100000); // 100ms 待機
    if (c != PICO_ERROR_TIMEOUT) {
        buf[count++] = (unsigned char)c;
    } else {
        return 0;
    }
    
    // 以降は非ブロッキングでバッファを一気に吸い出す
    while (count < max_len) {
        c = getchar_timeout_us(0); // 非ブロッキング
        if (c == PICO_ERROR_TIMEOUT) break;
        buf[count++] = (unsigned char)c;
    }
    
    return count;
}

int kbhit(void) {
    return con_rx_ready();
}

int getchar(void) {
    return (int)con_getc();
}

int putchar(int c) {
    con_putc((char)c);
    return c;
}

void con_raw(int raw) {
    (void)raw;
}


