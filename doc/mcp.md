# tizix MCP サーバ(mcpd)の使い方

ESP32 で動く tizix を、AI のクライアント(Claude Code など)から MCP で操作するための手引き。
対象アーキは esp32-wroom-32e だけ(WiFi が要る。QEMU では使えない)。経緯は task.md #110。

## 1. 起動する(ESP32 側)

`/etc/rc` に次の行を入れておくと、起動時に常駐する。

```
esp32d &
mcpd &
```

- `mcpd [PORT]` で待ち受ける。PORT を省くと 8000。
- `esp32d` は `esp32_device` ツールの依頼を受ける常駐。これが居ないと `esp32_device` は "esp32d is not running" を返す。
- リポジトリの `arch/esp32-wroom-32e/etc/rc` では mcpd の行がコメントになっている。使うときはコメントを外す。
- IP は `ifconfig` で確かめる。`/etc/wifi` に `ip=` が無ければ DHCP で取る。
- 動いているかは `ps` で見る(`mcpd` の行が出る)。

## 2. クライアントに登録する

接続先は `http://<ESP32 の IP>:8000/mcp`。パスは見ていないので、`/mcp` 以外でも通る。

### Claude Code

```
claude mcp add --transport http --scope user tizix http://<ESP32 の IP>:8000/mcp
claude mcp list          # tizix: ... Connected と出れば OK
```

- 名前で呼びたければ、hosts に `esp32.local` などを書いて URL に使う。
- 登録したあとに始めたセッションから使える。ツール名は `mcp__tizix__run` などになる。

### Gemini CLI(試していない)

`~/.gemini/settings.json` に次を書けば使えるはず。まだ試していない。

```json
{ "mcpServers": { "tizix": { "httpUrl": "http://<ESP32 の IP>:8000/mcp" } } }
```

### 手で叩く(確認用)

```
curl -s -X POST http://<IP>:8000/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"run","arguments":{"command":"ps"}}}'
```

回帰試験は `python/test_mcpd.py`(実機のみ。`TIZIX_HW` が無いと SKIP)。

## 3. ツール

| ツール | 引数 | すること | 限度 |
|---|---|---|---|
| `run` | `command` | tizix のコマンドを 1 つ実行し、出力と `[exit N]` を返す | 出力は先頭 3KB。30 秒で kill。`\|` と `>` は使えない |
| `read_file` | `path` | ファイルを読む | 先頭 3KB |
| `write_file` | `path`, `content` | ファイルに書く(上書き) | 1KB 未満 |
| `list_dir` | `path`(省略で `/`) | 一覧。ディレクトリは末尾に `/`、ファイルは大きさ付き | — |
| `esp32_device` | `request` | esp32d に周辺機器の操作を頼む | 引数は `esp32-*` コマンドと同じ |

- `run` の終了コードが 0 以外なら、エラーとして返る。見つからないコマンドは 127、空きスロットが無ければ 126、kill されたら 130。
- `esp32_device` の `request` は「装置名 引数…」の形で書く。
  - `gpio 2 1` / `gpio 2 toggle` / `gpio 5 in up`
  - `rgb red` / `rgb 0 0 40` / `rgb off`(Freenove ボードは GPIO16 の WS2812)
  - `pwm 5 1000 50` / `pwm 5 off`
  - `adc 34`(ADC1、GPIO 32〜39)
  - `i2c scan` / `i2c read ADDR REG [N]` / `i2c write ADDR BYTE…`(既定 SDA=21 SCL=22。`-p SDA SCL` で変更)
  - `spi 0x9f 0 0`(既定 SCK=18 MOSI=23 MISO=19 CS=5。`-p` で変更)
  - flash の GPIO 6〜11 と、コンソールの GPIO 1 / 3 は断られる。
  - 設定するだけの依頼(`gpio 2 1`、`rgb …` など)は空の返事で正常。

## 4. 制約と注意

- **認証も暗号化も無い。** LAN の中だけで使う。外から届く場所に置かない。
- **依頼は 1 本ずつ順番に処理する**(fork 無しの反復サーバ)。
  - 並行で来た接続は、カーネル(knet)が処理中の 1 本とは別に 3 本まで待たせる。待ちは 10 秒で捨てられる。
  - 長い `run` の後ろに並んだ依頼は、10 秒を超えると切られる。
  - ふだんの短い依頼なら 5 本同時でも通ることを確かめた。
- **カーネルの TCP は全体で 1 本。** mcpd が動いている間は、TCP を使うほかのコマンド(curl、telnet、nc など)が使えない。
- `run` の出力はリダイレクトでファイル(`/mcpd.out`)に取っている。そのため、
  - 実行中に出たほかのコンソール出力も混ざる。
  - `run` の中でリダイレクトは使えない。
- **たまに ECONNRESET で落ちる**。投げ直せば通る。原因はまだ分かっていない(task.md #110)。
- **Windows のセキュリティソフトの Web 保護が、平文 HTTP の応答を壊すことがある**(Content-Length を chunked に書き換える)。
  その場合は `http://<IP>:8000/*` を例外に入れる。
- 起動直後(ログを書いている最中)にリセットや電源断をすると、FAT が 4KB ぶん消えることがある。書き込み中は電源を落とさない。

## 5. 転送の仕様(実装の要点)

- MCP の Streamable HTTP の最小形。POST で JSON-RPC を 1 つ受け、`application/json` で 1 つ返す。
- GET(SSE)は 405、通知(`id` が無い)は 202。セッション ID は無い。
- `initialize` は、クライアントが名乗った `protocolVersion` をそのまま返す。
- メソッドは `initialize` / `ping` / `tools/list` / `tools/call` の 4 つ。
- 依頼ごとに `Connection: close`。返事のあとは相手が閉じるのを最大 2 秒待つ(TIME_WAIT で待ち受けを張れなくなるのを避けるため)。
- 依頼の上限は 2KB(超えると 413)。`Expect: 100-continue` に応える。
- 実装は `user/mcpd.c`(esp32 の ARCH_UCMDS)と `arch/esp32-wroom-32e/net/knet.c`(待ち受けと待ち)。
