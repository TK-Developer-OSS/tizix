/*
 * tzesp_at.ino - tizix / z80board 用 ESP-WROOM-02 ファームウェア
 *
 *   「SPI スレーブ(tizix の独自フレーミング) ⇔ ESP-AT 互換のコマンド解釈
 *     + 実 TCP」のブリッジ。Z80 側(user/netesp.c)から見ると、
 *   **純正 ESP-AT が SPI に繋がっているのと同じ**に見える。
 *
 *   なぜ純正 ESP-AT を使わないのか:
 *     ESP8266(ESP-WROOM-02)の純正 ESP-AT は **UART 専用**で、SPI/SDIO の
 *     AT インタフェースは ESP32 系にしか無い。SPI bit-bang で繋ぐという
 *     前提なので、ESP 側にも自前ファームが必要になる。ならば喋る言葉は
 *     ESP-AT のサブセットに揃えておくのが得 ── python/at_modem.py
 *     (既存の ESP-AT テストベッド)と同じ応答になるので、Z80 側の
 *     コードをシミュレータ(z80boardsim)で先に検証できる。
 *
 *   ■ 配線(ESP-WROOM-02 / ESP8266 HSPI スレーブはピン固定)
 *     GPIO12 = MISO  → Z80 側 74HC541 の入力(ポート bit7 で読む)
 *     GPIO13 = MOSI  ← Z80 側 74HC574 の bit7
 *     GPIO14 = SCK   ← Z80 側 74HC574 の bit5
 *     GPIO15 = CS/SS ← Z80 側 74HC574 の bit6(0 = 選択)
 *     EXT_RSTB       ← Z80 側 74HC574 の bit0(0 = リセット)【推奨】
 *     ★GPIO15 は ESP8266 のブートストラップピンで、**起動時に Low**
 *       でなければ起動しない。Z80 側のラッチは CS = High で待機するため、
 *       ~RST を Z80 から握れるようにしておくこと(netesp.c の esp_boot が
 *       「CS Low にしてから ~RST を Low → High」でリセットする)。
 *       ~RST を配線しない場合は、電源投入順によって起動しないことがある。
 *     ★3.3V 系。Z80 ボードが 5V なら MOSI/SCK/CS はレベル変換が必要
 *       (74HC541 の Vcc を 3.3V にする等)。MISO は 3.3V → 5V TTL の
 *       入力しきい値を満たすか確認すること。
 *
 *   ■ フレーミング(user/espat.h と同じ。両方を同時に直すこと)
 *     マスタ(Z80)→ CS Low → CMD(1B) → ADDR(1B) → [読み出しは DUMMY(1B)]
 *                  → データ(32B / ステータス 4B)→ CS High
 *     CMD 0x02 = WRBUF(32B 受信)  データ [0] = 長さ n、[1..n] = ペイロード
 *     CMD 0x03 = RDBUF(32B 送信)  同じ形式
 *     CMD 0x04 = RDSTA(4B 送信)   [0] = 0x5A / [1] = フラグ / [2] = 受信数
 *     ※CMD 値・DUMMY の有無は ESP8266 core(hspi_slave.c)の実装依存。
 *       合わなければ Z80 側(espat.h)・iosim.c と揃えて直す。
 *     ※status は uint32 を setStatus() するので、マスタが受け取る
 *       バイト順は core / ハード依存。Z80 側は先頭・末尾の両方で
 *       マジック 0x5A を探して吸収する(netesp.c esp_stat)。
 *
 *   ■ ビルド
 *     Arduino IDE / arduino-cli で ESP8266 ボード(Generic ESP8266 Module、
 *     ESP-WROOM-02 は 2MB/4MB Flash)を選び、このスケッチを書き込む。
 *     追加ライブラリは不要(SPISlave / ESP8266WiFi / EEPROM は core 同梱)。
 *     IP 設定の保存に EEPROM(フラッシュ末尾のエミュレーション領域)を使う
 *     ので、Flash Size は EEPROM 領域を含む設定を選ぶこと。
 *
 *   ★このファイルは実機での動作を **まだ確認していない**。2026-09-22 時点で
 *     ESP-WROOM-02 の現物はあるが、載っているのは純正 ESP-AT 1.3.0.0 の
 *     ままで、このスケッチはまだ焼いていない。Z80 側は z80boardsim +
 *     python/at_modem.py で検証済み。実機で合わせ込むときは上の「※」を疑うこと。
 */
#include <ESP8266WiFi.h>
#include <SPISlave.h>
#include <EEPROM.h>

#define ESP_MAGIC     0x5A
#define ESP_ST_RXRDY  0x01
#define ESP_ST_TXRDY  0x02
#define ESP_PAYMAX    31

#define OUTQ_SIZE     2048
#define LINE_MAX      160

/* ---- station の IP 設定(固定 IP / DHCP)のコンパイル時デフォルト ----
 *   ★自分の LAN に合わせて書き換える。STA_DHCP を 1 にすると DHCP。
 *   一度でも AT+CIPSTA= / AT+CWDHCP= で設定すると EEPROM に残り、以後は
 *   そちらが優先される(純正 ESP-AT の _DEF 相当)。EEPROM を捨てて
 *   ここの値に戻すには AT+CIPSTA_RESET。
 *
 *   ★固定 IP にすると DHCP から DNS サーバを貰えなくなる。STA_DNS を
 *   空にすると名前解決が死に、AT+CIPSTART がホスト名で失敗するように
 *   なるので、ゲートウェイか公開 DNS を必ず入れておくこと。 */
#define STA_DHCP      1
#define STA_IP        "192.168.1.200"   /* 例 */
#define STA_GATEWAY   "192.168.1.1"     /* 例 */
#define STA_NETMASK   "255.255.255.0"
#define STA_DNS       "192.168.1.1"     /* 例 */
#define STA_DNS2      "8.8.8.8"

/* ---- Z80 へ返すバイト列のキュー ---- */
static uint8_t outq[OUTQ_SIZE];
static volatile uint16_t outq_head = 0, outq_tail = 0;

/* ---- Z80 から受け取ったバイト列のキュー ----
 *   SPISlave のコールバックは SPI の ISR 文脈で呼ばれる。そこで
 *   WiFi.begin() や client.connect()(数秒ブロックする)を呼ぶと WiFi
 *   スタックごと壊れるので、**コールバックは積むだけ**にして解釈は
 *   loop() で行う。 */
static uint8_t inq[512];
static volatile uint16_t inq_head = 0, inq_tail = 0;

/* ---- Z80 から来たコマンド行 ---- */
static char line[LINE_MAX];
static uint16_t line_len = 0;

/* ---- AT の状態 ---- */
static WiFiClient client;
static long send_remain = 0;      /* AT+CIPSEND=<n> の残バイト数 */
static bool slot_busy = false;    /* RDBUF に積んだまま未読 */
static bool wr_ready = true;      /* WRBUF を受け取れる */

/* ---- station の IP 設定(EEPROM に永続化)---- */
#define IPCFG_ADDR    0
#define IPCFG_MAGIC   0x7A495001UL        /* 'zIP' + 版数 */

struct ipcfg {
  uint32_t magic;
  uint8_t  dhcp;                          /* 1 = DHCP / 0 = 固定 IP */
  uint8_t  pad[3];
  uint32_t ip, gw, mask, dns, dns2;
};
static struct ipcfg ipc;

static uint32_t ip_from_str(const char *s)
{
  IPAddress a;
  if (!*s || !a.fromString(s)) return 0;
  return (uint32_t)a;
}

static void ipcfg_defaults()
{
  ipc.magic = IPCFG_MAGIC;
  ipc.dhcp  = STA_DHCP;
  ipc.ip    = ip_from_str(STA_IP);
  ipc.gw    = ip_from_str(STA_GATEWAY);
  ipc.mask  = ip_from_str(STA_NETMASK);
  ipc.dns   = ip_from_str(STA_DNS);
  ipc.dns2  = ip_from_str(STA_DNS2);
}

static void ipcfg_load()
{
  EEPROM.begin(sizeof(struct ipcfg) + 16);
  EEPROM.get(IPCFG_ADDR, ipc);
  if (ipc.magic != IPCFG_MAGIC) ipcfg_defaults();
}

static void ipcfg_save()
{
  /* ★フラッシュ書き込み中は割り込みが数十 ms 止まり、その間の SPI 転送は
   *   取りこぼす。Z80 側はこのコマンドの応答待ちなので実害は無いが、
   *   **ISR 文脈(SPISlave のコールバック)からは絶対に呼ばないこと**。 */
  ipc.magic = IPCFG_MAGIC;
  EEPROM.put(IPCFG_ADDR, ipc);
  EEPROM.commit();
}

/* ★WiFi.begin() より **前** に呼ぶこと。ESP8266 core の begin() は
 *   config() が立てた _useStaticIp を見て DHCP を開始するかを決めるが、
 *   _useStaticIp は RAM 上の変数なので電源投入時は必ず false に戻って
 *   いる。「フラッシュに固定 IP が保存されているから大丈夫」は成立せず、
 *   setup() で毎回 config() を呼び直さないと DHCP に落ちる。 */
static void ipcfg_apply()
{
  if (ipc.dhcp) {
    /* 全 0 = DHCP へ戻す(core が _useStaticIp を下ろして dhcpc を回す) */
    WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0),
                IPAddress((uint32_t)0));
    return;
  }
  WiFi.config(IPAddress(ipc.ip), IPAddress(ipc.gw), IPAddress(ipc.mask),
              IPAddress(ipc.dns), IPAddress(ipc.dns2));
}

static uint16_t outq_count()
{
  if (outq_head >= outq_tail) return outq_head - outq_tail;
  return OUTQ_SIZE - outq_tail + outq_head;
}

static void outq_push(const uint8_t *p, size_t n)
{
  for (size_t i = 0; i < n; i++) {
    uint16_t nh = outq_head + 1;
    if (nh >= OUTQ_SIZE) nh = 0;
    if (nh == outq_tail) return;          /* 満杯: 捨てる */
    outq[outq_head] = p[i];
    outq_head = nh;
  }
}

static void reply(const char *s)
{
  outq_push((const uint8_t *)s, strlen(s));
}

static void inq_push(const uint8_t *p, size_t n)
{
  for (size_t i = 0; i < n; i++) {
    uint16_t nh = inq_head + 1;
    if (nh >= sizeof(inq)) nh = 0;
    if (nh == inq_tail) return;           /* 満杯: 捨てる */
    inq[inq_head] = p[i];
    inq_head = nh;
  }
}

static int inq_pop()
{
  if (inq_head == inq_tail) return -1;
  int c = inq[inq_tail];
  uint16_t nt = inq_tail + 1;
  if (nt >= sizeof(inq)) nt = 0;
  inq_tail = nt;
  return c;
}

/* ---- ステータス / データスロットの更新 ---- */
static void update_status()
{
  uint16_t n = outq_count();
  if (n > ESP_PAYMAX) n = ESP_PAYMAX;
  uint32_t flags = 0;
  if (slot_busy) flags |= ESP_ST_RXRDY;
  if (wr_ready)  flags |= ESP_ST_TXRDY;
  /* [0] = magic / [1] = flags / [2] = count(リトルエンディアン前提) */
  SPISlave.setStatus((uint32_t)ESP_MAGIC | (flags << 8) | ((uint32_t)n << 16));
}

static void fill_slot()
{
  if (slot_busy) return;
  uint16_t n = outq_count();
  if (n == 0) return;
  if (n > ESP_PAYMAX) n = ESP_PAYMAX;

  uint8_t buf[32];
  memset(buf, 0, sizeof(buf));
  buf[0] = (uint8_t)n;
  for (uint16_t i = 0; i < n; i++) {
    buf[1 + i] = outq[outq_tail];
    outq_tail++;
    if (outq_tail >= OUTQ_SIZE) outq_tail = 0;
  }
  SPISlave.setData(buf, sizeof(buf));
  slot_busy = true;
}

/* ---- AT コマンド 1 行の処理(応答文字列は純正 ESP-AT に合わせる)---- */
static bool starts(const char *s, const char *pfx)
{
  return strncmp(s, pfx, strlen(pfx)) == 0;
}

/* 次の "..." を取り出して中身を返す。*pp は閉じクォートの次へ進む。
 *   引数を順に剥がしていく用。無ければ NULL。 */
static char *next_quoted(char **pp)
{
  char *q1 = strchr(*pp, '"');
  if (!q1) return NULL;
  char *q2 = strchr(q1 + 1, '"');
  if (!q2) return NULL;
  *q2 = 0;
  *pp = q2 + 1;
  return q1 + 1;
}

static void handle_line(char *s)
{
  /* 前後の空白/CR を落とす */
  while (*s == ' ') s++;
  size_t l = strlen(s);
  while (l && (s[l - 1] == ' ' || s[l - 1] == '\r')) s[--l] = 0;
  if (!l) return;

  if (!strcasecmp(s, "AT")) {
    reply("\r\nOK\r\n");
  } else if (!strcasecmp(s, "ATE0") || !strcasecmp(s, "ATE1")) {
    reply("\r\nOK\r\n");
  } else if (!strcasecmp(s, "AT+GMR")) {
    reply("\r\nAT version:2.4.0.0(tzesp_at)\r\n"
          "SDK version:esp8266-arduino\r\n"
          "Bin version:1.0.0(TIZIX-ESP8266)\r\n\r\nOK\r\n");
  } else if (!strcasecmp(s, "AT+RST")) {
    reply("\r\nOK\r\n");
    delay(50);
    ESP.restart();
  } else if (starts(s, "AT+CWMODE")) {
    WiFi.mode(WIFI_STA);
    reply("\r\nOK\r\n");
  } else if (starts(s, "AT+CIPMUX")) {
    reply("\r\nOK\r\n");                 /* 単一接続のみ。値は見ない */
  } else if (starts(s, "AT+CWJAP")) {
    /* AT+CWJAP="ssid","pass" */
    char *q1 = strchr(s, '"');
    if (!q1) { reply("\r\nERROR\r\n"); return; }
    char *ssid = q1 + 1;
    char *q2 = strchr(ssid, '"');
    if (!q2) { reply("\r\nERROR\r\n"); return; }
    *q2 = 0;
    char *q3 = strchr(q2 + 1, '"');
    const char *pass = "";
    if (q3) {
      pass = q3 + 1;
      char *q4 = strchr(q3 + 1, '"');
      if (q4) *q4 = 0;
    }
    WiFi.persistent(true);               /* 次の電源投入で自動再接続 */
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(100);
    if (WiFi.status() == WL_CONNECTED)
      reply("\r\nWIFI CONNECTED\r\n\r\nWIFI GOT IP\r\n\r\nOK\r\n");
    else
      reply("\r\n+CWJAP:1\r\n\r\nFAIL\r\n");
  } else if (!strcasecmp(s, "AT+CIFSR")) {
    String r = "\r\n+CIFSR:STAIP,\"" + WiFi.localIP().toString() + "\"\r\n";
    r += "+CIFSR:STAMAC,\"" + WiFi.macAddress() + "\"\r\n\r\nOK\r\n";
    reply(r.c_str());
  } else if (!strcasecmp(s, "AT+CIPSTATUS")) {
    reply(client.connected() ? "\r\nSTATUS:3\r\n\r\nOK\r\n"
                             : "\r\nSTATUS:2\r\n\r\nOK\r\n");
  } else if (starts(s, "AT+CIPSTA") && !starts(s, "AT+CIPSTAT")) {
    /* AT+CIPSTA? … 現在の ip / gateway / netmask を返す
     * AT+CIPSTA="ip"[,"gw"[,"mask"[,"dns"]]] … 固定 IP にして EEPROM へ保存
     * AT+CIPSTA_RESET … EEPROM を捨ててコンパイル時デフォルトへ戻す
     *   _CUR / _DEF の接尾辞はどちらも同じ扱い(常に保存する)。
     *   ★"AT+CIPSTA" は "AT+CIPSTART" と "AT+CIPSTATUS" の接頭辞でもある。
     *     どちらも "AT+CIPSTAT" で始まるので、それを弾いて取り違えを防ぐ
     *     (この分岐を CIPSTART より前に書いても壊れないようにしてある)。 */
    if (!strcasecmp(s, "AT+CIPSTA_RESET")) {
      ipcfg_defaults();
      ipcfg_save();
      ipcfg_apply();
      reply("\r\nOK\r\n");
      return;
    }
    if (strchr(s, '?')) {
      String r = "\r\n+CIPSTA:ip:\"" + WiFi.localIP().toString() + "\"\r\n";
      r += "+CIPSTA:gateway:\"" + WiFi.gatewayIP().toString() + "\"\r\n";
      r += "+CIPSTA:netmask:\"" + WiFi.subnetMask().toString() + "\"\r\n\r\nOK\r\n";
      reply(r.c_str());
      return;
    }
    char *p = s;
    char *f = next_quoted(&p);
    if (!f) { reply("\r\nERROR\r\n"); return; }
    uint32_t v = ip_from_str(f);
    if (!v) { reply("\r\nERROR\r\n"); return; }
    f = next_quoted(&p);
    uint32_t gw = f ? ip_from_str(f) : 0;
    f = next_quoted(&p);
    uint32_t mask = f ? ip_from_str(f) : 0;
    f = next_quoted(&p);
    uint32_t dns = f ? ip_from_str(f) : 0;
    if (!mask) mask = ip_from_str("255.255.255.0");
    /* ★IPAddress の uint32_t 表現は a.b.c.d の **a が最下位バイト**。
     *   第 4 オクテットは最上位バイトなので 0x01000000 が「.1」にあたる。
     *   gw 省略時のこの補完が正しいのは /24 のときだけなので、それ以外の
     *   マスクを使うなら gw は明示すること。 */
    if (!gw) gw = (v & mask) | 0x01000000UL;
    ipc.dhcp = 0;
    ipc.ip   = v;
    ipc.gw   = gw;
    ipc.mask = mask;
    ipc.dns  = dns ? dns : gw;    /* 固定 IP では DNS を自力で持つしかない */
    ipc.dns2 = ip_from_str(STA_DNS2);
    ipcfg_save();
    ipcfg_apply();
    reply("\r\nOK\r\n");
  } else if (starts(s, "AT+CWDHCP")) {
    /* AT+CWDHCP? / AT+CWDHCP=<mode>,<en>(mode 1 = station、2 = 両方)
     *   ★純正は softAP の DHCP 分もビットに含めて 3 / 1 を返すが、tzesp は
     *     station only なので station のビット(2)だけを立てて返す。 */
    if (strchr(s, '?')) {
      reply(ipc.dhcp ? "\r\n+CWDHCP:2\r\n\r\nOK\r\n"
                       : "\r\n+CWDHCP:0\r\n\r\nOK\r\n");
      return;
    }
    char *eq = strchr(s, '=');
    char *comma = eq ? strchr(eq, ',') : NULL;
    if (!eq || !comma) { reply("\r\nERROR\r\n"); return; }
    int mode = atoi(eq + 1);
    if (mode != 1 && mode != 2) { reply("\r\nOK\r\n"); return; }  /* softAP 宛は無視 */
    ipc.dhcp = atoi(comma + 1) ? 1 : 0;
    ipcfg_save();
    ipcfg_apply();
    reply("\r\nOK\r\n");
  } else if (starts(s, "AT+CIPSTART")) {
    /* AT+CIPSTART="TCP","host",port */
    char *q1 = strchr(s, '"');
    if (!q1) { reply("\r\nERROR\r\n"); return; }
    char *q2 = strchr(q1 + 1, '"');
    if (!q2) { reply("\r\nERROR\r\n"); return; }
    char *q3 = strchr(q2 + 1, '"');
    if (!q3) { reply("\r\nERROR\r\n"); return; }
    char *host = q3 + 1;
    char *q4 = strchr(host, '"');
    if (!q4) { reply("\r\nERROR\r\n"); return; }
    *q4 = 0;
    char *comma = strchr(q4 + 1, ',');
    int port = comma ? atoi(comma + 1) : 0;
    if (client.connected()) { reply("\r\nALREADY CONNECTED\r\n\r\nERROR\r\n"); return; }
    if (WiFi.status() != WL_CONNECTED) { reply("\r\nno ip\r\n\r\nERROR\r\n"); return; }
    client.setNoDelay(true);
    if (client.connect(host, port))
      reply("\r\nCONNECT\r\n\r\nOK\r\n");
    else
      reply("\r\nERROR\r\n");
  } else if (starts(s, "AT+CIPSEND")) {
    char *eq = strchr(s, '=');
    long n = eq ? atol(eq + 1) : 0;
    if (!client.connected()) { reply("\r\nERROR\r\nNOT CONNECTED\r\n"); return; }
    if (n <= 0) { reply("\r\nERROR\r\n"); return; }
    send_remain = n;
    reply("\r\nOK\r\n> ");
  } else if (!strcasecmp(s, "AT+CIPCLOSE")) {
    if (client.connected() || client.available()) {
      client.stop();
      reply("\r\nCLOSED\r\n\r\nOK\r\n");
    } else {
      reply("\r\nERROR\r\nNOT CONNECTED\r\n");
    }
  } else {
    reply("\r\nERROR\r\n");
  }
}

/* ---- Z80 から来た生バイト列 ---- */
static void feed(uint8_t c)
{
  if (send_remain > 0) {               /* AT+CIPSEND のペイロード */
    if (client.connected()) client.write(&c, 1);
    send_remain--;
    if (send_remain == 0) reply("\r\nSEND OK\r\n");
    return;
  }
  if (c == '\n') {
    line[line_len] = 0;
    handle_line(line);
    line_len = 0;
    return;
  }
  if (c == '\r') return;
  if (line_len < LINE_MAX - 1) line[line_len++] = (char)c;
}

void setup()
{
  WiFi.persistent(true);               /* AP の SSID/パスワードはフラッシュへ */
  WiFi.mode(WIFI_STA);
  ipcfg_load();
  ipcfg_apply();                       /* ★必ず begin() より前(理由は関数のコメント) */
  WiFi.begin();                        /* 保存済みの AP へ自動再接続 */

  /* ★コールバックは ISR 文脈。**積むだけ**にして、AT の解釈(WiFi や
   *   TCP を触る=数秒ブロックしうる)は loop() 側でやる。 */
  SPISlave.onData([](uint8_t *data, size_t len) {
    uint8_t n = data[0];
    if (n > ESP_PAYMAX) n = ESP_PAYMAX;
    if (n > len - 1) n = len - 1;
    inq_push(&data[1], n);
    wr_ready = true;
    update_status();
  });

  SPISlave.onDataSent([]() {
    slot_busy = false;                 /* マスタが読み終えた: 次を積める */
    fill_slot();
    update_status();
  });

  SPISlave.onStatus([](uint32_t status) {
    (void)status;                      /* WRSTA は使っていない */
    update_status();
  });

  SPISlave.onStatusSent([]() {
    update_status();
  });

  SPISlave.begin();
  update_status();
}

void loop()
{
  /* Z80 から積まれたバイト列を解釈する(ここは通常の実行文脈なので
   * WiFi.begin() や client.connect() をブロックして呼んでよい)。 */
  for (int i = 0; i < 256; i++) {
    int c = inq_pop();
    if (c < 0) break;
    feed((uint8_t)c);
  }

  /* TCP からの受信を ESP-AT の +IPD 形式で Z80 へ流す */
  if (client.available()) {
    uint8_t buf[512];
    int n = client.read(buf, sizeof(buf));
    if (n > 0) {
      char hdr[24];
      snprintf(hdr, sizeof(hdr), "\r\n+IPD,%d:", n);
      reply(hdr);
      outq_push(buf, (size_t)n);
    }
  }
  static bool was_connected = false;
  bool now = client.connected();
  if (was_connected && !now) {
    client.stop();
    reply("\r\nCLOSED\r\n");
  }
  was_connected = now;

  fill_slot();
  update_status();
  delay(0);                            /* WiFi スタックへ実行を譲る */
}
