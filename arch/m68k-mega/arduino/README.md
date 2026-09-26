# arch/m68k-mega/arduino — Mega2560 側のファーム

m68k-mega 実機は、生の MC68000 のバスを Arduino Mega2560 が代行する(/DTACK・UART・SD・タイマ割込み)。
ここはその Mega 側のスケッチ置き場(#95、2026-09-26 に tizix へ取り込み)。

| フォルダ | 中身 |
|---|---|
| `MEGA2560_68000_DTACK_TEST/` | **TIZIX HOST**(現行)。tizix のカーネル(`rom.h`)を SRAM へ転送して 68000 を走らせ、UART / SD / タイマ(Timer5 → IPL6)を代行する |
| `busprobe_20260920/` | 旧 BUSPROBE(SRAM 基板のバス検査用、2026-09-20 時点)。参照用 |

## 使い方(Windows)

```powershell
make -C arch/m68k-mega              # rocky9 側。kernel.bin → rom.h を作り、スケッチのフォルダにも置く
cd arch\m68k-mega\arduino\MEGA2560_68000_DTACK_TEST
.\build_and_flash.ps1 -Port COM3     # avr-gcc でビルドして avrdude で書き込む
.\tizix.ps1 -Seconds 20              # 起動ログを見る(ポートを開くと Mega がリセットされる)
```

- `rom.h` は生成物なので git に入れない(`make` が置く)。**カーネルの TICK_HZ とスケッチの Timer5 の周期は一致させること**。
- PowerShell 5.1 は BOM なしの UTF-8 を cp932 として読むので、`.ps1` は ASCII だけで書く。
- 取り込み元: `D:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST\`(独自 git、HEAD bd82e0e + 未コミット変更、2026-09-26 時点の作業ツリーをそのまま)。
  元のフォルダの `bk/` にある他の古い版は取り込んでいない。
