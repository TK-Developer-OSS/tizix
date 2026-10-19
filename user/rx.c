/* rx.c - XMODEM receive command for tizix (Tera Term compatible) */
#include "stdio.h"

#define SOH  0x01   /* 128-byte packet header */
#define STX  0x02   /* 1024-byte packet header (XMODEM-1K) */
#define EOT  0x04   /* End of transmission */
#define ACK  0x06   /* Acknowledge */
#define NAK  0x15   /* Negative acknowledge */
#define CAN  0x18   /* Cancel */
#define CRC_MODE 'C'

#define TIMEOUT_HANDSHAKE 150  /* 開始待ちポーリング間隔 (1.5秒) */
#define TIMEOUT_BYTE      200  /* パケット内/パケット間タイムアウト (2秒) */
#define MAX_ERRORS        10   /* 最大再試行回数 */
#define MAX_INIT_RETRY    10   /* 'C' 送信回数 (約15秒待機後 NAK へフォールバック) */

static unsigned char calc_cksum(const unsigned char *data, int len)
{
    unsigned char sum = 0;
    while (len--) sum += *data++;
    return sum;
}

static unsigned short calc_crc(const unsigned char *data, int len)
{
    unsigned short crc = 0;
    int i;
    while (len > 0) {
        crc = crc ^ ((unsigned short)(*data) << 8);
        data++;
        len--;
        for (i = 0; i < 8; i++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
        }
    }
    return crc;
}

/* シリアル入力バッファの残りを読み捨てる */
static void purge(void)
{
    while (kbhit()) {
        (void)getchar();
    }
}

/* ループ回数でタイムアウトする 1 バイト受信(パケット内のバイト間用) */
static int raw_getc_timeout(long count)
{
    while (count-- > 0) {
        if (kbhit()) {
            return getchar();
        }
    }
    return -1;
}

/* パケットバッファ（最大1024バイト: XMODEM-1K対応） */
static unsigned char buf[1024];

int main(int argc, char **argv)
{
    FILE *fp;
    char *fname;
    unsigned char seq = 1;
    int errors = 0;
    int crc_mode = 1;
    int init_tries = 0;
    int started = 0;
    int c;
    int pending_len = 0;

    /* crt0cmd.s は argv[0] に引数文字列全体 (base + 0x0F00) を渡す */
    if (argc < 1 || !argv[0] || argv[0][0] == '\0') {
        printf("Usage: rx <filename>\n");
        return 1;
    }
    fname = argv[0];
    while (*fname == ' ') fname++;
    if (*fname == '\0') {
        printf("Usage: rx <filename>\n");
        return 1;
    }
    {
        char *p = fname;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        *p = '\0';
    }

    fp = fopen(fname, "w");
    if (!fp) {
        printf("rx: cannot open '%s' for writing\n", fname);
        return 1;
    }

    /* 端末を生モードに(tty の ISIG 無効に相当)。xmodem はバイナリなので、
     * シーケンス番号 3 や CRC に 0x03 が現れる。生モードにしないと sh の前景待ち
     * (con_break)がそれを Ctrl+C と取って rx を kill する(block 3 で止まった)。
     * 終了で戻す。異常終了しても sh が前景ジョブの後で戻す。 */
    con_raw(1);

    /* 転送開始ハンドシェイクおよびパケット受信ループ */
    for (;;) {
        if (!started) {
            putchar(crc_mode ? CRC_MODE : NAK);
        }

        c = getc_timeout(started ? TIMEOUT_BYTE : TIMEOUT_HANDSHAKE);

        if (c < 0) {
            /* タイムアウト */
            if (!started) {
                init_tries++;
                if (crc_mode && init_tries >= MAX_INIT_RETRY) {
                    /* CRCモードで応答なし -> チェックサムモードに切り替え */
                    crc_mode = 0;
                    init_tries = 0;
                } else if (!crc_mode && init_tries >= MAX_INIT_RETRY) {
                    printf("rx: handshake timeout\n");
                    goto error;
                }
                continue;
            } else {
                /* 受信中タイムアウト */
                purge();
                putchar(NAK);
                errors++;
                if (errors >= MAX_ERRORS) {
                    printf("rx: timeout\n");
                    goto error;
                }
                continue;
            }
        }

        if (c == CAN || c == 0x03) { /* CAN または Ctrl+C */
            printf("rx: cancelled\n");
            goto error;
        }

        if (c == EOT) {
            /* 最後の保留ブロックがあれば末尾の 0x1A (EOF) パディングを除去して書き込む */
            if (pending_len > 0) {
                int write_len = pending_len;
                while (write_len > 0 && buf[write_len - 1] == 0x1A) {
                    write_len--;
                }
                if (write_len > 0) {
                    if (fwrite(buf, 1, write_len, fp) != (size_t)write_len) {
                        printf("rx: disk write error\n");
                        goto error;
                    }
                }
                pending_len = 0;
            }
            putchar(ACK);
            /* 送信側がACKを受け取る時間を少し待って終了 */
            break;  /* 転送完了 */
        }

        if (c == SOH || c == STX) {
            int pkt_size = (c == SOH) ? 128 : 1024;
            int valid = 0;
            int pnum = -1, comp = -1;
            int i;

            /* #58: 保留中の直前ブロックは、buf へ次のパケットを読み込む **前** に書き出す。
             *   以前は読み込んだ後(検証成功時)に buf を書いていたため、保留分が次の
             *   パケットで上書きされ、1 つ目が消えて最後が 2 回書かれていた。
             *   新しい SOH/STX が来た = 保留分は最後のブロックではないので、末尾の
             *   0x1A 除去(EOT 時だけ)は要らない。
             * #58: 以前はここから先を di/ei で囲んでいた。z80board は受信バイトを
             *   ISR がリング(KW_RXBUF)へ積む方式なので、割込み禁止の間は kbhit() が
             *   一切 true にならず、パケット途中でタイムアウト → NAK を繰り返して
             *   転送が進まなかった(実機の症状)。受信バイトはカーネル側でバッファ
             *   されるので、タスク切り替えで取りこぼすことはない。 */
            if (pending_len > 0) {
                if (fwrite(buf, 1, pending_len, fp) != (size_t)pending_len) {
                    printf("rx: disk write error\n");
                    goto error;
                }
                pending_len = 0;
            }

            pnum = raw_getc_timeout(200000L);
            if (pnum < 0) {
                goto nak_retry;
            }

            comp = raw_getc_timeout(200000L);
            if (comp < 0) {
                goto nak_retry;
            }

            /* パケット番号とその補数の検証 (pnum + comp == 0xFF) */
            if ((pnum + comp) != 0xFF) {
                goto nak_retry;
            }

            for (i = 0; i < pkt_size; i++) {
                int d = raw_getc_timeout(200000L);
                if (d < 0) {
                    goto nak_retry;
                }
                buf[i] = (unsigned char)d;
            }

            /* チェックサム / CRC 検証 */
            if (crc_mode) {
                int chigh = raw_getc_timeout(200000L);
                if (chigh < 0) {
                    goto nak_retry;
                }
                /* 最初のパケットは短く待つ(raw_getc_timeout の 200000 回は 10 秒を
                 * 超え、送信側が先に再送して次の SOH を CRC の下位と取り違える) */
                int clow = started ? raw_getc_timeout(200000L)
                                   : getc_timeout(TIMEOUT_BYTE / 4);
                if (clow < 0) {
                    /* 'C' を無視してチェックサムで送ってくる送信側(Tera Term の
                     * checksum 設定)は 1 パケット 132B で、CRC の 2 バイト目が来ない。
                     * 最初のパケットに限り、届いた 1 バイトをチェックサムとして見て、
                     * 合えばチェックサムモードへ切り替える。 */
                    if (started || calc_cksum(buf, pkt_size) != (unsigned char)chigh) {
                        goto nak_retry;
                    }
                    crc_mode = 0;
                    valid = 1;
                } else {
                    unsigned short crc = ((unsigned short)chigh << 8) | (unsigned char)clow;
                    if (calc_crc(buf, pkt_size) == crc) valid = 1;
                }
            } else {
                int cksum = raw_getc_timeout(200000L);
                if (cksum < 0) {
                    goto nak_retry;
                }
                if (calc_cksum(buf, pkt_size) == (unsigned char)cksum) valid = 1;
            }

            if (valid) {
                if ((unsigned char)pnum == seq) {
                    /* 期待通りの新しいパケット -> 次のパケットかEOTが来るまでバッファに保持 */
                    pending_len = pkt_size;
                    seq++;
                    errors = 0;
                    started = 1;
                    putchar(ACK);
                } else if ((unsigned char)pnum == (unsigned char)(seq - 1)) {
                    /* 直前のパケットの再送 (ACK紛失時など) -> ACKだけ再送し重複書き込みはしない */
                    started = 1;
                    putchar(ACK);
                } else {
                    /* 予期しないシーケンス番号 */
                    goto nak_retry;
                }
            } else {
                goto nak_retry;
            }
            continue;

nak_retry:
            purge();
            putchar(NAK);
            errors++;
            if (errors >= MAX_ERRORS) {
                printf("rx: too many errors\n");
                goto error;
            }
            continue;
        }

        /* SOH, STX, EOT, CAN 以外 */
        if (!started) {
            /* 開始前のゴミデータ（Tera Term接続時の改行やエコーなど）は読み飛ばして継続 */
            continue;
        } else {
            goto nak_retry;
        }
    }

    fclose(fp);
    con_raw(0);
    printf("rx: '%s' received successfully\n", fname);
    return 0;

error:
    fclose(fp);
    con_raw(0);
    return 1;
}
