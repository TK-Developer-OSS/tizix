# WiFi 実験(task.md #109、W1)

ESP32 内蔵 WiFi を、ESP-IDF / FreeRTOS を使わずに動かす実験。tizix にはまだ組み込んでいない(単体の像)。

## 状態(2026-10-02、実機 COM4)
- 起動: ROM ローダ → flash 直接実行(キャッシュ MMU を自分で張る)→ PLL 80MHz
- 自前の OS 層: 協調スレッド(windowed ABI、`ctx_switch`)、セマフォ・キュー・ミューテックス・イベントグループ・タイマ(`osal.c`)、
  WiFi ドライバに渡す関数表 `wifi_osi_funcs_t`(`osi.c`)。割込みは ROM の xtos(VECBASE = ROM)を借りている
- Espressif のバイナリ(net80211 / pp / core / phy / rtc)と ESP-IDF のビルド済み部品(esp_phy / esp_wifi の一部 /
  esp_hw_support / wpa_supplicant / mbedtls)をリンク。NVS は無し(PHY 校正は毎回取り直し)
- **`esp_wifi_init_internal` → `esp_supplicant_init` → STA で `esp_wifi_start` → スキャン: 実機で周囲の AP が取れた**
- 接続(`esp_wifi_connect`)の経路は書いたが、資格情報が無いので未試験。TCP/IP(lwIP・DHCP)はまだ

## 部品の置き場所(リポジトリの外)
- バイナリと ESP-IDF のビルド済み部品・ヘッダ: rocky9 `~/tools/esp32-wifi-blobs`(Windows の Arduino-ESP32 3.3.2 /
  esp32-arduino-libs idf-release_v5.5 から写した)
- 参考ソース(ESP-IDF release/v5.5 の一部): rocky9 `~/tools/esp32-wifi-ref`
- ビルド: rocky9 で `bash build.sh`(今は `~/tmp/20261002/wt` で作っている。ここのファイルはその写し)

## flash の配置(実験像)
| 位置 | 中身 |
|---|---|
| 0x001000 | `wt.img`(RAM に載る部分。tizix の kernel.img と同じ席) |
| 0x03F000 | 接続先 `ssid=…\npsk=…\n`(実験用。`wtwifi.ps1` が書く) |
| 0x040000 | `drom.bin`(→ 0x3F400000) |
| 0x070000 | `irom.bin`(→ 0x400D0000) |
| 0x100000 | tizix のディスク(触らない) |

## 接続を試す(Windows、COM4)
    powershell -ExecutionPolicy Bypass -File wtwifi.ps1 -Ssid "ネットワーク名" -Psk "パスワード"
実験像を書いて起動し、`wifi: result CONNECTED` / `DISCONNECTED`(reason 付き)を出す。
資格情報は flash に平文で残るので、終わったら `-Clear` を付けてもう一度(-Ssid / -Psk は何でもよい)。

## tizix に戻す
tizix の kernel.img を 0x1000 に書き戻す(ディスクは 0x100000 のまま残っている):
    esptool --chip esp32 -p COM4 write-flash 0x1000 build/arch/esp32-wroom-32e/obj/kernel.img
