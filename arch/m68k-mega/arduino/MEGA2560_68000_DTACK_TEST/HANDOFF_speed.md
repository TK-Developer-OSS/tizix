# 引き継ぎ: m68k-mega 実機の高速化

## 0. 立ち位置

2026-09-21 時点で **M1(シェル起動)と M2(SD カード)は完了済み**。実機の生 MC68000 で
tizix が動き、SD の FAT を読み書きし、外部コマンドが SD から kexec されて走る。
**このタスクは「動くようになったものを速くする」フェーズ**であって、機能追加ではない。

> **2026-09-21 追記: 打ち手 A(ソフト最適化)は実施済み。速度は 2.6 倍になった。**
> `ls /bin` が 10.65 秒 → **4.2 秒**、バスサイクルは 121k/秒 → **318k/秒**。
> 数字と、5 章の地雷 4/5 がどう置き換わったかは **★9 章★** を先に読むこと。
> 以下の 4 章・6 章は着手前の見積りで、**支配項の特定を間違えていた**(9.2 節)。

旧記述: 1 コマンド約 15 秒。68000 換算で 100-200kHz 相当(本来 4MHz)。
ユーザーの表現は「Z80 8MHz より明らかに遅い」。実際そのとおりで、体感 15 倍遅い。

動いている証拠(この出力が出れば正常):

```
=== tizix 68000 host (Mega2560) ===
image = 23620 bytes -> 0x000000
SRAM transfer start
  verify OK=11810 NG=0
byte-lane verify (UDS / LDS separately)
  UDS NG=0  LDS NG=0
byte-write test @080000
  after even write = 5A00  (expect 5A00) OK
  after odd  write = 5AA5  (expect 5AA5) OK
SD init ... OK (SDHC/blk addr)  sec0: EB 3C 90 6D 6B 66 73 2E 66 61 74 00
  -> looks like a FAT VBR (correct: raw image)
---- release CPU ----
tizix
FAT Drive DETECTED
[/root]# ls /bin      → 81 ファイル
```

## 1. ハードウェア構成

生 MC68000(4MHz)+ Arduino Mega2560 がバスホスト。ROM は無く、Mega が起動前に
SRAM(AS6C4008 x2 = 1MB)へカーネルを書き込んでから CPU を解放する。
走行中は Mega が **/DTACK を返し、A20=1 空間の MMIO を代行する**。

```
アドレスマップ
  A20=0            SRAM 0x000000-0x0FFFFF(68000 が直結、Mega は /DTACK のみ)
  A20=1 かつ A23=1 UART 0x900000  偶数/UDS=DATA  奇数/LDS=STATUS
  A20=1 かつ A22=1 SD   0x500000  から plat.h の SD_* 6 本
  A20=1 かつ A21=1 ESP32 0x300000 (予約、未実装)

ピン(Mega 側)
  D0-7=PORTF  D8-15=PORTK  A1-7=PA1..PA7  A8-15=PORTC  A16-19=PL0..3
  A20=PL4  A21=PL5  A22=PL6  A23=PL7   ← PINL 1 回読みで A20-A23 が取れる
  /AS=PB6  /UDS=PE4  /LDS=PE5  R/W=PG5  /DTACK=PE3  /RESET=PB4  /HALT=PH6
  CLK=PB5(Timer1 OC1A, 4MHz)
  FC0=PH0 FC1=PD3 FC2=PD2   IPL0=PG0 IPL1=PG1 IPL2=PG2   /VPA=PJ0
  SD: SCK=D52 MOSI=D51 MISO=D50 CS=D53(Mega が駆動)

空きピン: D6 D7 D8 D13 D14 D16 D20(SDA,INT1) D21(SCL,INT0) D38
  ※ D22 は空きだが R1/R12 が付く。AREF は ADC 基準入力なので使えない
  ※ 68000 の A21=D44 / A22=D43 / A23=D42 は CPU の出力。Mega から駆動禁止
```

## 2. ファイルの場所

```
ファーム   d:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST\MEGA2560_68000_DTACK_TEST.ino
           (★tizix リポジトリの外)
rom.h      同フォルダ。arch/m68k-mega/rom.h(make で生成)をコピーして使う
クライアント 同フォルダ tizix.ps1 (115200 bps, ASCII のみで書くこと)
計測       同フォルダ bench.ps1 (コマンドごとの所要秒数を出す。9.7 節)
いま載っている HEX  %USERPROFILE%\fw_tizixhost_FAST_handshake.hex  ← 高速化版(推奨)
ロールバック用 HEX  %USERPROFILE%\fw_tizixhost_M2ok_115200.hex   ← 高速化前。動作確認済み
                    %USERPROFILE%\fw_busprobe.hex                ← SRAM 検査用の別ファーム
ソース退避 bk\ino_M2ok_20260921.ino_bk    ← 高速化前の .ino
tizix 本体 \\rocky9\tk\z80pack\tizix\arch\m68k-mega\  (= rocky9 の ~/...)
経緯の全記録 tizix の task.md #60
```

## 3. ビルド・書き込み・テストの手順

```powershell
# ビルド
Push-Location "D:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST"
.\build_and_flash.ps1 -BuildOnly
Pop-Location

# 書き込み(avrdude は $ErrorActionPreference に引っかかるので cmd /c 経由。
#            cmd は UNC カレントを嫌うので Push-Location C:\ してから)
Copy-Item "D:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST\build\firmware.hex" "%USERPROFILE%\fw_tizixhost.hex" -Force
Push-Location C:\
$av = "%USERPROFILE%\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17\bin\avrdude.exe"
$cf = "%USERPROFILE%\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17\etc\avrdude.conf"
cmd /c "`"$av`" -C `"$cf`" -p atmega2560 -c wiring -P COM3 -b 115200 -D -U flash:w:%USERPROFILE%\fw_tizixhost.hex:i 2>&1" | Select-String "verified|rror"
Pop-Location

# 起動ログの取得(ポートを開くと Mega がリセットされ最初から走る)
Push-Location "D:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST"
.\tizix.ps1 -Seconds 16 -Send "pwd`r" -After 15 -Log "%USERPROFILE%\log.txt"
Pop-Location
```

**COM3 はユーザーも TeraTerm で使う。** 掴む前に空いているか確認し、終わったら必ず離すこと。
ユーザーから「COM ポートが開放できたら教えて」と言われる。

```powershell
try { $sp=New-Object System.IO.Ports.SerialPort COM3,115200,"None",8,"One"; $sp.Open(); $sp.Close(); "free" } catch { "busy" }
```

カーネル側を触るなら rocky9 で `make -C arch/m68k-mega`。ssh は接続確立が数分ハングする
ことがあるので、スクリプトを共有(`\\rocky9\tk\tmp\run.sh`)に置いて 1 回だけ起動する。

## 4. 遅さの内訳(ここが本題)

`loop()` が 1 バスサイクルを処理するのに使っている時間の見積もり:

```
/AS 検出のポーリング          ~0.3us
IACK / A20 のデコード          ~0.6us
dtack_pulse (cli + 8nop + sei) ~0.9us
cycle_settle() の固定待ち      ★2.0us★   ← 最大の支配項
Arduino の loop() 復帰          ~0.3us
                        合計   ~4us/バスサイクル
```

68000 は 1 命令 3-5 バスサイクル → **1 命令 15us ≒ 65k 命令/秒**。

**未実測。まず数えること。** サイクル数カウンタを仕込んで「実際に何サイクル/秒出ているか」を
出してから最適化するのが正しい順序。ユーザーにもその順で提案してある(B → A → C)。

## 5. ★触る前に必ず読む: 既に踏んだ 6 つの地雷

この 6 つは全部実機で踏んで直したもの。**高速化で安易に戻すと同じ壊れ方をする。**

1. **SRAM 書込で /UDS,/LDS を必ず駆動する。** デコードが `CE# = A20|DS` なので DS を
   出さないと SRAM が選択されない(転送が全滅する)。
2. **前のファームで走行中の 68000 を載せたまま Mega だけリセットすると転送が全滅する。**
   クロックが止まった CPU がバスを掴んだままになる。`cpu_force_tristate()` が
   リセット中に 20ms だけクロックを与えて手放させている。消さないこと。
3. **/AS を見た瞬間に /UDS,/LDS を読むのは早すぎる。** 68000 は /AS を S2、DS を S4 で出す。
   早く読むと全部「偶数=UDS」に誤判定し、UART の DATA と STATUS が入れ替わる。
   → `wait_ds()` で DS を待ってから判定している。
4. **/DTACK を「/AS が戻るまで保持」にしてはいけない。** 落とすのが AVR の数命令ぶん遅れ、
   68000 が残った /DTACK で**次のサイクルを 0 ウェイト完走**させてしまい、Mega が
   そのサイクルを観測できない(コンソール文字が大量に落ちる)。
5. **逆に「/AS が H に戻るのを待つ」のも破綻する。** サイクル間の /AS=H は 1 クロック
   (4MHz で 250ns)しかなく、AVR のポーリング周期 ~190ns では一度逃すと
   **次サイクルを同一サイクルと誤認して、以後ずれ続ける自己維持ループ**になる。
   実測で 1 サイクル 0.7ms(本来の 1000 倍)まで落ちた。

   **★2026-09-21: この 4 番と 5 番は解消した。9.4 節を読むこと。**
   破綻の原因は「/AS を見たこと」ではなく (a) C で書いたポーリングが 310ns 周期で
   250ns の窓を跨いでいたこと、(b) 割り込みが入っていたこと、(c) 上限が 3000 回と
   長すぎたこと、の 3 つだった。8 回展開のアセンブラ(125ns 周期)+ cli + 上限 2us に
   したら取りこぼしは **0 回/4 秒**になり、いまは `/AS` の H を見てから `/DTACK` を
   落とす本来のハンドシェイクで動いている。`cycle_settle()` は廃止した。
   **ただし cli が前提。** `sei` のまま同じことをすると 4 番がそのまま復活する。
6. **DTACK パルスは cli/sei で囲む。** 割り込みでパルスが伸びると次サイクルまで完走
   してしまい 1 文字消える(`continuing` → `coninuing` で気付いた)。

## 6. 提案している打ち手

### B. まず実測(推奨・最初にやる)
バスサイクル数のカウンタを持たせ、1 秒ごと(あるいは N サイクルごと)に cycles/sec を出す。
**ただしコンソール出力に混ぜない工夫が要る**(シリアルは 68000 のコンソールと共用)。
`TRACE_BOOT` / `TRACE_REPEAT` と同じ流儀で `#define` で切れるようにするのが無難。

### A. ソフト最適化(2-3 倍が目標)
- **`cycle_settle()` の固定 2us を、`/DS` の立ち上がり観測に置き換える。**
  地雷 5 で破綻したのは `/AS` を見ていたから。**`/DS` なら窓が約 500ns ある**
  (S7 終わりで負けて、次サイクルの S4 まで上がったまま = 4 ステート = 2 クロック)。
  `/AS` の 250ns に対して倍の幅があるので、タイトなポーリングループで確実に捕まえられる。
  **ここが一番効く。**
- DTACK パルスを 8nop → 4nop へ。ウェイト中の 68000 は 250ns ごとに /DTACK を
  標本化するので、アサート幅は 250-375ns あれば足りる。
- `void loop()` の中を自前の `for(;;)` にする。Arduino の `main()` は毎回
  `serialEventRun()` を呼んでいるので、その分が丸ごと消える。
- デコードを `PINL` 1 回読みで済ませる(既にそうなっている)。IACK 判定の
  `IS_IACK()` は毎サイクル 3 ポート読むので、`PINL == 0xFF`(IACK では A16-A23 が
  全部 H)で代用できないか検討の余地あり。**ただし FC 線での判定のほうが正攻法**。

### C. ハードウェア(20 倍。本命)
**A20=0 のとき /DTACK を自動で返す回路を足す。** SRAM アクセスが Mega を経由しなく
なるので 4MHz フルスピードで走る。Mega の出番は UART と SD だけになる。

```
74HC74(D-FF 2個)で /AS から 1-2 クロック遅らせて /DTACK を生成
  ・A20=1 のときは出さない(そこは Mega の担当)
Mega の /DTACK と衝突させないため 74HC08(AND)で束ねる
  ・負論理なので AND = どちらかが L なら出力 L
```

**チップ 2 個。CPLD を持ち出す必要はない。** ユーザーは「そのうち CPLD にして
スタンドアロン化」と言っているが、いますぐの速度改善はこれで足りる。
実装するなら回路をユーザーと詰めてから。ハンダはユーザーの担当。

## 7. 成功判定

- `ls /bin` が 81 ファイルを最後まで正しく表示する(文字落ちゼロ)
- `echo hello > /root/x` → `cat /root/x` が一致する
- 起動時の `verify OK=11810 NG=0` と `UDS NG=0 / LDS NG=0` が維持されている
- `g_stuck` が増えていない(増えていたらタイミングを壊している)
- そのうえで 1 コマンドの所要時間が短くなっている

**壊したら 2 段階で戻せる。**

- 高速化版に戻す … `%USERPROFILE%\fw_tizixhost_FAST_handshake.hex`
- 高速化前に戻す … `%USERPROFILE%\fw_tizixhost_M2ok_115200.hex`

2026-09-21 の高速化版は上の 5 項目を全部満たしている(`ls /bin` 81 ファイル、
`echo hello-68k > /root/x` → `cat` 一致、`verify OK=11810 NG=0`、
`UDS NG=0 / LDS NG=0`、`stuck=0`、加えて `late=0`)。

## 8. その他の申し送り

- シリアルは **115200 bps**。1Mbps だと TeraTerm(Win16 由来)が化ける。
  16MHz AVR での 115200 は誤差 +2.1% だが実測で化けていない。
  それでも不安定なら **250000 か 500000 が 16MHz では誤差ゼロ**。
- ファームのシリアル出力は **ASCII のみ**にすること(日本語は端末で化ける)。
  `tizix.ps1` も ASCII のみ(PS5.1 が BOM 無し UTF-8 を cp932 で読むため)。
- **rocket68(m68ksim)は奇数番地 word アクセスのアドレスエラーを再現しない。**
  整列バグは実機で初めて出る。保険として crt0.s に例外レポータを入れてあり、
  `*** EXC nn PC=xxxxxxxx HALT ***` を UART へ直書きして停止する。
- タイマは Mega Timer5 の 1Hz(カーネルも `TICK_HZ=1`)。プリエンプション粒度が
  1 秒。SD が入ったので 100Hz 化を検討してよいが、IACK が毎秒増えるぶん
  速度には逆風。高速化が済んでからにすること。
- plat.h の LBA は 24bit = **8GB が上限**。今刺さっている 8GB SDHC は
  15,601,664 セクタでぎりぎり収まる。32GB 以上は上位が落ちて誤読する。
- `df` は m68k 用にビルドされておらず `not found`(#56 で外部化した分)。異常ではない。
- SRAM 基板を疑いたくなったら `fw_busprobe.hex` を焼いて `mt` / `fm` / `rl`。
  ただし**基板は 2026-09-20 に健全性を確認済み**(全域 1MB マーチ errors=0)。
  疑う順番を間違えないこと ── 今日の不具合は 6 件とも Mega 側のソフトだった。

## 9. 2026-09-21 高速化の結果(打ち手 A 完了)

### 9.1 数字

| | 旧(M2 完了時) | 新 | |
|---|---|---|---|
| バスサイクル/秒 | 121,533 | **318,000** | 2.6 倍 |
| 1 バスサイクル | 8.2us | **3.1us** | |
| `ls /bin` (81 ファイル) | 10.65 s | **4.2 s** | 2.5 倍 |
| `pwd` | 1.95 s | 0.8 s | |
| `/AS` 取りこぼし | (計測せず) | **0 回/4 秒** | |

計測は `bench.ps1`(後述)。`TRACE_PERF 1` にすると 4 秒ごとに
`[perf cyc=... ms=... cps=... stuck=... late=...]` を吐く。既定は 0。

### 9.2 遅さの内訳は 4 章の見積と違った

逆アセンブルで数えた実測値(16MHz = 1 サイクル 62.5ns):

```
  serialEventRun()          30 cyc  1.9us  ← 最大。Arduino の main() が毎回呼ぶ
  (++g_hk & 0xFF) の判定    13 cyc  0.8us  ← 16bit カウンタを毎回 load/store
  dtack_pulse (call/ret 込) 25 cyc  1.6us
  cycle_settle()            13 cyc  0.8us  ← ★2us ではなかった
  IACK デコード (FC 3 ポート) 5-8 cyc 0.4us
  A20 デコード               5 cyc  0.3us
                       合計 ~123 cyc 7.7us  (実測 8.2us)
```

**4 章の「支配項は cycle_settle() の 2us」は誤り。** `delayMicroseconds(2)` は
定数でインライン展開されると Arduino 側の呼び出しオーバーヘッド補正が消えて
13 サイクル(0.8us)にしかならない。本当の支配項は `serialEventRun()` だった。
これはスケッチが `serialEvent()` を定義していなければ何もしない関数だが、
4 本ぶんの NULL 比較が残っていて call/ret 込み 30 サイクルある。

### 9.3 やったこと

1. **`void loop()` の中身を自前の `for(;;)` にした。** これだけで serialEventRun が
   丸ごと消える(-1.9us)。
2. **IACK 判定を A20=1 の側へ移した。** IACK サイクルは A4-A23 が全部 H なので
   必ず A20=1 に落ちる。SRAM アクセス(全体の圧倒的多数)が FC 線を読まなくなる。
3. **定期処理の駆動を「loop() の回数」から「サービスしたバスサイクル数」へ。**
   `#define SVC_N 64` の 8bit カウンタ。ホットパスは `dec`+`brne` の 2 サイクル。
4. **`/AS` の監視を 8 回展開のインラインアセンブラにした**(`as_wait_high` /
   `as_wait_low`)。1 標本 2 サイクル = **125ns 周期**。C で書くと dec/brne が
   付いて 310ns 周期になり、サイクル間の `/AS=H` の 250ns を跨ぐ ── これが
   地雷 5 の正体の半分。もう半分は割り込みなので `cli` で囲む。
5. **★`/DTACK` を固定幅パルスから本来のハンドシェイクに戻した。**
   アサート → `/AS` の H を観測 → ネゲート。これが一番効いた(下記)。

### 9.4 ★地雷 4/5 はこう置き換わった(ここが本題)

5 章の 4 番(パルスにしろ)と 5 番(`/AS` の H を待つな)は、**監視ループが
遅かった時代の対処**であって、125ns 周期・割り込み禁止で見られるように
なった今は逆に足を引っ張っていた。

固定幅パルス方式で何が起きていたか、計器を入れて分かったこと:

- `/AS` の H を観測できずタイムアウトする事象が **MMIO サイクルに集中**
  していた(SRAM 92 回/4 秒 に対し MMIO 15,327 回/4 秒)。
- 機序: MMIO は `wait_ds()` と装置処理のぶん `/DTACK` を打つのが遅く、
  そのとき 68000 は既に標本点で待っている。だから **500ns のパルスを
  出している最中にサイクルが終わり、`/AS` が H → L と往復して次のサイクルまで
  始まってしまう**。監視を始めたときには既に次のサイクルの `/AS=L` で、
  H を一度も観測できない。
- つまり「取りこぼし」はエラーではなく **速すぎて見逃していた** だけ。
  だから待ち時間を伸ばすと悪化した(AS_SETTLE を 3us→10us にしたら
  270k → 104k cps に落ちた)。

ハンドシェイクに戻すと、この曖昧さが原理的に消える:

```
cli();
PORTE &= ~DTACK_MASK;                 /* アサート */
as_wait_high(AS_SETTLE);              /* サイクル終了を観測 */
PORTE |=  DTACK_MASK;                 /* それから落とす */
sei();
```

地雷 4 が心配した「次のサイクルが残った /DTACK で 0 ウェイト完走する」は、
タイミングを数えれば起きない:

- `/AS` の H を観測してからネゲートまで **3 命令 = ~190ns**
- 次のサイクルが `/AS` を下げるのは H から **250ns 後**(S0,S1)
- その最初の `/DTACK` 標本点はさらに **375ns 後**
- → 60ns 以上の余裕を残して確実に落ちている(実測 late=0)

**割り込みを禁止していることが前提。** `sei` していると ISR 一発で 190ns が
数 us になり、地雷 4 がそのまま復活する。

### 9.5 触っていないもの

- `wait_ds()`(地雷 3)、`cpu_force_tristate()`(地雷 2)、SRAM 書込での
  `/UDS,/LDS` 駆動(地雷 1)は一切変えていない。
- `AS_GUARD 3000` も `wait_ds()` 用にそのまま。
- tizix 側(カーネル)は 1 行も触っていない。rom.h も M2 完了時のまま。

### 9.6 次にやるなら

- **打ち手 C(74HC74 + 74HC08 で A20=0 の /DTACK を自動生成)は据え置き。**
  今 3.7us/サイクルのうち 68000 自身の最短バスサイクルが 1us なので、
  ハードで SRAM を Mega から外せばまだ 3 倍前後ある。
- ソフトで残っているのは細かい所だけ(`Serial.available()` の仮想呼び出し、
  MMIO 読みの `availableForWrite()`)。1 サイクルあたり 0.2-0.3us 程度で、
  費用対効果は C に劣る。
- タイマを 1Hz → 100Hz にする件は、速度が 2.2 倍になったので再検討してよい。

### 9.7 計測ツール `bench.ps1`

```powershell
Push-Location "D:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST"
.\bench.ps1 -Cmds "ls /bin","ps" -Log "%USERPROFILE%\bench.txt"
Pop-Location
```

ポートを開いて起動ログを拾い、プロンプト `]# ` を検出してから各コマンドを
送り、プロンプトが返るまでの秒数を出す。終わると必ずポートを閉じる。

### 9.8 追い込み: `always_inline`(2026-09-21 夕)

`dtack_cycle()` は `static inline` と書いてあっても `-Os` では関数のまま出ていて、
3 箇所から `call` されていた(逆アセンブルで確認)。ATmega2560 の `call`+`ret` は
**10 サイクル = 0.625us**。`__attribute__((always_inline))` を `dtack_cycle` /
`as_wait_high` / `as_wait_low` に付けて完全インライン化した。

```
270,000 cps -> 318,000 cps   (+18%)
ls /bin  4.5s -> 4.2s
```

**教訓: このホットパスでは `inline` は宣言しただけでは効かない。**
逆アセンブルして `call` が残っていないか必ず見ること。

### 9.9 「昔は PWM 1 パルスで 8MHz 出ていた」の調査結果

`bk\` の全バックアップと git の全コミットを調べた結果:

| | クロック | /DTACK の出し方 | digitalWrite |
|---|---|---|---|
| `コピー.ino__bk`(最古) | `OCR1A=1` = **4MHz**<br>(`OCR1A=0` はコメントアウト) | `PORTE` 直叩き + nop×6 | `M68K_RESET` のみ 3 箇所 |
| `コピー.ino_bk2` | `OCR1A=0` = **8MHz** | `PORTE` 直叩き | `M68K_RESET` のみ 1 箇所 |
| `コピー.ino_bk3/bk4` 以降すべて | 4MHz | `PORTE` 直叩き | 無し |

- **`/DTACK` が `digitalWrite` になっていた事実は無い。** 全世代で `PORTE` 直叩き
  (= `cbi`/`sbi` 2 サイクル)。`digitalWrite` が使われていたのは `/RESET` だけ。
- **`/DTACK` をタイマ/PWM で出していた世代も無い。** Timer1 の PWM は CLK 用。
- **8MHz 時代は実在する**(`bk2`)。ただしその世代は外部 SRAM ではなく Mega 内部の
  `ram[4096]` を叩いており、`loop()` は `digitalRead(M68K_AS)` で /AS を見ていた。

**★ここが肝心: クロック周波数と実効速度は連動しない。**
/DTACK を Mega が返す以上、1 バスサイクルの長さは「Mega が返すまでの時間」で決まる。
8MHz にしても 68000 のウェイトステートが増えるだけで、バスサイクル/秒はほぼ変わらない。
速くなるのは 68000 の内部サイクル(バスを使わない部分)だけ。

**そして 8MHz には現設計と噛み合わない問題がある。** サイクル間の `/AS=H` の窓が
250ns → **125ns** に縮む。いまの監視は 2 AVR サイクル = 125ns 周期なので、
窓と標本周期が同じになり取りこぼしが出る(= 9.4 節のハンドシェイクが崩れて
固定パルス時代に逆戻りする)。**8MHz を試すなら監視の作り直しがセット。**
加えて載っているのが 68000**P8** なら 8MHz は定格外。チップの刻印を確認のこと。
