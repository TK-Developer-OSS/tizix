# 鉄のおきて

この項目はgemini用なので、claudeはスルーでよい => 様子見

レート制限で全く進まなかうなるので、１アクションごとStart-Sleep 10を5回実行せよ。
他のコマンドとまとめてやるとタイムアウトするので、Start-Sleepだけのアクションを毎回
ストライプ式にはさめ。これをやらない場合は暴走とみなす。

Start-Sleepがタイムアウトになるのは、時間をあけるという目的が達成できているので、
依頼者としては全く問題ない。

作業コマンドでレート制限にかからないことが目的。


例：

Start-Sleep -s 10; Start-Sleep -s 10; Start-Sleep -s 10; Start-Sleep -s 10; Start-Sleep -s 10;


# プロジェクト概要

Z80ターゲットのC言語クロスコンパイラを新規開発する
サブセットに割り切り、ANSI Cの準拠は追わない

バックエンドは3系統: Z80(`generator.c`) / Z80 Tizix向けIY相対PIC(`--tizix-user`,`tizix.c`) /
x86-64(`gen_x86.c`, `--march=x86`)。x86 は将来のセルフホスト（tzcc で tzcc をビルド）用。

stdio.h string.h stdlib.h stdint.hをだいたいでいいので実装する

libcはposixを追わない　サブセットとして割り切り


# 実行環境

x86をホストとし、Rocky Linux 9を開発環境として与える

ターミナルから書きコマンドでRocky Linux 9に入れるので使用を許可する

ssh -i <秘密鍵> <ユーザー>@<ビルドホスト>

\\rocky9\tk\z80pack\tzcc
と
~/z80pack/tzcc/
は同じ領域をみている


## 動作環境の移行: CP/M 2.2 → お手製OS Tizix (2026/08/29)

CP/M 2.2 は卒業。次の動作環境は自作OS **Tizix**（`~/z80pack/tizix/`）。

- **disks/ を差し替え済み**（コピー元 tizix 側は 2026/08/29 に `../tizix/arch/z80pack/disks/` へ移動）:
  - `disks/drivea.dsk`  ← `../tizix/arch/z80pack/disks/drivea.dsk`（Tizix ブート floppy: boot.s + kernel.bin）
  - `disks/driveb.dsk`  ← `../tizix/arch/z80pack/disks/driveb.dsk`（FAT12。ユーザ `.BIN` を置く領域）
  - `disks/drivea.dsk_cpm_bk` … 旧 CP/M イメージ（テスト COM 入り）を退避
  - `disks/drivea.dsk_bk`     … さらに古い無改変 CP/M イメージ（08/26）
  - ※ tzcc 側 `disks/` を `arch/z80pack/disks/` へ揃えるかは保留（`make clean` が
    `rm -rf arch/z80pack` なので、揃えるなら clean を disks 温存にする必要あり）。
- `cd tzcc && ./cpmsim -z` で **Tizix が起動**（`# ` プロンプト。`FAT Drive DETECTED` /
  `System Driver LOADED` を確認済み）。同じ z80pack cpmsim バイナリ。
- **注意**: 旧 `make com` / `make cpm` は drivea.dsk を CP/M 前提で扱う（`cpmcp -f ibm-3740`）。
  今の drivea.dsk は Tizix なので **`make com` は実行するとブート floppy を破壊する**。
  Tizix 用ターゲットへの置き換えが必要（下記）。
- `make run`（`cpmsim -z -x output.ihx` ベアメタル）は disks を触らないので従来どおり使える。

### 【tzcc の存在意義】Tizix の IY 相対 位置独立コード(PIC)をネイティブ生成する

> **これが「SDCC があるのに tzcc を新規開発する」核心の理由。**
> C言語機能の充実と並行で、ここを最重要目標として進める。

#### Tizix (Z80) のプロセスモデル

- ユーザプロセスは **IY レジスタを一切操作しないように**コンパイルされる。
- カーネルはプロセスをロードする際 **IY = そのプロセスのベースアドレス**をセットする
  （`kexec.c`: 最初のユーザプロセスは block2 = `0xA000`、以降 0xB000…。ctx[0]=base を
  `pop iy` でセットしエントリ `base+0x20` へ `reti`）。
- プログラムは `sdldz80 -b _CODE=0x0000` でリンクされ、**全ラベルはセグメント先頭からの
  オフセット(0基準)**。実行時に **`jp` / `call` など動的アドレスが生じる箇所すべてで
  IY(base) を加算した値を飛び先にする**。これで再配置パッチ無しに任意アドレスへロード可能。
- **これがプリエンプティブ・マルチタスクの実現手段**（各プロセスを好きな空きブロックへ
  置ける。ロード時/実行時のアドレス書き換えが不要）。

#### 現状 Tizix は「SDCC + バイナリ(asmテキスト)パッチ」で凌いでいる

`../tizix/user/iy_reg_claude.py` が SDCC の `.asm` 出力を変換する。要点（＝tzcc が
ネイティブに出すべきコード）:

| 元の命令 | 変換後（`ind`=インデント, `L_skip`=採番ラベル） |
|---|---|
| `ld hl,#LBL` （LBL=セグメント内ラベル） | `ld hl,#LBL` の直後に **HL += IY**：<br>`push af / push de / push iy / pop de / add hl,de / pop de / pop af` |
| `ld de,#LBL` | 直後に **DE += IY**：`push af / push hl / push iy / pop hl / add hl,de / ex de,hl / pop hl / pop af` |
| `ld bc,#LBL` | 直後に **BC += IY**：`push af / push hl / push iy / pop hl / add hl,bc / ld b,h / ld c,l / pop hl / pop af` |
| `jp LBL` | **間接JP**（HL 保存, SP 平衡）：<br>`push hl / push af / push de / ld hl,#LBL / push iy / pop de / add hl,de / pop de / pop af / ex (sp),hl / ret` |
| `jp cc,LBL` | `jr ncc,L_skip`（cc が z/nz/c/nc）or `jp inv,L_skip`（po/pe/p/m）→ 間接JP列 → `L_skip:` |
| `call LBL` | **間接CALL**（HL は caller-saved なので潰してよい）：<br>`push af / push de / ld hl,#LBL / push iy / pop de / add hl,de / pop de / pop af / call ___sdcc_call_hl` |
| `call cc,LBL` | `jr ncc,L_skip` / `jp inv,L_skip` → 間接CALL列 → `L_skip:` |

**触らないもの**（base 非依存 or 固定番地）:
- `jr` / `djnz`（相対分岐）、`jp (hl)` / `jp (ix)` / `jp (iy)`、`ret` / `reti`
- 数値即値 `ld hl,#100` / 文字定数 `#'A'`
- **固定番地の外部シンボル**（Makefile の `-g` で束縛）:
  `___sdcc_call_hl`=0x50, `_kexit`=0x3B, `_kputchar`=0x3E, `_kgetchar`=0x41,
  `_getticks`=0x44, `_time_get`=0x4A, `_f_open/_f_read/_f_write/_f_close/_f_lseek/_f_sync`,
  `_drv_tbl`=0x9000。→ これらへの `call` は素通し（IY 加算しない）。
- `___sdcc_call_hl`（0x50）は「`jp (hl)`」相当のカーネル小ルーチン。間接CALLの実体。

#### tzcc がやること（＝「無理なバイナリ操作を不要にする」）

`--tizix-user` フラグで generator.c が **最初から上記の IY 相対形**を吐く。後処理スクリプト
（iy_reg_claude.py）を通さない。tzcc は自前 codegen なので:
- `ld hl,#var_X` / `#str_N` / 内部ラベル → +IY 列を直後に付ける（数値・外部固定シンボルは付けない）
- `jp` / `jp cc`（if/while/for の分岐）→ 間接JP列。`jr z/c` の比較ヘルパは短距離ローカルなので素通し
- `call _userfunc` / `call _libc`（crt0_tizix 内の printf 等も同一セグメント）→ 間接CALL列
- カーネル syscall（`call 0x3E` 等）は crt0_tizix.s 内に手書き、素通し
- `ret` は素通し（間接CALL が積む戻り番地は実アドレスなので正しく戻れる）

#### 実装フェーズ

1. **【済】generator.c `--tizix-user`（実体は `tizix.c` の後処理変換）** … tzcc 生成 `.s` を
   上表の IY 相対形へ in-place 変換（`tizix_iy_transform`）。`iy_reg_claude.py` の検証済み
   シーケンス移植＋tzcc 固有の絶対データアクセス（`ld (var_X),a` 等）も IY 相対化。
   後処理だが **tzcc 内蔵・1ツール・SDCC/外部パッチャ不要**（＝存在意義を満たす）。
   将来は生成時ネイティブ化（案B: ローカルを ix フレームへ）で更に締める。
2. **【済】`crt0_tizix.s`（bring-up 版）** … `_start`＠0x20（`crt0cmd.s` 同型: IY ベースで
   SP/argv、`main` を IY 相対で呼び、戻りは `jp _kexit`）＋ `_putchar`/`_putc`/`_getchar`
   （`call _kputchar`(0x3E) 素通し。内部 call/jp/データラベル無しで IY 加算不要）。
   `_puts`/`_printf`/string 系は後続。
3. **【済】`make tizix TEST_SRC=…`** … `./tzcc --tizix-user` → `sdasz80` →
   `sdldz80 -n -i -b _CODE=0x0000 -g ___sdcc_call_hl=0x0050 -g _kexit=0x003B -g _kputchar=0x003E -g _kgetchar=0x0041`
   （crt0_tizix.rel + prog.rel）→ `makebin -p` → `.BIN` → `mcopy -o` で driveb.dsk(FAT12) へ →
   `./cpmsim -z` 起動 → シェル `# ` で `NAME`。
   ※ `-g sym=addr` は「どこかで `.globl`(import) された symbol」にしか効かない。未参照を
     並べると `No definition of symbol`。
4. **以降、C言語機能は Tizix 実機（cpmsim -z）で動作確認する。** → 追記12 参照。

#### Tizix 外部コマンドの他仕様（参考）

- 固定エントリスタブ: `.BIN` 先頭 0x00-0x1F は予約、`_start`=0x0020。
- `.BIN` サイズがブロック(0x1000)超で nblk=2、0x2000 超で 3。SP は
  `base + nblk*0x1000 - 0x110`、argv 文字列は `base + nblk*0x1000 - 0x100`。
- 実行: Tizix シェル `# ` で `name`（`kexec_file("NAME.BIN", arg)`）。


# 進行ルール

タスク一覧の乱に書いてあるタスクを１個ずつこなす

レート制限にかかりがちなので、時々Start-Sleep -s 60かlinuxでsleep 60いれて
60秒活動して60秒休むくらいのペースでないとエラーでタスクが進まない
夜中に寝てても進捗してほしいので、スリープをかならずいれること



# タスク一覧　※ 対応済になったら【済】を入れる

・【済】字句解析を実装し、ターゲットソースを故意にくずしてシンタックスエラーを検知し、どこが悪いかおおまかにエラー標示すること
　gcc2.96風など簡易でよい

・【済】構造木を生成しアセンブルできる手前までもっていくこと

・【済】crt0を仮で作る。標準ライブラリに代わるもので必要そうなものをとりあえず空でいれておき
　sdasz80が動作するようにする

・【済】出力ファイル名は、ファイル名.cより前+拡張子.s

・【済】make runで、cpmsimでバイナリが実行できるようにせよ

・【済】char型 char型静的配列に対応せよ

・【済】char型ポインタに対応せよ

・【済】Linux ~/z80pack/tizix いかからC言語で使われている関数の洗い出し

・【済】前項の実装

・【済】関数および引数、戻り値
・【済】コマンドライン引数（mainのargc/argv引数およびcrt0ダミー初期化）に対応

・【済】よくつかうファイル操作


・【済】四則演算

・【済】よくつかう標準関数

・【済】標準入力、出力、エラーに対応する（`crt0.s` の `_stdin`/`_stdout`/`_stderr` と
　`_fgetc`/`_fputc` のストリーム判定。`tests/ok_14.c` を CP/M 2.2 実機で確認済み）





・【済】ソケットに対応する　ただし実装自体はユーザーが_socketを実装しないとsocket()を実行したらエラーにする
　現時点では_socketのダミーとしてi/oポート1番のuartに接続で作っておく
　→ 詳細は末尾「完了報告 (2026/08/28) 追記6」。


・【済】includeの外部ファイルに対応 . tests/以下のファイルの*.hが見当たらないので

・【保留】Tizix向けの実装として、--tizix-user オプション => jp/callの動的アドレスジャンプ命令の際に、iyレジスタをベースアドレスにしてアドレス計算した先に移動。retの戻り先をスタックに詰む際もアドレス計算する。
　→ 現状 tzcc は間接 jp/call を一切生成しない（全て `call _name` の静的リンク解決、関数ポインタ未対応）。
　　この最適化は「関数ポインタ / 間接呼び出しの codegen」（Tier4）が入って初めて意味を持つため、
　　そちらが前提。先行して flag だけ用意しても実効がないので、関数ポインタ実装とセットで対応する。

・【最重要・Phase1-3済 / 追記12】tzcc を Tizix ターゲット対応にする（`--tizix-user` IY相対PIC / `crt0_tizix.s` / `make tizix`）。
　→ tzcc を新規開発する核心の目的。詳細は上「### 【tzcc の存在意義】」節。
　　`make tizix TEST_SRC=…` で `.BIN` 生成 → `./cpmsim -z` の Tizix シェル `# ` で実行。実機動作確認済み。
　　残: `crt0_tizix.s` に `_puts`/`_printf` 追加 / 案B（ローカルを ix フレーム化して codegen を締める）。

・【済】コンパイル引数に対応 -oでファイル名指定 -s アセンブラコードのみで終了 -Xで標準出力にアセンブラコードを出して終了（追記8）


・【済】最後のほうのタスクtZccをスタティックリンクにし、x86 linuxならどこでもうごくようにする
　→ rocky9 に `glibc-static` 導入済み。Makefile の `STATIC` プローブが検出し `tzcc` は
　　スタティックリンク（`file` → "statically linked"）。任意の x86 Linux で動く（追記8, 13）。


## 【済 2026/08/28】crt0 のファイル I/O が CP/M 上で実動作しない

`_fputs` がコンソールに漏れる／`fopen("a.txt","w")` が拡張子なし `A` を作る、他。
`crt0.s` のみの修正で解決し、CP/M 2.2 実機で書き込み・読み戻しを確認済み。
→ 詳細は末尾「完了報告 (2026/08/28) 追記4」。


## 【済 2026/08/28】ローカル/静的配列のコード生成が壊れている

→ 対応完了。詳細は末尾「完了報告 (2026/08/28) 追記5」。
`char buf[N];` が `.ds N` で確保され、配列変数は一律アドレス渡し（`ld hl, #var_name`）に。
`tests/ok_16.c` を `fgets` 版へ戻し、CP/M 2.2 実機で `File IO Test` の書き込み→読み戻しを確認。

`tests/ok_16.c`（ファイル I/O 読み戻し）を `fgets` で書こうとして判明。
`make test` はコンパイラ終了コードしか見ないため露見していなかった
（`ok_10.c` / `ok_12.c` はリンクが通るだけの**偽陽性**。CP/M 実機では未動作）。

現象（`tests/ok_10.s` を見ると分かる）:
- `char buf[32];` を宣言しても `_DATA` に `var_buf: .db 0`（1 バイトのみ）しか出ない。
  → `fgets(buf,...)` 等が隣接メモリ（0x0000 の warm-boot ベクタ等）を破壊して暴走。
- 配列変数を関数引数に渡すと `ld a,(var_buf) / ld l,a / ld h,#0`
  （＝先頭 1 バイトの**値**をポインタとして push）になる。正しくは `ld hl, #var_buf`。
- `char src[] = "CopyTest";` の初期化子（文字列）も丸ごと捨てられ、`str_...` すら出ない。

原因の当たり:
- `generator.c` の `NODE_ARRAY_DECL` / `emit_data` が配列長・初期化子を見ずに
  スカラーと同じ 1 バイト確保をしている（tier2 課題 5「`int` を 2 バイト化」と同じ場所）。
- 引数渡しの分岐は `generator.c` の `NODE_CALL` 内。`find_sym_type()` が配列（type 2）を
  返せば `ld hl, #var_%s` を出すコードは既にある（`is_std_stream` 判定の近く）。
  ローカル宣言時にシンボルを type 2 で登録できていないのが要因と思われる → `parser.c` の
  宣言解析まわりを確認。

やること:
1. `char name[N];` を `_DATA` に `.ds N` 確保。`char name[] = "..."` は
   文字列長+1 を `.ascii` / `.db 0` で確保。
2. 配列変数の参照・引数渡しを一律アドレス（`ld hl, #var_name`）へ。
3. `parser.c` の記号表にローカル配列を type 2 で登録。
4. **テスト必須**: `tests/ok_16.c` を `fgets` 版に戻し（旧版は git 履歴 or 追記4 参照）
   `make com TEST_SRC=tests/ok_16.c` → CP/M で `OK16` 実行 → コンソールに
   `File IO Test` が出ること。`ok_10.c`（strcpy/strcat）/ `ok_12.c`（memset/memcpy）も
   CP/M 実機で出力確認する。
5. tier2 課題 5「`int` 2 バイト化」と同時にやると `emit_data` を一度触るだけで済む。

（`.COM` ビルド手順は本 readme「CP/M 2.2 上で実際に走らせる」節を参照）


## 言語機能の未実装（Cコンパイラとして必須級）※ 致命度順

・【済】1. コメント対応 `/* */` および `//` を lexer でスキップする（`lexer_next_token` の前処理ループで対応。`tests/ok_15.c` 追加）
・【済】2. 比較演算子 `== != < > <= >=` と 単項 `!` `-` を lexer + parser に追加（追記7。`tests/ok_18.c`）
・【済】3. `if` / `else` の構文解析と codegen（追記7。ラベル採番 `Lelse%d`/`Lend%d`。`tests/ok_19.c`）
・【済】4. `while` / `for` の実装（追記7。`tests/ok_20.c` / `ok_21.c`）
・【済】5. `int` を2バイト化（追記9。`Node.base_type` で char/int を区別。`.dw` 確保 +
  16bit ロード/ストア + 引数/戻り値の 16bit 受け渡し。`tests/ok_22.c` / `ok_23.c`）

### さらに後段（Tier 3 / 4）
・【済】配列添字 `a[i]` の実行時読み書き、`*p` デリファレンス / `&x` アドレス取得（追記14。
  `tests/ok_12.c`（`buf[9]='\0'` が効くように）/ `ok_26.c` / `ok_tz3.c`。Tizix 実機でも確認）
・【済】`&&` `||`（短絡）、`++` `--`（前置/後置）、複合代入 `+= -=`（追記15。
  `tests/ok_27.c` / `ok_28.c`。Tizix 実機でも確認）
・【済】`#define` オブジェクト形式マクロ（追記16。`tests/ok_29.c`。マクロ入れ子可。関数形式は無視）
・【済】ビット演算 `& | ^ ~ << >>`（追記16。`tests/ok_30.c`。Tizix 実機でも確認）
・【済】`enum`（追記16。`tests/ok_31.c`）
・【済】`struct` / `union`（union は struct 同扱い）/ `typedef` / `sizeof` / メンバアクセス `-> .`
  （追記17。全メンバ 2バイト固定のサブセット。`tests/ok_32.c`。Tizix 実機でも確認）
・【済】`long` / `short` / `unsigned` / `signed` / `size_t` 等の型修飾子（追記18。int/char 幅にマップ）
・【済】連鎖メンバアクセス `a->b->c`（追記18）
・【済】`switch` / `case` / `default` / `break` / `continue` / 三項 `?:`（追記19。`tests/ok_35.c`）
・関数ポインタ、多次元配列、キャスト `(type)e`、`malloc`（crt0） ← 次（セルフホスト向け）
・struct 未対応: char スカラーメンバ（全て2バイト扱い）/ 配列メンバ / ネスト値メンバ /
  関数引数・戻り値での struct 値渡し / `(*p).m`（`p->m` を使う）
・未対応: `*p` の int* / 多次元 / `*(p+i)`（`*ident` の1バイト r/w のみ）、
  `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`、`#if`/`#ifdef`、関数形式マクロ、
  式文としての `f();` 以外の副作用式、三項演算子 `?:`


## 組み込みマクロ `CALLI` / `FNADDR`(間接呼び出し。2026-09-25 tizix #70)

ANSI の関数ポインタ(宣言子のパースと型検査)ではなく、コード生成だけの組み込み。
**型検査は無い**(引数の個数・型を間違えても素通り)。

```c
unsigned fp = FNADDR(f_twice);        /* 関数の絶対番地(--tizix-user では +IY 済み) */
unsigned r  = CALLI(fp, 21);          /* 絶対番地を呼ぶ。引数は普通の呼び出しと同じ積み方 */
tbl[i] = FNADDR(f_sq);  CALLI(tbl[i], 5);   /* 実行時ディスパッチ表 */
```

- `FNADDR(name)` は `ld hl, #_name` を吐くだけ。--tizix-user では tizix.c が +IY するので
  **絶対番地**になる(リンク時オフセットを変数に持たせると「配列アドレスの代入は +IY
  されない」穴と同じ形になるので、絶対番地で返す)。
- `CALLI(addr, ...)` は引数を積んだ **後に** 呼び先を hl へ評価し、`call ___sdcc_call_hl`
  (`jp (hl)`)。tizix では 0x0050 のカーネルベクタ、CP/M では crt0.s に実体。
  呼び出し後の IX の張り直しは tizix.c が他の call と同じく入れる。
- 既存のコード生成は不変(33 コマンドの .s を変更前後で比較して一致。文字列ラベルの
  番号はノードのアドレス由来で実行ごとに変わるので、振り直してから比べる)。
- 検証: tizix の `user/calli.c` + `python/test_calli.py`(0 / 1 / 3 引数、入れ子、表経由)。


# テスト方法

## `make test` — コンパイル可否だけの軽いテスト
`tests/*.c` を tzcc に通し、終了コードだけを見る（実行はしない）。
- `ok_*.c` / `test_*.c`: 正常終了（コード0）を期待
- `err_*.c`: エラー検出（コード1）を期待

## `make runtest` — cpmsim 実機で実出力まで比較【推奨】
`tests/run_rt.sh`。各 `ok_*.c` を tzcc→sdasz80→sdldz80（ベアメタル `_HEADER=0x0000
_CODE=0x0020`）→ `cpmsim -z -x` で実行し、`Booting...` と `System halted` の間の
プログラム出力を **`// EXPECT:` 注記と照合**する。

テストファイル内の注記（ファイル内のどこでも可。末尾コメントで良い）:
```
// EXPECT: <期待する1行>      複数書けば複数行
// EXPECT-EMPTY               出力なしを期待
// RUN: skip <理由>           実機実行しない（file-io / needs-stdin など）
```
- `make runtest`            … 全 `ok_*.c`
- `make runtest T="ok_18 ok_20"` … 指定分のみ
- 現状: PASS=21 / SKIP=6（`ok_09/13/16/24`=BDOSファイルI/O、`ok_12`=`a[i]`書き込み未実装、
  `ok_14`=stdin 要）。file-io 系は `make com`（CP/M）、Tizix 系は `make tizix` で個別確認。

`tests/ok_01.c` をパターンごとに増やすこと（**新規テストには `// EXPECT:` を付ける**）。


## CP/M 2.2 上で実際に走らせる (.COM 実行)

`make run` は `./cpmsim -z -x output.ihx` でベアメタル実行（0x0000 起動、シミュレータの
halt ポートで終了）。これとは別に、生成コードを CP/M 2.2 の `.COM` にして A> から
起動する経路を用意した。

```
make com TEST_SRC=tests/ok_01.c   # OK01.COM を作り disks/drivea.dsk に転送
make cpm                           # ./cpmsim -z で CP/M 2.2 にログイン (A> が出る)
                                   #   A>OK01   -> プログラム実行
                                   #   A>BYE    -> 終了
```

仕組み:
- `crt0_cpm.s` を `crt0.s` から sed で自動生成（エントリを `.org 0x0100`、終了は
  ウォームブート `jp 0`。`di` / `ld sp` はしない = CCP のスタックを使う）。
- リンクは `-b _HEADER=0x0100 -b _CODE=0x0120`。`makebin -p` 後、先頭 0x100 バイトを
  `dd skip=256` で落として `.COM` 化。
- `cpmcp -f ibm-3740 disks/drivea.dsk prog.com 0:NAME.COM` でディスクイメージへ転送
  （`cpmtools` が `~/bin` に導入済み。z80pack drivea.dsk は ibm-3740 フォーマット）。
- コンソール入出力は crt0 の `out (1),a` / `in (1)`（z80pack CBIOS のコンソールポート）を
  そのまま使うので CP/M 下でも動く。stdout/stderr/stdin とも実機 CP/M で動作確認済み
  (`ok_01` = "Hello TzCC"、`ok_14` = stdout/stderr 出力 + stdin 1文字エコー)。


# 詳細仕様

・Z80が対象アーキテクチャで、AKI-80など小規模ボード向け
・MMUなし
・IOポート 1番がUART I/Oとする
・実際の利用時にはputcharを各プロジェクトで独自に実装する事を想定するが、
　いまは仮でついでに組み込んでしまう
・バイナリ生成の部分はアセンブラを使用するsdasz80でアセンブルしてihx => binに変換する
・引数や戻りアドレスはレジスタ渡しではなくスタック渡しのスタックフレーム ixレジスタでの操作を行う



# ここまでに対応できたこと

1. **コンパイル時出力ファイル名ルールの実装**:
   - 入力 C ファイル名（例: `foo.c`, `tests/ok_01.c`）の `.c` 拡張子前を取得し、`.s` 拡張子を付与したアセンブリファイル（例: `foo.s`, `tests/ok_01.s`）を動的に自動出力するよう `main.c` を改修。
   - `Makefile` の依存関係および `clean` ルールを `.s` 出力に合わせて調整。

2. **`make run` による `cpmsim` エミュレータ上でのバイナリ実行対応**:
   - `crt0.s` に `cpmsim` 仮想ハードウェア制御ポート (`0xa0`) への解錠 (`0xaa`) およびハルト (`0x80`) 出力を追加し、プログラム終了時にエミュレータが正常終了（`System halted, bye.`）する仕組みを構築。
   - `sdldz80` のエリア割り当てフラグ (`-b _HEADER=0x0000 -b _CODE=0x0020`) を追加し、スタートアップコードと生成コードのアドレス衝突を解決。
   - `Makefile` に `make run` ターゲットを追加し、`output.ihx` を `cpmsim -z -x` で即時実行できるように整備。

3. **`char` 型変数および `char` 型静的配列のサポート**:
   - Lexer に `char` キーワード (`TOKEN_CHAR`)、代入演算子 `=` (`TOKEN_ASSIGN`)、シングルクォート文字リテラル（`'H'`）、閉じクォートなし文字表記（`"H;`）、改行エスケープ (`\n`) の解析を追加。
   - Parser / Generator に `char a;` (`NODE_VAR_DECL`) および `char[] str = "TzCC\n";` (`NODE_ARRAY_DECL`)、代入文 (`NODE_ASSIGN`)、`putc(a)` / `printf(str)` 関数呼び出しの構文解析・Z80コード生成（`.area _DATA` への `.ascii`/`.db` 配置）を実装。
   - `crt0.s` に `_putc` スタブを追加。

4. **`char*` 型ポインタのサポート**:
   - Parser / Generator に `char *str1 = "llo ";` (`NODE_PTR_DECL`) のポインタ変数宣言・初期化および間接参照ロード (`ld hl, (var_str1)`) を実装。
   - `printf(str1)` 呼び出し時にポインタ変数に保持された文字列アドレスをスタック経由で正しく引数渡しし、ターゲットソース（`ok_01.c`）で `Hello TzCC\n` が正常出力されることを確認。
   - ソース上の表記揺れ・タイポに対応するトークン統合（`str 2` → `str2`）をサポート。

5. **Tizix (`~/z80pack/tizix`) 配下の C言語関数の洗い出し**:
   - `tizix` 配下の全 `.c` / `.h` ソースコードをスキャン・解析し、利用・定義されている関数群をカテゴリー別に洗い出し整理を完了。
     - `stdio.h` 相当: `putchar`, `getchar`, `printf`, `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `fgetc`, `fputc`, `fgets`, `fputs`, `puts` 等
     - `string.h` 相当: `strlen`, `strcpy`, `strncpy`, `strcat`, `strcmp`, `strncmp`, `strchr`, `strrchr`, `strstr`, `memset`, `memcpy`, `memcmp` 等
     - `stdlib.h` / `ctype.h` 相当: `atoi`, `atol`, `abs`, `labs`, `rand`, `srand`, `itoa`, `isalnum`, `isdigit` 等
     - Tizix/OS低レベル・FatFs/VFS API: `f_open`, `f_read`, `f_write`, `vfs_*`, `kexec` 等

6. **テストパターンの拡充 (`make test`)**:
   - `tests/` 配下にパターン別のテストケース `ok_01.c` 〜 `ok_05.c`（スカラー変数、静的配列、ポインタ、直接文字列等）および `err_01.c` 〜 `err_03.c`（エラー検出）を作成・整備。
   - `make test` で全8件のテストケースが全て成功することを確認。

7. **前項の実装（Tizix C言語標準ライブラリ群のZ80ランタイム実装）**:
   - 洗い出した Tizix/標準C関数群を `crt0.s` に Z80 アセンブリ言語でネイティブ実装・配置完了。
     - `stdio`: `putchar`, `putc`, `fputc`, `printf`, `puts`, `fputs`, `getchar`, `fgetc`
     - `string`: `strlen`, `strcpy`, `strncpy`, `strcat`, `strcmp`, `strncmp`, `memset`, `memcpy`, `memcmp`
     - `stdlib`: `atoi`, `abs`, `rand`, `srand`
     - `file/io`: `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `feof`, `ferror`, `fflush`, `kbhit` のスタブ実装
   - `tests/ok_06.c` を作成し、追加実装した各種関数のビルド・実行テストを実施しクリア。

8. **ユーザ定義関数・引数（パラメータ）・戻り値のサポート**:
   - `int`, `char`, `char*`, `void` 等を返却値・引数型に持つユーザ定義関数（例: `char* get_greeting(char *name)`）の構文解析に対応。
   - 関数パラメータを IX スタックフレーム (`ld ix, #0`, `add ix, sp`) から自動ロードするコード生成に対応。
   - C言語標準の右から左（Right-to-Left）引数プッシュ呼び出し規約（`push arg2`, `push arg1`, `call _func`, `pop af`）による複数引数渡しに対応。
   - 関数戻り値 (`hl` レジスタ返却) の代入 (`g = get_greeting(...)`) および後続関数への引き渡しに対応。
   - `tests/ok_07.c` を追加作成し、ユーザ定義関数・複数引数・戻り値の動作テストをクリア。
9. **コマンドライン引数（main関数の `argc`, `argv` 引数受け渡し）のサポート**:
   - `crt0.s` にダミーコマンドライン引数領域（`dummy_argv0`="tzcc", `dummy_argv`）を定義。
   - `_start` ルーチンにて `call _main` 呼び出し前に `argv` および `argc = 1` をスタックへプッシュ（`push argv`, `push argc`）するダミー初期化処理を実装。
   - `main(int argc, char *argv[])` でパラメータ `argc` (`2(ix)`) および `argv` (`4(ix)`) を正常受け取り・使用可能に対応。
# コマンド依頼欄
ssh tk@rocky9 "head -n 10 ~/z80pack/tzcc/tests/test_op.s"

# コマンド結果欄

   - `tests/ok_08.c` を作成し、動作テストをクリア。
10. **CP/M 2.2 BDOS 低水準ファイル I/O 操作の実装**:
    - `crt0.s` 内に CP/M 2.2 BDOS システムコール (`call 0x0005`) および FCB (File Control Block) 管理ルーチンを統合。
    - `fopen` (Open/Make BDOS 15/22), `fclose` (Close BDOS 16), `fgetc`/`fread`/`fgets` (Read BDOS 20, Set DMA 26), `fputc`/`fwrite`/`fputs` (Write BDOS 21), `feof` などの本格的なファイル操作ランタイムをネイティブアセンブリ実装。
    - `tests/ok_09.c` を作成。
    - **【訂正 2026/08/28】この時点の実装は「リンクが通る」だけで CP/M 実機では動作していなかった**
      （`make test` はコンパイラ終了コードのみ判定のため露見せず）。
      具体的には (a) `_fputs` が `_puts` と同一ルーチンで FILE\* を無視しコンソール出力、
      (b) `_fopen` の拡張子分割がソースポインタを潰して `TEST.TXT` が `TEST` になる、
      (c) `_fgetc` 読み取りパスに余分な `push de` があり `ret` が暴走、
      (d) `_fgets`/`_fread`/`_fwrite` がヘルパ呼び出し後に壊れた `ix` を参照。
      → 「完了報告 (2026/08/28) 追記4」で全て修正し、CP/M 実機での書き込み・読み戻しを確認済み。

11. **標準入出力ストリーム対応の完了確認 (2026/08/28)**:
    - `crt0.s` の `_stdin`/`_stdout`/`_stderr` 構造体と `_fgetc`/`_fputc` のストリーム判定は
      「完了報告 (2026/08/28) 追記2」で実装済み。`tests/ok_14.c` を CP/M 2.2 実機
      (`cpmsim -z`) で実行し stdout/stderr 出力・stdin 1文字エコーを確認、タスク一覧に【済】記載。

12. **ソケット対応（ダミー実装）(2026/08/28)**:
    - `crt0.s` に `_socket`/`_send`/`_recv` を追加。`socket()` は I/O ポート1番の UART を
      ソケットに見立てて `fd=1` を返すダミー、`send`/`recv` は UART パススルー。
    - `include/socket.h` 新規、`tests/ok_17.c` 追加。`make test` 全23件 OK。
      `make run`（ベアメタル）/ `make com`（CP/M 2.2）両経路で `Socket OK` 出力を確認。
    - 詳細は「完了報告 (2026/08/28) 追記6」。


# 引継ぎ用プロンプト

【次のタスク】
・【済】よくつかう標準関数（stdio.h / string.h / stdlib.h 等の拡充および動作検証）
・【済】標準入力/出力/エラー（追記2 で実装、ok_14 を CP/M 実機で確認 / 2026-08-28）
・【済】ソケット対応（追記6 / 2026-08-28）
・【済】言語機能 Tier2:「比較演算子」「単項 ! -」「if/else」「while/for」「int 2バイト化」（追記7〜9 / 2026-08-29）
・【済】コンパイル引数 -o / -s / -X（追記8 / 2026-08-29）
・【済(条件付き)】tzcc スタティックリンク（追記8。glibc-static があれば自動で -static）
・次の残り: --tizix-user（関数ポインタ codegen が前提で保留）/ Tier3（`a[i]` 実行時 r/w、
　`*p` / `&x`、`#define`、`&&` `||`、`++` `--`、複合代入）/ Tier4（struct 等）

【開発環境・条件】
- エミュレータ実行コマンド: `./cpmsim` （`~/z80pack/tzcc/cpmsim` に配置済み）
- ディスクイメージ: `./disks/drivea.dsk` （内容書き換え・テスト許可済み）
- コンパイラビルド＆テスト: `make clean && make run && make test` を実行。テストファイルは `tests/ok_10.c` 等をパターンごとに増やすこと。


# ANSI C 準拠度調査結果 (tzcc現状)

現在の 	zcc はZ80向けクロスコンパイラのサブセット実装であり、ANSI Cの完全なサポートは目標としていません。以下の通り過不足を整理します。

#### 1. サポートされている主要機能
*   **プリプロセッサ**: #include の簡易対応。
*   **データ型**: char, int, char*, oid。
*   **構造**: グローバル変数、静的配列、ポインタ、関数定義（パラメータ・戻り値対応）。
*   **標準ライブラリ**: Tizix環境を想定した stdio.h (printf, puts, getc等), string.h (strcpy, strlen等), stdlib.h (toi, bs等) のZ80ランタイム実装。

### 残タスク
- [ ] **算術演算の網羅的テスト**: `tests/test_arith.c` を拡充し、優先順位（`*`, `/` が `+`, `-` より先）が正しいか全パターン確認する。
- [ ] **除算ゼロ除算エラーのハンドリング**: 現在の `_div` 実装におけるゼロ除算の動作定義または例外処理の追加。
- [ ] **制御構文の実装**: `if` 文、`while` 文の構文解析および `generator.c` でのラベル生成・条件分岐 (`jp`, `jr`) 実装。
- [ ] **ANSI C 型システムの拡充**: `long`, `short` 等のサポート検討。

### ソースレベル引継ぎ情報
- **演算優先順位の制御**: `main.c` 内の `parse_expr` (加減算) -> `parse_mul` (乗除算) -> `parse_primary` の階層構造で制御。
- **コード生成の規約**: 算術演算は `generator.c` の `NODE_MUL`/`NODE_DIV` 等のケースにて `call _mul` / `call _div` を発行。`crt0.s` 上の同名ラベルへ実装済み。
- **ファイル編集**: `\\rocky9\tk\z80pack\tzcc\` 直下のファイルを直接編集し、VS Code ターミナルから SSH 経由で `make` を行うこと。

*   **I/O**: CP/M 2.2 BDOS互換のファイル操作関数 (open, close, read等) のネイティブ実装。

#### 2. ANSI C に対する主な不足・未実装機能
*   **型システム**:
    *   loat, double, long, short 等の数値型。
    *   struct, union, enum 型。
    *   const, static, extern 修飾子。

### 完了報告 (2026/08/27)
- **四則演算の対応完了**: 
  - パーサーの式評価順位（優先順位）を再帰降下解析により正しく実装 (`parse_mul` / `parse_expr` 階層化)。
  - `generator.c` に `NODE_MUL` / `NODE_DIV` 対応を追加。
  - `crt0.s` に Z80 用の乗算ルーチン `_mul` および除算ルーチン `_div` を実装し、リンク可能な状態にした。
  - `parser.c` のデバッグ出力機能を改善し、全ノード種別の可視化に対応。

---

    *   多次元配列。
*   **制御構文**:
    *   if / else 条件分岐（要改修）。
    *   or, while, do-while ループ構造。
    *   switch / case 文。
    *   goto, reak, continue。
*   **演算子**:
    *   比較・論理演算子 (==, !=, <, >, &&, ||, !)。
    *   算術演算子（現状は基本的なロード・プッシュのみで、加減算コード生成が限定的）。
    *   インクリメント・デクリメント (++, --)。
    *   ビット演算子。
*   **高度な機能**:
    *   マクロ定義 (#define)。
    *   	ypedef。
    *   関数ポインタ。



### 完了報告 (2026/08/28)
- **標準入出力対応の実装**:
  - `crt0.s` に `_stdin`, `_stdout`, `_stderr` 構造体およびポートI/Oルーチンを追加。
  - `fgetc`, `fputc` に標準入出力ストリーム判定ルーチンを統合。
- **演算対応の強化**:
  - `generator.c` に `NODE_MUL`, `NODE_DIV` のコード生成を追加。
  - `crt0.s` に `_mul`, `_div` アセンブリルーチンを実装。
- **課題**: `tests/ok_14.c` のパース時に無限ループが発生するバグを特定。パーサーの堅牢化（エラーハンドリング）が次期タスクとなる。

### 完了報告 (2026/08/28) 追記 — パーサー無限ループ修正
- **原因**: 文字化けで壊れた `tests/ok_13.c` / `tests/ok_14.c` を入力すると、`main.c` の関数呼び出し引数リスト解析ループが無限ループ。
  文字列リテラル中の `;` を lexer が「閉じ忘れ」として途中で打ち切り、残った `;` を `parse_expr`→`parse_primary` が消費できず（`NUMBER/STRING/IDENTIFIER` 以外は NULL 返却・トークン非消費）、`while (cur != RPAREN && cur != EOF)` が回り続ける。`next_token()` は `TOKEN_UNKNOWN` を表示するだけで停止しない。
  → `make test` はアルファベット順で `ok_13.c` に到達した時点で永久ハング（＝応答が返らない）。
- **修正**:
  - `main.c`: 引数リスト解析の 2 箇所（`parse_primary` 内・`parse_stmt` 内）に「次トークンが `,` でも `)` でもなければ `has_error=1` にして `break`」ガードを追加。予期せぬトークンで確実に脱出する。
  - `tests/ok_13.c` / `tests/ok_14.c`: 文字化けした内容を有効な C に修復（`if` は未実装のため `ok_13.c` からは除去）。
  - `Makefile` の `test` ターゲット: `timeout 10` を付与し、無限ループ再発時は `FAIL (timeout)` で即停止するように。`test_*.c` を成功期待として分類。
- **結果**: `make test` 全 19 件 OK、`make test` は正常終了（exit 0）。

- [x] **パーサーの堅牢化**: 引数リスト解析の無限ループを特定・修正。予期せぬトークンで `has_error` を立てて脱出するようにした。

### 完了報告 (2026/08/28) 追記2 — デバッグ出力除去とアセンブルまで通す
- **デバッグプリント除去**: `main.c` の `parse_primary` / `parse_expr` 内の `DEBUG:` 用 `fprintf(stderr, ...)` を削除。
  さらに `main.c` の成功時 stdout 出力（`print_ast(ast, 0)` による AST 全ダンプ、`AST generated and saved to ast.txt` / `Assembly generated in ...` の printf、中身が空同然の `ast.txt` 書き出し）も削除。`./tzcc foo.c` は成功時に無出力（エラーのみ stderr）＝通常のコンパイラ挙動に。`print_ast` 自体は `parser.c` に残置（将来の `-v` 用）。
- **`crt0.s` がそもそもアセンブルできなかった問題を修正**:
  - `_mul` / `_div` / `_stdin` / `_stdout` / `_stderr` とその構造体定義がまるごと二重貼り付けされており（`<m> multiple definitions` / `<p> phase error` / `<a> Branching Range Exceeded` が多発）、重複ブロックを削除。
  - `_kbhit` が `ret` なしで `_mul` に落ち込んでいたので `ret` を追加。
- **`FILE` 型と標準ストリームのコード生成対応** (`tests/ok_14.c` をリンクまで通すため):
  - `lexer.c`: `FILE` をサブセットの型キーワード（`int` 相当）として認識。`FILE *fp;` が宣言としてパースされる。
  - `generator.c`: `stdin` / `stdout` / `stderr` は `crt0.s` のグローバル `_stdin` 等を直接参照（`is_std_stream()` 追加）。`NODE_VAR_DECL` / `NODE_PTR_DECL` の初期化子に `NODE_CALL`・`NODE_IDENTIFIER` を追加し、`char c = fgetc(stdin);` や `FILE *f = stdout;` がコード生成されるように。
  - `parser.c`: `print_ast` が `node->right` を辿っていなかったため関数本体が AST ダンプに出ていなかったのを修正（表示のみの問題）。
- **結果**: `tests/ok_01.c`〜`ok_14.c` の全 14 本が `tzcc` → `sdasz80` → `sdldz80` までエラーなく通り、`cpmsim` で実行・ハルトすることを確認。`make test` 全 19 件 OK。

- [x] **`char c = fgetc(...)` などの動作検証**: 2026/08/28 追記4 で `_fgetc` のファイル読み取りを CP/M 実機で確認（`tests/ok_16.c`）。stdin 判定は `ok_14` で確認済み。

### 完了報告 (2026/08/28) 追記3 — #include 対応
- **`include/` にサブセットヘッダを新規作成**: `stdio.h` / `stdlib.h` / `string.h` / `stdint.h`。crt0.s が実装する関数のプロトタイプ宣言のみ（`int` / `char` / `char*` / `void` / `FILE` だけ使用。`#ifndef` / `#define` / `extern` / `typedef` / コメントは不可）。
- **lexer.c: `#include` を透過展開**（パーサに渡す前に処理）:
  - ファイルスタック方式（`inc_stack` / `cur_buf`）。`#include` トークンはもうパーサに出てこない。
  - 検索パス: `"foo.h"` = ソースと同じディレクトリ → `include/`、`<foo.h>` = `include/` → ソースディレクトリ。`main.c` から `lexer_set_source_dir()` で入力ファイルのディレクトリを渡す。
  - 同一ファイルは1回だけ展開（暗黙の `#pragma once`）。
  - `<sysheader>` が見つからない場合は stderr に warning のみで継続（既存の `<stdio.h>` 等がヘッダ無しでも動く後方互換）。`"local.h"` が無い場合は error（`lexer_error()` → 終了コード 1）。
  - `#include` 以外の `#` 行（`#define` 等）は行末まで読み飛ばす。
  - 副産物: `_DATA` に出ていた未使用の `str_"stdio"` が消えた。
- **main.c (parse): プロトタイプ宣言を握りつぶす**: `型 名前(引数);`（`{` が続かない）は `NODE_FUNC` を作らず `;` まで読み捨てる。これが無いとヘッダの `int printf(...);` が `_printf:: ret` を生成して crt0.s と**シンボル重複でリンクエラー**になる。`parse()` の `#`/`TOKEN_INCLUDE` 分岐は撤去。
- **検証**: `make test` 全 19 OK / `ok_01`〜`ok_14` の compile→sdasz80→sdldz80 全通過 / `ok_14` は cpmsim で従来どおり動作。`"local.h"` のスプライス・多重 include 抑止・ヘッダ未検出時の warning/error を個別に確認。


### 完了報告 (2026/08/28) 追記4 — crt0 ファイル I/O を CP/M 実機で実動作させる

「【最優先】crt0 のファイル I/O が CP/M 上で実動作しない」への対応。すべて `crt0.s` の修正。

- **`_fputs` を `_puts` から分離** (crt0.s):
  - 旧: `_puts:: _fputs::` が同一ルーチン。第2引数の `FILE *` を無視して `out (1),a`
    （コンソール）へ出力し、さらに `puts` 同様に末尾 `\n` を勝手に付けていた。
  - 新: `_fputs(s, f)` は文字列を1文字ずつ `_fputc(c, f)` に渡すだけ。`_fputc` 側の
    stdout/stderr 判定・FCB/BDOS 書き込みルートをそのまま再利用する。`\n` は付けない。
  - `_puts` は従来動作（コンソール + 改行付与）のまま単独ルーチンに。
- **`_fopen` の FCB 名/拡張子分割を修正** (crt0.s `_fopen_has_ext`):
  - 旧: `'.'` を見つけた後 `push iy / pop hl` でソース文字列ポインタ(hl)を潰しており、
    拡張子フィールドにゴミが入る（＝ `fopen("test.txt","w")` が `TEST`（拡張子なし）を作る）。
  - 新: `'.'` の次を指す hl を `push`/`pop` で退避してから書き込み先 `iy+9`（drive1+name8）を
    算出。英小文字→大文字化し 3 バイトへコピー。二つ目の `'.'` で打ち切り。
- **`_fgetc` 読み取りパスの暴走を修正** (crt0.s `_fgetc_read_buf`):
  - 旧: `push iy / pop de` の直後に **対応する pop の無い `push de`** があり、
    `ret` がその値（=FILE\* のアドレス）を戻り番地として拾って暴走（HALT/ハング）。
  - 新: 不要な `push de` を削除。番地計算 `hl = iy + 36 + index` は元のまま。
- **`_fgets` / `_fread` / `_fwrite` の `ix` 破壊を修正** (crt0.s):
  - 旧: ループ内で `_fgetc` / `_fputc` を呼んだ後（これらは `ld ix,#0 / add ix,sp` で
    `ix` を書き換える）、`8(ix)` 等で `FILE*` や戻り値カウントを再ロードしていたため
    2 周目以降ゴミを参照。
  - 新: エントリで `FILE*` を `_io_fp`、戻り値カウントを `_io_ret`（`_DATA` に新設、
    再入不可・単一スレッド前提）へ退避し、ループ内は静的領域から読む。
    `_fputs` も同じ `_io_fp` を使用。
- **読み戻しテスト追加**: `tests/ok_16.c`。`io.txt` へ `fputs("File IO Test\n", fw)` で
  書き込み、`fclose` 後 `fopen("io.txt","r")` で開き直し、`fgetc` を 13 回展開して
  1 文字ずつ `putchar`（ローカル配列・while 未実装のため手展開）。

**テスト結果（`make com` → CP/M 2.2 実機 = `cpmsim -z` に `printf 'NAME\r\nBYE\r\n'` を流し込み）**:
- `make com TEST_SRC=tests/ok_09.c` → `A>OK09` 実行 → コンソールには**何も出ない**
  （＝ `fputs` がコンソールに漏れていない）。
- `cpmls -f ibm-3740 disks/drivea.dsk` に `test.txt`（正しい名前・拡張子）が出現。
  旧実装では拡張子の無い `TEST` になっていた。
- `cpmcp` で吸い出した `TEST.TXT` の中身 = `File IO Test\n` + `0x1A` パディング（1 レコード）。
- `make com TEST_SRC=tests/ok_16.c` → `A>OK16` 実行 → コンソールに `File IO Test` +改行
  （書いた内容を `fgetc` で順次読み戻せている）。両テストとも実行後クリーンに `A>` へ復帰。
- `make test` 全 22 件 OK（`ok_16` / `test_arith` / `test_op` 追加分含む）。

**このタスクの範囲外で判明した宿題（→ 追記5 で対応済み）**:
- ~~ローカル/静的配列のコード生成が壊れている~~ → 「完了報告 (2026/08/28) 追記5」で対応。


### 完了報告 (2026/08/28) 追記5 — ローカル/静的配列のコード生成を修正

「【最優先】ローカル/静的配列のコード生成が壊れている」への対応。

- **根本原因**: `main.c` のパーサが後置の配列宣言子 `char name[N];`（C 標準の書き方）を
  一切解釈していなかった。`[` を識別子より **前** でしか見ておらず（`char[] name` という
  非標準表記のみ対応）、`char buf[32];` は `NODE_VAR_DECL "buf"` になり `[32];` は
  無名トークンとして読み捨てられていた。このため
  - `emit_data` がスカラー扱いで `.db 0`（1 バイト）しか確保しない
  - `find_sym_type` が配列（type 2）を返さず、引数渡しが先頭 1 バイトの **値** を
    ポインタとして push（`ld a,(var_buf) / ld l,a / ld h,#0`）
  になっていた。
- **修正**:
  - `parser.h` / `parser.c`: `Node` に `int array_size` を追加（`new_node` で 0 初期化）。
  - `main.c`: `parse_stmt`（ローカル宣言）と `parse`（グローバル宣言）の両方で、
    識別子の **後** に来る `[N]` / `[]` を解釈して `is_array` / `arr_size` を立て、
    `decl->array_size` に格納。従来の前置 `[]` 記法も残置（後方互換）。
  - `generator.c`:
    - `emit_data` の `NODE_ARRAY_DECL`: 文字列初期化子なしは `.ds N`（N=宣言サイズ、
      未指定時 1）で確保。`char s[] = "..."` / `char s[N] = "..."` は文字列長+1 を
      `.ascii`/`.db 0` で置き、宣言サイズが大きければ残りを `.ds` で埋める。
    - `generate_asm` に空の `NODE_ARRAY_DECL` ケースを追加（記憶域は `_DATA` 側で確保
      済みのため実行時コード不要。default で `node->left` を辿るのを防ぐ）。
- **検証**:
  - `make test` 全 22 件 OK。
  - `tests/ok_10.c`（`char buffer[32]` + strcpy/strcat）: 生成 `.s` が
    `var_buffer: .ds 32` / 引数渡し `ld hl, #var_buffer` になることを確認。
    `make com TEST_SRC=tests/ok_10.c` → CP/M 2.2 実機（`cpmsim -z`）で `A>OK10` 実行 →
    コンソールに `Hello TzCC` を確認。
  - `tests/ok_16.c` を `fgets(buf,32,fr)` 版へ戻し（ローカル配列 `buf[32]`）、
    `make com TEST_SRC=tests/ok_16.c` → `A>OK16` 実行 → コンソールに `File IO Test`
    （書いた内容を `fgets` でローカル配列へ読み戻せている）を確認。
  - `tests/ok_12.c`（memset/memcpy）: `memset`/`memcpy` へ配列アドレスが渡り
    9×`'A'` / `"CopyTest"` のコピーは成功。ただし `buf[9] = '\0'` の
    **配列添字への書き込み**は未実装（Tier 3「配列添字 `a[i]` の実行時読み書き」）で
    ヌル終端が付かず `puts` が隣接領域まで表示する。→ 配列添字 r/w は別タスク。


### 完了報告 (2026/08/28) 追記6 — ソケット対応（ダミー実装）

タスク一覧「ソケットに対応する」への対応。コンパイラは関数呼び出しを汎用に
`call _<name>` へ落とすため、C 側の言語機能追加は不要。ランタイム（`crt0.s`）に
ダミー実装を足すだけで `socket()` / `send()` / `recv()` が使える。

- **`crt0.s` に `_socket` / `_send` / `_recv` を追加**（`.globl` も追加。`_kbhit` の直後）:
  - `int socket(int domain, int type, int protocol)` — 常に `fd = 1` を返すダミー。
    I/O ポート1番の UART をソケットに見立てる。
  - `int send(int fd, const char *buf, int len)` — `fd` は無視し、`buf` から `len`
    バイトを `out (1),a` で UART へ出力。戻り値 = `len`。
  - `int recv(int fd, char *buf, int len)` — `in a,(1)` で `len` バイト読み込み `buf` へ。
    戻り値 = `len`。
  - いずれもループ終端は `ld a,b / or c / jr z`（`dec bc` はフラグ不変）で `_memcpy` と同じ形。
  - コメントに「本来はプロジェクト側が同名ラベルを自前実装で置き換える前提。これは
    未実装時のフォールバック」と明記。readme のタスク文
    「ユーザーが _socket を実装しないと socket() がエラー」という最終仕様は、実ネット
    スタックを載せる段階で `_socket` をこのダミーから置き換える形で担保する
    （現時点は「_socket ダミーを UART 接続で作っておく」の指示どおり動く実装を優先）。
- **`include/socket.h` を新規作成**: `socket` / `send` / `recv` のプロトタイプのみ
  （サブセットヘッダ規約どおり `int` / `char*` だけ使用）。未検出でも lexer は warning
  継続なので必須ではないが用意した。
- **`tests/ok_17.c` を追加**: `fd = socket(2,1,0);` → `send(fd, "Socket OK\n", 10);`。

**検証**:
- `make test` 全 23 件 OK（`ok_17` 追加分含む）。
- `make build_test TEST_SRC=tests/ok_17.c` → `make run`（cpmsim ベアメタル）→
  コンソールに `Socket OK` を出して `System halted, bye.`。
- `make com TEST_SRC=tests/ok_17.c` → `cpmsim -z` の CP/M 2.2 で `A>OK17` 実行 →
  `Socket OK` 表示後クリーンに `A>` へ復帰。


### 完了報告 (2026/08/29) 追記7 — 言語機能 Tier2（比較演算子・単項・if/else・while/for）

タスク一覧「言語機能の未実装」2〜4 への対応。lexer / parser (main.c) / generator.c を横断。

- **lexer.c / lexer.h**: 2文字演算子 `==` `!=` `<=` `>=` と単項 `!`（`TOKEN_EQ/NE/LE/GE/NOT`）、
  キーワード `if` `else` `while` `for`（`TOKEN_IF/ELSE/WHILE/FOR`）を追加。
- **parser.h / parser.c**: `NodeType` に `NODE_EQ..GE` / `NODE_NOT` / `NODE_NEG` /
  `NODE_IF` / `NODE_WHILE` / `NODE_FOR` を追加。`Node` に `third`（if の else節 / for の post式）
  と `fourth`（for の body）ポインタを追加。`new_node` / `print_ast` も追随。
- **main.c（パーサ）**: 式解析を優先順位つき再帰下降に再構成。
  `parse_expr`(== !=) → `parse_relational`(< > <= >=) → `parse_add`(+ -) →
  `parse_mul`(* /) → `parse_unary`(! -) → `parse_primary`（括弧 `( )` 対応を追加）。
  `parse_stmt` に `if/else`・`while`・`for`・`{ }` ブロック・空文 `;` を追加。
  `parse_block_or_stmt()` / `parse_simple_stmt_nosemi()`（for の init/post 用、末尾 `;` を消費しない）を新設。
- **generator.c**:
  - `generate_asm` に一般式評価ケースを追加（`NODE_NUMBER`/`NODE_IDENTIFIER`/`NODE_STRING` を
    hl へロード）。これで `NODE_ADD` 等の再帰が実際に値を出すようになった（従来は限定的）。
  - 比較6種は `sbc hl,de` + `jr z/c` で hl に 0/1 を生成（符号なし比較）。`NODE_NOT` / `NODE_NEG` も実装。
  - `NODE_IF` は `jp z, Lelse%d` / `jp Lend%d`、`NODE_WHILE` / `NODE_FOR` は `Lbeg%d:` ループ +
    `jp z, Lend%d`。ラベルは `label_id` グローバル連番で全体一意。
  - `NODE_ASSIGN` / `NODE_RETURN` / `NODE_CALL` 引数に「式ノードなら `generate_asm` を再帰」経路を追加
    （`is_expr_node()`）。関数引数では兄弟(`next`)を辿らないよう一時退避してから評価。
  - `emit_data` が `node->third` / `node->fourth` を辿っていなかったため、else節や for body 内の
    文字列リテラルが `_DATA` に出ず未定義シンボルになる不具合を修正。
- **テスト**: `tests/ok_18.c`（比較・単項・優先順位）/ `ok_19.c`（if/else if/else, `if(n)`, `if(!n)`）/
  `ok_20.c`（while で `ABCDE`）/ `ok_21.c`（for で `0123456789`）を追加。
  `make test` 全27件 OK。`make build_test` → `cpmsim -z -x` ベアメタル実行で
  ok_19=`two`+`nonzero` / ok_20=`ABCDE` / ok_21=`0123456789` を実出力で確認。

**次の残タスク**: Tier2-5「`int` 2バイト化」/ `--tizix-user` オプション /
Tier3（`a[i]` 実行時 r/w、`*p`/`&x`、`#define`、`&&` `||`、`++` `--`、複合代入 …）。


### 完了報告 (2026/08/29) 追記8 — コンパイル引数 -o / -s / -X と tzcc スタティックリンク

- **main.c の引数処理を書き換え**: 位置引数を入力ファイルとして受けつつオプションを解釈。
  - `-o <file>` / `-o<file>` : 出力ファイル名を明示。
  - `-X` : `.s` を書かず `generate_asm(ast, stdout)` で標準出力へ出して即 `return 0`。
  - `-s` : アセンブラのみ生成して終了（tzcc は元々アセンブルしないので既定と同義。受理のみ）。
  - 未知の `-` オプションは warning を出して無視。入力ファイル未指定は従来どおり `return 1`。
- **Makefile**: `STATIC := $(shell ... $(CC) -static ... && echo -static)` で
  「実際に静的リンクできる環境なら `-static` を付ける」自動判定に。`$(TARGET)` ルール末尾で
  `file` の結果を見て static/dynamic を表示。明示要求用に `make static` ターゲットも追加。
  rocky9 は `glibc-static` 未導入のため現状は動的リンク（`/usr/bin/ld: cannot find -lc`）。
  導入すれば再ビルドで自動的に静的リンクになる。
- **検証**: `make test` 全26件 OK。`./tzcc -X tests/ok_20.c` が asm を stdout へ、
  `./tzcc -o /tmp/xx.s tests/ok_01.c` が指定先へ出力、`-s` が従来どおり `.s` を生成することを確認。


### 完了報告 (2026/08/29) 追記9 — int を2バイト化

タスク一覧「言語機能の未実装 5」への対応。char は従来どおり 1バイト、`int` のみ 2バイト化。

- **parser.h / parser.c**: `Node` に `int base_type`（0=char, 1=int）を追加。`new_node` で 0 初期化。
- **main.c（パーサ）**: 宣言を解釈する3か所（`parse_stmt` のローカル宣言 / `parse` のグローバル宣言 /
  関数パラメータ）で `int` キーワードを見たら `decl->base_type = 1`。ポインタ（`int *p`）は
  従来どおり `NODE_PTR_DECL`（2バイト）なので影響なし。
- **generator.c**:
  - `find_sym_type` の戻り値に `4 = int スカラー(2byte)` を追加（char スカラーは従来どおり `1`）。
    `third` / `fourth`（else節・for body）も探索するよう再帰を追加。
  - ヘルパ `gen_load_scalar(name, st)` / `gen_store_scalar(name, st)` を新設。
    `st==4` は `ld hl,(var_x)` / `ld (var_x),hl`（16bit）、それ以外は従来の 8bit ゼロ拡張。
  - `emit_data`: int の `NODE_VAR_DECL` は `.dw 0`（char は `.db 0`）。
  - 参照・代入・関数引数・`return`・変数初期化子・関数パラメータ受け取りの各サイトを
    ヘルパ経由に統一。引数は元々常に 16bit（`push hl`）なので int パラメータは
    `ld l,N(ix) / ld h,N+1(ix)` で正しく 2バイト受領。
  - ついでに `int x = a + b;` のような式初期化子（`is_expr_node`）も `NODE_VAR_DECL` で
    codegen されるようにした（従来は未対応で捨てられていた）。
  - **安全側の設計**: 取りこぼしたサイトがあっても `find_sym_type` の `4` は `else` に落ちて
    従来の 8bit 動作になるだけ（暴走しない）。
- **テスト**: `tests/ok_22.c`（`int n=500` を 100 ずつ減算しながら `.` を出力 → char では
  桁溢れするが int なら丁度 `.....`）/ `ok_23.c`（`int add(int,int)` で 200+100=300、
  300/100 で `###`）。`make test` 全28件 OK。`make build_test` → `cpmsim -z -x` ベアメタルで
  ok_22=`.....` / ok_23=`###` を実出力で確認。char 系の回帰確認として ok_01/ok_06/ok_10 が
  それぞれ従来どおり `Hello TzCC` / `LibTest` / `Hello TzCC` を出力することも確認。

**次の残タスク**: `--tizix-user`（関数ポインタ／間接呼び出し codegen が前提。上のタスク一覧参照）/
Tier3（`a[i]` 実行時 r/w、`*p` デリファレンス / `&x`、`#define` 定数マクロ、`&&` `||`、
`++` `--`、複合代入 `+= -=`）/ Tier4（struct/union/enum、long/short/unsigned、typedef、switch）。


### 完了報告 (2026/08/29) 追記10 — printf 書式指定子の実装

従来 `_printf` は「第1引数の文字列を NUL まで出すだけ」で `%d` 等は素通りだった。
`%d %i %u %x %X %c %s %%` に対応（幅・精度・フラグは読み飛ばして無視）。

- **crt0.s `_printf` を書き換え**（`_pf_*` ルーチン群 ≈ 約330バイト増）:
  - スタックから `fmt`（`sp+2`）と可変引数開始（`sp+4`）を取得。可変引数は tzcc が
    常に 16bit で push するので 1 個 2バイトずつ `_pf_next_arg` で取り出す。
  - `%d`/`%i`: 符号付き16bit。負なら `'-'` を出して 2の補数。10進変換は
    10000/1000/100/10 を `sbc hl,de` で引きながら桁を数える方式（除算ルーチン不要）。前ゼロ抑制。
  - `%u`: 符号なし16bit。`%x`/`%X`: 16bit を4桁hex、前ゼロ抑制、小文字 `a-f`。
  - `%c`: 引数の下位8bit。`%s`: `char*` を NUL まで。`%%`: リテラル `%`。
  - 未知指定子は `%` + その文字をそのまま出力。`%5d` 等の幅は数字/`. - + # 空白` を
    読み飛ばして無視（値は正しく出る）。
  - 解析用スクラッチ `_pf_ptr` / `_pf_arg` / `_pf_lz` を `_DATA` に追加（再入不可・単一スレッド前提）。
- **generator.c のバグ修正**: 数値リテラル引数・`return 数値` が `ld l,#N / ld h,#0`
  （8bit ロード）で生成されており、`printf("%d", 300)` が 300&0xFF=44 になっていた。
  `ld hl, #N`（16bit）に修正。>255 の即値引数・戻り値が正しく渡るように。
- **テスト**: `tests/ok_25.c`。`make test` 全30件 OK。`make build_test` →
  `cpmsim -z -x` ベアメタルで
  `d=300 u=40000 x=12c c=A s=abc %` / `neg=-5 zero=0` / `width ignored [42]` を実出力で確認。
- **サイズ**: 追加後の END アドレスは ok_01=0x0CCE(3278B) / ok_25=0x0D18(3352B) /
  ok_18=0x0D97(3479B)。4KB（0x1000）以内を維持（残り ~600B）。

**注意**: `make com`（CP/M .COM）経路の対話実行はこの環境の cpmsim への stdin パイプが
安定せず未確認。ベアメタルと CP/M でコード生成・crt0 コンソール I/O（`out (1),a`）は
共通なので printf 自体はベアメタル確認で担保。`disks/drivea.dsk` は容量が逼迫していたため
テスト用 `OK18`〜`OK25.COM` を削除して元の状態（`ok09/10/12/16/17.com` + 標準ツール）に戻した。

（CP/M stdin パイプは「PowerShell 側 `"..."` ＋ リモート側 `'...'` ＋ 8進エスケープ
`printf 'OK24\015\012BYE\015\012'` なら通ることが後で判明。`\r\n` 直書きは経路の
どこかで潰れて `rn` リテラルになりCCPが暴走する。）


### 完了報告 (2026/08/29) 追記11 — fgets ループの EOF 終了 & ok_24 の不具合診断

`tests/ok_24.c`（`A>OK24` で `Bdos Err On D: Bad Sector`）の調査。原因は**すべて
ok_24.c 側のバグ**でコンパイラは正常だった:

1. **`fgets(f, str, 32)` の引数順違い**（致命）。crt0 `_fgets` は
   `2(ix)=buf / 4(ix)=size / 6(ix)=FILE*` を期待（標準Cの `fgets(s, size, stream)`）。
   `f` を buf、`32` を FILE* として扱い、アドレス 32 をFCBとみなして BDOS を叩いた結果、
   でたらめなドライブ選択で `Bdos Err On D:` になっていた。正しくは `fgets(str, 32, f)`。
2. **`while(feof(f))` の判定が逆**。EOF まで回すなら `!feof(f)`。しかも crt0 `_feof` は
   128バイトのレコード境界でしか true にならない簡易実装なので、`feof` だけでループ制御すると
   短いファイルで無限ループになる。
3. **`fopen` の NULL チェック無し**。失敗時 `f=0` で以降メモリ破壊。

**crt0.s の小修正**: `_fgets` が EOF（1文字も読めず）でも常に buf ポインタを返していたのを、
**0文字なら NULL(0) を返す**よう変更（標準Cの `fgets` セマンティクス）。これで
`while (fgets(str, n, f) != 0)` でファイル末尾まで安全に読めるようになった。
`_feof` 依存のループ制御が不要に。

**ok_24.c を修正版に差し替え**（`fgets(str,64,f)` 正順 / `while (fgets(...) != 0)` /
`if (f == 0)` チェック / 出力は `fputs(str, stdout)`）。

**検証**: `make test` 全30件 OK。`make com TEST_SRC=tests/ok_24.c` → CP/M 2.2
（`printf 'OK24\015\012BYE\015\012' | cpmsim -z`）で `A>OK24` → `File IO Test`
（`TEST.TXT` の中身）を表示 → クリーンに `A>` へ復帰 → `BYE` で `System halted, bye.`。
`Bdos Err` も無限ループも解消。


### 完了報告 (2026/08/29) 追記12 — Tizix ターゲット対応 Phase 1〜3（IY相対PIC を実機で実証）

「### 【tzcc の存在意義】」の Phase 1〜3 を実装。**tzcc 出力が Tizix 実機で動いた。**

- **`tizix.c` 新規 + `main.c --tizix-user`/`-T`**: tzcc 生成 `.s` を IY 相対 PIC へ in-place 変換。
  - `ld hl,#var_X`/`#str_N`/`#Lxxx` → 直後に `push af/push de/push iy/pop de/add hl,de/pop de/pop af`
  - `ld hl,(var_X)` / `ld a,(var_X)` → `ld hl,#var_X` + 上記 + `(hl)` ロード
  - `ld (var_X),hl` / `,a` → `push af/push bc / (ex de,hl|ld e,a) / ld hl,#var_X / push iy/pop bc/add hl,bc / ld (hl),e[/inc hl/ld (hl),d] / pop bc/pop af`
  - `call _name` → `push af/push de / ld hl,#_name / push iy/pop de/add hl,de / pop de/pop af / call ___sdcc_call_hl`
  - `jp Lxxx` → 間接JP列（`ex (sp),hl / ret`）。`jp z,Lxxx` → `jr nz,Ltz<k>` + 間接JP + `Ltz<k>:`
  - 数値即値 `#0`/`#123`/`#'A'`、`jr`/`djnz`/`ret`、`.area`/`.db`/ラベル定義、`ld hl,(_stdX)` は素通し。
- **`crt0_tizix.s` 新規（bring-up 版）**: `.ds 0x20` + `_start`（`crt0cmd.s` 同型、IY ベースで
  SP=top-0x110 / argv、`main` を IY 相対 call、戻り `jp _kexit`）+ `_putchar`/`_putc`/`_getchar`
  （`call _kputchar`(0x3E) / `call _kgetchar`(0x41) 素通し）。内部 call/jp/データラベル無し
  → それ自体は IY 加算不要。`_puts`/`_printf`/string 系は未実装（次フェーズ）。
- **`Makefile`: `make tizix TEST_SRC=…`** 追加。`SRCS` に `tizix.c`。
  `sdldz80 -n -i -b _CODE=0x0000 -g ___sdcc_call_hl=0x0050 -g _kexit=0x003B -g _kputchar=0x003E -g _kgetchar=0x0041`
  → `makebin -p` → `mcopy -o` で `disks/driveb.dsk` へ `NAME.BIN`。

**検証（Tizix 実機 = `printf 'NAME\015\012' | ./cpmsim -z`）**:
- `tests/ok_tz1.c`（`putchar` のみ）→ `OKTZ1.BIN` 229B → シェル `# OKTZ1` → `OK` → クリーンに `# ` 復帰。
- `tests/ok_tz2.c`（`for(i=0;i<10;i=i+1) putchar(48+i)` / int変数 / 比較 / 加算 / for分岐）
  → `OKTZ2.BIN` 366B → `# OKTZ2` → `0123456789` → `# ` 復帰。
  （`jp z`/`jp` の間接JP化、`ld (var_i),hl`/`ld hl,(var_i)` の IY相対データアクセス、
  `call _putchar` の間接CALL化が実機で正しく動作）。
- `make test` 全28件 OK（`--tizix-user` 無しの通常経路は不変）。

**次**: `crt0_tizix.s` に `_puts`/`_printf` 追加（IY問題を避けるため内部 call を持たない形で） →
それから中断中の C言語機能（`a[i]` 実行時 r/w、`*p`/`&x`、`++`/`--`、`+= -=`、`&&`/`||`）を
Tizix 実機で確認しながら再開。案B（ローカルを ix フレーム化）は codegen 最適化として後日。


### 完了報告 (2026/08/29) 追記13 — スタティックリンク / arch/z80pack ビルドレイアウト / make runtest

- **tzcc をスタティックリンク化**: rocky9 に `sudo dnf install -y glibc-static` を導入。
  Makefile の `STATIC` プローブが `-static` 可能を検出し、`tzcc` は
  `file` → "statically linked" / `ldd` → "not a dynamic executable" に。任意の x86 Linux で動く。
- **ビルド成果物を `arch/z80pack/` 以下に集約**（src を散らかさない）。ルートに残るのは `tzcc` のみ。
  - `OBJS = $(SRCS:%.c=$(BUILD)/%.o)`、`$(BUILD)/%.o: %.c | $(BUILD)`（order-only 依存）
  - `crt0.s` / `crt0_tizix.s` は `$(BUILD)/` にコピーしてからアセンブル（sdasz80 は `.lst`/`.sym`
    をソースと同じ場所に吐くため）。`crt0_cpm.s` は元々 `$(BUILD)/` へ生成。
  - test の中間 `.s` は `tzcc -o $(BUILD)/名前.s`。`.rel/.ihx/.bin/.com/.map` すべて `$(BUILD)/`。
  - `make clean` = `rm -rf arch/z80pack` + `rm -f tzcc ast.txt`。`.gitignore` に `/arch/`。
  - **注意**: parse 時 `$(shell mkdir)` だと `make clean all` で clean 後に消えたまま all が
    走り失敗する。order-only 依存 `| $(BUILD)` + `$(BUILD): mkdir -p` で解決。
- **`make runtest`（`tests/run_rt.sh`）**: 各 `ok_*.c` を cpmsim ベアメタルで実行し、
  `// EXPECT:` 注記と実出力を比較。`make test`（コンパイル可否のみ）を補完。
  全 `ok_*.c` に `// EXPECT:` / `// EXPECT-EMPTY` / `// RUN: skip <理由>` を付与。
  結果: **PASS=21 / SKIP=6**（`ok_09/13/16/24`=BDOSファイルI/O、`ok_12`=`a[i]`書込未実装、
  `ok_14`=stdin 要）。詳細は「# テスト方法」節。

### 【将来目標】tzcc のセルフホスト（Z80 版 tzcc）

最終的に **tzcc 自身を Z80(Tizix) 向けにコンパイルして Tizix 上で走らせ、tzcc が tzcc を
ビルドできる**状態を目指す。必要になるもの（現状の tzcc に無い）:
- C言語機能: `struct`（`Node` 相当）、`enum`、関数ポインタ回避、多ファイル分割コンパイル or
  1ファイル連結、`sizeof`、`static`/グローバル、`switch`、可変長引数の受け側。
- Z80 libc: `malloc`/`free`（アリーナで可）、`fopen`/`fread`/`fwrite`/`fgets`/`fprintf`、
  `str*`/`mem*` 一式、`atoi`/`snprintf`/`vfprintf`。→ Tizix の `_f_*` / FatFs を叩く。
- ビルド: `tzcc --tizix-user` で自身をコンパイル → `.BIN`（複数ブロック nblk>1 前提）。
まずは C言語機能を積み上げ、tzcc が自分のソースをパースできる所まで持っていく。


### 完了報告 (2026/08/29) 追記14 — 配列添字 a[i] の実行時 r/w、*p デリファレンス、&x

Tier3 の最優先項目。`tests/ok_12.c` の `buf[9]='\0'` が捨てられていた不具合の解消も含む。

- **lexer.c/h**: `&` `|` `&&` `||` `++` `--` `+=` `-=` のトークンを追加（`&&`/`||`/`++`/`--`/`+=`/`-=`
  は次フェーズで使用。今回使うのは `&`＝`TOKEN_AMP`）。
- **parser.h/c**: `NodeType` に `NODE_INDEX`（`a[i]` 読み）/ `NODE_STORE_INDEX`（`a[i]=v`、
  left=識別子 right=添字 third=値）/ `NODE_DEREF`（`*p` 読み）/ `NODE_STORE_DEREF`（`*p=v`）/
  `NODE_ADDR`（`&x`）を追加。`print_ast` も追随。
- **main.c（パーサ）**:
  - `parse_primary`: 識別子の後置 `[expr]` → `NODE_INDEX`。
  - `parse_unary`: 前置 `*` → `NODE_DEREF`、`&` → `NODE_ADDR`。
  - `parse_stmt`: 文頭 `*ident = expr;` → `NODE_STORE_DEREF`。識別子後の `[expr] = expr;` →
    `NODE_STORE_INDEX`。
- **generator.c**:
  - `find_decl_is_int(name)` を新設。要素サイズ = int配列/int* は 2、char は 1。
  - `NODE_INDEX`: base アドレス（配列は `ld hl,#var_X`、ポインタは `ld hl,(var_X)`）を push、
    添字を評価、`add hl,hl`（int時）、`pop de / add hl,de` で要素アドレス、`ld a,(hl)` で読み。
  - `NODE_STORE_INDEX`: 値 push → base push → 添字 → 要素アドレス算出 → `pop de`(値) → `ld (hl),e`。
  - `NODE_DEREF` / `NODE_STORE_DEREF`: `ld a,(hl)` / `ld (hl),e`（下位1バイト）。
  - `NODE_ADDR`: `ld hl,#var_X`。
  - `is_expr_node` に `NODE_INDEX`/`NODE_DEREF`/`NODE_ADDR` を追加（代入RHS・関数引数・return・
    条件式で汎用に評価される）。
  - `emit_data`: int 配列は要素2バイトで `.ds N*2`。
  - `ld (hl)` / `ld a,(hl)` はレジスタ間接なので `--tizix-user` 変換は触らない
    （base アドレスの `ld hl,#var`/`ld hl,(var)` 側で IY 加算済み）。
- **テスト**: `tests/ok_26.c`（`a[i]` r/w + `*p` + `&`）/ `ok_tz3.c`（`a[i]` r/w を putchar のみで）。
  `ok_12.c` は `// EXPECT: AAAAAAAAA` / `CopyTest` へ（SKIP 解除）。
  結果: `make runtest` **PASS=24 / SKIP=5**（file-io 4 + stdin 1）。
  Tizix 実機: `make tizix TEST_SRC=tests/ok_tz3.c` → `# OKTZ3` → `ABCDE`
  （IY相対下でも配列 r/w が正しく動作）。


### 完了報告 (2026/08/29) 追記15 — ++ / -- / += -= / && / ||

- **parser.h/c**: `NODE_PREINC` `NODE_PREDEC` `NODE_POSTINC` `NODE_POSTDEC`（`value`=変数名）、
  `NODE_AND` `NODE_OR`（短絡）を追加。
- **main.c（パーサ）**:
  - `parse_primary`: 識別子後の `++`/`--` → `NODE_POSTINC`/`NODE_POSTDEC`。
  - `parse_unary`: 前置 `++`/`--` → `NODE_PREINC`/`NODE_PREDEC`。
  - `parse_stmt` / `parse_simple_stmt_nosemi`: 文頭 `++i;` / `i++;` / `i += e;` / `i -= e;`。
    `+=`/`-=` は `i = i (+|-) e` へ脱糖（`NODE_ASSIGN`＋`NODE_ADD`/`NODE_SUB` を合成）。
  - 優先順位: `parse_expr`(||) → `parse_and`(&&) → `parse_equality`(== !=) → …（既存の
    `parse_expr` 本体を `parse_equality` に改名し、上に 2 段追加）。
- **generator.c**:
  - `NODE_PREINC/DEC`: `gen_load_scalar` → `inc hl`/`dec hl` → `gen_store_scalar`（結果=新値）。
  - `NODE_POSTINC/DEC`: load → `push hl` → inc/dec → store → `pop hl`（結果=旧値）。
    char は `gen_load_scalar` が 0拡張なので `inc hl` の桁上げは下位バイト格納で正しく巻き取る。
  - `NODE_AND`/`NODE_OR`: `jp z,` と無条件 `jp` のみで短絡（`--tizix-user` 変換が
    `jp nz,` 等を扱わないため意図的に z/uncond に限定）。結果は hl に 0/1。
  - `is_expr_node` に上記 6 種を追加。
- **テスト**: `tests/ok_27.c`（`for(i++)` / `+=` / `-=` / `--k` / `k--`）→ `01234 / 8 / 1`。
  `tests/ok_28.c`（`&&` `||` 短絡、`r = a>0 && a<5`）→ `A / C / 1`。
  `make runtest` **PASS=26 / SKIP=5**。Tizix 実機でも `# OK27` / `# OK28` で同出力を確認。
- **crt0_tizix.s に `_puts` 追加**（`jr` のみ・`call _kputchar` のみ → IY 加算不要）。
  これで `puts` を使うテストも `make tizix` で通る。`_printf` は未（内部 call を持つため後日）。

**次**: `#define` 定数マクロ / ビット演算 `& | ^ ~ << >>` / `crt0_tizix.s` の `_printf`。


### 完了報告 (2026/08/29) 追記16 — #define オブジェクトマクロ / ビット演算

**`#define`（オブジェクト形式マクロ）**:
- **lexer.c**: `macro_name[]` / `macro_body[]` テーブル（最大128）。`#define NAME 本体` を
  検出して登録（`NAME(` は関数形式なので行ごと無視、行コメントは本体から除外）。
- 識別子をレキシングした際、マクロ名なら **本体を新バッファとして include スタックに push
  して再レキシング**（`#include` と同じ機構を流用）。マクロ入れ子・複数トークン本体・
  `(COUNT + 2)` のような式も展開できる。深さは `MAX_INCLUDE_DEPTH` で頭打ち（`#define A A` でも無限にならない）。
- `lexer_init` で `macro_n = 0` リセット。
- テスト `tests/ok_29.c`（`#define COUNT 5` / `#define GREET "Hi\n"` / `#define BASE ('A')` /
  `#define LIMIT (COUNT + 2)`）→ `ABCDE` / `0123456` / `Hi`。

**ビット演算 `& | ^ ~ << >>`**:
- **lexer**: `^`=`TOKEN_CARET` `~`=`TOKEN_TILDE` `<<`=`TOKEN_SHL` `>>`=`TOKEN_SHR` を追加。
- **parser**: 優先順位を C 準拠で追加 —
  `|| < && < | < ^ < & < == < 関係 < << >> < + - < * /`。
  `parse_bitor` / `parse_bitxor` / `parse_bitand` / `parse_shift` を階層に挿入。`~` は単項。
  新ノード `NODE_BITAND/BITOR/BITXOR/BITNOT/SHL/SHR`。
- **generator**:
  - `& | ^`: `pop de` 後に `ld a,l / <op> e / ld l,a` ＋ 上位バイトも（16bit をバイト分割）。
  - `~`: `ld a,l / cpl / ld l,a` ＋上位。
  - `<< >>`: シフト量を `a` に取り、`add hl,hl`（左）/ `srl h \ rr l`（右・論理）を `a` 回ループ。
    分岐は `jr` のみ（`--tizix-user` 変換対象外）。
- テスト `tests/ok_30.c`（`12&10` `12|10` `12^10` `1<<3` `64>>4` `~12 & 15`）→ `8>6` / `84` / `3`。

`make runtest` **PASS=28 / SKIP=5**。Tizix 実機でも `# OK30` → `8>6 / 84 / 3` を確認。

**次**: `crt0_tizix.s` の `_printf` / Tier4（`struct` `enum` `typedef` `switch` `long` `unsigned`
… セルフホストに必要な言語機能）。


### 完了報告 (2026/08/29) 追記17 — struct / union / typedef / sizeof / メンバアクセス

セルフホスト（tzcc の `Node` は struct）への布石。**全メンバ 2バイト固定**のサブセット。

- **lexer**: `struct` `union`(=struct扱い) `typedef` `sizeof` キーワード、`->`=`TOKEN_ARROW`。
  `lexer_define_macro()` をパーサから呼べるよう公開（enum 用に追記16 で追加済み）。
- **parser.h/c**: `NODE_MEMBER`（`p->m` / `v.m` の読み）、`NODE_STORE_MEMBER`（代入）。
- **main.c**:
  - struct 型テーブル `g_structs[]`（タグ/別名 → メンバ名配列, self index）、
    変数→struct紐付け `g_svars[]`。`struct_by_name` / `member_index` / `svar_struct`。
  - `parse_struct_decl()`: `struct [tag] { 型 名; ... } [別名];` を解析。メンバは
    「識別子の次が `; , [` ならメンバ名」ヒューリスティックで型部分と分離（`NodeType type;`
    のような typedef 型名メンバも拾える）。自己参照 `struct Node *next;` OK。`typedef` は
    `enum`/`struct` へディスパッチ。
  - 宣言: `Tag *p;` → `NODE_PTR_DECL` + `svar_register`。`Tag v;` → `NODE_ARRAY_DECL`
    （`.ds メンバ数*2`）+ `svar_register`。`parse_stmt` と `parse()` 両方に追加。
  - `parse_primary` / `parse_stmt`: 識別子後の `-> ident` / `. ident` を `NODE_MEMBER` /
    `NODE_STORE_MEMBER`（`= expr` が続く場合）に。`array_size` に byte オフセット、
    `value` に `"->"` or `"."`。
  - `parse_unary`: `sizeof(int)`=2 / `sizeof(char)`=1 / `sizeof(Type)`=メンバ数*2 /
    `sizeof(T*)`=2 → `NODE_NUMBER` に畳む。
- **generator.c**:
  - `NODE_MEMBER`: `->` は `ld hl,(var_p)`（ポインタ値）、`.` は `ld hl,#var_v`（アドレス）。
    `+ ld de,#off / add hl,de`、`ld a,(hl)/inc hl/ld h,(hl)/ld l,a`（2バイトロード）。
  - `NODE_STORE_MEMBER`: 値 push → base → `+off` → `pop de` → `ld (hl),e/inc hl/ld (hl),d`。
  - `ld de,#off` は数値なので `--tizix-user` 変換は base の `ld hl,#var`/`ld hl,(var)` だけ IY 化。
  - `is_expr_node` に `NODE_MEMBER` 追加。
- **Makefile バグ修正**: 追記13 で `$(BUILD):` ルールを `all:` より前に置いたため
  `make`（引数なし）が「ディレクトリ arch/z80pack を作る」を default goal にしてしまい
  何もビルドしなかった。`.DEFAULT_GOAL := all` を追加し `all` を先頭へ。
  （`make test`/`make runtest` は `$(TARGET)` 依存で再ビルドされていたので機能検証は有効だった）
- **テスト**: `tests/ok_32.c`（`typedef struct Point{int x;int y;int tag;struct Point*next;}` /
  `p=&pt` / `p->x=3` / `pt.x` 読み / `p->tag==12 && p->next==0` / `sizeof(Point)`）
  → `34Y` / `8`。`make runtest` **PASS=30 / SKIP=5**。Tizix 実機 `# OK32` でも同出力。

**次**: `switch` / `long`（`int` 別名として受理）/ `unsigned`（同）/ 関数ポインタ →
その後 tzcc 自身のソースを tzcc でパースできるか試す（セルフホスト第一歩）。


### 完了報告 (2026/08/29) 追記18 — 連鎖メンバアクセス / 型修飾子 / _mul _div 修正

- **連鎖メンバアクセス** `a->b->c` / `a->b->c = v`: `parse_member_chain()` を新設し
  `NODE_MEMBER` をネストで構築。`StructType.memb_struct[]`（ポインタメンバが指す struct index、
  自己参照 `struct Node *next` を含む）で次段の型を追う。generator の `NODE_MEMBER` /
  `NODE_STORE_MEMBER` は `node->left` が IDENTIFIER なら `ld hl,#var`/`ld hl,(var)`、
  内側 `NODE_MEMBER` なら `generate_asm` で再帰（内側メンバ値＝ポインタが hl）。
- **型修飾子**: `long` `short` `size_t` `uintptr_t` `uint8_t`… → lexer で `TOKEN_INT` 扱い。
  `unsigned` `signed` → `TOKEN_SIGN`（幅に影響しない修飾）。パーサに `parse_type_run()` を
  追加し、宣言の型トークン連（`unsigned int` `unsigned char` 等）を消費。`char` のみ →
  1byte、それ以外 → 2byte。struct 型名の関数戻り値（`Node *new_node(...)`）を既存の
  関数定義パスへ通すよう `parse()` の型分岐を struct にも拡張。struct ポインタ引数は
  `svar_register` して関数本体で `->` が効くように。
- **`&a[i]`**: `NODE_ADDR` が `NODE_INDEX` を受けたとき「配列アドレス + i*要素サイズ」を計算
  （char=1 / int=2。struct 配列は未対応）。
- **`_mul` / `_div` を全面修正**（従来テストが結果を印字していなかったため壊れたまま放置されていた）:
  - 旧 `_div` は `HL/DE` 契約なのに codegen は `DE/HL` を渡しており逆算していた。ルーチン自体も不正。
  - 新: **スタック引数**（`4(sp)`=左, `2(sp)`=右, 呼び出し側が `pop af` x2）。
    レジスタ渡しをやめた理由は `--tizix-user` の間接CALL変換が HL を潰すため。
    `_mul`=シフト加算、`_div`=復元法（除数 < 0x8000 前提）。crt0.s / crt0_tizix.s 両方。
- **テスト**: `tests/ok_33.c`（`unsigned`/`long` + `pa->nx->v` 連鎖 read/write + `(x+y)/100`）
  → `5794`。`tests/ok_34.c`（`*` `/` の実値: `6*7` `100/20` `255/100` `1000/100`）→ `3522:`。
  `make runtest` **PASS=32 / SKIP=5**。Tizix 実機 `# OK33`=`5794` / `# OK34`=`3522:` も一致。

**次**: `switch` / `malloc`（crt0）/ 関数ポインタ / `crt0_tizix` の `_printf` →
tzcc で tzcc 自身のソースをパースできるか。


### 完了報告 (2026/08/29) 追記19 — switch / break / continue / 三項 ?:

- **lexer**: `? :` = `TOKEN_QUESTION`/`TOKEN_COLON`、`switch case default break continue` キーワード。
- **parser.h/c**: `NODE_TERNARY`（left=c right=t third=f）、`NODE_BREAK` `NODE_CONTINUE`、
  `NODE_SWITCH`（left=式 right=本体BLOCK）、`NODE_CASE`（value=数値文字列）、`NODE_DEFAULT`。
- **main.c**:
  - `parse_expr` 末尾で `?:` を処理（`||` の下・代入の上）。
  - `parse_stmt` に `break;` `continue;` `switch (e) { case N: … default: … }`。
    switch 本体は `{`…`}` を走査し、`case`/`default` を専用ノード、それ以外は通常の文として
    フラットに body BLOCK へ並べる（fallthrough をそのまま表現）。
- **generator.c**:
  - `g_brk[]` / `g_cont[]` スタック（ループ/switch のネスト）。`NODE_BREAK` → `jp Lbrk%d`、
    `NODE_CONTINUE` → `jp Lcont%d`（スタック先頭）。
  - `NODE_WHILE`/`NODE_FOR` を `Lcont%d:`（while=条件再評価 / for=post へ）/ `Lbrk%d:` 方式へ書換。
  - `NODE_SWITCH`: 式を `ld a,l`（下位8bit比較）、`case` ごとに `cp #v / jp z, Lcase%d_%d`、
    最後に `jp Ldef%d` or `jp Lbrk%d`。本体は case ラベルを出しつつ順に展開（fallthrough）。
  - `NODE_TERNARY`: `jp z, Ltf%d` / `jp Lte%d`。すべて `jp z`/`jp` なので `--tizix-user` 変換対象。
  - `is_expr_node` に `NODE_TERNARY`。
- **テスト**: `tests/ok_35.c`（`for` 内 `continue`/`break`、`switch` の fallthrough と `default`、
  `x>5 ? 'Y' : 'N'`）→ `0134` / `ABCDD` / `YN`。
  `make runtest` **PASS=33 / SKIP=5**。Tizix 実機 `# OK35` も一致。

**制限**: `switch` の比較は下位8bit のみ（トークン種別・文字なら十分）。`case` は数値/文字/
定数マクロのみ（範囲 `case 1 ... 5:` 不可）。

**次（セルフホスト向け）**: キャスト `(type)expr` / `malloc` / 関数ポインタ / `crt0_tizix._printf`。


### 完了報告 (2026/08/29) 追記20 — キャスト & x86-64 バックエンド

**キャスト `(type)expr`**: `parse_primary` の `(` で `is_type_start()` を先読み。
`(unsigned char)e` → `e & 255`、それ以外（`(int)` `(T*)` `(char*)` …）は値そのまま透過。
`tests/ok_36.c`。

**x86-64 バックエンド（`gen_x86.c` / `--march=x86`）**:
- tzcc が **x86-64 の GNU as アセンブリ**を吐き、`gcc -no-pie out.s -o out` で libc とリンクして
  Linux ネイティブ実行。crt0 不要（`printf`/`puts`/`putchar` は libc をそのまま呼ぶ）。
- 値は `%rax`（Z80 の hl 相当）。char=1byte / int=ポインタ=8byte。変数は `var_NAME` グローバル
  （RIP相対）。関数は System V（引数 rdi,rsi,… / 戻り rax）。`main:` はアンダースコア無し。
  1引数 `putc(c)` は `putchar` へ読み替え。
- `main.c` に `--march=x86` フラグ。`Makefile` に `make x86 TEST_SRC=…` と
  `make x86test`（`tests/run_x86.sh`: tzcc→gcc→ネイティブ実行→`// EXPECT:` 照合）。
  `// RUN: skip-x86 <理由>` 注記対応（socket/send は z80 ダミー専用なので x86 では skip）。
- **結果**: `make x86test` **PASS=33 / SKIP=6**（file-io 4 + stdin 1 + socket 1）。
  Z80 `make runtest` も PASS=34 で不変。同じテスト群が Z80(cpmsim/Tizix) と x86(native) の
  両方で通る。

**セルフホスト現状**: `tzcc --march=x86 parser.c` / `lexer.c` はコンパイル成功。
`main.c` `generator.c` `tizix.c` `gen_x86.c` は未（`arr[i].member`（構造体配列の添字→メンバ）、
2次元配列 `a[i][j]`、`sizeof(変数)`、関数形式 `#define F(x)`、`{…}` 配列初期化子 が未対応）。
これらを順次実装して tzcc で tzcc をビルドできる状態を目指す。


### 完了報告 (2026/08/29) 追記21 — 構造体実装の拡充 & tzcc 全ソースがパース可能に

**この回で追加/修正:**
- **struct 実サイズ**: メンバごとに実バイトサイズ（char=1 / int・ptr=`g_intsz` / ネスト struct
  = そのサイズ / 配列は次元積）でオフセット・全体サイズを計算。`g_intsz` は
  `--march=x86` で 8、Z80 で 2（backend でレイアウトが変わる）。`sizeof(Type)` /
  `sizeof(*p)`（ポインタの指す struct サイズ）に対応。
- **struct 配列** `Rec pool[N];` — 要素サイズ = struct サイズで確保・添字。
- **`arr[i].m` / `arr[i]->m` / `p->m[i]` / `p.m[i]` / 連鎖** — `parse_member_chain_from()` に
  統合。配列メンバは NODE_MEMBER を「アドレスモード(base_type=2)」にして続く `[i]` を
  NODE_INDEX（stride ベイク）で処理。2次元メンバの行サイズも把握。
  generator / gen_x86 の NODE_INDEX / NODE_MEMBER / NODE_ADDR / gen_elem_addr を
  非 IDENTIFIER ベース対応に一般化。
- **変数名 dedup**: tzcc はスコープ無しの「全変数グローバル var_名前」モデルなので、
  関数をまたいで `i` / `p` / `n` 等が再利用されると `_DATA` で多重定義になる。
  emit_data / x_emit_data で出力済み名をスキップ（同名は同一記憶域を共有）。
- **lexer**: `;` 閉じ忘れハックを「`"` 直後 1〜2 文字 + `;`」に限定。`"; ..."` や `;` を
  含む正当な文字列（`fprintf(out, "; Generated ...")` 等）が通るように。

**セルフホスト状況**: `tzcc --march=x86` で **parser.c / lexer.c / tizix.c / generator.c /
gen_x86.c / main.c すべて 0 エラーでパース完了**。`make runtest`(z80)=36 /
`make x86test`(native)=35 で回帰なし。

ただし**「パースできる」≠「動く」**。tzcc で tzcc をビルドして実際に動かすには:
1. **関数ローカルをスタックフレーム(ix / rbp 相対)へ**（案B）。現状スコープ無しの
   グローバルモデルなので `parse_expr` 等の再帰で局所変数が壊れる。最大の障壁。
2. 匿名 struct（`static struct { char var[64]; int sidx; } g_svars[512];`）のメンバオフセット。
3. 複数 `.s` をまたぐ `var_*` グローバルのリンク衝突（`static` の扱い or 単一翻訳単位化）。

**次**: 案B（スタックフレーム化）に着手する。


### 完了報告 (2026/08/30) 追記22 — x86 スタックフレーム化(案B) & tzcc2 セルフコンパイル成立

**この回の成果:** `tzcc --march=x86` が **自分自身(6ファイル)をコンパイルして動くコンパイラ
`tzcc2` を生成**できるようになった。`tzcc2` は実テスト `ok_02/05/09/13/18` を正しく
セルフコンパイルし、自ソース `all.c` も最後まで(クラッシュせず)コンパイルできる。
回帰なし: **Z80 `make runtest`=36 / x86 `make x86test`=35**。

**tzcc2 = tzcc がコンパイルした tzcc。** `tzcc`(gcc製) と挙動が違えば tzcc 自身のコード生成
バグ、という炙り出しで以下を修正:

- **案B: x86 バックエンドの関数フレーム化** (`gen_x86.c`)。関数ローカル/仮引数を
  スコープレスな `var_NAME` グローバルではなく `-off(%rbp)` スロットへ。
  `x_collect()` が関数ごとに割り付け、`x_mem/x_addr_rax/x_st/x_int_elem` で参照を
  フレーム or 真のグローバルに振り分け。ローカル char 配列の文字列初期化子は宣言点で
  スタックへコピー。これで関数間の同名変数衝突が解消、再帰が健全化(パーサは再帰的)。
- **`lexer_next_token` の構造体値返し** → `Token*`(静的バッファ)返しに変更。tzcc は
  構造体の値返し/値代入を未サポート。`next_token` はフィールドを個別コピー。
- **for 初期化子に struct/typedef 型宣言**(`for (Node *p = params; ...)`)を許可。
- **for 後置のカンマ式**(`for (...; p=p->next, i++)`)対応。init も同様。
- **`Struct *name[N]`(構造体ポインタ配列)** の添字 stride をポインタ幅に(従来 sizeof(struct))。
- **`char **` / ポインタ配列** の添字を 8バイト刻みに(`base_type=1` マーク)。
- **連鎖代入 `a=b=c=NULL` 未対応** → `new_node` の初期化を分割(未対応のままだと
  一部フィールド未初期化 → `add_child` クラッシュ)。
- **`g_svars`(変数→struct型表)を関数スコープ化** — 他関数の `Node *a` が main の
  `char *a` の添字 stride を汚染していた。
- **`*=` `/=` 複合代入をサポート**(従来 `+=` `-=` のみ)。`gen_x86.c` の `n *= 8` 等が
  無言で無効化され、ポインタ配列の `.bss` サイズが 1/8 になっていた。
- **2次元 char 配列** `x_lname[512][64]` 等 → strdup ポインタ配列に(tzcc は 2次元配列宣言
  未対応)。**`{…}` 配列初期化子**(`argreg[6]`)→ 関数化して回避。
- typedef 名レジストリ / 多重宣言子 `int a,b;` / 匿名 struct のファイルスコープ宣言 /
  `MAX_MACROS` 4096 へ増量。

**fixpoint(tzcc2.s == tzcc3.s)未達の残差分:**
1. `gen_x86` の `switch(node->type)` を tzcc2 がコンパイルすると **NODE_SWITCH の
   case ラベルが重複**(`.Lcase0_0` 多発 = `l`/`ci` カウンタが進まない)。tzcc2 の
   NODE_SWITCH コード生成バグ。
2. **ヘッダ由来の `typedef struct {…} Token;`** が tzcc2 で未登録 →
   `static Token _lx_result;` の記憶域が出ず、`Token *tk` が `var_Token` に誤解析。

**次:** 上記 2 バグを潰して fixpoint 達成 → tzcc3 が動くことを確認。
その後 Z80 側の案B(ix 相対フレーム化)へ。


### 中断メモ (2026/08/30) — セルフホスト fixpoint は後回し、関数充実を優先

**セッション切り替え時点の状態:**
- `tzcc --march=x86` によるセルフコンパイル(`tzcc2`)は**実用レベルで動作**。
  `ok_02/05/09/13/18` を正しくセルフコンパイル、`all.c` も完走。回帰なし(z80 36 / x86 35)。
- コミット済み最新: `0a00b6e`（この作業で計13コミット。追記22参照）。

**残: fixpoint(tzcc2.s == tzcc3.s / tzcc3 が動く)未達 — 深追いは後回し**
- 症状: `tzcc2` が `main.c` の `parse_struct_decl`（構造体メンバ登録）を実行すると、
  **ヘッダ(`typedef struct {…} Token;`)由来の struct のメンバが `nmemb=0` になる**。
  結果 `t.line` 等が全部 offset 0 に潰れ、`print_ast` の `switch` が `.Lcase0_0` 重複で
  `tzcc3.s` がアセンブル不能。
- 原因は「reference tzcc(gcc製)が `parse_struct_decl` のどのC構文をコード生成しくじって
  いるか」の特定。`s->nmemb++`（ポインタメンバのインクリメント）/ `cur_off += esz*count`
  / 2次元メンバ処理あたりが容疑。`tzcc2.s` の `parse_struct_decl` を逐一トレースする作業で、
  やや重いので優先度を下げる。
- この構文（ヘッダの typedef struct を別ファイルから使い、そのメンバに書き込む）は
  tzcc 自身のセルフホスト以外ではあまり出ないパターン。実アプリ用途では `arr[i].m` /
  `p->m` 等（同一TU内の struct）は既に動いている。

**優先タスク: 標準関数(ライブラリ)の充実**
- x86 は libc 直結なので当面OK。**Z80/Tizix の `crt0.s` / `crt0_tizix.s` のランタイム関数**を
  増やす。現状: `printf`(簡易フォーマット) / `puts` / `putc`/`putchar` / `getchar` /
  `fgets` / `_mul` / `_div` 程度。
- 追加候補: `strlen` `strcmp` `strcpy` `strncpy` `strcat` `memcpy` `memset` `atoi`
  `isdigit`/`isalpha` 等の ctype、`abs`、`itoa`/`sprintf`(簡易)。
- `include/` の各ヘッダ(`string.h` `stdlib.h` `stdio.h` 等)にプロトタイプを揃える。
- テストは `tests/` に追加して `make runtest`(z80) / `make x86test`(native) 両方で確認。


### 完了報告 (2026/08/30) 追記23 — crt0_tizix.s の libc 拡充 + printf / f(g(x)) codegen 修正

**この回の成果:** `crt0_tizix.s`(Tizix IY 相対 PIC)に string / mem / ctype / stdlib /
`printf` を実装。`crt0.s`(CP/M・ベアメタル)にも同じ追加関数群。Tizix 実機
(`cpmsim -z`)で `printf` 含む全関数の動作を確認。回帰なし: **z80 runtest=39 /
x86test=37**(以前 36 / 35)。

**追加した関数(両 crt0):**
- string: `strchr` `strrchr` `strstr` `strncat` `memmove` `memchr`（既存の `strlen`/
  `strcpy`/`strncpy`/`strcat`/`strcmp`/`strncmp`/`memset`/`memcpy`/`memcmp` は
  `crt0_tizix.s` へ移植）
- ctype(新規 `include/ctype.h`): `isdigit` `isalpha` `isalnum` `isspace` `isupper`
  `islower` `isxdigit` `isprint` `isgraph` `iscntrl` `ispunct` `toupper` `tolower`
- stdlib: `atol`(=atoi) `labs`(=abs) `itoa`(base 10/16、10 進のみ '-' 対応)
- `crt0_tizix.s` に `printf`（`%d %i %u %x %X %p %c %s %%`。数値整形は `_itoa` へ委譲）

**Tizix PIC 制約への対処:**
- `crt0_tizix.s` の追加関数は全て「内部 `call` 無し・絶対 `jp` 無し・絶対データラベル
  無し・分岐は `jr`/`djnz` のみ・固定ベクタ(`_kputchar` 0x3E)呼び出しのみ」で記述。
  よって `iy_reg_claude.py` / `tizix.c` の変換を通さずそのまま IY 相対で動く。
- `printf` の数値整形だけは規模が大きいので `_itoa` へ委譲。同一セグメント内 `_itoa` を
  **固定ベクタ `___sdcc_call_hl`(0x50) 経由の IY 相対 間接CALL**で呼ぶ
  (`ld hl,#_itoa / push iy / pop de / add hl,de / call ___sdcc_call_hl`)。
  `Makefile` の `TZVEC` に `-g ___sdcc_call_hl=0x0050` は既にあり、`.globl` を追加。
- `printf` 本体は IX フレーム(SDCC 版 `_kputchar` は IX/IY 保存契約)。分岐が `jr` の
  ±127 に収まるよう「中央リレー」`_pf_put1`/`_pf_back`/`_pf_done` を関数中央に配置。

**ついでに直したバグ:**
- **`f(g(x))`(関数呼び出しの戻り値を実引数に渡す)が Z80 で壊れていた** — `generator.c`
  の `NODE_CALL` 実引数ループが `NODE_CALL` 型の引数を評価せず、直前の `hl` をそのまま
  `push` していた(`is_expr_node()` に `NODE_CALL` が無い)。`|| arg->type == NODE_CALL`
  を追加。`printf("%d", strlen(s))` 等が正しく動くように。x86 バックエンドは元々
  catch-all 節があり問題なし。
- **`atoi` の符号取りこぼし** — 桁積算(×10)で `ld c,l` が符号フラグを置く `c` を破壊し、
  剰余次第で `atoi("6789")` が `-6789` になっていた。符号を `push bc`/`pop bc` で退避。
  `crt0.s` / `crt0_tizix.s` 両方。

**テスト追加:** `ok_39`(string/mem) `ok_40`(ctype、libc の isXX 非ゼロ差を `?1:0` で吸収)
`ok_41`(itoa/atoi、`RUN: skip-x86`) `ok_42`(strstr/memchr) `ok_tz4`/`ok_tz5`(Tizix
実機で `printf` 系を確認)。z80 runtest=40 / x86test=38。

**次:** `sprintf`/`snprintf`(簡易) / `strtol` / `malloc`(crt0 の bump allocator) /
`crt0.s` 側の `qsort`・`memchr` 等。あるいはセルフホスト fixpoint に戻る。


### 完了報告 (2026-09-07) 追記24 — Tizix コマンド対応: drv_tbl builtin / argv ABI / 32bit long / { } 初期化子

**目的:** SDCC+iy_reg_claude.py が分岐の多い arg 解析で壊れるため、tizix の外部コマンドを
tzcc `--tizix-user` でビルドする経路を実用化する。branch `feat/tizix-cmd`。

**この回で入れたもの (実機 = tizix cpmsim で検証):**

- **`_start` を tizix の現行 kexec_argv ABI に**: 旧「argc=1 / argv[0]=単一 blob」から
  `IY=base / HL=argc / DE=&argv[0](abs) / BC=nblk`、`SP=top-0x140`、argv[] 配列は
  カーネルが最終ブロックへ配置済み、へ。tizix `user/crt0cmd.s` と同一。
- **`drv_tbl`(0x9000) トランポリン** を `crt0_tizix.s` に追加: tzcc は関数ポインタ
  codegen 未対応なので `user/stdio.h` の `((fn_x_t)drv_tbl[N])(...)` が使えない。
  `_fopen`/`_fread`/`_fwrite`/`_fclose`/`_fputs`/`_fgets`/`_fgetc`/`_fputc`/`_fseek`/
  `_ftell`/`_feof`/`_ferror`/`_fflush`/`_kbhit`/`_getc_timeout`/`_proc_block`/`_proc_wake`/
  `_opendir`/`_readdir`/`_closedir`/`_mkdir`/`_unlink`/`_rename`/`_readdir_size` を
  各 `ld hl,(0x90xx) / jp (hl)` で。SDCC `--sdcccall 0` と tzcc のスタック規約が一致
  するのでテールジャンプで ABI 透過。putchar/getchar/puts/printf は crt0_tizix 自前実装。
- **`include/tizix.h`** 新規 (tzcc 用の素プロトタイプ stdio) + Makefile
  **`make tizixcmd CMD=<name>`**: `../tizix/user/<name>.c` をコピー・`tizix.h` を
  `stdio.h` として横に置き `--tizix-user` ビルド、`.BIN` を `../tizix/arch/z80pack/
  disks/driveb.dsk` の `/bin/<name>.bin` へ mcopy。
- **codegen バグ修正**:
  1. `char **argv` の `s = argv[i]`(ポインタ配列パラメータの添字代入)が無出力だった
     → `NODE_PTR_DECL` に式初期化子ケース追加。
  2. `p++` / `*p++`(ポインタの postinc)が 8bit に切り詰め → `gen_load_scalar` st==3
     を 16bit ロードに。
  3. 関数途中の `return X;` が hl ロードだけでフォールスルー(cp が usage 出力と
     コピーを両方実行) → `NODE_RETURN` に `ret` を出す(tzcc はスタックフレームを
     積まないので文位置では SP=入口 SP、bare ret で可)。
  4. lexer が整数接尾辞 `u/U/l/L` と 16進 `0x..` を食わず `if (i + 1u < ac)` が
     パーサ破綻 → lexer で対応。
- **32bit `long` / `short` / `uint8_t`**:
  - `short`=16bit(=int)。`uint8_t`/`int8_t`=1byte。`long`/`uint32_t`/`int32_t`=**32bit**。
  - z80 は 32bit を `DE:HL`(上位:下位)で持つ。`expr_is_long()` が式ごとに判定、
    `gen_long()` が任意の式を DE:HL に(16bit 結果はゼロ拡張)。
  - リテラル / VAR_DECL・ASSIGN 初期化 / RETURN / ADD・SUB(バイト単位 add/adc,
    sub/sbc) / EQ〜GE(32bit 減算 → 最終キャリー = 符号なし借り) / PRE・POST
    INC/DEC(上位への桁上げ) / `*(long*)p` DEREF / `long *p` の `*p`・`*p=` /
    long 配列 `arr[i]` の r/w(ストライド4) / long 関数引数(呼び 4byte push /
    受け 4byte) / long を返すプロトタイプ(`func_returns_long` レジストリ)。
  - キャストは `NODE_CVT`: `(long)`拡大 / `(int)`縮小 / `(T*)`は long ポインタ性を記録。
  - **`--tizix-user` の肝**: 32bit の var ロード/ストアは `ld hl,#label`(+IY)+
    レジスタ間接だけで組む。`tizix.c` の IY 変換は `ld de,(label+2)` /
    `ld (label+2),de` を解釈できず、上位ワード書き込みを 1バイトのゴミ書き込みに
    化けさせていた(→ `0 >= 86400UL` が真になり date のループがハング)。
- **`{ a, b, c }` 配列初期化子**: `NODE_INITLIST` + `parse_init_list()`。emit_data が
  `.db`/`.dw`(z80) / `.byte`/`.quad`(x86) で並べる。以前は `.ds` で未初期化 →
  `static const unsigned int pow10[]={10000,...}` の要素が 0 → `while(n>=pow10[i])`
  無限ループだった。
- **x86 backend**: `x_demote_long()` が宣言ノードの `base_type==2`→1 に均す
  (x86 は int/long/ptr が全部 8byte)。`NODE_CVT` / `NODE_INITLIST` 対応。

**検証:** tizix の `user/date.c` / `user/cp.c` / `user/echo.c` が
`tzcc --tizix-user`(= `make tizixcmd`)でそのままビルドでき、tizix cpmsim で正しく
動作(`date` は epoch=0 で `1970-01-01 00:00:00`、SDCC 版と一致)。
新テスト `tests/ok_long1.c`(epoch 1700000000 → `2023-11-14 22:13:20`)/
`ok_long2.c`(long 引数・配列・`*p`・プロトタイプ戻り値)。
`make runtest` 42/42・`make x86test` 39/39。`make test` の test99.c FAIL は
master でも同じ既存不具合(1 行目の mojibake)。


### tzcc 既知の制限 / TODO (2026-09-07 時点、方針つき)

**★ struct: Phase 1 完了(追記26) / Phase 2 残**

- **Phase 1 (済, 追記26)**: スカラーメンバの実バイトサイズを尊重。
  `char`=1 / `int`・ポインタ=2 / `long`=4 バイトで r/w し、隣のメンバを壊さない。
  `p->m` と `v.m` の両方。連結リスト(`p = p->next`)、`p->big`(long メンバ)+
  `printf %lu` まで tizix cpmsim で確認。混在メンバの record 型が実用になった。
- **Phase 2 残(readme 止め)**:
  - **ネスト値メンバ** `s.inner.x`。`memb_struct[]` はポインタメンバにしか
    セットしておらず、値ネスト struct メンバの先へチェーンが辿れない。
    (`.inner` の中間 NODE_MEMBER を「アドレスを返す」モードにする改修が要る)
  - **struct 値渡し / 値返し** `void f(struct T v)` / `struct T g(void)`。
    NODE_CALL / NODE_RETURN が struct 塊のコピーを扱わない。ポインタ渡しで代用。
  - **`(*p).m`**。パーサが `(*p)` 起点のメンバチェーンを認識しない。`p->m` で代用。
  - char 配列メンバ `p->name[i]` は要素サイズ 1 で動く(Phase 1 の副産物)。

**A. すぐは問題にならないが難易度が高い（着手時は覚悟が要る。当面 readme 止め）**

- **`long` の乗除算・剰余・シフト・ビット演算**(`* / % << >> & | ^ ~`)。
  現状 `long` は加減算・比較・inc/dec・代入のみ。32bit ルーチン
  (`__llmul` / `__lldiv` / `__llshl` 相当)を crt0_tizix と generator に足す必要。
  → 直近の tizix コマンドは long の +/-/比較で足りる見込み。`printf %lu` の
    10 進整形に必要な 32bit /10 だけは crt0 の `_ultoa` で個別対応した(追記25)。
- **多次元配列** `a[i][j]`(z80 backend)。部分対応のみ(struct 配列の
  `arr[i].m` 連鎖は動く)。→ **ポインタのポインタで代用できている間は後回し**。
- **素の単一 `*` ポインタ経由の多バイト r/w**。`int *p; *p = x;` は 1 バイトしか
  読み書きしない(PTR_DECL が指す先の型を記録していない。単一 `*` は
  `base_type=0` 固定)。→ **あとまわし**。Z80 はポインタが一律 2 バイトなので
  「ポインタ自体のサイズ」問題は無い。回避: 添字形 `p[0]` を使う
  (要素サイズが効く)か `(long*)` 等のキャスト形にする。
- **`long long`(64bit)**。非対応・予定なし。

**方針で解決済み（実装しないと決めた）**

- **セルフホスト / セルフコンパイル(fixpoint)**。追わない(別ラインの趣味)。無視。
- **汎用の関数ポインタ / 間接呼び出し codegen**。作らない。間接呼び出しが要る
  箇所(固定アドレスのカーネル関数・drv_tbl・コールバック相当)は、その都度
  crt0 に「固定アドレスへ jp するだけのトランポリンを名前付きで」足して、
  普通の関数呼び出しとして直接呼ぶ(= `crt0_tizix.s` の drv_tbl トランポリン方式)。
  コンパイラを自作している利点(ランタイムと codegen を一体で設計できる)を活かす。

**B. 比較的軽微 / すぐ問題になる → 追記24・25 で修正済み**

- `long` を返すプロトタイプ関数の 32bit 受け取り(`readdir_size()` / `ftell()`)。
- `long` 関数引数(呼び出し側 4 バイト push / 受け側 4 バイト)。
- `long *p` 変数の `*p` / `*p =`(4 バイト)。
- `long` 配列 `arr[i]` の 4 バイト r/w とストライド。
- `printf` の `%ld` / `%li` / `%lu` / `%lx` / `%lX` / `%lp`(追記25。長さ修飾子 `l` を
  読み、可変引数を 4 バイト消費、`_ultoa` で 32bit 整形)。
- 整数リテラルの接尾辞・16進、`{ }` 配列初期化子、関数途中 `return`、
  ポインタ postinc の桁、`char**` パラメータの添字代入。


### 完了報告 (2026-09-07) 追記26 — struct Phase 1: スカラーメンバのサイズ正しさ

**問題:** パーサは struct のメンバオフセット・実バイトサイズ(char=1 / int・ptr=2 /
ネスト struct=そのサイズ)を `g_structs[].memb_off/memb_esz` に正しく計算していたが、
**codegen が無視**して NODE_MEMBER / NODE_STORE_MEMBER を常に 2 バイトで r/w して
いた。結果:
- `char` メンバの読みは 16bit(隣バイト込み)、書きは隣メンバを破壊。
- `long` メンバは上位ワードを落とす。
→ `struct { char flag; int count; char *name; }` のような混在 record が使えない
  = 実用プログラムが組めない。

**修正:**
- `parser.h`: `Node` に `int esz`(NODE_MEMBER スカラーの実バイトサイズ 1/2/4)。
- `main.c parse_struct_decl`: `long` メンバ用に `line_long` フラグ →
  `esz = (g_intsz>=4) ? g_intsz : 4`(z80=4 / x86=8)。
- `main.c parse_member_chain_from`: 生成する NODE_MEMBER に `m->esz = esz` を刻む。
- `generator.c`:
  - `NODE_MEMBER`: `esz==1` は `ld a,(hl)/ld l,a/ld h,#0`、`esz==4` は 4 バイト
    を de:hl へ、それ以外は従来の 2 バイト。
  - `NODE_STORE_MEMBER`: `esz==1` は `ld (hl),e` のみ、`esz==4` は `gen_long` で
    値を de:hl にして 4 バイトストア、それ以外は 2 バイト。
  - `expr_is_long(NODE_MEMBER)` = `esz==4 && base_type!=2`。
  - `gen_long` に NODE_MEMBER ケース: long メンバは `generate_asm` の 4 バイト
    ロード済みなので `g32_widen`(= `ld de,#0`。上位ワード破壊)を掛けない。
- `gen_x86.c`: `NODE_MEMBER`/`NODE_STORE_MEMBER` を `esz==1` で `movzbq`/`movb`、
  それ以外 `movq`(x86 は int/ptr/long 全部 8 バイト)。

**検証:**
- `tests/ok_43.c`(char/int/long メンバの r/w と隣接非破壊、`p->m`・`v.m`)
  → z80 `make runtest` 43/43 / x86 `make x86test` 40/40。
- `tests/ok_tz7.c`(`// RUN: skip`。連結リスト `p=p->next` + `printf %d/%lu` で
  char/int/long メンバ表示)→ tizix cpmsim で
  `n0 f=1 v=100 big=4000000000` / `n1 f=2 v=200 big=4000000005` / `sum=300`。

### 完了報告 (2026-09-11) 追記27 — 後段の覗き穴最適化 peep.py(比較の 0/1 materialise を畳む)

`--tizix-user` の出力に **後段(peep.py)** を 1 段足した。tzcc 本体(tizix.c /
generator.c)は 1 行も変えていない。

**何を畳むか。** tzcc は式を「値を HL に作る」規則で一様に吐くので、`a == b` は
一度 HL に 0/1 を作ってから if がそれを 0 と比べる:

```
    or a / sbc hl, de          ← ここでフラグは既に立っている
    ld hl, #1
    jr z, LcmpN
    ld hl, #0
LcmpN:
    ld a, h
    or l
    jr z, Lend                 ← 「HL が 0 なら飛ぶ」
```

後半 12B は **直前のフラグを見る jr 1 個(2B)** と等価。`vi.c` だけで 127 箇所。

**なぜ生成器で直さないか。** 「この式の消費者は分岐である」という文脈を
生成器へ通す必要があり、式生成のほぼ全域に手が入る。出力側では完全に定型
なので、**jrfix.py と同じ立場の後段**で畳む方が blast radius が小さい。

**呼び出し位置。** `ASTZ = python3 jrfix.py` が唯一の入口なので jrfix.py の
先頭から `peep.run(asm)` を呼ぶ。**jr の範囲修正より先**に回すこと(畳むと
分岐の距離が縮むので、後に回すと無駄な間接化が残る)。

**安全性の根拠。**
- 置換後の jr はフラグを立てた命令の直後に来る(間に挟まるのは `ld hl,#imm`
  だけで、これはフラグを変えない)。よって条件は同じ。
- `Lcmp` ラベルの**参照数を数えて 1 のときだけ**畳む。`&&` / `||` の短絡が
  作る `Land` / `Lor` の合流には掛からない。
- 畳むと HL と A の値が変わる(元: 0/1、後: 比較の差分)が、tzcc は条件式の
  後で HL/A を読み直さない(必ず IX スロットから load し直す)。

**実測(tizix、同一ソースの --clean ビルド)。**

| コマンド | 前 | 後 |
|---|---|---|
| vi | 13006 | **11742** |
| sed | 2773 | 2593 |
| tail | 2082 | 1858 |
| uniq | 2060 | 1980 |
| wc | 2195 | 2085 |
| spawn | 2632 | 2498 |
| xblk | 1947 | 1863 |

tizix の回帰スイート全 12 本 + `/dev` のホスト側バイト比較 + DRIVER サイズ
ガードが PASS。sh は SDCC 製なので不変(5157B)。

**第 2 パターン collapse_ldde。** 定数を DE へ置くのに必ず HL を経由する遠回りを畳む。
**レジスタ・フラグまで完全に等価**:

```
push hl / ld hl, #K / pop de / ex de, hl   →  ld de, #K              (6B → 3B)
push hl / ld hl, #K / pop de               →  ld de, #K / ex de, hl  (5B → 4B)
```

`ex` 付きは `sbc hl, de`(順序が要る)の前、`ex` 無しは `add hl, de`(可換)の前に出る。
vi.c で 121 箇所。**vi 11742 → 11461(-281)** / sed 2593 → 2535 / tail 1858 → 1805 /
wc 2085 → 2059 / uniq 1980 → 1956 / spawn 2498 → 2458 / xblk 1863 → 1837。
回帰スイート全 12 本 + ホスト側バイト比較 + DRIVER サイズガード、すべて PASS。

**第 3 パターン collapse_cp。** tzcc は char も一度 HL へゼロ拡張してから 16bit で
引き算する。ゼロ拡張した 8bit 同士なら **Z も C も `cp` と完全に一致する**
(`hl < de` ⟺ `a < n`)ので、9B を `cp #n`(2B)へ畳める。

```
ld a, -16(ix) / ld l, a / ld h, #0      ← hl = 0x00AA(ゼロ拡張の保証)
ld de, #0x68 / or a / sbc hl, de        →  ld a, -16(ix) / cp #0x68 / jr nz, Lend
jr nz, Lend                                                     (9B → 2B = -7B)
```

安全条件:
- `ld l, a / ld h, #0` が**直前にあること**がゼロ拡張の保証。16bit 変数には掛からない。
- 定数が **0..255** のときだけ(`cp` は 8bit 即値)。シンボルや 256 以上は畳まない。
- 後続は `jr z/nz/c/nc` のみ。`cp` が壊すのは F だけで、元の列が壊していた
  HL / DE は**むしろ保存される**(壊れた値に依存はできない)。
- collapse_bool → collapse_ldde の**後**に回す(この形はその 2 つが `jr cc` と
  `ld de, #K` を作って初めて現れる)。

実測: **vi 11703 → 11138(-565、80 箇所)** / cp 2510 → 2405 / dd 5029 → 4932 /
tail 1805 → 1777 / sed 2535 → 2514 / wc 2059 → 2052。
7B × 箇所数より減っているのは、縮んだぶん jr が射程に戻って **jrfix の間接化
(1 箇所 18B)が減った**ためと思われる。回帰スイート全 12 本 + ホスト側バイト比較 +
DRIVER サイズガード PASS。

**3 パターン合計で vi は 13006 → 11138B(-1868、-14.4%)。**
