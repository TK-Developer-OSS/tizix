# x86-ia16 と z80pack / gcc 系の設計世代の差 — まとめと移行案

作成: Claude(#169〜#172 の Gemini 比較を検証して統合)。01-syscall / 02-build / 03-boot-mem-sched / 04-user / 05-z80pack が根拠。

## 結論(先に)

x86-ia16 は「共通カーネルを共有しない別実装」ではない。すでに src/ の init / kernel / io / vfs / dev を共有している(Makefile の SOBJ)。
差は **3 点だけ**: (A) syscall 層が別実装で番号が欠けている (B) ビルドが common-gcc.mk を使わない (C) user 層が tzstdio.h の複製ビルド。
Gemini の 05 は全部「arch 層に残す」と結論したが、これは採らない。(B)(C) は寄せられる。(A) も番号体系は共通に揃えられる(ディスパッチ本体は arch に残してよい)。

※ 05 は「共通 sysfile が PLAT_FLAT32 前提」と書くが、PLAT_FLAT32 は m68k/esp32 等 gcc 32bit フラット系の印。z80pack は SDCC の別系統(common-sdcc.mk)。x86-ia16 は第 3 の系統(16bit セグメント)で、どちらにも属さない。

## 差と判断

| # | 差 | 判断 | 理由 |
|---|---|---|---|
| A1 | syscall 番号の欠け(13 seek / 17 input_ready / 21〜34 / 35 con_raw / 36〜51 / 52) | **寄せる**(番号と意味は共通側に合わせて順に実装) | 17/35 は tzstdio.h が既に呼んでいて壊れている(#176)。13 は vi / cp 大物に必要 |
| A2 | arch/x86-ia16/sysfile.c の独自ディスパッチ(レジスタ渡し・farcpy) | arch に残す | 16bit セグメントのポインタ事情。ただし番号表は共通側ヘッダから取る |
| A3 | 戻り値 16bit(0xFFFF)を `_sc_ext` で 32bit 化 | 残す | ABI 変更は禁止済み(#147 の失敗) |
| B1 | ヘッダ依存(HDRS)が無い | **寄せる**(最優先・低リスク) | 古いオブジェクト混入で実験が何度も無効になった |
| B2 | TZPORT_CMDS 手書き + obj/tzport へコピーでビルド | common-gcc.mk の UCMDS 方式に寄せる | 02 の指摘どおり。16bit 用 UCFLAGS は維持 |
| B3 | ディスク像(FAT12 二段ブート) | arch に残す | ブート方式が本質的に違う |
| C1 | tzstdio.h の static 複製(printf 等) | 段階的に共通 user/stdio.h + libc へ | コマンド追加のたびの移植コストが出ている |
| D1 | スケジューラ(_isr08, SS:SP 切替) | arch に残す | HW 固有 |
| D2 | sh が SOBJ(カーネル内蔵) | 共通は外部 sh.bin。**当面は内蔵のまま** | 64KB カーネル枠の余裕と tzsh 除外(#139)次第 |
| D3 | プロセスロード(セグメントスロット) | arch に残す | 共通 kexec は flat 前提。ただし kexec の呼び口(plat フック)を共通に切り出せるか要検討 |

## 移行の順番(提案・各 1 カード 1 セッションで Gemini)

1. **B1**: arch/x86-ia16/Makefile にヘッダ依存を足す(`make clean` 不要にする)。
2. **A1(17, 35)**: kernel 側に case 17 / 35 を実装(#176、実機コンソールにも要る)。
3. **A1(13 seek)**: seek 実装 → #166(大物 cp)の再検討材料にもなる。
4. **B2**: TZPORT_CMDS → UCMDS 方式。コマンド単位で移し、test_interactive.py の FAIL 0 を毎回確認。
5. **A1(21 pipe_tail ほか)**: パイプ・外部 sh 系は x86 の pipestub.c の扱いと合わせて別途設計(Claude が先に設計メモ)。
6. C1 は 4 の後。D2/D3 は 5 の後に再評価。

## ThinkPad 実機との関係

実機で最低限必要なのは: 2(17/35 = コンソール入力)、BIOS INT 10h/16h コンソール(#174)、HDD/USB ブート(#175)。移行 1〜4 と並行でき、実機側は arch 層だけで完結する。
UEFI のみの機種だと起動しないため、機種(BIOS/UEFI、シリアルの有無)の確認が先に要る(TK 回答待ち)。

## 守ること

- syscall ABI(crt0.s / usyscall.s)と KYIELD は Gemini に触らせない。
- 各ステップで `make ARCH=x86-ia16 clean all` + test_interactive.py(2 周)を通す。
- commit は TK。

## 追記(移行 1〜3 の結果と 移行 4 の設計メモ)

- 移行 1(HDRS)・2(syscall 17/35)・3(seek 13)は完了(#178/#179/#180)。3 の検証で `prnuml`(32bit 表示)の 16bit 切り捨てを発見し修正済み。
- **移行 4(UCMDS 方式)は単純な Makefile 置き換えでは済まない。** 理由:
  1. 共通コマンドは `#include "stdio.h"` で、引用符 include は**ソースと同じ user/ の stdio.h を先に探す**。x86 が tzstdio.h を stdio.h としてコピーして使うのはこのため(cp で .c を obj/tzport に写している)。.c を直接コンパイルするには共通 user/stdio.h 自体が x86 を扱える必要がある。
  2. 共通 user/stdio.h の gcc 枝は `syscall5(func, a1..a4)` で全引数が 32bit・ポインタが平坦。x86 は `syscall5(func, al, bx, cx, si, di)` で、番号ごとにレジスタ割り当てが違う(ポインタは near + cseg)。
  3. よって寄せるには (a) 共通 stdio.h に `__ia16__` 枝を足す(関数ごとに x86 のレジスタ割り当てで syscall を呼ぶ)か、(b) x86 カーネル側を共通の引数規約(a1..a4)で受ける、のどちらか。(b) は ABI(crt0.s/usyscall.s)変更なので Gemini には触らせない。**推奨は (a)**: 共通 stdio.h の末尾に ia16 枝を追加し、tzstdio.h の中身をそこへ移す。tzstdio.h は薄い互換層に縮める。
  4. 利得は「コマンド追加のたびの tzport 複製が消える」こと。実機(ThinkPad)対応の優先度が高い間は後回しでよい。#181 は**保留(塩漬け)**にして、実機コンソール(#174)・ブート(#175)を先に進める。
