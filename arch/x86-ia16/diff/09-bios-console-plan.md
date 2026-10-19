# #174 実機(ThinkPad) BIOS コンソール設計調査計画

## (1) 現 `console.c` の関数一覧と呼び出し箇所
- **関数一覧 (`arch/x86-ia16/console.c`)**:
  - `void con_init(void)`: COM1 (0x3F8) の初期化 (115200bps, 8N1, FIFO有効).
  - `void con_putc(char c)`: LSR THRE 待ちを行い、COM1 に 1 文字送信 (`\n` は変換なし).
  - `int con_rx_ready(void)`: LSR DR チェックにより受信データの有無を返す.
  - `unsigned char con_getc(void)`: 受信待ちループ後、COM1 から 1 文字取得 (Ctrl-] = 0x1D で QEMU 終了ハッチ).
  - `void con_puts(const char *s)`: 文字列を出力 (`\n` を `\r\n` に補正).
- **呼び出し箇所**:
  - `src/io.c` や `src/kmain.c` などの層 2 コンソールおよびカーネル層から `con_init`, `con_putc`, `con_getc`, `con_rx_ready`, `con_puts` が呼び出されている.

## (2) BIOS INT 10h / INT 16h による実装案とキーボード入力の検討
- **出力 (INT 10h)**:
  - `AH = 0Eh` (Teletype Output), `AL = 文字`, `BH = 0` (ページ 0).
  - 実装案: `__asm__ volatile("int $0x10" : : "a"(0x0E00 | (unsigned char)c), "b"(0));`
- **入力 (INT 16h)**:
  - 有効性確認: `AH = 01h` (Check for Keypress). `ZF=0` ならキーあり. `ZF=1` ならキーなし.
  - 取得: `AH = 00h` (Read Keyboard Input). ブロッキングでキー取得.
- **カーネル IF=0 と `sti` の注意 / 8042 直接読みとの比較**:
  - **事実**: tizix x86-ia16 カーネルは割り込み禁止 (`IF=0`) で実行されることが多く、PIT (INT 08h) 以外は基本割り込みが有効になっていない.
  - **推測・注意**: INT 16h は内部でキーボード割り込み (IRQ1) や BIOS キーボードバッファを利用しているため、`IF=0` のまま INT 16h(AH=01h) をポーリングするとキーボード割り込みが処理されず入力が検知されない可能性がある. したがって、一時的に `sti` (`__asm__ volatile("sti");`) を入れる配慮が必要.
  - **8042 ポーリング方式との比較**: ポート `0x64` / `0x60` を直接読む方式であれば `IF=0` でも動作するが、スキャンコードのデコードやシフトキー処理が必要になるため、BIOS INT 10h/16h (`sti` 併用) の方が実装・保守コストが低い.

## (3) COM1 と画面の両対応および QEMU 試験方法
- **両対応の案**:
  - ビルド時切替 (`-DCONSOLE_BIOS` vs `-DCONSOLE_COM1`)、または起動時にシリアルの有無を判定する方式. 実機 ThinkPad では BIOS コンソール (`INT 10h/16h`)、開発用 QEMU では COM1 シリアルを選択できるようにプリプロセッサまたは Makefile フラグで切替える設計が現実的.
- **QEMU での試験方法**:
  - 画面表示: `-display sdl` または `-display gtk` を指定してグラフィック画面を有効化する (`-nographic` を外す).
  - キー入力試験: QEMU ウィンドウにフォーカスを当ててキーボード入力を行う、または QEMU モニターでの `sendkey` コマンド活用.

## (4) 懸念点と ABI / 依存関係
- **懸念点**:
  - `crt0.s` や `bios13.s` の ABI 変更は原則不要だが、16bit 実モードでの BIOS 割り込み呼び出し時に汎用レジスタやセグメントレジスタの破壊がないか注意する.
  - 実機環境固有の BIOS 実装の違いによる挙動差異（特にキーボードバッファのオーバーランや文字コード変換）に留意.
