# Incline タスクトラッカー (task.md)

Incline のバックログ。下部パネル "Incline Tasks" の Webview がこれを読み書きする。
人間が直接編集してもよい。AI もタスク情報・進捗をここに追記する。

## フォーマット規約
- 1タスク = `## [#<ID>] <件名>` セクション。ID は連番（既存最大 +1）。
  この形式の見出しだけがタスク。前文の見出しは無視される。
- 見出し直後にメタを `- <キー>: <値>` で1行ずつ:
  - `status`   : 未着手 | 対応中 | 保留 | 完了 | 削除
  - `priority` : HIGH | MIDDLE | LOW（一覧は HIGH が上。省略時 MIDDLE）
  - `assignee` : 担当者（TK / AI / 空欄）
  - `creator`  / `created` : 登録者 / 登録日時（YYYY-MM-DD HH:MM）
  - `updater`  / `updated` : 更新者 / 更新日時（変更したら必ず更新）
  - `備考`     : 任意メモ（省略可）
- その下に `### 履歴`。内容（掲示板履歴）を `- <日時> <名前>: <本文>` で **追記のみ**。
  既存行は書き換え・整形・並べ替えをしない。

## ルール
- パーサは `## [#<数字>] ` で分割し、メタ行と `### 履歴` 以降（不透明文字列）だけを見る。
  `- key: value` の形式は保つ（字下げ・順序は自由）。
- `削除` はソフト削除。行は残す。UI では既定で非表示。完了/削除もファイルから消さない。
- AI: 着手時に `対応中`、区切りごとに `### 履歴` へ1行追記、完了時に `完了`。
  自分が担当なら `assignee: AI`。
- AI: 判断を仰ぎたい質問は ntfy へ push する(トピック名は公開しない。TK の手元と AI の memory にだけ置く)
  (`curl -d "..." https://ntfy.sh/<トピック>`)。該当タスクの `### 履歴` にも要旨を残す。

---

## カテゴリ
- バグ
- タスク
- 設計
- メモ

## [#1] [P1] VFS One-Linerization - Complete VFS unification
- status: 完了
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-03 21:19
- updater: AI
- updated: 2026-09-25 11:11

### 履歴
- 2026-09-03 21:19 AI: Complete VFS unification by eliminating direct calls to ff.c from the middleware layer. This involves moving FatFs behind the scenes as a backend interface.
- 2026-09-05 00:04 me: 対応後、特に異変はなかったので合格にする
- 2026-09-05 00:05 me: 完了にする
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。09-05 に me が「完了にする」と書いたがステータスが未反映だった。

## [#2] [P1] Backend Interface Definition - Define VFS ↔ block operation types
- status: 完了
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-03 21:19
- updater: AI
- updated: 2026-09-05 00:14

### 履歴
- 2026-09-03 21:19 AI: Define VFS operation ↔ block operation interface types, moving FatFs implementation behind the scenes as a backend component.

## [#3] [P1] /dev/fda Integration - Add /dev/fda to vtree and implement raw block backend
- status: 完了
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-03 21:19
- updater: AI
- updated: 2026-09-05 00:13

### 履歴
- 2026-09-03 21:19 AI: Add /dev/fda to the vtree and implement a raw block backend that interfaces with /dev/fda for block operations.

## [#4] [P2] Makefile Architecture - KOBJ splitting and conditional backend linking
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-03 21:19
- updater: AI
- updated: 2026-09-25 16:15

### 履歴
- 2026-09-03 21:19 AI: Finalize Makefile architecture: split KOBJ into core/backend, link backend conditionally based on ARCH_* parameters.
- 2026-09-25 16:15 AI(Claude): 棚卸し: KOBJ は arch/common-sdcc.mk で KOBJ_BASE(共通)+ KOBJ_ARCH(z80board は spi/sdcard)に分割済み。m68k-mega / x86-ia16 は FSBACKEND(fsbackend_fat/fatcmd/ff)を別変数で持つ。バックエンドは FAT しか無く、切り替え先(#6 Compact-FAT)は保留なので、分割の部分をもって完了とする。

## [#5] [P2] Vector Table Base Parameterization - Make vector table base configurable per architecture
- status: 保留
- priority: LOW
- assignee: AI
- creator: AI
- created: 2026-09-03 21:19
- updater: me
- updated: 2026-09-26 13:19

### 履歴
- 2026-09-03 21:19 AI: Make the vector table base address configurable per architecture (currently fixed at 0x0038) to support different platform layouts.
- 2026-09-25 16:15 AI(Claude): 棚卸し: 9/3 起票以降、着手の記録が無い(ベクタ基点は 0x0038 / 0x003B〜 固定のまま)ので対応中 → 未着手。
- 2026-09-25 22:30 AI(Claude): 棚卸しで保留へ。Z80 の RST 0x38 などハード由来の番地は変えられず(memory: 低位ベクタはハードウェア用)、ソフトベクタ 0x003B〜 は全コマンドが FIXED_SYMS で参照している。基点を変えたくなるのは 0x0000 を ROM が塞ぐ機種(#9 PC-8001 / #11 PC-8801)を始めるとき。**未解決**: その時点で要否を決める。

## [#6] [defer] Compact-FAT Implementation - Self-contained FAT system
- status: 保留
- priority: LOW
- assignee: AI
- creator: AI
- created: 2026-09-03 21:19
- updater: AI
- updated: 2026-09-03 21:19

### 履歴
- 2026-09-03 21:19 AI: Implement a self-contained FAT12 filesystem implementation (read → write-in-place → grow → create/delete → rename → block persistent).

## [#7] [note] FatFs-in-ROM Safety Verification
- status: 完了
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-03 21:19
- updater: AI
- updated: 2026-09-25 22:30

### 履歴
- 2026-09-03 21:19 AI: Verify that FatFs-in-ROM is safe after completing the above tasks (drop-in backend swap capability).
- 2026-09-25 16:15 AI(Claude): 棚卸し: 着手の記録が無いので対応中 → 未着手。
- 2026-09-25 22:30 AI(Claude): 棚卸しで完了へ。事実: z80board は FatFs をカーネル ROM に置いたまま実機で SD の FAT を読み書きしている(2026-09-19 実機で ls / ps、以後 cat / rx / dd 等。#71 で ROM を空け、z80board の回帰を z80boardsim で通している)。バックエンドの差し替え(#6 Compact-FAT)は保留のまま。

## [#8] [P2] block1 Abolition - Remove kload_driver/driver.bin and consolidate stdio into kernel image
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-03 21:22
- updater: me
- updated: 2026-09-26 13:20
- 備考: 2026/9/26 TKにより対応不要と判断する

### 履歴
- 2026-09-03 21:22 AI: Remove the block1 concept (kload_driver / driver.bin), consolidate stdio into the kernel image, and update kmem.h for block0+1=8KB work / block2-7=6 frames.
- 2026-09-25 16:15 AI(Claude): 棚卸し: 着手の記録が無い。kload_driver / DRIVER.BIN は現役(src/kexec.c・init.c)なので対応中 → 未着手。
- 2026-09-25 22:30 AI(Claude): 棚卸しで保留へ。事実: z80board のカーネル ROM の空きは 27B(2026-09-25)。DRIVER(block1、約 3.6KB)の stdio をカーネル像へ入れる余地が無い。z80pack はロード像で 719B 空き。**未解決**: FatFs(約 20KB)の外出しなど ROM を大きく空ける手が入った後に再検討。
- 2026-09-25 23:40 AI(Claude): 追記: #87 で z80board の FT245 受信リング(256B)を block1 の末尾 0x9F00 に置いた。block1 を廃止するならリングの行き先(256B・ページ境界)も要る。

## [#9] [P3] ARCH_PC8001 Flavor - Tape loader + RAM disk FS backend + archive boot
- status: 保留
- priority: LOW
- assignee: AI
- creator: AI
- created: 2026-09-03 21:22
- updater: AI
- updated: 2026-09-25 22:30

### 履歴
- 2026-09-03 21:22 AI: Implement the PC-8001 architecture flavor: N-BASIC CMT trampoline tape loader, RAM disk FS backend, and archive boot support.
- 2026-09-25 16:15 AI(Claude): 棚卸し: 着手の記録が無い(PC-8001 系の arch は未作成)ので対応中 → 未着手。
- 2026-09-25 22:30 AI(Claude): 棚卸しで保留へ。PC-8001 実機も arch もまだ無い(構想のみ)。

## [#10] [P3] Architecture Parameterization - Block size, crt0 bank switch
- status: 保留
- priority: LOW
- assignee: AI
- creator: AI
- created: 2026-09-03 21:22
- updater: AI
- updated: 2026-09-25 22:30

### 履歴
- 2026-09-03 21:22 AI: Parameterize architecture-specific settings: block size, crt0 bank switching for PC-8001/PC-8001mkII.
- 2026-09-25 16:15 AI(Claude): 棚卸し: 着手の記録が無いので対応中 → 未着手。
- 2026-09-25 22:30 AI(Claude): 棚卸しで保留へ。#9 / #11(バンク切替のある機種)を始めるときに必要になる。

## [#11] [P3] PC-8801 Architecture - Sub CPU FIFO disk or N88-DISK trampoline
- status: 保留
- priority: LOW
- assignee: AI
- creator: AI
- created: 2026-09-03 21:22
- updater: AI
- updated: 2026-09-25 22:30

### 履歴
- 2026-09-03 21:22 AI: Implement PC-8801 architecture: sub CPU FIFO disk or N88-DISK trampoline, ROM bank-out crt0, direct VRAM or ROM console.
- 2026-09-25 16:15 AI(Claude): 棚卸し: 着手の記録が無い(PC-8801 は実機購入予定の構想段階)ので対応中 → 未着手。
- 2026-09-25 22:30 AI(Claude): 棚卸しで保留へ。PC-8801 実機は購入予定の段階(memory: pc8801-target-consideration)。

## [#12] [P2] Remove "DRIVER" CP/M-BIOS Misnomer - Rename to Command Runtime
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-03 21:22
- updater: me
- updated: 2026-09-26 13:20
- 備考: 2026/9/26 TK判断により不要とする

### 履歴
- 2026-09-03 21:22 AI: Remove the CP/M-BIOS-derived misnomer "DRIVER" and rename it to "Command Runtime" (コマンドランタイム).
- 2026-09-25 16:15 AI(Claude): 棚卸し: 着手の記録が無い(DRIVER の名前は現役)ので対応中 → 未着手。
- 2026-09-25 22:30 AI(Claude): 棚卸しで保留へ。#8(DRIVER の廃止)次第。今改名だけすると DRIVER.BIN / kload_driver / 多数のコメントと task.md の用語が一斉に変わる割に得るものが名前だけ。**未解決**: #8 と同時にやる。

## [#13] [P1] x86 Known Issue - kill range check arch differentiation
- status: 完了
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-03 21:22
- updater: AI
- updated: 2026-09-25 11:11

### 履歴
- 2026-09-03 21:22 AI: Fix the kill command's range check in src/builtin.c to be arch-aware: currently Z80-specific (block 2..7), but x86 uses slot 1 as a regular process slot so `kill 1` is rejected. Needs #if for 1..7.
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。src/builtin.c の KILL_MIN/KILL_MAX がアーキ別に分かれている(x86・m68k は 1..、z80 は 2..7)。

## [#14] [TODO] pkgcmd - Fill 32B header and emit custom format
- status: 完了
- priority: LOW
- assignee: AI
- creator: AI
- created: 2026-09-03 21:22
- updater: AI
- updated: 2026-09-25 16:05

### 履歴
- 2026-09-03 21:22 AI: Fill the 32B header for pkgcmd and emit custom format as specified in bk/mnt/user-data/outputs/user/Makefile.
- 2026-09-25 16:05 AI(Claude): 棚卸しで完了へ。先頭 32B ヘッダを埋める処理は #38 で実装済み:
  src/kmem.h の HDR_*(0x10-0x11 マグ 'TZ' / 0x12 追加ブロック数 / 0x1C-0x1E はロード時に kexec が
  書き戻す)と、tzcc/Makefile の `make tizixcmd CMD=... XBLK=n` がビルド時にマグと xblk を刻む(vi / xblk が使用)。
  bk/ の Makefile にあった TODO はこの形で解消済み。

## [#15] [未実施] ROM 実測 - Verify ROM usage after make
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-03 21:22
- updater: AI
- updated: 2026-09-04 11:03

### 履歴
- 2026-09-03 21:22 AI: Verify actual ROM usage by checking kernel.img's trailing zero bytes after make. Use the provided Python script to measure top and free space.

## [#16] [未実施] Pipe | 動作確認 - Verify pipe operation on real hardware/simulator
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-03 21:22
- updater: AI
- updated: 2026-09-04 11:03

### 履歴
- 2026-09-03 21:22 AI: Verify the pipe `|` operation works correctly on real hardware or simulator (e.g., `echo hello | cat`).

## [#17] [P3] sh / init 常駐化またはプロセスビルド選択
- status: 保留
- priority: LOW
- assignee: 
- creator: AI
- created: 2026-09-03 22:55
- updater: AI
- updated: 2026-09-25 22:30

### 履歴
- 2026-09-03 22:55 AI: sh / init を常駐 or プロセスのビルド選択に。 - 参照元: doc/readme.md (P3)
- 2026-09-25 22:30 AI(Claude): 棚卸しで保留へ。事実: z80 は sh がすでに外部プロセス(user/sh.c、3 ブロック)、m68k-mega / x86-ia16 はカーネル内蔵(src/sh.c)で、アーキごとに実質選べている。1 アーキでビルド時に切り替えたい需要が出たら再開。

## [#18] [P1] boot.s の NSEC 増強
- status: 完了
- priority: HIGH
- assignee: 
- creator: AI
- created: 2026-09-03 22:56
- updater: AI
- updated: 2026-09-25 11:11

### 履歴
- 2026-09-03 22:56 AI: boot.s の NSEC を増やす。現在 NSEC=64(8KB)しか読まない。FatFs でカーネルが ~13KB になるので NSEC=128(16KB) 程度へ。 - 参照元: bk/README-bk-ignore.md
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。arch/z80pack/boot.s で NSEC=254 になっている([[nsec-254-ceiling]])。

## [#19] [P3] cpmsim 自動テスト基盤の整備
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-04 00:23
- updater: AI
- updated: 2026-09-04 11:02

### 履歴
- 2026-09-04 00:23 AI: cpmsim がインタラクティブなため自動テストが難しい。expect やスクリプトでの自動実行を可能にする。

## [#20] [P2] README/ドキュメントの公開用整備
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-04 10:34
- updater: AI
- updated: 2026-09-25 22:00

### 履歴
- 2026-09-04 10:34 AI: GitHub公開に向けて、現在の readme.md を「プロジェクトの意義・技術的ハイライト・ビルド方法・設計哲学」を含めた魅力的な構成に再編・ブラッシュアップする。
- 2026-09-25 22:00 AI(Claude): **完了(TK の確認待ち)。** リポジトリ直下に README.md を新規作成(概要と実際の
  画面例 / できること / 対応アーキと状態 / 設計の芯 / ビルド / 動かし方 / テスト / ディレクトリ / 既知の制約)。
  書いた事実は今日の実行結果とメモリで確かめたものだけ。doc/readme.md は開発記録としてそのまま残した。
  未記載: ライセンス(#22)、cpmsim / z80boardsim(z80pack-tizix)の入手方法(配布ツリーの構成が決まってから)。

## [#21] [P2] リポジトリの公開準備・クリーンアップ
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-04 10:34
- updater: AI
- updated: 2026-09-25 22:30

### 履歴
- 2026-09-04 10:34 AI: 公開用リポジトリとしてのクリーンアップ。ハードコードされたパスの排除、不要な一時ファイルの整理、公開用ビルド手順の確認。
- 2026-09-25 21:40 AI(Claude): **パスの直書きを外した(7f7019a)。** 回帰とビルドの経路にあった開発機の絶対パスは 6 箇所:
  python/tzpaths.py ほか 3 本・run_dev_dd_check.sh(TIZIX_ROOT の既定をスクリプトの位置から導く)、tzcc/Makefile
  と tzcc/tests/run_rt.sh(SDCC を PATH から探す。`make SDCC_BIN=` で指定可。CPMSIM も tizix の arch/z80pack/cpmsim)。
  **検証(事実)**: /tmp へ git clone(git 管理外の cpmsim / z80boardsim だけコピー)して make・z80board・
  run_regress.sh を通し **ALL PASS**(tzcc runtest 45/45 含む)。
  **TK 判断待ち**: python/ の一回きりの調査スクリプト(回帰にも Makefile にも出てこない。多くが
  ~ を直書き)を消すか残すか。候補: `_debug_nettest2.py` `_esp_*.sh` `_s27_*` `check_*.py`(17 本)
  `find_*.py` `inspect_*.py` `print_kexec_asm*.py` `read_kexec_c.py` `read_map.py` `trace_*.py` `run_dbg.py`
  `cpm_run.py` `run_all.sh` `sim_test.py`、回帰外の古い test_*(test_cp_dbg / test_crash_hunt / test_ls_hammer 等)。
  残すもの: tz80.py / at_modem.py / tzftp_srv.py / ntpdate_srv.py / tzpaths.py / 回帰の test_* / sched_harness.py。
  公開用ツリー(GitHub)へ出すときに選別するなら、開発リポジトリでは消さなくてもよい。
- 2026-09-25 22:30 AI(Claude): **完了(aba5ead)。** 回帰にも Makefile にも出てこない一回きりの調査スクリプトと、新しいテストで置き換わった古いテスト(計 66 本)を python/attic/ へ移動(削除ではない。attic/README.md に経緯)。ESP 関連(#62 進行中)・ネットワーク/rc の試験・Makefile が使うサーバ類・tz80.py は残した。**未解決**: attic をいずれ消すか(公開ツリーでは選別する前提なら不要)。

## [#22] [P3] GitHub公開・X告知用パッケージング
- status: 対応中
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-04 10:34
- updater: me
- updated: 2026-09-26 01:00

### 履歴
- 2026-09-04 10:34 AI: GitHub リポジトリとしてのメタデータ（LICENSE 等）設定、および X で紹介するための技術的要約の作成。
- 2026-09-25 22:50 AI(Claude): doc/announce-draft.md を作成: X 投稿案(4 本のスレッド)、同梱物のライセンス表
  (FatFs = 独自の 1 条項 BSD 風 / rocket68 = MIT / z80pack = MIT、いずれもファイルで確認)、公開前に残ること。
  **未解決**: tizix 本体のライセンス(MIT にすると同梱物と揃う、という整理だけ書いた。LICENSE は未配置)、
  z80pack-tizix の配布方法、attic と開発記録を公開ツリーに含めるか、README の URL。
- 2026-09-26 20:30 TK: 判断 ── (1) ライセンス MIT。(2)(3) rocket68 と z80pack-tizix フォークは Docker にだけ同梱し GitHub のツリーには入れない(今は同梱しない)。
  (4) dev ブランチには attic・task.md・開発記録も全部入れる(task.md は Incline で使い、賛同者と共有する)。開発メモはルートに置く。
  公開用の clone は ~/tizix/dev(git@github.com:TK-Developer-OSS/tizix.git、dev ブランチ)。
- 2026-09-26 20:30 AI(Claude): LICENSE(MIT、著作権者 TK-Developer-OSS、FatFs など同梱物の注記付き)を置いた。開発メモ doc/readme.md → ルートの
  DEVLOG.md(参照も更新、task.md の過去の記述は追記のみのルールなので触らない)。~/tizix/work/rsync_dev.sh を作った:
  開発ツリーの `git archive HEAD`(コミット済み・追跡対象だけ)を dev へ rsync(--delete、.git は除外、-n で dry-run)。
  コミット / push はしない(差分を見てから手で)。dry-run: 463 ファイル、消えるもの無し。追跡対象に秘密鍵・アカウント ID・
  トークン・メールアドレスが無いことを git grep で確認済み。

## [#23] x86-ia16 Driver Dispatch Refactoring
- status: 完了
- priority: LOW
- assignee: 
- creator: AI
- created: 2026-09-04 13:43
- updater: me
- updated: 2026-09-26 00:59

### 履歴
- 2026-09-04 13:43 AI: arch/x86-ia16 側のシステムコールルーティングを drv_tbl (テーブルドリブン) 方式にリファクタリングする。arch/z80pack と同様の構成にして移植性を向上させる。

## [#24] コマンド増やす
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: me
- created: 2026-09-06 12:01
- updater: AI
- updated: 2026-09-06 21:00
- 備考: コマンドの実装・整備。

### 履歴
- 2026-09-06 12:01 me: 実装対象コマンド一覧（ls, cp, mv, rm, mkdir, rmdir, pwd, chmod, chown, ln, dd, cat, echo, head, tail, grep, wc, sort, uniq, tee, touch, uname, ps, kill, df, du, date, id, whoami, sleep）
- 2026-09-06 12:09 AI: rmdir コマンドの実装完了。
- 2026-09-06 12:34 AI: コマンド実装状況の更新。
- 2026-09-06 14:58 AI: tail コマンドの実装完了。引き続き未実装コマンドを順次実装。
- 2026-09-06 14:58 AI: ln は FAT 環境でリンク機能が提供できないためスキップ。次は dd を実装。
- 2026-09-06 15:00 AI: dd コマンドの実装完了。次は grep を実装。
- 2026-09-06 15:02 AI: grep コマンドの実装完了。引き続き未実装コマンドを順次実装。次は wc。
- 2026-09-06 15:04 AI: wc コマンドの実装完了。sort は動的メモリ確保不可のためスキップ。次は uniq を実装。
- 2026-09-06 15:10 AI: du, id, whoami コマンドの実装完了。スキップ対象(ln, chmod, chown, sort)を除き、すべての指定コマンドの実装が完了。タスク終了。
- 2026-09-06 21:00 AI(Claude): 前回分の不具合修正。(1) make が「無限ループ」化する主因: arch/z80pack/mkfatdisk.sh の CMDS が固定リストで新コマンドを載せず、driveb.dsk 上に旧 tail.bin(fseek 巻き戻し失敗 → skip 行読み飛ばしループが EOF を無限に読む)が残存。cpmsim 起動後に tail 実行で Z80 が無限ループ。mkfatdisk.sh を user/*.bin の glob に変更(#25 の一部も解消)。(2) tail.c を fseek 不使用の 2 パス方式(fopen→行数カウント→fclose→再 fopen で位置 0→skip 行読み飛ばし)に書き換え。(3) sleep.c が絶対番地 0x8520 を uptime と決め打ち(実際は KW_TICKS=0x8527)して busy-wait が抜けずハング → getticks() ベクタ(0x0044, 100Hz)使用に修正。(4) uname.c は実装済だが Makefile COMMANDS 未登録だったので追加。cpmsim 実機で全新コマンド(wc/head/tail/uniq/grep/id/whoami/uname/sleep/du/rmdir/dd/tee)の動作確認済。make clean && make も正常終了(~1m50s)。コミット bd07de9。
- 2026-09-06 23:15 AI(Claude): パイプ改善。(5) user/sh.c のパイプ一時ファイル方式が右辺/左辺を builtin_try でしか実行せず `ps | head` 等が「no input redirect for external」で不可だった → builtin_try が NONE なら exec_external にフォールバック。(6) head/wc/grep/uniq/tail をファイル引数なしで stdin(パイプ後段 / < file)から読むよう対応。tail の stdin は巻き戻せないので tail.tmp へスプール→2 パス(リング配列 1280B はスタック溢れで Op-code trap になり不採用)。cpmsim で ps|grep / ps|tail / ls|wc / cat|tail 等を確認。コミット 79c7b90。
- 2026-09-06 23:40 AI(Claude): (7) `>>` 追記リダイレクト実装(redir_begin に append 引数、FA_OPEN_APPEND)。`echo a > f; echo b >> f` でテスト用マルチ行ファイルが作れる。(8) is_path_cmd に rmdir/wc/head/tail/uniq/du/dd/tee/grep 追加。cwd!="/" のとき相対パス引数が絶対化されず「cannot open」だった。grep は PATTERN を絶対化しないよう build_pack に skip_first 追加。コミット 4318425。
- 2026-09-06 23:55 AI(Claude): head/tail の行数オプション(-N / -n N)を試作したが、arg 解析の分岐を増やすと iy_reg_claude.py の変換が不安定になり(変換数 28→74)、パイプ後段で早期 exit したとき pipe teardown 経路でメモリ破壊 → リブートする現象が再現。commit せず revert。行数オプションと wc 複数ファイル・dd の if=/of= は自作コンパイラ移行後か、iy_reg に手を入れてから。今の head/tail/wc は「全行 or 先頭/末尾 10 行固定」「単一ファイル or stdin」で安定。

## [#25] user/のビルド結果がarch以下にきていないのでuser/がよごれる
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: me
- created: 2026-09-06 18:34
- updater: AI
- updated: 2026-09-07 01:30

### 履歴
- 2026-09-06 18:34 me: user/のビルド結果がarch以下にきていないのでuser/がよごれる
- 2026-09-07 01:30 AI(Claude): tzcc 導入の前段。user/Makefile を書き換え、生成物を全部
  OBJ=../arch/z80pack/user/ へ集約(user/ にはソース .c/.h/.s/.py だけ)。sdcc は
  `-o $(OBJ)/名前.asm` で .asm を直接そこへ、iy_reg_claude.py の -o/--map も $(OBJ)/、
  sdas/sdld/makebin も全部 $(OBJ)/。`clean` は `rm -rf $(OBJ)` + 旧 user/ 残骸掃除。
  install の read-back verify は basename でディスク側パスを組む。
  arch/z80pack/mkfatdisk.sh の glob を `user/*.bin` → `arch/z80pack/user/*.bin`。
  トップ Makefile の vestigial ルール `user/hello.bin…` を新パスへ。.gitignore に
  `arch/z80pack/user/` 追記。検証: `make clean && make` 0 で完了(3m12s、driveb.dsk に
  37 cmd + driver)。user/ に生成物ゼロ。cpmsim 実機で ls/echo(> >>)/cat/wc/grep/
  head/tail/rm 正常、clean exit。注: トップ `clean:` は元々 driveb.dsk も消すので
  今回の clean ビルドで /root のテスト残置ファイルは消えている(既存挙動)。

## [#26] tzcc で user/ コマンドをビルド・運用する(iy_reg 卒業)
- status: 完了
- priority: HIGH
- assignee: AI
- creator: me
- created: 2026-09-07 16:00
- updater: AI
- updated: 2026-09-25 11:11
- 備考: iy_reg_claude.py が分岐の多い arg 解析で壊れる件の恒久対策。

### 履歴
- 2026-09-07 16:00 me: 続きを HIGH で登録。head -2 / tail -N 等のオプション系がまだ。
- 2026-09-07 16:00 AI(Claude): ここまでの到達点。
  (A) `make tizixcmd CMD=xxx TIZIX_USER=…` で user/*.c を `tzcc --tizix-user` ビルド →
      crt0_tizix.rel リンク → driveb.dsk /bin へ mcopy。crt0_tizix.s に drv_tbl(0x9000)
      トランポリン一式 + kexec_argv ABI の _start。
  (B) recon: 19 コマンド中 18 ビルド可、cpmsim 実機で cat/wc/whoami/uname/echo/cp/date/
      head/id/du/tee/dd/tail/uniq/sleep/grep が動作。
  (C) tzcc 修正(branch feat/tizix-cmd, commit 856d416、tzcc master 未マージ):
      `x <op> 0` を符号付き比較に特殊化(tail/uniq の `while(read_line()>=0)` 無限ループ解消)、
      `_getticks` ベクタ .globl 化(sleep リンク解消)、tests/ok_44.c。z80 44/44・x86 41/41。
      さらに前段で 32bit long/short/uint8_t/`{}`初期化子/printf %ld/struct Phase1 実装済
      (commit 52cd650→…→a8a9cf2)。
  (D) grep(tzcc 版 4000B)のハングは tzcc ではなく **tizix ローダのバグ**だった:
      kexec_argv が nblk を生ファイルサイズだけで決め、最終ブロック上端 0x140(argv[]/
      文字列プール/初期SP)の予約分を勘定せず、3777〜4096B のバイナリが自分の argv/
      スタックを踏み潰していた。`fsz = f_size(&fp) + 0x140UL;` で修正、master に
      merge 済(commit 499e6e9)。cpmsim で grep 全ケース + 各サイズのコマンド検証済。
      副作用: 4KB 境界の 0x140 以内のバイナリは 2 ブロック消費(許容の方針)。
- 2026-09-07 16:00 AI(Claude): 残タスク(次セッション)。
  1. head/tail の行数オプション(-N / -n N)。iy_reg では arg 解析の分岐増で変換不安定
     (#24 履歴 2026-09-06 23:55 参照)だったが、tzcc なら分岐を増やしてよい。
  2. wc 複数ファイル対応 / dd の if= of= / sort(要動的メモリ、方式検討)。
  3. tzcc 側の残: struct Phase 2(ネスト値メンバ/値渡し・値返し/`(*p).m`)、
     long の四則以外(`* / % << >> & | ^ ~`)、`argv[i][j]` が char を 2 バイト読み
     (実害は空文字列引数のみ・要修正)、多次元配列、`long long`。
  4. tzcc feat/tizix-cmd を tzcc master にマージするか判断。
  5. tzcc 出力が iy 相対アドレッシングで肥大(grep 4000B)。1 ブロックに収めたいなら
     codegen 最適化。当面は 2 ブロック運用で可。
  6. user/ の全コマンドを恒久的に tzcc ビルドへ切り替えるか(現状 make は SDCC 版で
     driveb.dsk を作る。tzcc 版は make tizixcmd で個別上書き)。
- 2026-09-07 20:35 AI(Claude): 残タスク 1/2/3(argv[i][j])/6 を実施。tizix commit 276a083、
  tzcc(feat/tizix-cmd)commit 92ac927 + f728522。いずれも master 未マージ。
  (6=全面移行) coreutils 21 本(cat cp date dd du echo grep head id ls mkdir mv rm
  rmdir sleep tail tee touch uname uniq wc whoami)を tzcc ビルドへ。tzcc Makefile に
  `tizixcmds`(一括、TIZIX_USERBIN へ .bin 出力)、tizix トップ Makefile に `tzcc-cmds`
  (TZCC_DIR 既定 ../tzcc、user-bins/user から呼ぶ)。user/Makefile COMMANDS は
  `hello a b date_dbg rx cp_dbg test1 t2 blk wak ptx prx sh` のみに縮小。
  mkfatdisk.sh は arch/z80pack/user/*.bin glob のまま(変更不要)。sh は SDCC 据置。
  (1=head/tail -N) `-n N` と `-N` 短縮形。sh.c build_pack に prev_optarg 追加
  (is_path_cmd の絶対化が "-n" の次の行数を /root/3 に化けさせていた真因)。
  (2=wc/dd) wc 複数ファイル + total、カウンタ long→unsigned16(サイズ削減)。
  dd if=/of= パース(位置引数併用可)、sh.c build_pack に is_kv('=' 含む語は非絶対化)。
  dd の掟: sizeof(配列変数) は tzcc で intsz(=2) を返すので使わず fgetc/fputc 1B コピー。
  (3=argv[i][j]) char** を二段添字したとき外側ストライドが既定 2/8 に落ち char を
  多バイト読みしていた。generator.c/gen_x86.c の find_ptr_is_ptrarr で 1B 矯正。
  tests/ok_45.c。z80 45/45・x86 42/42。
  (追加) crt0_tizix.s の string/ctype/stdlib を tzcstr/tzcctype/tzcstdlib.s へ分離し
  libtzc.lib 化(sdld -k/-l)。iy 相対肥大で head/grep/ls/uniq/wc が 1 ブロック(3776B)
  超 → パイプ後段で「no free block」だったのを解消。head 4516→3391 等。
  (追加) ls は再入可能 getopt 依存を除去、tzcc の掟(スコープレス変数の同名衝突回避 /
  assignment-in-condition で call(args) 回避 / continue 前提の while)に沿って各コマンド調整。
  検証: cpmsim で全 coreutils 直接起動 + パイプ(cat|head/wc/uniq/grep, ls|grep, wc 複数,
  dd if=/of=/位置, head -3/-n2, tail -3/-n1)を確認。make clean&&make も正常。
- 2026-09-07 20:35 AI(Claude): 残(次セッション)。
  a. tail 4329B = 2 ブロックのまま。`X | tail`(パイプ後段)は「no free block」。
     `tail FILE` は可。1 ブロック化するにはさらに ~550B 削るか、sh のパイプ
     ブロック割当を見直す(失敗時に前段の block をリークする既存バグも露見)。
  b. cp 4878B も 2 ブロック(フィルタではないので実害小)。
  c. tzcc 残: struct Phase2 / long の四則以外 / 多次元配列 / long long / sort(動的メモリ)。
  d. tzcc feat/tizix-cmd を tzcc master へマージ判断(z80 45/45・x86 42/42 は通過)。
  e. tizix commit 276a083 / tzcc 92ac927・f728522 を各 master へマージ判断。
- 2026-09-07 22:00 AI(Claude): 「tail が 4KB 超はおかしい」指摘を受けて追い込み。
  真因は C ではなく **tzcc --tizix-user の IY 相対 PIC 展開が 3〜4 倍**(変数
  アクセスごとに push/pop+add iy の 9B プロローグ、内部 jump は push実addr+
  ex(sp),hl+ret) + **crt0_tizix が 1 モジュールで printf 一式(itoa/ultoa 込み
  ~546B)を未使用コマンドにも常時リンク**(id/whoami/uname が ~950B だった原因)。
  対策 2 件:
  (1) tzcc: printf/itoa/ultoa を tzcprintf.s へ切り出し libtzc.lib に追加
      (commit d9333fc, feat/tizix-cmd)。id 963→417 whoami 944→398 等。
  (2) tizix: tail.c を圧縮(str_to_int ループ→num2 1〜2桁、while 引数ループ→
      線形、printf→puts)、head.c も printf→puts(commit 5a2d044, master)。
      tail 4329→3481 / head 3391→2824。**両方 1 ブロックに。**
  検証: cpmsim で cat|head / cat|wc / cat|grep / ls|grep + 全 coreutils 直接
  + printf 系(grep/wc/ls/cp/date)回帰なし。tzcc runtest 45/45。make も正常。
  **未解決(a を差し替え)**: `cat | tail` は依然ダメ。ブロック数の問題ではなく
  (tail は 1 ブロックになった)、tail が stdin を tail.tmp へスプールする際に
  上流 cat も同時に FatFs を叩き **FF_FS_TINY=1 の共有セクタ窓が壊れる**
  ([[cpmsim-di-fatfs-hang]] と同系)。head/wc/grep は FS 非接触なので無問題。
  `tail FILE` は正常。恒久策候補: kernel パイプを「上流を全部バッファしてから
  下流起動」の直列化 / FF_FS_TINY=0(NSEC 天井とのトレード) / tail stdin を
  プロセス外スクラッチ RAM にバッファ。1 ブロックプロセスはスタック ~295B
  しか無いので大きな RAM バッファは不可。
  f. IY 相対 PIC 展開の最適化(tizix.c): add hl,de 前後の push af/push de … pop
     (概ね dead な flags/DE の退避)を liveness で外せば展開が半減の見込み。
- 2026-09-07 22:30 AI(Claude): `cat | tail` の真因を特定。**スタック不足**。
  tzcc 肥大で tail は 1 ブロック(4096B)にコード+データ 3481B → 実効スタック
  ~295B(SP 開始 = 上端-0x140 = 3776、code/data 末尾 3481 との差)。`tail FILE`
  なら FatFs 読み取りだけで浅く済み動くが、pipe stdin は tail_stdin →
  fopen("w")+fputc(FatFs 書込みの深い呼出し) → 入れ子で tail_file(さらに
  fopen×2)で、FatFs の呼出しチェーンがプロセス自身の code/data を踏み潰す。
  putchar デバッグを 2 個足したら `tail FILE` すら XF 出力後ハングした
  (= スタック残 49B に落ちた)。SDCC 版 tail は小さくスタックに余裕があり前は
  動いていた。head/wc/grep は FS 非接触なので 1 ブロックでも pipe 可。
  → 方針 A〜D を ntfy で照会。回答待ち。
    A=据置 / B=tizix.c の IY 変換最適化(全コマンドに効く) /
    C=カーネルが 1 ブロックプロセスにスタック用 2 ブロック目 /
    D=巻き戻せる pipe stdin のカーネル機構(旧 512B バッファ設計に近い)
  g. run_kpipe 既存バグ: マルチブロック reader を pipe 起動 → writer が
     no free block のとき sh.c:319 が PIDTBL[rn] だけ 0 にし、継続ブロック
     (PIDTBL[rn+1..]=PID_CONT)を解放し忘れてリークする。
- 2026-09-08 AI(Claude): tail の pipe stdin を GNU tail の pipe_lines 相当
  (末尾 RING=192B だけ RAM 線形バッファに保持、満杯で前半スライド、FatFs も
  heap も不使用、EOF 後に末尾 N 行切り出し)に書き換えて試作。**tail 3481→4833B
  = 2 ブロックに膨張**して不採用。内訳: `char ring[192]` は tzcc スコープレスで
  `.ds 192` グローバル +192B / memmove +60B / 後方スキャン+巡回 index が IY 相対
  展開で +~1100B(素朴には ~300B 相当)。アルゴリズムは正しいが tzcc の展開が
  1KB のループを 1 ブロック超にする。選択肢 B(tizix.c 最適化)が入ればそのまま
  採用可。spool 版 tail(3481B, 1blk, `tail FILE` 動作)を master 据置とする。
- 2026-09-08 00:30 AI(Claude): `cat | tail` の恒久対応は #27(パイプを 4KB
  ブロックバッファに統一)へ分離。当面 `cat|tail` は out of memory で綺麗に落とす
  方針(実装は #27 + sh ダイエット待ち)。#26 のコア(coreutils 移行 / head・tail -N /
  wc 複数 / dd if=/of= / argv[i][j] 修正 / libtzc 分離)は完了・検証済。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。09-08 の履歴でコアは完了・検証済み。cat|tail は #27(完了)へ切り出し済み。

## [#27] パイプを 4KB ブロックバッファに統一
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: me
- created: 2026-09-08 00:30
- updater: AI(Claude)
- updated: 2026-09-08 16:30
- 備考: #26 の `cat | tail` 不可(スタック枯渇)を受けた設計。tail 特例にせず

### 履歴
- 2026-09-08 00:30 me: 方式を決めた。
  - パイプ `A | B` は **必ず 4KB ブロックを1個確保**してリングバッファにする
    (現状の 128B カーネルリング KW_PIPE を廃し、run_kpipe が空きブロックを
    alloc / teardown で free)。tail だけの特例にしない。
  - 空きブロックが無ければ `sh: out of memory` でパイプを実行しない
    (プロセスが色々動いてバッファが取れないケースはエラーで可)。
  - ストリームが **4KB を超えたら `out of memory`** でパイプ中断。当面は
    フロー制御も wrap もしない。→ **4KB 超の正しい扱い(streaming / 動的拡張)
    は malloc/realloc 実装後。それまで LOW で保留**(下記 a)。
  - 読み側は kernel から `(base, len)` 相当を受け取り 4KB を直接読める。
    tail はこれを後方スキャンして末尾 N 行(スプール不要・遡り ~4KB)。
    4KB 窓のログ加工にも使える。
  - 期待効果: 4KB バースト転送でコンテキストスイッチが ~1/32、性能向上。
- 2026-09-08 00:30 AI(Claude): 前提と依存。
  - RAM は 0x8000-0x10000 の 8 ブロック。block0(カーネル作業)/block1(DRIVER)
    固定 → プロセス用 6 ブロック。
  - **sh が現在 14993B = 4 ブロック**(block2-5)を占有 → 空き 2 ブロック。
    パイプは `A(1) + B(1) + buf(1) = 3 ブロック`必要なので、**sh を 3 ブロック
    (<12288B)に絞らないと `cat|head` を含む全パイプが out of memory になる**。
    → sh ダイエット(SDCC 機能削り or tzcc+libtzc 載せ替え、~2.7KB 減)が
    事実上の前提タスク。
  - それまで `cat | tail` は out of memory エラーのまま(#26 の spool 版 tail は
    revert 済、`tail FILE` は動作)。誤出力やハングではなく綺麗に落ちる。
  a. [LOW] 4KB 超ストリームの streaming / 動的バッファ拡張。malloc/realloc
     (tzcc + カーネルヒープ)実装が前提。
- 2026-09-08 AI(Claude): リダイレクト `> < >> ` は #27 の対象外。パイプは
  プロセス↔プロセスで中間バッファが要るが、リダイレクトはプロセス↔ファイルで
  1プロセスが逐次 FatFs I/O するだけ(kputchar→redir_sink / kgetchar→in_src)。
  共有バッファもブロック確保も並行性も無く、FF_FS_TINY 窓も逐次なら壊れない。
  `<<` ヒアドキュメントは未実装、入れるなら「sh が行を流し込む」= パイプ側。
  注: `tail < file` は tail が argv パス無し → stdin スプール経路に入り
  `cat|tail` と同じ問題を踏む。対策は sh が `tail < file` を `tail file`
  (argv 渡し)に書き換える(小改修、#27 とは独立)。
- 2026-09-08 12:00 AI(Claude): 着手。ユーザー確認: sh ダイエットも #27 の一部として
  実施 / 4KB 超は out of memory 中断(task.md 通り)。実装ステージ:
  S0 ベースライン(test_5b_pipe.py PASS 確認済、sh.bin=14993=4blk)。
  S1 sh ダイエット: sh.bin を 3 ブロック(<=11968B、fsz=size+0x140<=0x3000)へ。
  S2 カーネル 4KB ブロックパイプ: kmem.h PID_PIPEBUF=0xFC / crt0.s sched_pick スキップ /
     pipe.c は buf[128] を廃し確保ブロック(BLKBASE(bufblk))へ。線形 4KB・wrap 無し・
     4096B 到達で ovf。pipe_setup がバッファブロック確保(空き無しは 0 返し)、
     pipe_teardown が解放。新ベクタ pipe_getwin(base,len) と pipe_ovf。
  S3 run_kpipe: pipe_setup 失敗→`sh: out of memory`。待ちループで pipe_ovf 検出→
     両者 kill + `sh: out of memory`。teardown でブロック解放。
  S4 tail: パイプ後段のとき pipe_getwin の 4KB 窓を後方スキャン(スプール廃止)。
     `tail < file` は sh が `tail file` へ書換え。
  各ステージ cpmsim 検証 + git コミット。
- 2026-09-08 13:30 AI(Claude): S1(sh ダイエット)完了・コミット。
  sh.bin 14993 → 11942B(-3051, -20%)。3 ブロック化(fsz=size+0x140<=0x3000)。
  手法: (1) main の 4 ディスパッチ枝を run_one() へ集約 + build_pack/ls_default_cwd を
  prep_side() へ集約 + is_path_cmd を 16×strcmp → NUL 区切り 1 リテラル走査。
  (2) inline char ループを string.rel(strcpy/strcat/strchr/strlen)へ置換できる所は置換
  (path_norm/build_pack 内部は逆に太ったので戻した)。(3) redir/inred を rd+rin へ統合。
  (4) エラー文言を既存リテラルへ寄せて重複削減。
  スタック対策: 3 ブロック sh は crt0cmd の SP=top-0x140 だと実効スタック ~70B しか
  残らず FatFs 呼出しで即クラッシュ → **user/crt0sh.s 新設**(sh は argv 不使用なので
  最終ブロック上端の argv[]/文字列プール 0x130 を回収、SP=top-0x10)。加えて
  build_pack の tok[48]/abs[48]・do_cd の abs[48] を struct sh_state(SH_STATE、
  block0)の scr1/scr2 へ退避。実効スタック ~324B、full regression で確認。
  iy_reg の穴を 1 件踏んで修正: `arg = "文字列リテラル"`(ポインタ変数への代入)は
  SDCC が `ld n(ix),#<(__str)` 分割ロードを吐き iy_reg が変換できずゼロページを指す。
  → リテラルは必ず関数引数として渡す(strcpy 経由に変更)。
  検証(cpmsim, fresh disk): test_5b_pipe / test_pwd_cd / test_vfs_step8/9/10 /
  test_cmds_all 全 PASS。smoke で ls/cat/head -1/wc/redir(> >> )/`>/dev/null`/
  `cat|wc`/`cat|grep`/`ls|grep`/`ps|grep`(builtin|ext 一時ファイル経路)/cd 各系 OK。
  test_pwd_cd の期待文言を "no such directory"→"no such dir" に追随(文言短縮分)。
- 2026-09-08 15:00 AI(Claude): S2(カーネル 4KB ブロックパイプ)完了・コミット。
  ・src/pipe.c 全面改修: 128B リング(KW_PIPE 内 buf[128])を廃し、pipe_setup が
    プロセス枠(block2..7)の空きブロックを 1 個 PID_PIPEBUF で予約 → BLKBASE の
    4096B を線形バッファに。pipe_teardown が解放。**wrap/フロー制御なし**、tail が
    BUFCAP(4096)到達で ovf。head/tail は絶対 index(masking 無し)。
  ・struct kpipe は KW_PIPE ヘッダ 11B のみ(0x8544-0x85C1 が空きに)。
  ・kmem.h: PID_PIPEBUF=0xFC。crt0.s sched_pick が cp/jr z でスキップ
    (_kexit の kx_cont は PID_CONT でないので触らない)。
  ・pipe_getc/getwin は writer 側 PIDTBL も見て EOF 自動検出(相手が sh 文脈でも可)。
  ・**run_kpipe をカーネルへ移設**(相談 → 方式 B)。src/pipe.c に krun_pipe():
    "/bin/X.bin" 組立 + kexec_argv ×2 + pipe_setup/attach + con_break 付き監視
    ループ + ovf 判定。戻り 0/1(oom)/2(ovf)/3(^C)。iy_reg 非適用の素の Z80。
    user/sh.c run_kpipe は krun_pipe を 1 回呼んでコードでメッセージ出すだけ
    (~60行 → ~8行)。**sh.bin 11884 → 11298B**(-586、実効スタック ~974B に激増)。
  ・drv_tbl[39] pipe_ovf / [40] pipe_getwin / [41] krun_pipe 追加
    (drvvec.s / shvec.h / user/Makefile FS_SYMS)。
  ・カーネル: s__INITIALIZER 0x7275 → 0x7478(+515B)。load_limit 0x8000 内、空き ~2.9KB。
  検証(cpmsim, fresh disk): test_5b_pipe / test_pwd_cd / test_vfs_step8/9/10 /
  test_cmds_all 全 PASS。smoke: `cat big(5632B)|wc` → `372 373 4096` +
  `sh: out of memory`(4096B ちょうどで打ち切り・ovf メッセージ表示)。
  `cat small|wc`=`1 1 6`、`ptx 40|prx`=40 行 +EOF、`ps|grep sh` OK。
  既知の割り切り: 総量 > 4096B のパイプは 4096B で打ち切り(`X|head` で X が
  4KB 超なら先頭が拾えないことがある)。streaming は #27 項目 a(malloc 後)。
- 2026-09-08 16:30 AI(Claude): S3(tail の 4KB 窓)完了 → **#27 完了**。
  ・S2 の pipe_getwin(base+len を返す)は tail 側に後方スキャンを持たせると tzcc
    展開で tail.bin が 3481→3960B = **2 ブロック**化 → `X|tail` は sh(3)+tail(2)+
    buf(1)+writer(1)=7 > 6 で不可、かつ writer 起動失敗時に reader の PID_CONT
    継続枠がリーク(pipe/コマンドが軒並み out of memory 化)。
  ・対策: 後方スキャンもカーネルへ。drv_tbl[40] を pipe_getwin → **pipe_tail(want)**
    に差し替え。src/pipe.c pipe_tail(): writer 完了を待って 4KB 窓を後方スキャンし
    末尾 want 行を kputchar。tail.c は `if (path==0) pipe_tail(want);` の 1 行に。
    → tail.bin **3056B(1 ブロック)**。`X|tail` は 6 枠ちょうどに収まる。
  ・krun_pipe に kill_proc()(先頭+PID_CONT を解放。crt0.s _kexit と同じ範囲)を
    追加し、writer 起動失敗 / reader 先行終了 / Ctrl+C の各経路のリークを封鎖。
  ・`tail < file` → sh が `tail file` に書換え(main、`rin && !*arg && cmd=="tail"`)。
  ・rc==2(ovf)のとき sh は `\nsh: out of memory\n`(打ち切られた行を分離)。
  ・tzcc(feat/tizix-cmd): crt0_tizix.s に `_pipe_tail` トランポリン(0x9050)。別コミット。
  検証(cpmsim, fresh disk): 回帰 6 本(test_5b_pipe / test_pwd_cd / test_vfs_step8/9/10 /
  test_cmds_all)全 PASS。smoke:
    `cat m.txt|tail`=l1..l5 / `|tail -2`=l4 l5 / `|tail -n 3`=l3..l5 /
    `tail m.txt` `tail -2 m.txt` `tail < m.txt` OK /
    `cat m.txt|wc`=`5 5 15` / `cat s.txt|tail -1` ×3 連続=three(リーク無し) /
    `cat big(5376B)|tail -3` → 先頭 4KB の末尾 3 行相当 + `sh: out of memory` /
    `cat big|wc`=`195 196 4096` + oom / `ps|grep sh`(builtin|ext 一時ファイル)OK。
  カーネル: s__INITIALIZER 0x7275(S1前)→ 0x755A。load_limit 0x8000 内、空き ~2.7KB。
  変更: src/pipe.c pipe.h kmem.h、arch/z80pack/crt0.s、user/{sh.c,tail.c,drvvec.s,
  shvec.h,Makefile,crt0sh.s}、doc/readme.md(changelog)、python/test_pwd_cd.py。
  tzcc: crt0_tizix.s(feat/tizix-cmd、tizix とは別コミット・master 未マージ)。
  残(#27 スコープ外・別タスク候補): (a) 4KB 超 streaming/動的拡張(malloc 前提、LOW)。
    (b) builtin|ext を今も一時ファイル(TMP.PIP)。カーネルパイプへ寄せるなら別途。
- 2026-09-08 me: 「一旦合格にしよう。使えるには使える」で #27 合格。
  手動確認で気づいた別件(#27 とは無関係・既存の coreutils 制限):
    ・`wc -l` 非対応(`-l` をファイル名扱いして "cannot open -l"、その後 m.txt は処理)。
      `cat|wc -l` も同様で stdin へ落ちない。→ wc にオプション解釈が要る。
    ・`cat m.txt|grep 4` の出力末尾に空行が 2 つ付く。→ grep の改行処理。
  いずれも別タスク化する場合は新規 ID で。

## [#28] sh / コマンドの肥大解消 → getopt とコマンド引数の恒久対策
- status: 完了
- priority: HIGH
- assignee: AI
- creator: me
- created: 2026-09-08 17:00
- updater: AI(Claude)
- updated: 2026-09-08 21:00
- 備考: sonnet5 で解けなかった 3 件(sh/ls の 4KB 超過・getopt 実装・コマンド引数)を

### 履歴
- 2026-09-08 17:00 me: sh や ls の 4KB 超過、getopt 実装、コマンド引数の問題を
  一挙解消できるか調査してほしい。fable5 へ上げる前に opus5 で見てくれ。
- 2026-09-08 17:00 AI(Claude): 調査結果。3 件は 2 つの根本原因に還元できる。
  RC1 = 実行時 IY 相対 PIC のグルーがコードを 2〜3.8 倍にしている。
        実測(iy_reg 無しでリンクした場合との比較): sh 11589→5939 / ls 3397→1826 /
        wc 3510→1852 / grep 3503→1617 / cp 4882→2158。
  RC2 = sh がヒューリスティクスで「どの引数がパスか」を推測している
        (build_pack の resolve / is_path_cmd の 16 個ハードコード / prev_optarg="-n"
        決め打ち / is_kv / grep 用 skip_first)。`wc -l` が "cannot open -l" に
        なるのも、新オプションを足すたびに sh が壊れるのもこれ。getopt を入れても
        直らない。恒久策はカーネルに cwd を持たせ、drv_open / kdir_open /
        kfs_mkdir|unlink|rename の 5 箇所で相対パスを解決すること。
  ★当初 opus5 はロード時再配置(差分リンク)を主提案にしたが、**それは過去に
    opus4.8 が起動不能に壊し fable5 が数時間かけて復旧した道**(51f4e2e /
    cadd397 → 4c7301d〜371882a)。kexec は init と sh を含む全プロセスの唯一の
    ロード経路なので、payoff ではなく blast radius で並べ直して撤回した。
    メモリ [[loadtime-reloc-forbidden]] に記録。
- 2026-09-08 18:30 AI(Claude): 「アセンブラで書けば小さくならないか。相対ジャンプや
  ポインタが活用できていない、ジャンプが多すぎる」という指摘 + 「iy_reg に jr を
  誤変換していた時期があった」で真因が確定。**手書きアセンブラは不要だった。**
  三段重ねの自縄自縛:
    (1) jr / djnz は相対分岐 = base 非依存なので iy_reg が触る必要は無い
        (ヘッダ [2] にその方針が明記されていた)。にもかかわらず実装は jr を
        全部 jp 相当へ間接化しており、1 個 14-16B のグルーが付いていた。
    (2) その変換が誤変換の温床になったため、Makefile が --no-peep を付けて
        「そもそも jr を出させない」運用になった。
    (3) 結果 sh.c の分岐は jp 405 / jr 0 に膨らみ、405 個すべてにグルーを払って
        いた。実測でこれが約 5KB。
  対応(commit 7f4c62a):
    ・iy_reg_claude.py: jr / djnz を素通しへ(ヘッダ [2] の方針どおりに戻した)
    ・user/iy_jrfix.py 新設: 挿入を跨いで ±127 を超えた jr **だけ** を間接化する
      後段パス。sdasz80 の Branching Range Exceeded を捕まえ、消えるまで反復。
      範囲外以外のエラーはそのまま落とす。全部を先回りで間接化しないのが要点。
    ・Makefile: CFLAGS_CMD から --no-peep を除去。gcse/loop/label は据置
      (入れても jp 35 / jr 192 で改善無し)。driver.c は iy_reg 非経由なので据置。
    ・iy_reg_claude.py に検証パス: 未変換の再配置参照が 1 個でも残ったらビルドを
      止める。拾い損ねた絶対参照は 0 基準のまま実行されゼロページへ wild jump し、
      実行時にしか分からずフレークするため。
  効果: jp 405/jr 0 → jp 35/jr 198。変換数 492 → 122(+ 範囲外 jr 7 個を後段処理)。
    **sh.bin 11589B(3 ブロック)→ 6507B(2 ブロック)**。
    rx 4758→3032 / date_dbg 1895→1516 / ptx 466→249 / prx 337→222 など全体が縮小。
    プロセス枠 6 個に対し従来 sh(3)+パイプ(3) で満杯だったのが 1 個空いた。
    `a &` を走らせたまま `ptx 30 | wc` が通る(従来は 7 > 6 で out of memory)。
  検証: make cleandisk からのフルビルド。回帰 5 本 PASS(test_5b_pipe / test_pwd_cd /
    test_vfs_step8/9/10)、test_5b_pipe 3 連続 3/3 PASS(フレーク無し)。
    smoke は date / リダイレクト / 各種パイプ / ls -l / クォート / cd / 相対パス /
    ovf(ptx 600|wc → 4096 + out of memory)まで確認。カーネル無変更(0x755A)。
- 2026-09-08 18:30 AI(Claude): 残タスク(この順で)。
  1. カーネル cwd (RC2 の本丸)。cwd をカーネルへ移し、drv_open / kdir_open /
     kfs_mkdir|unlink|rename で相対解決。sh から path_norm / is_path_cmd /
     build_pack の絶対化 / ls_default_cwd / prev_optarg / is_kv / skip_first を
     全削除。cd は kchdir、プロンプトは kgetcwd。**これで「コマンド引数の問題」が
     構造的に解消する。** サイズ試算では sh は更に約 1.5KB 縮む(測定用ビルドで
     8278B→、jr 対応後は 4993B の実測値あり)。カーネル側は +300〜400B の見込みで、
     [[nsec-254-ceiling]] のサイレント切詰めに注意しながら進めること。
  2. オプション解釈。POSIX getopt(user/getopt.c)は tzcc で +約 2015B と重い。
     coreutils が実際に要るのは -l -h -n N -c -i -v -a 程度なので、フラグを
     ビットマスクで返す最小 optbits() を共有 lib に 1 本置く方が安い(300〜500B)。
     これで `wc -l` / `head -c` / `grep -iv` / `ls -a` が入る。
  3. coreutils 側(tzcc ビルド)は今回の peephole 改善の恩恵を受けていない
     (tzcc 版 ls は jp 37 / jr 19)。tzcc にも「届く範囲は jr を出す」改造を
     入れると同種の削減が得られる。あわせて (IY+d) インデックス addressing が
     どちらのコンパイラでも未使用(`ld a,(iy+d)` は 3B・再配置不要なのに、今は
     同じことに 12〜15B 払っている)。tzcc は全ローカルを var_* グローバルに置く
     方式なので、フレームを IY±127 に収めて (iy+d) で触れば効きが大きい。
  4. 別件(#27 から持ち越し): `cat m.txt|grep 4` の末尾に空行が 2 つ付く。
- 2026-09-08 20:00 AI(Claude): 残タスク 1(カーネル cwd)完了。commit 4ea26e4。
  **「コマンド引数の問題」は構造的に解消。** sh から推測機構を全廃した:
  path_norm / is_path_cmd(16 個ハードコード)/ build_pack の絶対化 /
  prev_optarg("-n" 決め打ち)/ is_kv('=' 判定)/ skip_first(grep 用)/
  redirbuf の絶対化。build_pack はトークンをそのまま積むだけになった。
  cwd はカーネル(KW_CWD)が持ち、パス入口が kpath() で解決する:
    drv_open(DRIVER) / kdir_open / kfs_mkdir / kfs_unlink / kfs_rename /
    redir_begin / in_begin。sh は kchdir(drv_tbl[42])/ kgetcwd([43])だけ。
  ★実装中に自分で入れた競合を 1 件発見・修正: パス正規化スクラッチを共有 1 枚に
    すると「A が解決 → f_open 前にプリエンプト → B が上書き → A が B の
    ファイルを開く」。`cat a | tee b` や `&` で到達しうる。旧実装は sh の
    プロセス毎 argv に絶対パスを載せていたので無かった競合。KW_CURRENT で
    添字するブロック毎スクラッチ(KW_PATHS, 8×2×48B)にして解消。
  ★cp.c の `dst=="." を "/" に書き換える` 特別扱いを撤去(sh の絶対化前提)。
    ls.c の無引数既定を "/" → "."。
  サイズ: sh.bin 6507 → 5145B(11589B から -56%、2 ブロック)。cp 4882 → 4644B。
    カーネル s__GSFINAL 0x7945(load limit 0x8000 まで 1723B)。
  検証: 回帰 5 本 PASS + フレーク 9/9 PASS。/root/w と /root/w/deep からの
    相対パス smoke(cat/head -2/tail -1/wc/grep/dd if= of=/cp/mv/touch/ls/ls -l/
    rm/mkdir/> >> </cat|wc/cat|tail -1/cat|grep/cat ../m.txt/cp ../m.txt here.txt/
    rm deep/here.txt)全て正常。ルート ls の dev/ 合成表示も維持。
  x86-ia16 は builtin シェルが絶対パスで降りてくるので #ifndef で丸ごと除外(変更ゼロ)。
- 2026-09-08 20:00 AI(Claude): 残タスク更新。
  2'. オプション解釈(旧 2)。**これで `wc -l` が直る。** 最小 optbits() を共有 lib へ。
      POSIX getopt(user/getopt.c)は tzcc で +約 2015B と重く、coreutils が要るのは
      -l -h -n N -c -i -v -a 程度。sh 側はもう何も足す必要が無い(引数を素通しするだけ)。
  3'. tzcc に「届く範囲は jr を出す」改造(tzcc 版 ls は jp 37 / jr 19)。
      あわせて (IY+d) インデックス addressing が未使用(`ld a,(iy+d)` は 3B・
      再配置不要なのに 12〜15B 払っている)。tzcc は全ローカルを var_* グローバルに
      置く方式なのでフレームを IY±127 に収めれば効きが大きい。
  4'. ls の /dev 合成表示。ls は path がちょうど "/" のときだけ dev/ を出すため、
      無引数 ls には sh が cwd を 1 トークン足している(ls_default_cwd)。本来
      kdir_read が "/" の反復で dev を返すべきで、直せばこの特別扱いは消える。
  5'. 既存コマンドのバグ(#28 とは無関係。絶対パス = 変更前の渡り方でも同じく
      失敗することを実機で確認済み):
      ・uniq FILE が空を返す(`cat|uniq` は正常)
      ・du FILE が "cannot open"(du DIR は正常)
      ・tee が 1 バイトしか書かない(`<` でもパイプでも)
      ・`cat m.txt|grep 4` の末尾に空行 2 つ(#27 から持ち越し)
- 2026-09-08 20:30 me: wc -l はそもそも実装していないので一旦よい。そういう仕様ってだけ。
- 2026-09-08 20:30 AI(Claude): 上を受けて残タスクの位置づけを訂正。
  2' のオプション解釈は「バグ修正」ではなく **未実装機能の追加**(優先度は下げてよい)。
  #27 履歴が `wc -l` を不具合として記録していたが、実際には wc にオプション解釈が
  無いだけで、`-l` をファイル名として扱い "cannot open -l" と言うのは仕様どおりの
  挙動。#28 のカーネル cwd 化でも挙動は変わっていない(sh は元々 '-' 始まりを
  絶対化しなかったので、wc が受け取る文字列は前後で同一)。
  一方 5' の uniq / du / tee は「実装されているのに動かない」= 本物の不具合。
- 2026-09-08 21:00 me: 一旦残課題を readme に書いて、ここまでをコミットしてクローズする。
- 2026-09-08 21:00 AI(Claude): #28 クローズ。残課題は doc/readme.md
  「コマンド 実装状況と残タスク」節へ移した(task.md からは追わない):
    ・既知の不具合(実装済みなのに動かない。#28 以前から。絶対パスでも同様):
      uniq FILE が空 / du FILE が cannot open / tee が 1 バイト /
      `cat f|grep pat` の末尾に空行 2 つ
    ・仕様上の制限(不具合ではない): オプション解釈未実装(`wc -l` 等)/
      パイプ 4KB 上限 / ls の無引数だけ sh が cwd を足している
    ・未着手の基盤拡張: tzcc の jr 出力 + (IY+d) addressing / cwd の x86 横展開
  成果: sh.bin 11589B(3 ブロック)→ 5145B(2 ブロック、-56%)。
    コマンド引数の推測機構は全廃し、相対パスは全コマンドで通る。
    commit 7f4c62a(jr 素通し + peephole)/ 4ea26e4(カーネル cwd)。

## [#29] コマンドの実バグ(uniq / du / tee / grep)
- status: 完了
- priority: HIGH
- assignee: AI
- creator: me
- created: 2026-09-08 22:00
- updater: AI(Claude)
- updated: 2026-09-08 22:00
- 備考: #28 クローズ時に doc/readme.md へ退避した「既知の不具合」4 件。

### 履歴
- 2026-09-08 22:00 me: #27 #28 の残課題を取り込む。まずは既定路線でバグ修正から。
- 2026-09-08 22:00 AI(Claude): 4 件を実機(cpmsim)で再現 → 3 件が実バグ、
  原因は 2 つに割れた。1 件は再現せず。**完了。**
  (1) **uniq FILE がゴミを出す = スタック枯渇**(readme の「空を返す」は誤り。
      実際はバイナリのゴミが出る)。1 ブロックプロセスの実効スタックは
      3776 - バイナリサイズしかなく、uniq.bin=3640B で残り **136B、全コマンド中
      最小**(参考: wc 266 / grep 273 / ls 379 / cat 1659)。fgetc から FatFs へ
      降りるとスタックが _DATA 末尾の line[]/prev[] を踏み潰していた。
      `cat f | uniq` が無事なのは getchar 経路が浅いから。
      → LINE_LEN 128→64 で 264B 確保(同じ read_line を使う grep が 273B で正常)。
      **PIC グルー削減が入れば 128 に戻せる。**コメントに理由を明記した。
  (2) **du FILE / tee = tzcc が条件式中の代入を黙って誤コンパイル**。
      `while ((c = getchar()) != EOF)` / `while ((t = readdir(name)) != 0)` は
      parse_expr が代入を式として扱えず(代入は parse_simple_stmt_nosemi の
      文レベルだけ)、条件が裸の `c` になり、代入はループ外へ追い出され、
      比較は捨てられ、本体はループの後ろへ落ちる。generator の NODE_WHILE は
      正しく、**パーサ側の問題**。エラーにならないのが最悪だった
      (tee が NUL 1 バイトだけ書いていたのはこれ)。
      → 両コマンドを for(;;) + 明示 break へ書換え。あわせて **tzcc に
      reject_assign_in_cond() を入れ、if / while / for の全条件でこの形を
      ビルドエラーにした**(式としての代入を実装するまでの安全網)。
      既存コマンドを全数リビルドし、他に該当が無いことを確認済み。
      ついでに du FILE を実装(opendir 失敗ならファイルとして fseek(END)+ftell。
      readme の「fseek/ftell 未実装」は誤りで drv_tbl[11]/[12] に実体がある)。
  (3) **`cat f|grep pat` の末尾空行 2 つ = 再現せず**。#27 と同じ m.txt(l1..l5)
      のほか、先頭一致 / 末尾一致 / 不一致 / 20 行 / `ls|grep` / `ps|grep` /
      `grep pat FILE` / `|wc` 連結 のいずれも余分な改行なし。#28 で sh の
      skip_first(grep の第 1 引数を PATTERN とみなす特別扱い)が消えた際に
      解消したとみられる。readme の該当行は「再現せず」として書き換えた。
  サイズ: uniq.bin 3640→3512 / du.bin 1534→2091(fseek/ftell + long)/
    tee.bin 1420→1537。カーネル無変更。
  検証: 回帰 6 本 PASS(test_5b_pipe / test_pwd_cd / test_vfs_step8/9/10 /
    test_cmds_all)+ 4 件の再現スクリプトで修正確認。
  調査中に見つかった別件(仕様上の制限として readme へ追記、タスク化はしていない):
    ・`cat f | grep pat > o.txt` はパイプとリダイレクトの併用が未対応で、
      `>` が grep の引数として渡り "grep: cannot open >" になる。3 段パイプも同様。
    ・`ls` は FAT 8.3 の大文字短縮名を出すので `ls | grep txt` は空
      (`ls | grep TXT` が正しい)。
- 2026-09-08 22:00 AI(Claude): 副産物 — tzcc の PIC グルーを実測した。
  user/wc.c を PIC 有/無で同じ crt0/libtzc へリンクして比較:
    **PIC 3510B / 素 1892B → グルーが 1618B = 全体の 46%**。
    内訳(139 箇所): ld hl,(var_X) ×51=612B / ld (var_X),hl ×31=341B /
    jp z,L ×20=260B / jp L ×16=176B / call ×10=100B / ld hl,#reloc ×11=88B。
  グルー 8B のうち **7B は AF/DE の退避だけ**(実仕事は add hl,de の 1B)。
  削減の手は 3 つ。詳細と数値は doc/readme.md「未着手の基盤拡張」へ記載:
    (a) jr 化 -436B(実績あり・最も低リスク)
    (b) ベースを BC へ常駐 -970B(**tzcc の生成コードは BC を全く使っていない**)
    (c) グルーを固定番地のカーネルベクタ化 -1120B(レジスタ規約を変えない)
  ★**裏レジスタは使えない**。add hl,rr は BC/DE/HL/SP しか取らず、再配置したい
    オフセットは常に HL にある。exx は HL ごと入れ替えるのでベースを同じバンクへ
    渡せず、add hl,iy という命令も無い。裏表を跨ぐ 16bit の経路はメモリ/IX/IY/SP
    だけ。ISR も既にフルコンテキスト退避方式なので裏を専有できない(むしろ
    tzcc が裏を使い始めると切替コストが 4 ペア分増えて損)。
  → 次タスク候補: #30 tzcc コード削減((a) → (b) or (c) の順で比較)。
    #31 最小 optbits()(`wc -l` 等。未実装機能の追加であってバグではない)。

## [#30] tzcc の PIC グルー削減
- status: 完了
- priority: HIGH
- assignee: AI
- creator: me
- created: 2026-09-08 22:30
- updater: AI(Claude)
- updated: 2026-09-08 22:30
- 備考: #29 で実測。user/wc.c を PIC 有/無で同じ crt0/libtzc へリンクして比較すると

### 履歴
- 2026-09-08 22:30 me: それで着手せよ。
- 2026-09-08 22:30 AI(Claude): (a) jr 化から着手(実績があり最も低リスク)。
  方針: 生成器(generator.c)と素の z80/cpm 経路は触らず、**tizix.c の変換だけ**を
  変える。今 tizix.c は `jp L` を 14B の間接列へ、`jp z,L` を 16B へ展開しているが、
  jr は相対 = base 非依存なので変換不要。`jp` → `jr`、`jp cc` → `jr cc`(cc は
  z/nz/c/nc のみ)に落とし、範囲外になったものだけを後段 jrfix が間接化する。
  tizix の user/iy_jrfix.py と同じ機構(#28 commit 7f4c62a で検証済み)を tzcc へ移植。
  ついでに既存の潜在バグを 1 件潰す: 現行 tizix.c は `jp z,` しか個別処理せず、
  `jp nz,L` は汎用枝に落ちて `ld hl,#nz,` という壊れたシンボルを吐く
  (tzcc が nz を出さないので露見していないだけ)。
- 2026-09-08 23:30 AI(Claude): (a) jr 化 完了。**coreutils 22 本で
  48281B → 43689B(-4592B, -9.5%)。** カーネル・sh は無変更。
  実装(tzcc branch feat/tizix-cmd):
    ・tizix.c: セグメント内 `jp [cc,]LBL` を間接 JP 列(無条件 14B/条件 16B)へ
      展開するのをやめ、`jr [cc,]LBL`(2B、グルー 0B)へ落とす。条件部を正しく
      切り出すよう書き直した。
    ・tzcc/jrfix.py 新設(tizix user/iy_jrfix.py の移植)。sdasz80 を回し
      Branching Range Exceeded と報告された jr **だけ** を間接列へ戻し、
      消えるまで反復。実測 wc/tail 7 個・uniq 4 個・sleep/tee 2 個が範囲外で
      1〜2 反復で収束。全部を先回りで間接化しないのが要点。
    ・Makefile: --tizix-user の 3 経路(tizix / tizixcmd / EXTRA)を ASTZ 経由へ。
      素の z80 / CP/M 経路は PIC 変換が無いので $(AS) のまま(無変更)。
    ・潜在バグ 1 件修正: 旧実装は `jp z,` しか個別処理せず、`jp nz,LBL` は
      汎用枝に落ちて `ld hl,#nz,` という壊れたシンボルを吐いていた
      (tzcc が nz を出さないので露見していなかった)。新実装は jr にできない
      条件(po/pe/p/m)を素通しせず **ビルドを止める**(素通しはゼロページへの
      wild jump になり実行時にしか分からない → [[flaky-wildjump-root-cause]])。
  主な削減: ls 3397→3007 / wc 3510→3126 / grep 3503→3153 / head 2828→2454 /
    tail 3056→2716 / cp 4644→4190 / mv 1972→1710 / sleep 1422→1210 /
    cat 2117→1917 / dd 2948→2680。id/uname/whoami は分岐が少なく不変。
  副次効果: uniq の実効スタックが 264B → 638B に回復したので、#29 でやむなく
    64 へ落とした LINE_LEN を **128 へ戻した**(uniq.bin 3266B、スタック 510B)。
  検証: 回帰 6 本 PASS(test_5b_pipe / test_pwd_cd / test_vfs_step8/9/10 /
    test_cmds_all)。全コマンド smoke(cat/head -2/tail -2/wc/grep/uniq/du/tee/
    ls/ls -l/cp/mv/mkdir/rm/rmdir/touch/date/uname/whoami/id/sleep/dd/
    相対パス/パイプ/リダイレクト/ovf)で異常なし。tzcc test_all.sh は
    ok_tz1..tz7 含め PASS(test99 は #29 と同じ既存 FAIL)。
  次: (b) BC 常駐 -970B か (c) 固定ベクタ -1120B。(b) は tzcc の生成コードが
    BC を全く使っていないので実装は素直だが、libtzc / crt0 トランポリン /
    カーネル syscall が BC を潰す点の手当てが要る(___sdcc_call_hl と各
    トランポリンの戻りで BC=IY を復元すれば呼び出し側は無料)。
    (c) はレジスタ規約を変えないぶん低リスクだが速度を払う。**未着手。**
- 2026-09-09 00:30 me: 構造についてこれ以上変える場合は、かなりの検討のうえ
  「究極的に変えないと無理」となってはじめて前向きに検討する。
  基本的に user/ 以下のプログラムは効率が追求できておらず、tzcc 固有の性質も
  活かせていない。一旦次のセッションでクールダウンして議論する。
- 2026-09-09 00:30 AI(Claude): **#30 クローズ。(a) のみ実施、(b)(c)(d) は保留。**
  (a) を入れた時点で **coreutils 22 本中 21 本が 1 ブロックに収まり(2 ブロックは
  cp 4190B のみ)、スタック余裕も最小 510B まで回復**したため、残る案を今やっても
  機能的に変わるのは cp の 1 ブロック化だけ、という結論。
  保留にした 3 案(いずれも wc 現 3126B に対する概算。詳細は doc/readme.md
  「tzcc のコストモデル」節へ移した):
    (b) BC 常駐 -650B。**BC を永久に失う**ため却下寄り(LDIR/CPIR が使えなくなり
        将来のレジスタ割り付けも縛る)。なお「BC 完全未使用」は当方の誤りで、
        date 34 / du 8 / ls 2 箇所が long 演算のスクラッチに使っている。
    (c) 固定ベクタ -830B。カーネル変更が要る。RST 版なら -1040B だが
        **0x0000-0x0037 はハードウェア専用**(ユーザー確定)なので不可。
    (d) `IX = IY + 変数領域` + `(ix+d)` -707B。レジスタも低位ベクタもカーネルも
        触らず 2.7 倍速いが、generator.c の構造変更ではある。
  ★どれもアクセス**回数**は減らない(単価が下がるだけ)。回数を減らすのは
    レジスタ割り付け = コンパイラの本工事(wc 82 箇所 → 20 箇所前後の見込み)。
  ★裏レジスタは使えない。結論と理由は readme に記載。
  調査で分かった一番の収穫: **tzcc は値をレジスタに保持しないので、生成コードの
  変数アクセス数は C ソースに変数名が出てくる回数と 1:1**(wc: load 51/store 31。
  `c` はループ内で 6 回読み直している)。同一変数の read-modify-write は 82 中 8 件
  しかないので文内 CSE は -56B 程度。効くのは「変数名の出現回数」「仮引数の数」
  「関数呼び出しの数」を減らすこと。**コストモデルの表を doc/readme.md に新設した。**
  次セッションの議題: user/*.c をそのコストモデルで見直す。着手候補として
  wc の count_stream(5 引数中 3 個が出力先ポインタ。ファイルスコープ static に
  すれば仮引数 3 個 + `*p=v` 3 組 + 呼び出し側 11B×3×2 が消える。概算 200B 強)。
  あわせて残る唯一の実害「パイプとリダイレクトの併用不可(`a|b > f` が `>` を
  引数として黙って渡す)」も readme の「未処理の不具合」へ格上げして記載した。

## [#31] user/*.c をコストモデルで見直す
- status: 完了
- priority: HIGH
- assignee: AI
- creator: me
- created: 2026-09-09 01:00
- updater: AI
- updated: 2026-09-25 11:11
- 備考: #30 クローズ時の申し送り。コンパイラの構造変更は最後の手段とし、

### 履歴
- 2026-09-09 01:00 me: user/*.c をコストモデルで見直す。着手候補は wc の count_stream。
  残る唯一の実害は `cat f | grep pat > o.txt`(パイプ + リダイレクト併用)。
  getopt もなぜ膨張したか、AI がサイズを理由に諦めたコマンドもある。
  まず「なぜこんな非効率なプログラムなのか」の検討結果を報告すること。
- 2026-09-09 01:00 AI(Claude): 調査完了(コード変更なし・計測のみ)。
  coreutils 22 本 43689B を .s / .rel から実測分解した:
    実ロジック 9659B (22%) / PIC グルー 15642B (36%) /
    crt0+libtzc の取り込み 15776B (36%) / データ 2612B (6%)。
  **非効率の原因は 5 つで、うち上位 2 つは C の書き方ではなく「取り込み単位」と
  「もう存在しない制約に合わせた設計」だった。**
  (1) ライブラリの粒度が粗い(最大・7.6KB)。printf は 546B の単一モジュールで
      22 本中 14 本がリンクしているが、うち **10 本は %s と %d しか使っていない**
      (ls は `printf(" <DIR>  ")` = 定数文字列で引き込んでいる)。tzcstr も
      15 関数 625B が 1 モジュールで、grep は strstr 1 個のために全部払っている。
      実測: mkdir を printf 無し(putchar ループ)にすると 1452→1140B (-21%)。
      pr() を crt0 側に置けば own の増分も消えて -546B/本。
  (2) tzcc のコストモデルに合っていない書き方。tzcc はローカルも全部 `var_*` の
      静的領域なので、**仮引数は純粋な損**(呼び出し側 push + 入口 23B/個。
      退避先は global にした場合と同じ場所)。出力をポインタ引数で返すのは
      仮引数 + `&var` 11B + 間接ストアの三重払い。
      実測: wc の count_stream から出力ポインタ 3 個 + fp/from_stdin を外して
      ファイルスコープ変数にすると **3126→2822B (-304B, -9.7%)**(見積 200B 超を上回る)。
  (3) 比較・定数のコード生成が素朴で、C 側でそれを避けていない。`c == EOF` は
      -1 を即値で持てず実行時に `0-1` を作る。比較は毎回 0/1 をレジスタに作って
      から分岐するので `if (a==b)` 1 個 ~20B。しかも `from_stdin ? getchar() :
      fgetc(fp)` を **最内ループに置いている**(cat 以外の全フィルタ)。
  (4) 32bit と除算を避ける手書き展開が大きい。date は own 2603B で最大
      (86400 減算ループ + pow10 テーブル)。du は `%ld` のために printf と 32bit を
      引き込んで 1943B(ファイルは 64KB 未満なので 16bit で足りる = wc は既にそう)。
  (5) 同じコードが 22 本にコピー。`path = (argv && argv[0] && argv[0][0]) ? ...`
      が 10 本以上、read_line が grep と uniq に完全同一、mkdir/rm/rmdir は
      本文が同一で 3 本とも 1452B。
  ★getopt が 2238B(_CODE 0x8BE、load 61 / store 10)になった理由は (1)(2) の合成。
    状態を `getopt_t *g` に置き全アクセスを `g->field` にしているが、これは
    **「この環境は書込み可能な file-scope static を持てない」という SDCC/iy_reg
    時代の制約**への対策で、getopt.h の冒頭にそう明記されている。tzcc では
    ローカルが最初から静的領域なのでこの制約は消えているのに設計だけ残った。
    加えて 5 引数の入口退避 115B、エラー文のためだけの printf 546B。
    素で見積もって 700〜900B 相当。**「getopt は重いので諦める」は早すぎた結論。**
  ★サイズを理由に諦めた機能: sort(動的メモリ)/ more・less / cmp・clear・sync・
    free・reboot / オプション解釈全般(`wc -l` 等)/ cp の 512B 超 / tail の RAM
    リングバッファ版(3481→4833B で不採用)。いずれもアルゴリズムではなく
    **1 ブロック 4096B(実効 3776B、スタック = 3776 - サイズ)に入らなかった**。
    サイズを詰めるのは見た目の最適化ではなく機能を取り戻す作業。
- 2026-09-09 AI(Claude): 実装フェーズ完了。**coreutils 22 本 43689B → 31604B
  (-12085B, -27.7%)。全 22 本が 1 ブロックに収まり(cp が 2→1)、最小スタック
  余裕 1108B。** カーネルは無変更。段階と数値:
    43689 → 38505  tzcc IX ベース方式(PIC グルー 15/17B → 6B)
          → 37698  cp を逐次コピーへ(512B 上限撤廃)
          → 33301  printf を 14 本から排除(libtzc に tzcout: prs/prnum/prnuml)
          → 31833  libtzc の string 分割 + date 圧縮
          → 31604  仮引数の削減(grep/uniq/ls/tail/wc)
  途中で 2 件の実バグを踏んで直した:
   ・cp が 1 ブロックに落ちてスタック 191B になり、FatFs がイメージ末尾の
     文字列リテラルを踏んで printf が無言化(test_vfs_step10 4 件 FAIL)。
     → 逐次コピー化で 2668B / スタック 1108B。512B 上限も消えた。
   ・date が `1970-01-00`。p2() のローカル `d` と main の `d` が
     **tzcc のスコープレス変数モデルで同一記憶域**だった。
     → **tzcc に検出を入れた**(generator.c check_dup_locals。--tizix-user では
       ビルドエラー)。この検出で grep/uniq/ls/tail/wc の潜在衝突も出て、
       仮引数を廃してファイルスコープ直接参照にする形で解消(コストモデル的にも正)。
  検証: make cleandisk からのフルビルド、回帰 6 本 PASS(test_5b_pipe /
    test_pwd_cd / test_vfs_step8/9/10 / test_cmds_all)、test_5b_pipe 3 連続 PASS、
    tzcc runtest z80 45/45・x86 42/42。cpmsim スモークで全 coreutils 直接起動 +
    パイプ + リダイレクト + 相対パス + 970B/2590B ファイルの cp + prnum/prnuml の
    境界値 15 件(0 / 65535 / 65536 / 4000000000 等)。
  コミット: tzcc(feat/tizix-cmd) 2890066 ほか / tizix(master) c26eba5 ほか。
  残(判断待ち・未着手):
   ・switch の dispatch レイアウト改善(1 case 18B → 4B の余地。実測で switch は
     if/else 連鎖より 1 分岐あたり -36B)。
   ・機能分割 + exec(再帰系 cp -r / rm -r / ls -R を入れるときに必要。
     exec 未実装 / リダイレクト継承なし / ブロック 2 個消費 の 3 点が前提)。
   ・機能そのものの取捨(cp の dst=ディレクトリ解決、dd の存在、ls -h、
     wc の複数ファイル、成功メッセージ)は **ユーザー判断待ち**。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。09-09 に実装フェーズ完了(coreutils -27.7%)。

## [#32] dd で /dev/fda を叩く(生ブロックデバイスの read/write)
- status: 完了
- priority: MIDDLE
- assignee: AI
- creator: me
- created: 2026-09-09 03:10
- updater: AI
- updated: 2026-09-25 11:11
- 備考: #31 のジャッジで「dd は残す(残課題)」と決まった。cp が逐次コピー化して

### 履歴
- 2026-09-09 03:10 me: `dd if=/dev/fda of=vbr.img bs=512 count=1` が
  `dd: cannot open /dev/fda`。dd は残課題でよい。
- 2026-09-09 03:10 AI(Claude): 原因は 3 つ。いずれも未実装で、dd 側だけでは直らない。
  (1) **drv_open が /dev 配下を一律で拒否している**。user/driver.c:139
      `if (vfs_resolve(name) != -1) return -1;`(= VFS_FAT 以外は開けない)。
      コメントに「当面」とある通りの暫定ガード。/dev/fda を読むにはここを
      通す必要がある。ただし [[commands-access-via-syscall]] の方針どおり
      生パスを直に開かせるのではなく、**/dev の実体は専用ベクタ**で扱う。
  (2) **/dev/fda が型としては未実装**。src/vfs.c:76 が
      `VTREE[3].type = VT_DEV_NULL; /* Reuse NULL type or add VT_DEV_FDA */`
      と placeholder のまま。src/dev.c:21 に `{ "fda", &fda_ops }` の実体は
      あるので、VT_DEV_FDA を足して vfs_resolve → dev_ops へ繋ぐ。
      (#3「/dev/fda Integration」は vtree への登録までで、read/write 経路は未接続)
  (3) **dd の bs= / count= / skip= / seek= が未実装**(user/dd.c 冒頭に明記)。
      count= が無いと `dd if=/dev/fda of=x` がデバイス全体(360KB)を
      ~120KB の FS へ書こうとする。VBR 1 セクタを取るには最低限 bs=/count= が要る。
  加えて性能上の注意: 現行 dd は fgetc/fputc の 1 バイトコピー。生ブロック
  デバイスに対して 1 バイトずつ降りるとセクタ再読み込みが噛むので、
  bs= を入れるならブロック単位の read/write 経路も要る(FF_FS_TINY の
  共有セクタ窓と干渉しないこと。[[cpmsim-di-fatfs-hang]])。
- 2026-09-10 11:30 AI(Claude): **完了。`dd if=/dev/fdb of=VBR.IMG bs=512 count=1`
  が動き、取り出した 512B が driveb.dsk の先頭 512B と *バイト一致* する
  ところまでホスト側で確認した。** 上の (1)(2)(3) を全部塞いだ。

  ■ 名前の訂正: **VBR が欲しいなら /dev/fdb**
    旧 dev_fda.c は `disk_read(0, ...)` と書いてあったが、diskio.c は pdrv を
    **無視して常に drive B** を読んでいた。つまり「名前は fda・実体は B・
    しかも誰からも呼ばれていない」状態。今回ドライブ番号を実引数にして、
      /dev/fda = cpmsim drive A(boot + kernel の floppy)
      /dev/fdb = cpmsim drive B(FatFs がマウントしている FAT12 ボリューム)
    と 1:1 に対応させた。FAT の VBR は B 側にあるので `if=/dev/fdb`。

  ■ 変更点
   ・arch/z80pack/diskio.c: `disk_raw_rw(drive, buf, sect, op)` を切り出した
     (512B セクタ 1 本 = 128B×4)。FatFs 経路は従来どおり drive B 固定で呼ぶ。
   ・src/vfs.h/vfs.c: VT_DEV_FDA / VT_DEV_FDB を追加し、/dev/fda と /dev/fdb を
     vtree に登録(VFIXED 4→5)。placeholder の `VT_DEV_NULL 再利用` を解消。
   ・src/dev.c: **関数ポインタ(dev_ops)は使わない**方式へ。ノード type から
     switch で振り分ける kdev_open / kdev_stream / kdev_seek / kdev_tell。
     間接呼び出しがユーザープロセスの iy=base を壊す恐れがあるという掟
     (src/io.c)に合わせた。**位置(セクタ番号)もカーネルが fd 番号で持つ**。
     旧 dev_ops 表は x86-ia16 の sysfile.c が現に使っているので残してある
     (z80pack だけ #if で fda エントリを外した)。arch/z80pack/dev_fda.c は削除。
   ・user/driver.c: drv_open の「非 FAT は一律拒否」ガードを外し、/dev を
     開けるようにした。read/write/close/fseek/ftell/fflush に薄い分岐を追加。
   ・user/dd.c: bs= / count= / skip= / seek= を実装。512B バッファで
     fread/fwrite する(旧: fgetc/fputc の 1 バイトコピー)。

  ■ 踏んだ罠 3 つ(いずれも「ビルドは通るのに実行時に壊れる」型)
   (a) **DRIVER は 0x9000 から *ちょうど 4096B* しかロードされない**
       (src/kexec.c kload_driver)。/dev 対応で l__CODE が 3821→4898B に
       なり、**末尾の drv_printf が載らず sh が無言でハングした**。
       リンクは通る・警告も出ない。判定は driver.map の l__CODE ≦ 0x1000。
       対策 2 段:
         ・判定 / 位置管理 / 32bit 換算をカーネルへ寄せた(driver に
           `(long)x * 512L` を 1 つ書くだけで SDCC のヘルパが載る)。
         ・**DRIVER から --no-peep を外した**。据置の理由は「変数を 2 つ
           同時に動かさない」だけで正しさの理由は無かった(driver は
           iy_reg を通さないので jr/jp の話と無関係)。→ 3672B。
       結果 424B の余裕ができた。
   (b) **sdld の `-g sym=val` は「参照されている未定義シンボルに値を与える」**
       ものなので、誰も参照していない名前を渡すと
       `No definition of symbol ...` でリンクが落ちる。driver.c が
       vfs_resolve を呼ばなくなった瞬間にこれを踏んだ。user/Makefile の
       FS_SYMS に並べてよいのは **実際に参照しているシンボルだけ**。
   (c) **tzcc の比較は符号なし**。dd で `count = -1` を「無制限」の番兵に
       したら `count < -1` が count=1 で真になり、正しい引数が弾かれた。
       番兵は 0xFFFF 側に取り、判定は == で行う。既存の掟
       (stdio.h の FS_DENIED=0xFF)と同じ話。

  ■ ついでに直した: **sh の行長超過が無音だった**
    LINE_MAX=48。`dd if=/dev/fdb of=SEC1.IMG bs=512 count=1 skip=1` は
    ちょうど 48 文字で、末尾 1 文字が黙って落ちて `skip=` になり、dd が
    意味不明な引数エラーを出す。原因がシェルだと気付きにくいので、
    入らない文字はベル(0x07)で知らせるようにした。**48 文字上限そのものは
    据置**(sh の状態は KW_SHSTATE 544B にぴったり詰まっており、伸ばすには
    KW_CWD 以降のレイアウト変更が要る。別タスク向き)。

  ■ 仕様(意図的な割り切り)
   ・生デバイスは **512B セクタ粒度**。bs は 512 の倍数でないと転送しない
     (0 blocks copied)。バイト単位の窓を付けるとカーネルに 512B の
     バウンスバッファが要るため。通常ファイル同士なら bs は任意。
   ・生デバイスの fseek は **SEEK_SET のみ**・512 の倍数のみ。
   ・feof/ferror は生デバイスでは更新しない(誰も呼んでいないので、
     fd_table[fd] の 37B ストライド乗算を払わない)。
   ・容量は 500 セクタ(256256B の floppy を 512B 単位で切り捨て)。

  ■ 検証
   python/test_dev_dd.py(17 ケース)全 PASS: ls /dev に null/fda/fdb /
   cat /dev/null が空で戻る / fdb・fda から 1 セクタ / skip= / count=2 /
   bs=128 は 0 blocks / 通常ファイル回帰(既定 bs と bs=4)/ /dev/null への書込み。
   python/run_dev_dd_check.sh(ホスト側バイト比較)PASS:
     /dev/fdb sector0 == driveb.dsk[0:512](TIZIX ラベル入りの FAT12 VBR)
     /dev/fda sector0 == drivea.dsk[0:512](boot セクタ)
     /dev/fda skip=1  == drivea.dsk[512:1024]
   ※ drive B の 2 セクタ目は FAT 本体で、ゲストが .IMG を作った時点で変わる
     ので照合対象にしていない(skip= の中身照合は書き換わらない A 側で行う)。
   回帰 8 本 PASS(test_vi / test_sed / test_5b_pipe / test_pwd_cd /
   test_vfs_step8/9/10 / test_cmds_all)。

  ■ 残(未検証・意図的)
   ・**生デバイスへの書き込みは /dev/null 経路しか検証していない**。
     kdev_rw の write 側は実装済みだが、`dd of=/dev/fdb` を実際に流すと
     テスト用 driveb.dsk の FAT を壊す可能性があるため、無人実行では
     叩いていない。試すなら `make cleandisk` で戻せる状態で、
     `dd if=/dev/fdb of=X bs=512 count=1` → `dd if=X of=/dev/fdb bs=512
     count=1`(同一内容の書き戻し)から始めるのが安全。
   ・seek= は実装済みだが上記の理由で未検証(skip= と同じコード経路)。
   ・sh の 48 文字上限。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。09-10 の履歴に「完了」とある。

## [#33] 簡易viの移植 (vitest → user/)
- status: 完了
- category: タスク
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-09 14:08
- updater: AI
- updated: 2026-09-25 11:11

### 履歴
- 2026-09-09 14:08 AI: ~/vitest の簡易viを tizix user/ に移植し、cpmsim 上で動作確認を行う。
- 2026-09-09 16:02 AI: 進捗報告: vitestのソース構成(vi.c, vi-view.c, vi-buffer.c, vi-write.c, vi.h)を確認。user/Makefileのビルド規則調査中。
- 2026-09-10 10:20 AI(Claude): **user/vi.c を新規実装。cpmsim で 13 ケース PASS。**
  合わせて **カーネル入力層の実バグを 1 件直した(これが本題だった)**。

  ■ 方針: ソース移植ではなく「設計の移植」にした
    vitest 版をそのまま持ってくることはできない。3 つとも回避不能:
     (1) **容量**。tizix の 1 プロセスは連続 4KB × 最大 4 ブロック = 16KB。
         vitest 版は C ソースだけで 46KB(vi-buffer.c 27KB)あり、tzcc の
         生成コードでは桁が違う。
     (2) **tzcc の型**。struct のメンバは全て 2 バイト固定で char 配列メンバを
         持てないので Buffer 構造体が表現できない。sprintf も無い。
     (3) **設計**。vitest 版は編集のたびにスワップファイルへ書き戻す。
         cpmsim の FDC ではキー 1 個あたりのコストが重すぎる。
    → **本文まるごと RAM の平坦バッファ + バイト位置カーソル**に置き換えた。
      行配列も行インデックスも持たず、行頭・行末はその都度 '\n' を走査する。
      tzcc が苦手な多次元配列・関数ポインタ・struct を 1 つも使わない形。

  ■ 実装したもの(user/vi.c、tzcc の TIZIX_CMDS 経由でビルド)
    移動 h/j/k/l・カーソルキー・0・$・G・gg・w / 挿入 i・I・a・A・o・O /
    削除 x・dd / ヤンク yy・貼付 p / ex `:w` `:q` `:q!` `:wq` `:x` /
    ステータス行(ファイル名・[+]・-- INSERT --・メッセージ)/ ~ 埋め /
    ESC シーケンスのタイムアウト判定(getc_timeout)と 1 文字戻し。
    端末は 24x80 固定(tizix に窓サイズを問い合わせる手段が無い)。
    TEXT_MAX=1024 を超えるファイルは開けるが **[readonly] にして :w を拒否**
    する(黙って切り詰めて保存するのが一番まずい)。

  ■ ★本題: sh の Ctrl+C ポーリングが前景コマンドの入力を食っていた
    最初のテストは「キーがランダムに消える / 順序が入れ替わる」で全滅した。
    原因は vi ではなく **カーネル/sh 側**で、user/cat.c の冒頭に既知の制限として
    書かれていたもの([[ctrlc-kill-fg]] の系):
      sh の前景待ちループは Ctrl+C 検出のため con_break() を回し続けるが、
      旧実装は **Ctrl+C 以外の 1 バイトを読んで捨てて**いた。端末から読む
      前景コマンド(対話 cat / vi)とは 1 バイト単位の取り合いになる。
    直し方(src/io.c):
      ・con_break は Ctrl+C 以外を **1 バイトの戻しバッファ con_ung へ退避**する。
        con_ung が埋まっている間は新たに読まないので 1 バイトで足りる。
      ・kgetchar は物理層より先に con_ung を見る。**確認は待ちループの中**で
        行う(ループ外で 1 回だけだと、待っている最中に con_break が横から
        入れた 1 バイトを永久に取り逃してハングする)。
      ・「con_ung の確認」と「物理層からの取り出し」は **di で束ねる**。
        割ると追い越しが起きる。実測で `vi` と打つと `iv` になった:
          kgetchar が con_ung を見る(空)→ プリエンプト → con_break が 'v' を
          con_ung へ → kgetchar が RXRDY を見て後着の 'i' を返す。
      ・DRIVER の kbhit / getc_timeout も con_pending() を見るようにした
        (見ないと、sh に先取りされた文字が RXRDY から消えて vi の ESC
        シーケンス判定が空振りする)。drv_tbl[44] に con_break を出し、
        外部 sh の sh_break を自前 kbhit+getchar からこれに置き換えた。
    → **対話 cat も直ったはず**(cat.c 冒頭の既知制限は解消。別途確認したい)。

  ■ ついでに直した実バグ
    外部 sh は kexec_argv の戻り 0(= 連続空きブロック無し)を
    `sh: %s: not found` と表示していた。**資源不足を「ファイルが無い」と
    言う**ので、存在するファイルを探しに行かせてしまう。カーネル側 sh
    (src/sh.c)は元から `no free block` と出していて、外部 sh 化のときに
    握り潰されていた。4 ブロック要求の vi は背景ジョブが 1 個居るだけで
    ここに来るので、実害として出た。

  ■ サイズ(残課題)
    vi.bin 13550B = **4 ブロック**、実効スタック 2514B。動作は検証済みだが、
    プロセス枠は block2..7 の 6 個で sh が 2 個使うため、**vi が動く条件は
    「他に何も走っていないこと」**。背景ジョブが 1 個でもあると
    `sh: vi: no free block` になる(クラッシュはしない。テスト済み)。
    3 ブロック(≤11968B)に収めたい。今回の削減で 14358→13550B。
    残る主因は tzcc のコストモデルそのもので、内訳は:
     ・switch の case ラベル 1 個につき **PIC 間接ジャンプ 15 命令**。
       関数が大きいと jr の射程(±127B)に入らず jrfix が間接化する
       (vi 全体で 83 箇所)。do_cmd は 26 ラベルで ~620B がこれ。
       カーソルキーを伝統キーへ畳んでラベルを 5 個減らした。
     ・変数アクセス 1 回 6B のまま。line_head(cur) / line_tail(cur) の
       呼び直しが効く。j/k は move_line() に括り、カーソル正規化は
       メインループ 1 箇所へ集約した。
     ・0 で始まる大域の初期化代入を全廃(kexec が .BIN をそのまま読むので
       _DATA はリンク時の値 = 0 で始まる)。
    次にやるなら: do_cmd の分割 / w・yy・p の取捨 / render の圧縮。

  ■ 未実装(意図的)
    undo・redo / 複数行カウント(12dd)/ 単語削除 dw / 検索 / 行番号指定 /
    `:w FILE` / スワップファイル / バックアップ(.bak)/ タブ展開
    (TAB は空白 1 個に落とす)/ 80 桁を超える行の折り返し(横は切って表示)。

  検証: python/test_vi.py 新設(15 ケース)。起動描画 / :q / x / A / i / o /
   j+dd / :q! / :q 拒否 / 新規ファイル作成 / 3 行に育てる / カーソルキー /
   差分描画 / ブロック不足。**全 PASS。** 回帰も PASS
   (test_sed / test_5b_pipe / test_pwd_cd / test_vfs_step8/9/10 /
   test_cmds_all / test_a_bg / test_a_fg)。カーネルサイズは NSEC=254 のまま。
- 2026-09-10 12:10 AI(Claude): **差分描画を入れた(実機シリアル対策)。**
  初版は 1 打鍵ごとに 24 行を全部描き直していた。cpmsim では気付かないが、
  実機の 115200 シリアルでは **1 キーあたり ~2KB ≒ 170ms** かかる ── 移植先の
  Z80 実機([[tizix-scope-and-roadmap]] の bit-bang SPI 段)では使い物にならない。
  本文が変わったか画面がスクロールしたときだけ全面を描き、それ以外は
  ステータス行 + カーソル移動だけ出す(`touched` フラグ)。
  ついでに status() を「24 行目へ絶対移動してから描く」形にした(従来は
  「23 回改行したので今 24 行目にいるはず」という暗黙の前提だった)。
  **実測: カーソル移動 23B / 編集 167B**(従来はどちらも全面)。
  test_vi.py に出力量の回帰ガードを追加(移動 < 200B かつ 移動 < 編集)。
  代償は vi.bin 13550→13740B (+190B)。4 ブロックのまま。
  ★3 ブロック化(≤11968B)は **見送り**。必要なのは -1770B ≒ コードの -14% で、
    内訳は「switch の case ラベル 1 個 = PIC 間接ジャンプ 15 命令」が支配的
    (vi 全体で 92 箇所 ≒ 2.2KB)。これを削るには do_cmd を「1 case = 1 関数
    呼び出し」へ分解する構造変更が要り、**動いてテストも通っている新規コードに
    対して見合わない**と判断した。4 ブロックで困るのは「背景ジョブが 1 個でも
    居ると vi が起動できない」点だけで、失敗は `sh: vi: no free block` という
    明示メッセージ(クラッシュしない)。判断はユーザーに委ねる。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。vi は実装済み。その後の改良は #39/#41/#44/#69 で完了。

## [#34] sed コマンドの実装
- status: 完了
- category: タスク
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-10 00:46
- updater: AI(Claude)
- updated: 2026-09-10 08:40

### 履歴
- 2026-09-10 00:46 AI: 簡易版 sed (s/old/new/ のみ) の実装。grep.c のロジックを流用し、新規ファイル user/sed.c を作成する。既存のビルドシステム(Makefile)に sed.bin を追加する。既存コードの変更は最小限にする。
- 2026-09-10 08:40 AI(Claude): **完了。ビルド経路を SDCC/iy_reg → tzcc へ移し、
  `g` フラグを足して回帰込みで検証した。** 初版(7f2b869)は user/Makefile の
  COMMANDS に載っていた = **iy_reg 経路**で、これは #26 以降 sh + スクラッチ専用。
  sed は分岐の多い引数解析(strchr で s/OLD/NEW/ を切る)を持つので、
  iy_reg が壊す既知パターン(head/tail の -N と同型)にちょうど当たる。
  他の coreutils 22 本と同じ tzcc 経路へ揃えた。
    ・tzcc(feat/tizix-cmd)の TIZIX_CMDS に sed を追加。
    ・user/Makefile の COMMANDS から sed を外し、sed.ihx リンク規則を削除。
    ・user/sed.c を書き直し。old_pat[32]/new_pat[32] + strcpy をやめ、
      argv[0] を破壊的に NUL で切って **そこを指すだけ**にした(実体配列と
      strcpy が消える)。仮引数ゼロ・printf 不使用(prs/puts)でコストモデル準拠。
      空パターン `s//X/` は一致が進まないので usage で弾く。
    ・`s/OLD/NEW/g` を実装(既定は行ごとに最初の 1 個 = 本家 sed と同じ)。
  サイズ: 2769B(1 ブロック)。実効スタック 3776-2769 = 1007B で
  「FatFs を叩くなら 250B 以上」の目安に対して十分。
  (参考: iy_reg 版は 1546B だったが、経路の正しさを優先した)
  検証: python/test_sed.py を新設(17 ケース: ファイル/パイプ/リダイレクト/
  g 有無/1 文字パターン/NEW 空/usage 4 種/存在しないファイル)。**全 PASS**。
  回帰 6 本(test_5b_pipe / test_pwd_cd / test_vfs_step8/9/10 / test_cmds_all)も PASS。
  ★ハーネスの落とし穴を 1 つ記録: **ファイル引数なしの sed は stdin を読んで
    戻らない**(本家と同じ挙動)。pty ハーネスから `sed s/a/b/` を単独で叩くと
    シェルごと固まり、以降の全ケースがタイムアウトして「sed が壊れている」ように
    見える。stdin 経路は必ずパイプかリダイレクトで与えること。
  未実装(意図的): 正規表現 / アドレス指定(`1,3s/…`)/ `-n` / `d`・`p` コマンド /
    `/` 以外の区切り。128B を超える行は分割される(grep・uniq と同じ read_line)。

## [#35] プロセス分割の基盤(同期 exec + 保護なし共有メモリ)
- status: 完了
- category: タスク
- priority: HIGH
- assignee: AI
- creator: me
- created: 2026-09-10 12:40
- updater: AI
- updated: 2026-09-25 11:11

### 履歴
- 2026-09-10 12:40 me: vi はプロセスを本体とコマンドで分割して kexec か fork() で
  コマンドに応じた処理を行って、つど バッファ更新すれば実装可能。
- 2026-09-10 13:20 AI(Claude): **成立を実証した。基盤を入れてコミット済み。**
  ご指摘のとおりで、tizix にメモリ保護が無い(意図的)ことが効きます ──
  block2..7 は同じ 64KB に並んでいるので、**子プロセスが親のバッファを
  そのまま書けます**。ただし前提となる機能が 2 つとも未実装だったので用意しました。

  ■ 入れたもの
   ・`krun_wait(fname, argpack, argc)`(src/kexec.c、drv_tbl[45])。
     子を起動して終了まで待つ同期 exec。これまで kexec_argv を呼べたのは sh
     だけで、普通のコマンドは子を持てませんでした(#31 の申し送りにあった
     「exec 未実装」)。待ちループを各コマンドに書かせると PIDTBL の絶対番地と
     Ctrl+C 処理が全員にコピーされるので、カーネルに 1 本だけ置いています。
   ・共有メモリ プリミティブ(tzcc crt0_tizix.s): `peek` / `poke` /
     `absmove`(LDIR)/ `getbase`。
   ・実証プログラム user/spawn.c(親)+ user/spawnc.c(子)、python/test_spawn.py。

  ■ 実測(cpmsim)
        spawn: buf before = parent
        spawn: base=49152 direct=51426 viavar=112
        spawnc: base=53248 wrote 14B at 51426
        spawn: buf after  = CHILD-WAS-HERE
    親が block4(0xC000)、子が block5(0xD000)の**別プロセス**。子が親の
    0xC8E2 を書き換え、親が続きを実行しています。連続実行(ブロック解放)も OK。

  ■ ★途中で tzcc のコード生成バグを 1 個見つけました(未修正・要判断)
    **配列のアドレスは「関数引数の位置で評価すれば絶対番地」だが、いったん
    変数へ代入すると +IY されず素のラベル値が入る。**
      `prnum((unsigned)buf)`      → 51426 (正しい絶対番地)
      `a = (unsigned)buf; prnum(a)` → 112   (壊れている)
    iy_reg の「ポインタ = リテラル代入の穴」([[iy-reg-literal-assign-blindspot]])
    と同型です。既存コマンドが踏んでいないのは、配列名を常に引数の位置でしか
    使っていないため(`strstr(line, pat)` など)。
    最初にこれを踏んだとき、子が 0x8905 = **sh のセッション状態(KW_SHSTATE)**を
    14 バイト上書きして以降のコマンドが無反応になりました。危険な穴です。
    **当面の掟: `unsigned a = (unsigned)arr;` と書かない。引数で直接渡す。**
    test_spawn.py が direct/viavar の両方を出すので、直ったら [NOTE] が出ます。
    tzcc 本体の修正は generator/tizix.c のバックエンドに手を入れる話なので、
    無人では触らず判断待ちにしています。

  ■ vi への適用について(トレードオフ・判断待ち)
    機構は動きますが、**vi の「保存」を子に出すのは勧めません。**
      ・素直な分割は ex コマンド(`:w` `:q` `:wq`)+ ファイル I/O を子に出す形で、
        vi 本体は 13740B → 約 11.3KB = **3 ブロック**に収まる見込みです。
      ・ところが `:w` の瞬間に vi(3)+子(1)+sh(2) = 6 ブロック = 全部が必要。
        背景ジョブが 1 個でもあると **「起動はできるが保存できない」**状態になります。
        いまは「起動できない」で止まる(= 書きかけを失わない)ので、
        **データ安全性としては今より悪化**します。
      ・逆に、この機構が本当に効くのは **機能を増やす方向**です。undo・検索・
        矩形操作のような「重いが稀にしか使わない」機能を子に出せば、
        vi 本体を 4 ブロックのまま太らせずに増やせます。
        同じ理由で #31 の申し送りにあった `cp -r` / `rm -r` / `ls -R` も
        これで書けるようになりました(exec が前提条件でした)。
    どちらへ進めるかはご判断ください。基盤自体は入っているので、どちらでも
    すぐ着手できます。

  ■ 注意
    子の .BIN は **毎回 FAT から読み込まれます**(spawnc.bin で 1757B)。
    cpmsim では一瞬ですが実機のフロッピでは数十 ms 掛かるので、
    1 打鍵ごとに呼ぶ設計にはしないこと。ヘッダとカーネルに明記しました。

  検証: python/test_spawn.py(7 チェック)PASS。`make cleandisk` からの
   フルビルドで回帰 10 本 + /dev バイト比較 + DRIVER サイズガード ALL PASS。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。基盤(krun_wait / peek・poke・absmove)を実装・コミット済み。

## [#36] オーバーレイ(自前で半ロード)
- status: 完了
- category: タスク / viシュリンク
- priority: HIGH
- assignee: AI
- creator: me
- created: 2026-09-10 13:40
- updater: AI
- updated: 2026-09-25 11:11

### 履歴
- 2026-09-10 13:40 me: vi の子プロセス側は常駐でなくてもよいのでは。
  自前で半ロードていう奥の手もある。
- 2026-09-10 14:30 AI(Claude): **オーバーレイで実証。#35 の弱点が消えました。**
  ご指摘のとおり、子プロセスにしなくても「自分の空間の空き番地へコード片を
  読み込んで呼ぶ」で済みます。**ブロックを 1 個も余分に食わない**ので、
  #35 で私が挙げた「`:w` の瞬間に空きブロックが要る = 起動できても保存できない」
  という弱点がそもそも発生しません。オーバーレイ側は常駐しません。

  ■ 成立の理由(ここが確認したかった点)
    tzcc の --tizix-user は「実行時に IY を足す」PIC を吐き、変数領域の基点
    `tzc_vb` は **各コンパイル単位が自分で吐く**。したがって
    **オーバーレイを「読み込む先のオフセット」でリンクしておけば、親の IY の
    まま IY + リンク時オフセット が正しい実アドレスになる。**
    番地がずれると親の別の場所を触るので、リンク番地と読み込み番地の一致が
    唯一にして絶対の条件。

  ■ 入れたもの
   ・tzcc `crt0_ovl.s` … オーバーレイ入口(8 バイト)。IY 相対で _main へ飛ぶ
     だけ。**_start / SP 張替 / kexit は持たない**(kexit したら親ごと終わる)。
     カーネル低位ベクタはここで実体定義する ── sdld の `-g` は「参照されている
     未定義シンボル」にしか値を与えられず、使わないベクタを渡すと落ちるため
     (#32 で踏んだのと同じ罠)。
   ・tzcc `callovl(addr, arg)`(crt0_tizix.s)… tzcc は関数ポインタを作れない
     ので、実行時に決まる番地への call はここを通す。0x0050 の
     ___sdcc_call_hl を call して戻り番地を積む。
   ・tzcc Makefile `tizixovl` … `make tizixovl CMD=x OVLADDR=0x1600`。
     `-b _CODE=$(OVLADDR)` でリンクし、makebin の像から先頭 OVLADDR バイトを
     dd で捨てて「その番地から始まる実体」だけを .bin にする(DRIVER.BIN と
     同じ流儀)。TIZIX_OVLS に `名前:番地` で並べると一括ビルドされる。
   ・実証: user/ovlmain.c(親)+ user/ovlsub.c(オーバーレイ)、python/test_ovl.py。

  ■ 実測(cpmsim)
        ovl: base=49152 load-at=54784      (0xC000 + 0x1600 = 0xD600)
        ovl: loaded 396 bytes
        ovl: rc=4242                        戻り値が返る
        ovl: buf=OVL-OK1                    自分の static を読み、親のバッファへ書いた
        ovl: rc2=4242                       読み直さず 2 回目も呼べる
    `OVL-OK` はオーバーレイ自身の tag[](= 0x1600 以降の自分の _DATA)由来。
    **オーバーレイ内の IY 相対データ参照が親の IY で正しく解決されている証拠。**

  ■ 効き方
   ・**ovlsub.bin は 396 バイト**。crt0_tizix(_start / putchar / puts / printf)を
     持たないので、オーバーレイ 1 個の単価が非常に安い。
   ・ブロック増加ゼロ。#35 の子プロセス方式(1 ブロック + .BIN ロード)より
     軽く、失敗要因も少ない。
   ・ただし **リンク番地と読み込み番地の一致が必須**で、親の像の末尾と
     スタック(SP=top-0x140 から下へ伸びる)の間に収める必要がある。
     ここは #37 でレイアウトを決める。

  ■ 使い分け(#35 と #36)
    オーバーレイ  … 同じプロセスの中で完結する処理(vi の ex コマンド、undo、
                    検索など)。ブロック不要。同じ IY なのでポインタもそのまま。
    子プロセス     … 独立して動く/失敗しても親を巻き込みたくない処理、
                    あるいは並行に走らせたいもの(cp -r の再帰など)。

  検証: python/test_ovl.py(6 チェック)PASS。`make cleandisk` からのフルビルドで
   回帰 11 本 + /dev バイト比較 + DRIVER サイズガード ALL PASS。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。オーバーレイは実証済みで、#69 で本採用。

## [#37] vi をオーバーレイ化する試み(実測して見送り)
- status: 完了
- category: タスク / viシュリンク
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-10 14:40
- updater: AI
- updated: 2026-09-25 11:11

### 履歴
- 2026-09-10 15:10 AI(Claude): #36 の機構を vi に適用しようとして、
  **実測の結果いったん戻した。** 動いている vi を壊さないための判断。

  ■ やったこと
    `:` コマンド(行編集 + :w/:q/:wq/:x + ファイル書き出し)を user/viex.c へ
    切り出し、vi 本体は状態を語配列 vst[] で渡してオーバーレイを呼ぶ形にした。
    ビルドは通り、リンク番地(0x3400)への配置も期待どおりになった。

  ■ ★測った数字(これが結論)
        vi 本体   13101 B
        viex       2672 B(切り詰めた後。最初は 3167 B)
        合計      15773 B
        4 ブロック = 16384 B、SP = 0x3EC0 = 16064 B
        → **スタックに残るのは約 290 B。FatFs 呼び出しに全く足りない。**

  ■ 分かったこと(オーバーレイの経済性)
    **ピークの消費は「本体 + 読み込んだオーバーレイ」なので、オーバーレイが
    1 個しか無いなら分割は損。** ライブラリのグルー(トランポリン等)が
    二重になる分、合計はむしろ増える。実測でも本体は 13740→13101 B と
    639 B しか減らないのに、オーバーレイ側は 2672 B かかった。
    しかも本体側は状態の受け渡し(pokew/peekw)で +227 B 太った。

    **効くのは排他的なオーバーレイが複数あるとき。**
    常駐が core + max(overlay) で済み、core + sum(overlay) にならない。

  ■ 再挑戦するならこの設計(見積り)
    vi の実コードは約 12.6KB(データ 1.2KB 別)。これを
      core(移動 + 挿入 + 描画)      ~7.0KB   常駐
      overlay A: ex(: コマンド)     ~2.7KB
      overlay B: 編集オペレータ(dd/yy/p/x) ~2.0KB
      overlay C: ファイル入出力      ~1.5KB
    と 3 本に割ると、ピーク = 7.0 + 1.2(データ) + 2.7(最大) + 1.5(スタック)
    ≒ **12.4KB = 3 ブロック**に収まる。さらに undo / 検索を D, E として
    足してもピークは動かない ── ここが本命の効き方。
    ただし do_cmd / do_ins の分解を伴う実質的な再構成で、いまの
    「動いてテストも通っている」状態を一度崩すことになる。**着手はご判断で。**

  ■ 残したもの / 戻したもの
    ・残した: #36 のオーバーレイ機構一式(crt0_ovl.s / callovl / tizixovl /
      peekw・pokew / tzctramp.s)。ovlmain + ovlsub + test_ovl.py で回帰済み。
      **tzctramp.s** は「オーバーレイは crt0_tizix を持たないので DRIVER
      トランポリンが未解決になる」問題への答え。ライブラリに同じ実体を置き、
      通常コマンドは crt0_tizix(明示リンク)で解決されるので引かれない
      = 既存コマンドのサイズは 1 バイトも変わらない。
    ・戻した: user/vi.c(git checkout)と user/viex.c(削除)、TIZIX_OVLS。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。見送った 3 ブロック化は #69(ovlsplit 方式、3 枠)で達成。

## [#38] 追加ブロック(像に含めない作業領域)
- status: 完了
- category: タスク / viシュリンク
- priority: HIGH
- assignee: AI
- creator: me
- created: 2026-09-10 15:20
- updater: AI
- updated: 2026-09-25 11:11

### 履歴
- 2026-09-10 15:20 me: viメインが1k、呼び出した機能に3kをきっかりオフセット位置を
  固定してリンカスクリプト書いたらいいのでは。バッファはこの際4kを空きメモリ
  ブロックを必須にして。
- 2026-09-10 16:10 AI(Claude): **「バッファを専用の空きブロックに」を実装した。**

  ■ 先に 1 点だけ数字の訂正
    **vi の常駐部は 1KB には落ちません。** 毎打鍵で要るもの(getkey 434B /
    render 621B / status 722B / place・row_of・scroll_fix ~530B / line_* ~480B /
    move_line 190B / ins_at・del_at 490B / main ~1577B / do_ins 1047B /
    esc・mvcur ~290B)だけで **実測 ~6.4KB**。常駐は 6〜7KB が下限です。
    オフセット固定のリンクは #36 で実装済み(`make tizixovl OVLADDR=…`)なので、
    「3K の窓に機能を差し替える」部分はいつでも組めます。

  ■ 入れたもの: .BIN 先頭 32B の予約ヘッダを使う
    crt0 の `.ds 0x20` は従来まったくの死に領域だったので、そこにロード情報を載せた。
      ビルド時に刻む: 0x10 'T' / 0x11 'Z' / 0x12 追加ブロック数
      ロード時に kexec が書き戻す: 0x1C-0x1D imgtop(追加領域の先頭)/ 0x1E nblk
    ・kexec は **先頭 32B だけ先に読んで**ヘッダを見る。シークはしない
      (読んだ 32B をそのまま base へ置き、続きを base+32 から読む)。
      FDC は巻き戻しに弱いので([[cpmsim-di-fatfs-hang]])。
    ・マグが無ければ xblk=0 = 従来どおり。**既存の .BIN は 0xFF 埋め**なので
      誤検出しない(makebin -p の詰め物は 0x00 ではなく 0xFF だった)。
    ・**SP と argv[] は像側の上端**に置く(crt0 へ渡す BC を像のブロック数に
      した)。おかげで追加ブロックは丸ごと作業領域として使える。
    ・追加ブロックはゼロクリアしない(4KB の memset がロード毎に乗るため)。
    ・使う側: `getxbase()` / `getxsize()`(tzcshare.s)。0 なら宣言していない。
    ・ビルド: `make tizixcmd CMD=x XBLK=1`、一括は TIZIX_XCMDS に「名前:数」。

  ■ 実測(cpmsim)
        xblk: base=49152 xbase=53248 xsize=4096      (0xC000 → 0xD000)
        xblk: wrote+verified 4096 bytes, bad=0
    **像は 1888B のまま、作業領域だけ 4KB。** 全域書き込み+読み返しで
    像にもスタックにも当たっていないことを確認。連続実行(解放)も OK。

  ■ ★途中で見つけた既存バグ 2 件(どちらも今回初めて表面化)
   (1) **kexec.c がローカルで `#define MAX_PROC_BLK 3` を持っていた。**
       kmem.h は 4(sh の 4 ブロック化で 3→4 にしたときの更新漏れ)。
       この値をどこも参照していなかったので気付かれずに残っていた
       (サイズの比較ラダーは 0x4000 のリテラル)。#38 で初めて参照したところ、
       4 ブロックの vi が `no free block` で起動できなくなって露見。写しを撤去。
   (2) **`make cleandisk` は driveb.dsk しか作り直さない。** カーネルが載るのは
       drivea.dsk($(DISK) 側)で、cleandisk だけだと kernel.ihx は再ビルド
       されるのに起動ディスクへ書かれない = **直したはずのカーネルが載っていない**
       状態でテストが走る。(1) の修正が効かず同じバグを二度追いかけた。
       → `run_regress.sh --clean` は cleandisk の後に make も回すようにした。

  検証: python/test_xblk.py(6 チェック)新設。`make cleandisk` からの
   フルビルドで回帰 12 本 + /dev バイト比較 + DRIVER サイズガード ALL PASS。

- 2026-09-11 AI(Claude): **vi の本文バッファを追加ブロックへ移した(#38 の適用)。**
  `static char text[1024]` → `static char *text = (char *)getxbase()`。
  tzcc は char* の添字参照ができるので **`text[i]` は 1 文字も直さずに済んだ**
  (peek/poke 化は不要だった。README の「`*(p+i)` 未対応」は古い記述)。

  ■ 結果
        vi.bin 13744 → **11868 B**(像 3 ブロック)+ 追加 1 ブロック = **合計 4**
        本文バッファ 1024 → **2560 B**(2.5 倍)
    合計ブロック数は据置のまま、編集できるファイルが 2.5 倍になった。

  ■ 途中で踏んだ設計ミス(自分の)
    最初「追加ブロックを丸ごと空けたい」から kexec が crt0 へ **像の**ブロック数を
    渡す形にした。すると SP が像の直上に来て、像が上限ぎりぎり(11940B)のとき
    **スタックが 28 バイトしか残らず**即破壊された。
    → **SP と argv[] は常にプロセス最上端**という従来の invariant に戻した。
      追加ブロックは「像とスタックの間に増えた空き」であり、使う側が
      上端に余白を残す(vi は 1536B)。単純な不変条件のほうが事故りにくい。

- 2026-09-11 AI(Claude): **落としたコマンドを全部戻した(11952B、像 3 ブロック維持)。**
  「i コマンドが無い vi は vi なのか」という指摘を受けて。まず訂正: 外していたのは
  **大文字 `I`(行頭で挿入)**で、小文字 `i` は最初から入っていました(表の書き方が
  紛らわしかった)。とはいえ `yy` と `w` を落としたのは vi らしさを削るので戻した。

  戻す原資は「組み込みというより PC 向けの書き方だ」という指摘そのものから出た:
   ・**フラグ類を int → unsigned char**。tzcc は int/ポインタの読み書きが 1 回 6B、
     **char は 3B**。mode / dirty / quitf / toobig / touched / msgid / scr_row /
     scr_col は 0..255 に収まるので全部 char。16bit が要るのは本文の位置
     (tmax/tlen/cur/top)と ylen / pending だけ。
   ・**`line_head(cur)` / `line_tail(cur)` を do_cmd の先頭で 1 回だけ求める**。
     以前は case ごとに呼び直していて、tzcc では 1 呼び出しが「引数 push + call +
     IX 張り直し」≒ 30B。12 箇所 → 2 箇所で ~300B。cur を書き換える case は
     いずれも書き換える前にしか使わないので安全。
   ・**2 打鍵コマンド(dd / yy / gg)を switch の外の if へ**。
     case ラベル 1 個 ≒ 24B(PIC 間接ジャンプ)に対し if は ~10B。
   ・終了時の `ESC[H` を省略(直後に sh がプロンプトを出すので実害なし)。

  結果 12370 → **11952B**。像 3 ブロック + 追加 1 ブロック = 合計 4(据置)、
  本文 2560B、コマンドは h/j/k/l・カーソルキー・0・$・G・gg・w・i・I・a・A・
  o・O・x・dd・yy・p・`:w :q :q! :wq :x`・Ctrl+L で **全部入り**。

- 2026-09-11 AI(Claude): **実機で使ってもらったフィードバックへの対応。**
  ★`gg` が効かない = **バグだった**。カーソルは 0 へ動いていたが `touched` を
  立てておらず画面が再描画されていなかった。gg は `top` を自分で 0 にするため
  scroll_fix が「動いた」と判定できず、カーソルだけ左上へ行って本文が古いまま
  残る。`G` は cur が大きく動いて scroll_fix が拾うので出ていた ── 差分描画
  (#33)を入れたときの見落としで、見つけにくい類。
  追加: カウント接頭辞(`3dd` / `12yy`)、`e`(語末へ)。
   カウントは移動には付けていない(要望が「行番号 + dd/yy」だったのと、
   `while (dc_n-- > 0)` の包みが 1 コマンドあたり ~25B 効くため)。
  削除(本人が使わないとのこと): `A` / `I` / `w` と Delete キーの ESC 判定。
   ※ `A`(行末追記)は一般には使用頻度が高い方なので、他の人が使うなら戻す価値あり。
   代替: `A`→`$`+`a` / `I`→`0`+`i` / `w`→`e`。
  収めるための整理で見つけたもの:
   ・paste_line が 1 文字ずつ ins_at を呼んでいた(= O(ylen × tlen) の移動 +
     ylen 回の関数呼び出し)。**一括シフト**に変更。小さくなるうえ速い。
   ・**`if (cur < 0)` / `if (lf_n < 0)` は死にコードだった**
     (tzcc の比較は符号なしなので永久に偽)。撤去。
   ・fname[64]→[40] / yank[128]→[96] と実寸に。
  vi.bin 11952 → **11893B**。test_vi は 18 ケースへ拡充。

  ■ undo が入らない(未対応・要判断)
    像 3 ブロックの残りが **75B** しかなく、行単位 undo でも ~250B 要る。
    選択肢は 2 つ:
     (A) **像 4 ブロック + 追加ブロック無し**に戻し、本文バッファを像の中へ。
         合計ブロック数は 4 で **いまと同じ**。像の上限が 14528B になるので
         **~1100B の余裕**ができ、undo も入る。代償は本文が 2560B → ~1500B。
     (B) #37 のオーバーレイ分割。ex や load/save を窓へ出せば常駐が減るが、
         実質的な再構成になる。
    どちらも「動いているものを作り直す」ので、判断を仰ぎたい。

  ■ ブロックの空き(質問への回答)
    プロセス枠は block2..7 の 6 個。sh が 2 個(block2,3)、vi が 4 個
    (像 3 + 追加 1)で **ちょうど埋まる**。つまり **空きゼロ**で、背景ジョブが
    1 個でもあると vi は起動できない(`sh: vi: no free block`。クラッシュはしない)。
    ★コマンドを増やしてもブロック数は変わらない。効くのは「像が 3 ブロック
      (≤11968B)に収まるか」だけ。いまの余裕は **16 バイト**。

  ■ 旧・収めるために落としたコマンド(上記で解消済み。記録として残す)
    像を 3 ブロック(≤11968B)に入れるため、**使用頻度の低い順に 5 つ外した**:
      `I`(行頭で挿入)   → `0` + `i` で代替
      `O`(上に行を開く) → `k` + `o` で代替
      `w`(語単位の移動) → `l` の連打で代替
      `gg`(先頭へ)      → `0` + `k` 連打で代替
      `yy`(行コピー)    → **代替なし**(`dd` + `p` は動く = 切り取り貼り付けは可)
    ステータス行のメッセージも 6 種 → 3 種に統合した。
    理由は構造的なもので、**tzcc の switch は case ラベル 1 個につき PIC 間接
    ジャンプ(15 命令 ≒ 24B)を払う**ため、コマンド 1 個が重い(vi 全体で 88 箇所)。
    残っているのは h/j/k/l・カーソルキー・0・$・G・i・a・A・o・x・dd・p・
    `:w :q :q! :wq :x`・Ctrl+L。
    **戻したいものがあれば言ってください。** #37 のオーバーレイ分割と併せれば
    全部戻したうえで更に小さくできる(ただし実質的な再構成)。

  ■ 「2 ブロック + バッファ」への距離
    ご指定の基準(像 2 ブロック = 7872B)まではあと **-4000B**。
    常駐に必要な hot path が実測 ~6.4KB あるので、**単純な削りでは届かない**。
    届かせるなら #37 の分割(core + 排他オーバーレイ複数)が要る:
      core ~7.0KB は 2 ブロックに入らないので、正確には
      「core 2 ブロック + オーバーレイ窓 + バッファ」の 3 ブロック構成を狙う形。
    いまは合計 4 ブロックで動いており、機能追加はオーバーレイで可能。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。追加ブロックは実装済みで、vi の本文バッファに使っている。

## [#39] vi にコマンド単位の undo(4 段)
- status: 完了
- category: タスク
- priority: HIGH
- assignee: AI
- creator: me
- created: 2026-09-11 09:00
- updater: AI(Claude)
- updated: 2026-09-11 10:30

### 履歴
- 2026-09-11 09:00 me: コマンドベースのundoはいれたいな。何か機能を削ろう。
  とりあえず256バイト確保できたらアンドゥ4回くらいできんか。

- 2026-09-11 10:30 AI(Claude): **入りました。機能は 1 つも削っていません。**
  結果: `vi.bin` **11742B**(像 3 ブロック = 11968B に対し余裕 226B)。
  合計ブロック数は **4 のまま**(像 3 + 追加 1)。

  ■ 記録の置き場 ── 像を 1 バイトも使わない
    追加ブロック(#38)の**末尾 272B**を undo 用に切り出した。
      1 段 = 68B … `[0]種別 [1]長さ [2..3]位置 [4..67]内容(最大 64B)`
      4 段のリング。5 回目を積むと一番古いものが落ちる。
    本文バッファはそのぶん 2560 → **2288B**。ご指定の「256B 確保できたら」は
    ここ(272B)で、**像側の増加は純粋にコード**です。

  ■ 種別は 2 つだけ
      種別 1 = 「消した」の取り消し → 記録した内容を位置へ挿し戻す
      種別 2 = 「入れた」の取り消し → 位置から長さぶん消す(内容は要らない)
    積むのは `x` / `dd` / `p` / `i..ESC`(挿入は ESC を押した時点で 1 段)。
    64B を超える削除(長い行の dd)は正しく戻せないので**その場で履歴を捨てる**
    ── 中途半端に戻すより安全。

  ■ LIFO だから位置がずれない
    記録は絶対位置を持つが、**戻す順が積んだ順の逆**なので、n 段目を戻した
    直後のバッファは n-1 段目を積んだ直後と同一 = 位置は必ず有効。
    ただし「挿入中にカーソルキーで動く」「挿入開始位置より手前まで BS」を
    やると i..ESC が純粋な追加でなくなるので、そのときは履歴を捨てる
    (`ins_ok` フラグ)。

  ■ 像を太らせない書き方(実測)
    最初の実装は 13386B(**+1858B**)で 3 ブロックに 1418B 足りなかった。
     ・挿し戻し / 消しの自前シフトループ → **ins_at / del_at を 1 文字ずつ呼ぶ**
       に変更。遅くなるが undo は 1 打鍵に 1 回しか走らない。**-380B**。
     ・記録への写しは **absmove(LDIR)1 発**。
    それでも 13006B で 1038B 超過 → **#40 の覗き穴最適化**(tzcc 側)で
    **-1264B**。結果 11742B。

  ■ test_vi.py に 5 ケース追加(全 23 ケース PASS)
    x3→u3 / 5 回消して u5 = 4 段ぶんだけ戻る / dd→u / i..ESC→u / yy p→u。

## [#40] tzcc: 比較の 0/1 materialise を畳む覗き穴最適化(peep.py)
- status: 完了
- category: タスク
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-11 10:00
- updater: AI(Claude)
- updated: 2026-09-11 10:30

### 履歴
- 2026-09-11 10:30 AI(Claude): **tzcc 製の全コマンドが 5〜10% 縮みました。**

  ■ 何が起きていたか
    tzcc(tizix.c)は式を「値を HL に作る」規則で一様に吐くので、`a == b` は
    **一度 HL に 0/1 を作ってから** if がそれを 0 と比べていた:
      ```
        or a / sbc hl, de      ← ここでフラグは立っている
        ld hl, #1 / jr z, LcmpN / ld hl, #0 / LcmpN:
        ld a, h / or l
        jr z, Lend             ← 「HL が 0 なら飛ぶ」
      ```
    後半 12B は **直前のフラグを見る jr 1 個(2B)** と等価。vi.c だけで
    **127 箇所**あった。

  ■ 直し方 ── 生成器ではなく後段で
    「この式の消費者は分岐である」という文脈を生成器へ通すと式生成の全域に
    手が入る。出力側では完全に定型なので **jrfix.py と同じ立場の後段**
    (`tzcc/peep.py`)で畳む。ASTZ の入口が jrfix.py しかないのでそこから呼ぶ。
    **jr の範囲修正より先**に回すこと(畳むと距離が縮むので後だと無駄が出る)。

  ■ 安全性
    ・置換後の jr はフラグを立てた命令の直後(間は `ld hl,#imm` だけ = 無影響)。
    ・`Lcmp` ラベルの**参照数を数えて 1 のときだけ**畳む(`&&`/`||` の短絡が
      作る `Land` / `Lor` には掛からない)。
    ・畳むと HL/A の値が変わるが、tzcc は条件式の後で HL/A を読み直さない
      (必ず IX スロットから load し直す)。回帰スイート全 12 本 + /dev の
      ホスト側バイト比較 + DRIVER サイズガード、すべて PASS で裏取り済み。

  ■ 実測(同一ソース、--clean ビルド)
      vi   13006 → **11742**(-1264、これで undo が 3 ブロックに入った)
      sed  2773 → 2593   tail  2082 → 1858   uniq 2060 → 1980
      wc   2195 → 2085   spawn 2632 → 2498   xblk 1947 → 1863
    sh は SDCC 製なので不変(5157B)。

- 2026-09-11 11:20 AI(Claude): **第 2 パターン(collapse_ldde)を追加。**
  定数を DE へ置くのに必ず HL を経由する遠回りを畳む。
    ```
    push hl / ld hl, #K / pop de / ex de, hl   →  ld de, #K              (6B → 3B)
    push hl / ld hl, #K / pop de               →  ld de, #K / ex de, hl  (5B → 4B)
    ```
  **レジスタ・フラグまで完全に等価**な置換なので手堅い。`ex` 付きは
  `sbc hl, de`(順序が要る)の前、`ex` 無しは `add hl, de`(可換)の前に出る。
  実測: **vi 11742 → 11461(-281、像 3 ブロックに余裕 507B)** / sed 2593 → 2535 /
  tail 1858 → 1805 / wc 2085 → 2059 / uniq 1980 → 1956 / spawn 2498 → 2458 /
  xblk 1863 → 1837。回帰 12 本 + ホスト側バイト比較 + DRIVER ガード、すべて PASS。

## [#41] vi のチラつきを減らす(行単位の描き直し + ステータス行の据え置き)
- status: 完了
- category: タスク
- priority: MIDDLE
- assignee: AI
- creator: me
- created: 2026-09-11 09:00
- updater: AI(Claude)
- updated: 2026-09-11 11:50

### 履歴
- 2026-09-11 09:00 me: 若干動きがチラつくが実用にかなり近いレベルは出ている。

- 2026-09-11 11:50 AI(Claude): **1 打鍵あたりの出力を実測で 1/4〜1/28 にしました。**

  | 操作 | 前 | 後 |
  |---|---|---|
  | カーソル移動 | 23B | **6B** |
  | 行内の編集(文字を打つ / x) | 167B | **38B** |
  | 行が増減する編集(o / dd / p) | 167B | 180B(全面。据置) |

  ■ 「動きがチラつく」の正体はステータス行だった
    移動キーの出力 23B は **毎打鍵 24 行目を `ESC[K` で消してから書き直す**
    ぶん。本文は 1 ドットも変わらないのに最下行だけが点滅していた。
    → ステータス行の内容は `dirty` / `mode` / `msgid` だけで決まるので、
      その 3 つから 1 バイトの印(`stold`)を作り、**同じなら 1 バイトも出さない**。
      `:` の後と Ctrl+L と起動直後は 0xFF を入れて必ず引き直す。

  ■ 行内の編集は、その行だけ描き直す
    `lnr`(0=本文を触っていない / 1=行内だけ / 2=全面)を ins_at・del_at が立てる。
    **'\n' が絡んだら 2**(行が増減 = 下が全部ずれる)。スクロールも 2。
    1 のときだけ `render_line()` = 「その行へ移動 + ESC[K + 1 行出す」。
    ★行を 1 本出す処理は `put_line()` に切り出して render(全面)と共有した。
      写しを 2 つ持つと **それだけで +106B**(実測)。

  ■ 原資: switch を if の連鎖へ(#40 でコストが逆転していた)
    tzcc の switch は dispatch の `cp`/`jr`(5B)の後に **case 本体への PIC
    間接ジャンプ(18B)と break の PIC 間接ジャンプ(18B)** を払う。
    一方 if は #40 の覗き穴最適化で **比較 14B + ret 1B**。
    → do_cmd の 15 ケースを if 連鎖へ:**-297B**。do_ins も同じ形に(-8B)。
    ついでに del_at の `da_pos < 0`(tzcc の比較は符号なし = 死にコード)を撤去。
    **ラベル数を減らす昔の定石(カーソルキーを畳む等)はもう要らない。**

  結果 vi.bin **11652B**(像 3 ブロックに余裕 316B)。合計 4 ブロック据置。
  test_vi は 23 ケース、回帰 12 本 + ホスト側バイト比較 + DRIVER ガード PASS。

## [#42] vi: k / 上カーソルが無反応 → 真因は tzcc が `return f(x);` を黙って捨てていた
- status: 完了
- category: バグ
- priority: HIGH
- assignee: AI
- creator: me
- created: 2026-09-11 12:00
- updater: AI(Claude)
- updated: 2026-09-11 12:40

### 履歴
- 2026-09-11 12:00 me: 十字カーソルの上が動かない。ちらつきはほぼ消えた(許容範囲)。

- 2026-09-11 12:40 AI(Claude): **vi のバグではなくコンパイラのバグでした。**

  ■ 切り分け
    テストを足したところ **`k`(伝統キー)も同じく無反応**だった = カーソル
    キーの解読ではなく「上へ move する処理」そのもの。生成アセンブラを見ると
    `line_prev()` の最後が:
    ```
    Lend10:
        ret          ← `return line_head(lpv_h - 1);` が 1 命令も無い
    ```
    **文が丸ごと消えていた。** hl には直前の `line_head(cur)` の結果が残って
    いるので、line_prev() は「前の行頭」ではなく **「いまの行頭」を返す**。
    move_line が同じ行の同じ桁へ動かす = 何も起きない、に見えていた。

  ■ 真因(tzcc generator.c、NODE_RETURN)
    戻り値の式を種類ごとに if で振り分けていて、**NODE_CALL の枝が無かった**。
    最後が `else if (is_expr_node(...))` で終わっており、そこに該当しない形は
    **何も出さずに素通り**する。`x = f(y);`(NODE_VAR_DECL / 代入)には
    NODE_CALL の枝があるので、`return f(x);` だけが落ちていた。

  ■ 直した内容
    1. tzcc: NODE_RETURN に NODE_CALL の枝を追加。さらに **どの枝にも当たらない
       形は stderr に出して exit(1)**(黙って捨てない番人)。全コマンドを
       ビルドし直して引っかかるものは無し。
    2. vi: `line_prev()` は変数へ受けてから返す形に(安いので据置)。
    3. test_vi.py に **`j j k` / `下下上カーソル` / `1 行目で上は何もしない`**
       の 3 ケースを追加(全 26 ケース)。**上下の移動にテストが無かったのが
       そもそもの見落とし**。

  vi.bin 11703B(像 3 ブロックに余裕 265B)。回帰 12 本 + ホスト側バイト比較 +
  DRIVER ガード、すべて PASS。

  ■ 付随して分かったこと
    `x < 0` は **tzcc では正しく動く**(符号ビットを `and #0x80` で見る専用の
    コードが出る)。「tzcc の比較は符号なし」は **変数どうし / 0 以外の定数**
    の話で、`< 0` は例外。move_line の `if (ml_start < 0) return;` は生きている。

## [#43] tzcc: Z80 固有命令を使う原資の棚卸し(検討のみ・未着手)
- status: 完了
- category: 設計
- priority: MIDDLE
- assignee: AI
- creator: me
- created: 2026-09-11 14:00
- updater: AI
- updated: 2026-09-25 11:11

### 履歴
- 2026-09-11 14:00 me: tzcc は ANSI だけを追うつもりは毛頭ない。インラインアセンブラ
  ではないが、独自の組み込み関数でマニアックなアセンブラコードを出して特定機能に
  最適化させた関数があってもよい。わざわざコンパイラまで作った理由もそれ。
  チョークポイントに組み込み関数を入れたらシュリンクの余地はどうか検討だけ。

- 2026-09-11 14:00 AI(Claude): **実測して棚卸しした。実装はしていない。**

  ■ チョークポイントの実体(vi.s 実測)
    `line_tail()`(改行の前方走査)の内側ループは **25 命令 ≒ 60B、1 文字あたり
    250〜300 T-state**。Z80 の `CPIR` なら **2B / 21 T-state**。
    「C の形のままでは絶対に届かない」典型。

    パターン出現数:
      | | vi | dd | cp | sed | wc | grep |
      |比較 sbc hl,de| 167 | 32 | 17 | 21 | 12 | 13 |
      |添字 add hl,de| 247 | 111 | 61 | 45 | 34 | 28 |
      |call          | 123 | 46 | 25 | 17 | 20 | 10 |

  ■ 線引き ── builtin が要る所と要らない所
    **N バイトの塊の操作(走査・移動)は手書きアセンブラのライブラリで足りる。**
    呼び出し 24B に対しループ本体が 60〜100B あるため。`absmove()`(LDIR)と
    `user/lstr.s` / `lstd.s` が既にその前例。
    **builtin が要るのは 1 回が数命令しかない高頻度の操作**(call の方が高く付く)。

  ■ 候補(効率順)
    1. **peephole(第 3 パターン、コンパイラ側)**: 8bit 同士の定数比較を
       `ld a,X / cp #K / jr` へ。1 箇所 ~8B、vi で ~100 箇所 = **-800B 級**。
       **全コマンドに自動で効く**ので単独で一番大きい。添字 `add hl,de` も同様。
    2. **手書きライブラリ(コンパイラ変更ゼロ)**
       ・`__scan(p,ch,n)` = CPIR  → line_tail 68行→~20行、**速度 ~10 倍**
       ・`__rscan(p,ch,n)` = CPDR → line_head 同上
       ・`__movd(dst,src,n)` = LDDR → **いま LDIR しか無く挿入方向が書けない**
         という機能欠落でもある。ins_at / paste_line / do_undo の巻き上げが消える。
       vi 全体で **-400〜600B** と体感速度。
    3. **本物の builtin**
       ・`BIT`/`SET`/`RES`(いま `x & 0x80` が 16bit and に落ちる)
       ・**`INIR`/`OTIR`(ブロック I/O)** ← シリアルと **bit-bang SPI → SD** の本命。
         tizix の次の段を考えるとリターンが一番大きい builtin。
       ・`EXX` / `EX AF,AF'`(将来のプロセス切り替え)

  ■ 落とし穴(実装するときに必ず読むこと)
    1. **PIC**: CPIR/LDIR は絶対番地。`getxbase()` の値は絶対で可。像内の配列を
       渡す場合は **+IY 済みのポインタ値**が前提 ── builtin はシンボル名ではなく
       **式の値**を受け取る形にする(さもないと iy_reg のリテラル代入の穴の再発明)。
    2. **IX**: tzcc は呼び出しごとに IX を張り直す規約。壊すなら張り直しを吐く。
       曖昧にすると #42 と同種の「静かに壊れる」バグになる。
    3. **peep.py / jrfix と衝突しないこと**。`ld de, #K` や
       `ld hl,#1 / jr z,Lcmp...` を builtin 側で使わない。
    4. 割り込み: LDIR/CPIR は途中で割り込み可(PC が巻き戻る)。tick と共存する。
    5. **1 本作ったら .s を目視 + 回帰**。見積もりは外れる(`lhead()` の引数削減を
       -300B と見積もって実装したら **+104B 太った**実例がある)。

  ■ 最初の 1 本に推すもの
    **`__movd`(LDDR)**。機能上の欠落でもあり、効果が単独で測れて、コンパイラを
    触らないので blast radius が最小。

  ※ vi 単体にはやる必要が無い(像 3 ブロックに余裕 265B、機能追加の予定なし)。
    やる理由は「全コマンドが縮む」「vi の体感速度」「SD/SPI 段の前提投資」の 3 つ。

- 2026-09-11 14:40 AI(Claude): **(1) の peephole 第 3 パターンを実施。**
  ゼロ拡張した 8bit 同士の比較は `cp` と Z も C も完全に一致するので、
  9B → 2B へ。**vi 11703 → 11138B(-565、80 箇所)。像 3 ブロックの余裕は 830B**。
  cp 2510→2405 / dd 5029→4932 / tail 1805→1777 / sed 2535→2514 / wc 2059→2052。
  7B × 箇所数より減っているのは、縮んだぶん jr が射程に戻って **jrfix の間接化
  (1 箇所 18B)が減った**ためと思われる。回帰 12 本 + ホスト側バイト比較 +
  DRIVER ガード PASS。**vi.c は 1 行も触っていない**(#42 のクローズ状態のまま)。
  → **覗き穴 3 本の合計で vi は 13006 → 11138B(-14.4%)。**

  残り: (2) 手書きライブラリ(`__movd` = LDDR が本命。**vi.c を開ける必要がある**
  ので vi のクローズを解くかどうかの判断が要る)、(3) builtin(SD/SPI 段の前提)。
  添字 `add hl, de`(vi で 247 箇所)も未着手で、ここは覗き穴では届きにくい
  (ベースをレジスタに残す = 生成器側の話)。

- 2026-09-11 15:30 AI(Claude): **(2) 手書きライブラリを実施。走査と巻き上げを
  ブロック命令へ。** tzcc に `tzcblk.s`(absmovd=LDDR / absscan=CPIR /
  absrscan=CPDR)を追加し、vi の以下を置き換えた:
    line_head → CPDR / line_tail → CPIR / ins_at・paste_line の巻き上げ → LDDR /
    del_at・del_line・yank_line・paste_line のヤンク写し → LDIR
  **1 文字あたり 250〜300 T-state → 21 T-state。** 走査は 1 打鍵で何度も回る
  (row_of_cur が行ごとに line_tail を呼ぶ)ので、実機で効くのはここ。

  ■ サイズは **+73B(11138 → 11211B)。見積もり(-400〜600B)は外れた。**
    内訳: 呼び出し側は **-65B**(line_head -17行 / paste_line -25行 /
    del_at -8行 …)、しかしライブラリ 3 本が **+140B**。
    **今日 3 度目の「コストモデルの見積もりが外れた」例**。#43 に自分で
    「見積もりは外れる」と書いた直後にこれなので、記録として残す。
  ■ 他コマンドへの波及は **モジュール分離で 0 に戻した**。同居させると
    getxbase() しか使わない xblk まで +95B 払っていた(1932 → 1837B)。
  ■ line_tail に `if (lt_p >= tlen) return tlen;` を足してある。旧ループと違い
    **n が負だと CPIR が 64KB 走査してしまう**ため(安全側の追加)。

  判断: **+73B を払って速度を取る**。像 3 ブロックの余裕はまだ 757B あり、
  vi は 26 ケース + 回帰 12 本 PASS。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。(1) peephole の第 3 パターンと (2) tzcblk.s を実施済み。

## [#44] vi: 行番号ジャンプ(N G / N gg)
- status: 完了
- category: タスク
- priority: MIDDLE
- assignee: AI
- creator: me
- created: 2026-09-11 16:30
- updater: AI(Claude)
- updated: 2026-09-11 16:50

### 履歴
- 2026-09-11 16:30 me: gg が効かないと思ったのは **n + gg の意味**だった。そもそも
  仕様になかったか。(G が効かなかったのはリモートデスクトップで右 Shift が
  効かない問題で、左 Shift で動いた = vi のバグではない)

- 2026-09-11 16:50 AI(Claude): **`12G` / `12gg` を実装。** カウント接頭辞は
  #39 の時点で dd / yy 専用だったので、G / gg にも効くようにした。

  ■ `dc_n` の意味を変えた
    これまで `dc_n = count ? count : 1;` と**その場で 1 に丸めて**いたが、それだと
    **`1G`(1 行目へ)と素の `G`(最終行へ)が区別できない**。`dc_n = count;` にして
    **0 = 数字を打っていない**を残し、回数が要る dd 側で `if (dc_n == 0) dc_n = 1;`
    と丸める形にした。
      G  … 数字あり = N 行目 / 無し = 最終行
      gg … 数字あり = N 行目 / 無し = 先頭(top も 0 にするので touched を立てる)

  ■ `line_at(n)` は先頭から数える
    行配列を持たない設計なので N 行目は先頭からの走査になるが、**1 行ぶんの走査が
    CPIR 1 命令(#43)** なので 200 行でも瞬時。行数を超えた指定は最終行で止まる。

  vi.bin 11211 → **11510B**(像 3 ブロックに余裕 458B)。
  test_vi に 5 ケース追加(全 31)、回帰 12 本 + ホスト側バイト比較 +
  DRIVER ガード PASS。

  ■ 副産物: 調査中に分かった「効かないように見える」正体
    **1 画面に収まるファイルで `G` を押すと出力は 6 バイト**(カーソル移動のみ)。
    本文が変わらずスクロールもしないので、画面は 1 ドットも変わらない。
    仕様どおりだが、**行番号がどこにも出ていないので動いた感じがしない**。
    ステータス行に行番号を出す案はあるが、**移動のたびに 24 行目を書き直す**ことに
    なり #41 のチラつき対策(移動 6B)が元に戻るので保留。

## [#45] sh: コマンドヒストリ(↑↓)
- status: 完了
- category: タスク
- priority: MIDDLE
- assignee: AI
- creator: me
- created: 2026-09-11 18:00
- updater: AI(Claude)
- updated: 2026-09-11 19:00

### 履歴
- 2026-09-11 18:00 me: sh のコマンドプロンプトにヒストリがないのが使いにくい。
  ファイルベースの読み出しで作れるか。↑ で 1 個前のコマンドを呼び出す。
  書き換えによる上書きは不要。どんどん追記されて 100 個でローテーション。
  ファイル内容を移動させるのはしんどいだろうから、リングバッファ風でもよい。

- 2026-09-11 19:00 AI(Claude): **`/root/history` に固定長スロットのリングで実装。**
  ↑ = 1 個前 / ↓ = 1 個後(新規行まで戻ると空行)。呼び出した行は打った行と
  同じ扱いで BS で削って打ち足せる。ヒストリ側は書き換えない(実行すると末尾に
  1 件追記されるだけ)。空行は積まない。

  ■ ファイル形式(4802B 固定)
      [0]               hhead  次に書くスロット番号 0..99
      [1]               hcnt   有効件数 0..100
      [2 + k*48]        スロット k = 行バッファ 48B そのまま(NUL 終端)
    101 件目からは最古のスロットを上書きする。**中身は一度も動かさない。**
    RAM には hhead/hcnt の写し 2B だけ(struct sh_state 末尾。KW_SHSTATE の
    544B 枠に対し 540B 使用)。sh 起動時(init の respawn 含む)に先頭 2B を読む。

  ■ 前方 seek だけで済ませた
    外部コマンドの掟「fseek で巻き戻さない」に合わせ、1 回の操作を
    fopen → 前方 fseek 1 回 → read/write → fclose で完結させている。
    追記は「ヘッダ 2B を書く(位置 0→2)→ スロットへ前方 seek → 48B 書く」の順。
    呼び出しは ↑↓ 1 回ごとに fopen → slot へ seek → 48B 読む。
    ファイルが消されていたら(rm /root/history)次の追記で空から作り直す。

  ■ 端末
    ↑↓ は ESC [ A / ESC [ B。vi の getkey と同じく ESC の後ろを
    getc_timeout(10) で拾う。**以前は ↑ を押すと ESC が捨てられ "[A" が行に
    入っていた。**

  ■ サイズ
    sh.bin 5157 → **6765B(+1608B)**。2 ブロックのままで、スタックは約 1.4KB 残る。
    増分の大半は drv_tbl 経由の fopen/fread/fwrite/fseek/fclose 呼び出し 1 個ずつに
    付く SDCC + iy_reg のグルー(1 呼び出し 40〜60B)で、特に異常な箇所は無い。

  python/test_sh_hist.py(16 ケース)を追加し run_regress.sh に登録。
  ↑ / ↑×N / ↑↓ / 件数超えの ↑ / ↑↓ 混在 / 呼び出し後の BS 編集 / 打ちかけ行の
  差し替え / exit → respawn 後の保持 / 105 件積んで 100 件ローテーション /
  ファイルサイズ 4802B。

## [#46] history コマンド
- status: 完了
- category: タスク
- priority: MIDDLE
- assignee: AI
- creator: me
- created: 2026-09-11 19:20
- updater: AI(Claude)
- updated: 2026-09-11 19:50

### 履歴
- 2026-09-11 19:20 me: (#45 を受けて)こうなると history コマンドも欲しくなってくるな。

- 2026-09-11 19:50 AI(Claude): **外部コマンド `/bin/history`(tzcc、1627B = 1 ブロック)。**
  `/root/history` を古い順に `  1  xA` 形式(番号 3 桁右寄せ + 空白 2)で出す。
  builtin にしなかったのは、カーネル像が上限近く(NSEC)で、外部化の方針とも合うため。

  ■ 読み方
    有効なのは hhead の直前 hcnt 個。100 件溜まるまでは hhead == hcnt なので
    [0, hhead) の 1 区間。ローテーションが始まると [hhead, 100) → [0, hhead) の
    2 区間になり、2 区間目は fopen し直す(fseek 巻き戻し禁止の掟)。先頭から
    読み進めて区間外を捨てるだけなので、seek は 1 回も使わない。
  ■ `history` と打った行もヒストリに積まれてから起動されるので、最後の 1 件は
    history 自身になる(bash と同じ)。ファイルがまだ無ければ何も出さない。

  tzcc 側は Makefile の TIZIX_CMDS に history を足しただけ(feat/tizix-cmd)。
  test_sh_hist.py に 7 ケース追加(全 24): 1 区間 / ローテーション後の件数・先頭・
  スロット 99→0 の境目の前後・末尾。

## [#47] arch/m68k-mega を z80pack 相当のフル機能ポートとして新規に起こす
- status: 完了
- category: タスク
- priority: MIDDLE
- assignee: AI
- creator: me
- created: 2026-09-12 00:00
- updater: AI
- updated: 2026-09-25 15:20
- 備考: 旧 arch/m68k(a45084e で放置された78行のプロトタイプ + 未結線の

### 履歴
- 2026-09-12 me: 「arch/m68kが古いのでz80packに合わせて最新化してくれ、
  x86-ia16は完了した」。調査の結果、旧 arch/m68k は z80board/x86-ia16 の
  ような既存ポートへの追従ではなく、ゼロから起こす規模と判明(doc/readme.md
  にも設計記載なし)。方針を協議し確定:
    (1) スコープは「z80pack相当のフル機能ポートを新規に起こす」(VFS一本化・
        カーネルパイプ・外部コマンド・drv_tbl syscall まで見据える、複数
        セッションに跨る前提)。
    (2) 実行基盤は rocket68(CPUコアのみのライブラリ)を組み込んだ自前の
        システムエミュレータ(m68ksim、cpmsim のm68k版)。
    (3) 実機は生 MC68000(4MHz) + Arduino Mega2560 がバスホスト
        (RESET/HALT/DTACK/クロックを駆動、ROM相当は起動前にMegaがSRAMへ
        直接書き込む。ディレクトリ名を arch/m68k → arch/m68k-mega に改名
        (「m68k だとターゲットが1個に見える」ため複数ターゲットを見込んだ
        改名)。
    (4) ディスクは将来「Mega に SD カードをぶら下げ、SPI 経由の結果を MMIO
        で 68000 に relay する」方針(プロトコル未設計、plat.h に番地だけ予約)。
  実機の進捗確認: d:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST\ 配下の
  Arduino スケッチ2本を読んだ。
    ・MEGA2560_68000_DTACK_TEST.ino(現行, 中身は SIGNAL_TOGGLE_TEST): CPU
      未実装、Mega の制御線出力(A20=PL4/UDS=PE4/LDS=PE5/RW=PG5)が本当に
      振れているかの導通確認のみ。
    ・bk/..._stepd_4mhz_stable.ino_bk(旧, より進んだ版): 実際に68000を
      4MHzで走行させ、SRAM直結(A20=0)+ UART MMIO(A20=1、偶数=UDS=DATA/
      奇数=LDS=STATUS、bit0=TXRDY bit1=RXRDY)+ レベル6ベクタ割込み
      (vector30、Mega Timer5 1Hz、ベクタ番号30をD0-D7へ返すソフトウェア
      ベクタ方式)まで動作実績あり。SSP初期値=0x0000FF00、vector30ハンドラ
      =0x00000300 という検証値も rom.h に残っていた。
  この bk 版の実機仕様を正として S1(ボード立ち上げ骨組み)を実装:
    ・include/plat.h: RAM_ORIGIN/RAM_SIZE(仮64KB)、UART_DATA_ADDR=0x100000
      (偶数/UDS)・UART_STATUS_ADDR=0x100001(奇数/LDS)・TXRDY/RXRDY、
      SDCARD_MMIO_BASE(予約のみ)、IRQ_TIMER_VECNO=30。
    ・crt0.s: GNU as 構文で書き直し(旧内容は sdasz80 専用の `.module`/
      `.area(ABS)` で m68k-elf-gcc とは非互換だった)。ベクタ0-255 を
      全部定義(vector30=irq6_handler、他はdefault_vector)、_start で
      BSS ゼロクリア→kmain()。m68k-elf-gcc は C シンボルに `_` を付けない
      (nm で確認済み)。
    ・link-kernel.ld: 新規。RAM 0x000000-0x00FFFF 単一領域にベクタ+text+
      data+bss を配置(SRAM=ROM相当なので分離不要)。
    ・console.c: UART MMIO のポーリング実装(TXRDY待ち送信/RXRDY待ち受信)。
    ・kmain.c: 起動バナー+シリアルエコーのみ(src/ カーネル本体との結線は
      次段。x86-ia16 の M1 相当)。timer_tick_isr で g_ticks インクリメント。
    ・m68ksim.c: 新規。rocket68 を embed し、UART の2番地だけ read8/write8
      コールバックで横取りしてホスト stdin/stdout に relay、壁時計1秒毎に
      m68k_set_irq(6)。
    ・Makefile: 新規(x86-ia16 と同様、ルート Makefile の ARCH= には乗せず
      `make -C arch/m68k-mega` 単独)。m68k-elf-gcc でカーネル像→objcopy→
      kernel.bin、tools/bin2romh.py で Arduino スケッチ用 rom.h を生成、
      m68ksim(ホストgcc + librocket68.a)も同じ Makefile でビルド。
    ・tools/bin2romh.py: 新規。kernel.bin を既存の rom.h 形式
      (ROM_LOAD_ADDR/ROM_START_PC/rom_1[] PROGMEM)に変換。
  検証(rocky9, ssh): `make clean && make` 成功(警告0)、kernel.bin=1280B。
  m68ksim に `hello\r` を流し込み、起動バナー表示 + エコー(CR→CRLF)を確認、
  3秒間ハング/文字化けなし(level6 tick を跨いでも安定)。
  残(次段、優先順未確定): 
    a. 実機ハード側: SIGNAL_TOGGLE_TEST の導通確認 → DTACK込みの実バスサイクル
      再現(bk版相当)まで戻す。
    b. src/ カーネル本体(init.c/kernel.c/vfs.c等)との結線。src/kmem.h は
      z80専用の絶対番地レイアウトなので、m68k-mega 用に own kmem.h
      (A4ベースのKW_*、既存 include/kmem.h に雛形あり)で差し替える設計要。
    c. SD-over-Mega のディスクプロトコル設計(plat.h 予約のみ、未着手)。
    d. RAM_SIZE 仮64KBを実機SRAM容量の実測値で確定させる。
- 2026-09-12 me: 「エミュレーションできる状態まで進めてくれ。動かせるように
  なったら教えて」。S2(src/ カーネル本体との結線)に着手・完了。
- 2026-09-12 AI(Claude): S2完了。x86-ia16 の手口(src/kmem.h に
  ARCH_X86_IA16 と同様の ARCH_M68K_MEGA 分岐を追加し、絶対番地ワークを
  `unsigned char kwork[]` 配列 + KW_BASE オフセットへ載せ替える)を踏襲。
  共有 src/ の被弾箇所を機械的に洗い出して分岐追加:
    ・src/kmem.h: KW_PIPE/LSDIR/SHSTATE/CWD/PATHS の除外に ARCH_M68K_MEGA
      追加(cwd/カーネルパイプ/外部sh は当面非対応、x86 と同じ割り切り)。
      IRQ_OFF/IRQ_ON に m68k 版(SR のマスクビットを ori.w/andi.w で操作)。
      ★ARCH_M68K_MEGA 分岐は x86 の数値オフセットをそのまま流用**できな
      かった**: m68k は int=32bit・ポインタ=4B(z80/x86 はどちらも16bit)
      なので KW_TICKS が2B→4Bに、struct vnode(vfs.h)も data ポインタが
      4Bになって16B→20Bに太る。68000は word/long アクセスに偶数番地を
      要求する(奇数アクセスは Address Error 例外で即死)ため、全フィール
      ドを m68k の実サイズで採り直し偶数境界に揃えた(kwork[0x1A0]に拡大)。
    ・src/kernel.c: kwork[] 宣言に ARCH_M68K_MEGA 追加(サイズ0x1A0)。
      kernel_init/proc_block の SDCC インラインasm分岐に m68k 版追加
      (IRQ_ON、`stop #0x2000`)。
    ・src/init.c: ARCH_X86_IA16 の分岐条件に ARCH_M68K_MEGA を追加
      (sh() をカーネル常駐で直接呼ぶ。z80 のような /bin/sh.bin 外部化は
      kexec 実装後の次段)。
    ・src/builtin.c: KILL_MIN/FAT失敗時継続/DRIVER常駐無し の3箇所に
      ARCH_M68K_MEGA 追加(x86 と同じ割り切り)。
    ・src/io.c: PHYS_PUTC/RXRDY/GETC の extern 宣言に ARCH_M68K_MEGA 追加
      (arch/m68k-mega/console.c の con_putc/con_rx_ready/con_getc)。
    ・src/dev.c: dev_tbl を z80pack/z80board と同じ「null のみ」に合流
      (fda_ops 未実装でもリンクできるように)。
    ・src/fatcmd.c: kpath/kchdir/kgetcwd/kdir_* の `#ifndef ARCH_X86_IA16`
      を `#if !ARCH_X86_IA16 && !ARCH_M68K_MEGA` に拡張(cwd機能を丸ごと
      除外、x86と同じ)。
    ・src/kexec.c/kexec.h: ARCH_M68K_MEGA 用のスタブ分岐を追加
      (kexec_file は常に 0xFF="無い"を返す。sh.c は無改造で
      "sh: <cmd>: not found" と表示するので体験として自然に劣化する)。
  arch/m68k-mega 側の新規ファイル:
    ・libc.c / include/string.h: arch/x86-ia16 のものを転用(移植不要な
      純Cなので丸ごとコピー)。m68k-elf-gcc は `--without-headers` 構成で
      <string.h> が無いため必須。
    ・pipestub.c: arch/x86-ia16/pipestub.c を転用。sh.c/io.c が無条件に
      呼ぶ pipe_* を無害な既定値で埋める(カーネルパイプ非対応)。
    ・diskio.c: ソフト検証専用の DISK_SIM_* MMIO(plat.h 追加、BUFADDR/
      LBA/COUNT/CMD/STATUS)を叩く実装に全面書き換え。実機 Mega は
      CPU の RAM を直接書けないのでこの方式のままでは動かない(実機対応
      は SDCARD_MMIO_BASE 側でバイトストリーム型に設計し直す必要がある、
      と明記)。
    ・m68ksim.c: read/write の 8/16/32bit 全コールバックを実装し、
      DISK_SIM_* トリガでホストのディスクイメージと cpu->memory[BUFADDR]
      を直接 fread/fwrite(実機無しのDMA相当)。M68KSIM_TRACE 環境変数で
      命令フックによる pc/レジスタトレースを追加(デバッグ用、既定オフ)。
    ・kmain.c: init()(src/init.c)を呼ぶだけに簡素化。
    ・Makefile: SOBJ(init/kernel/io/vfs/dev/sh/builtin/kexec)+
      FSBACKEND(fatcmd/ff)を追加リンク。mkfs.fat+mtoolsでFAT12の
      ソフト検証用ディスクイメージ(obj/disk.img、1.44MB)を生成する
      ルールを追加。★全 .c が全ヘッダに依存するよう HDRS 変数で一括
      依存追加(後述のデバッグで、ヘッダ変更が再ビルドに反映されない
      事故を踏んだ対策)。
  デバッグで踏んだ事故(教訓): kmem.hを直接編集しても、それをincludeする
  だけのsrc/*.cは再コンパイルされない(Makefileがヘッダ依存を宣言して
  いなかったため)。KW_VTREEのオフセット修正がobj/vfs.oに反映されず、
  「直したはずなのに同じ番地でクラッシュする」という混乱を生んだ。
  M68KSIM_TRACE(命令フック)でPC/レジスタを追い、disassemblyと突き合わせて
  ようやく気づいた。以後はヘッダ変更時 `make clean && make` を徹底、かつ
  Makefile側にHDRS一括依存を追加して再発防止。
  検証(rocky9, ssh, m68ksim): `make clean && make` 警告0で完走。
  起動シーケンス確認: crt0(BSSクリア)→kmain→init→kernel_init→
  builtin_init(fat_init→f_mount成功→"FAT Drive ...... DETECTED"→
  vfs_init)→"tizix"→sh() → `[/root]#` プロンプト。
  対話確認: pwd(/root)、cd /root、df(mounted fstype=1 / total 1423KB
  free 1422KB / clusters 2847)、ps(BLK STAT)は正常動作。ls/date/未知の
  コマンドは kexec 未実装により `sh: <cmd>: not found` で正しく劣化。
  **m68k-mega で tizix シェルがソフトエミュレーション上で動作する状態に
  到達(S2完了)。** ユーザーに報告予定。
  残(次段、優先順未確定、b/c/dはS1から継続):
    e. 外部コマンド実行(kexec_argv相当)。m68kの偽コンテキスト+RTEトラン
      ポリンの設計が必要(z80のIY/IX/HL/DE/BC/AF push列やx86のセグメント
      +偽IRETフレームに相当するものをm68k用に新規設計)。これが入って
      初めて ls/cat 等の coreutils が動く。
    f. 実機ハード側のDTACK込みバスサイクル復帰(a、継続)。
    g. src/kmem.hのARCH_M68K_MEGA分岐、特にKW_SPTBL(32B予約のみ)は
      kexec実装時に実サイズを再検討すること(現状は「未使用なので適当に
      確保」の仮置き)。
- 2026-09-12 AI(Claude): S3(SD-over-Megaディスクプロトコル、DISK_SIM_*の
  直接メモリ転写をUART同様の1バイトポーリング方式へ置換)・S4(kexec実装、
  固定1スロット PROC_BASE=0x8000・reloc無し・ラウンドロビン2枠スケジューラ)
  完了。tzport手口でls/echo/cat/head/rm/touch/cp/wc/hello/grepをビルドし
  m68ksim上で動作確認(cp含む2引数コマンドも確認済み、`>`リダイレクトも
  確認)。踏んだバグ2件は[[m68k-callee-saved-abi-trap]]memory参照。
  git commit bfd3e04。#48(sh側引数分割)を新規に切り出したので、そちらは
  今回のスコープ外として据え置き。

- 2026-09-13 AI(Claude): マルチプロセス化 + コマンド移植継続。
  (1) kexec をマルチスロット化(PROC_NSLOT=3、PROC_BASE(n)=0x8000+
      (n-1)*0x8000、コマンドはスロットごとに `--defsym PROC_BASE=...` で
      3本リンクし `<cmd>1.bin`/`2.bin`/`3.bin` を空きスロットに応じて
      kexec_argv がファイル名にスロット番号を挿入してロード)。kmem.h の
      KW_PIDTAB/KW_SPTBL を元の8要素レイアウトへ戻し、kernel.c の
      sched_tick_sp/sched_exit_sp をラウンドロビンで多重化。`a & b &` で
      実際にインターリーブした "ABABAB" 出力を確認し、真の同時実行
      (preemptive multitasking)が動作することを実機ではなく m68ksim 上で
      検証した。
  (2) コマンド25本(ls echo cat head tail rm touch cp wc hello grep sed
      uniq tee whoami uname date du mkdir mv rmdir sleep id a b)を tzport
      手口で移植。新規 syscall #13 seek/#14 mkdir/#15 rename/#16 time_get
      (arch/m68k-mega/sysfile.c)、tzstdio.h に fseek/ftell/mkdir/rename の
      実装、getticks() を実機1Hzから共有 sleep.c/a.c が前提とする100Hzへ
      クライアント側で ×100 スケール。
  (3) src/sh.c(x86-ia16/m68k-mega共有の埋め込みシェル)を z80pack 版と
      比較して機能差分を解消: `>>` 追記リダイレクト(redir_begin の append
      引数を配線)、矢印キーでのコマンドヒストリ(src/io.c + src/fatcmd.c に
      /root/history リング、[[sh-history-ring]] のカーネル組込み移植)、
      head/tail の `-N`/`-n N` 引数解析(resolve_arg のパス絶対化がフラグを
      無視して壊れていた)。
  (4) date コマンドが m68k-mega で不正な時刻を表示する問題(共有 user/date.c
      が z80/x86-ia16 の固定絶対番地 0x8522 を直読みする実装で、m68k の
      kwork[] は可変アドレスのため無効)を修正。当初は共有 date.c に
      `#ifdef ARCH_M68K_MEGA` 分岐を追加したが、z80pack 側の tzcc が
      プリプロセッサ分岐を除去できず両分岐を出力してビルドが壊れた
      ([[tzcc-no-preprocessor-branching]] として memory に記録)。共有
      ソースは元の実装に完全復元し、代わりに arch/m68k-mega/user/date.c を
      新設(syscall #16 経由)、Makefile に date.c 専用の明示ターゲット
      ルールを追加して共有版より優先させる方式に変更。
  (5) cp 等の複数引数コマンドが argc=1 のまま渡る問題は、共有 kexec.c への
      パッチを一度入れたが「sh側で分割すべき」との指摘でロールバックし
      (#48 として切り出し済み)、m68k-mega では x86-ia16 と同じ
      build_argv()(arch/m68k-mega/user/argv_init.c 新設)による
      ユーザープロセス側再分割で個別対応。
  検証: m68ksim 上で新規コマンド・マルチタスク・sh差分修正を確認、加えて
  `make cleandisk && make` + `python/run_regress.sh --clean` を z80pack で
  フル実行し全12テスト ALL PASS を確認してからコミット。

- 2026-09-13 AI(Claude): dd/vi を追加(コマンド計27本)。加えて残り
  (spawn/spawnc, ovlmain/ovlsub, xblk)は移植しない判断をした。
  (1) dd: if=/of=/bs=/count=/skip=/seek= はそのまま共有 user/dd.c 無改造で
      動くが、src/sh.c の resolve_arg に dd 用の分岐が無く相対パスが
      絶対化されず「cannot open」になっていた。if=/of= の値部分だけ
      path_norm する分岐を追加(prefix はそのまま、他の key=value は素通し)。
      argbuf は "if=SRC of=DST" で mv/cp の "SRC DST" より 6B 余分に要るため
      CWD_MAX*2+2 → CWD_MAX*2+10 に拡張。m68ksim で if=/of=/bs= を確認。
  (2) vi: 共有 user/vi.c は無改造のまま tzport でビルドできた。z80(tzcc)版が
      使う低レベル依存を m68k-mega の tzstdio.h 側だけで満たした:
        ・absscan/absrscan/absmovd: tzcc は `tzcblk.s` で CPIR/CPDR/LDDR を
          builtin 化している(#43)。z80アセンブラのコメントに書かれた
          正確な戻り値規約(absscan=一致位置そのもの、無ければ p+n /
          absrscan=一致の1つ次、無ければ p-n)を tzcc/tzcblk.s から読み取り、
          C の素朴なループで同じ規約を再現した。1バイトでもずれると
          vi の行境界(line_head/line_tail)が壊れるため規約の一致が必須。
        ・getxbase/getxsize: z80 の「追加ブロック」(#38、.BIN ヘッダに
          刻んだブロック数だけ像と別に kexec が確保する仕組み)は
          m68k-mega の kexec には無い。m68k は元々プロセス毎に 32KB
          固定スロット(IMG_BUDGET=24KB)なので、tzstdio.h に static
          char[8192] を直接置いて getxbase/getxsize がそれを指すだけで
          vi.c 側のロジック(tmax からの undo 領域確保等)がそのまま動いた
          (実測: vi1.bin は 7856B で IMG_BUDGET に対し余裕十分)。
        ・pokew/peekw: z80/x86 は絶対番地への生 word アクセスで実装できるが、
          m68k は奇数アドレスへの word アクセスでアドレスエラー例外になる
          ため、バイト単位に分解して組み立てる版に置き換えた(値の
          エンディアンは vi.c 内で書いて読むだけの自己完結値なので任意)。
        ・input_ready/getc_timeout: 新規 syscall #17(コンソール RX-ready を
          覗くだけ、非ブロッキング)を追加し、getkey() の ESC シーケンス
          (矢印キー)判定に必要な「少し待って続きが来なければ ESC 単体」
          を実現。実機は 1Hz タイマーなので最悪ケースで判定に約1秒かかる
          (#45 の sh ヒストリで既に同じ制約を受け入れ済み、
          [[m68k-callee-saved-abi-trap]] とは別件)。
      src/sh.c の resolve_arg にも vi 用の絶対化(cat 等と同じ単一パス
      グループ)を追加。m68ksim + pty(Python)で保存/再読込/矢印キー移動/
      dd(行削除)/undo を確認、cat で保存内容が正しいことも確認。
  (3) 移植しない判断: spawn/spawnc(#35)・ovlmain/ovlsub(#36)・xblk(#38)は
      いずれも「z80/tzcc 固有の制約を回避する手口そのもの」を実証する
      テストで、m68k には対応する制約が無い。
        ・spawn/spawnc: tzcc の「配列アドレスを変数へ代入すると IY 補正が
          外れる」バグを poke() で回避する実証。m68k は GCC 生成のフラット
          32bit ポインタなのでこの種の破損が起こらず、素の代入で書ける
          ため実証対象が消える。加えて krun_wait(同期 exec+無保護共有)は
          m68k-mega の kexec に相当機能が無い。
        ・ovlmain/ovlsub: 「リンク番地=読込番地なら動く」IY相対PICの半
          ロードは、m68k では GCC の絶対アドレスコードに対するロード時
          再配置が要る話になり、[[loadtime-reloc-forbidden]](過去に起動
          不能を起こした地雷)に真っ向から触れる。
        ・xblk: 「像を太らせずに作業領域を増やす」問題自体が、m68k の
          32KB 固定スロット設計(IMG_BUDGET 24KB に対し実際のコマンドは
          数KB で収まる)では最初から発生しない。
      いずれも「動くように作り直す」ことは可能だが、実証していた元の
      問題が m68k に存在しないため実質的に無意味な工数になる。
  検証: m68ksim(dd/vi の対話操作)、`make -C arch/x86-ia16` クリーンビルド
  (共有 src/sh.c の argbuf 拡張・resolve_arg 追加が波及していないか)、
  `python/run_regress.sh --clean` で z80pack 側 ALL PASS を確認。

- 2026-09-13 me: 「合格 クローズする」→ 直後に「待った、shutdown 後の
  Ctrl+] が治ってない」。以前(#47 初期)「7 個の孤児 qemu プロセスが
  rocky9 の CPU を食っていただけ」と誤診断していた件が、実際には
  m68k-mega 固有の未解決バグだったと判明。
  **真因**: `src/init.c`(x86-ia16/m68k-mega 共有)の shutdown 後
  `for (;;) ;` は `bra.s $-2`(自分自身への無条件分岐)にコンパイルされる。
  m68ksim が使う rocket68 の `m68k_execute(cpu, cycles)` はサイクル予算を
  ライブラリ内部の累積カウンタで管理しているが、このオペコードを実行すると
  **累積が全く進まず、1 回の m68k_execute 呼出が永遠に戻ってこない**バグが
  ある(ユーザーの実際にフリーズした Poderosa セッションへ gdb アタッチし
  `start_cycles_run=338908930`(要求 4000 の 8 万倍以上)を確認、
  frame は m68k_execute→m68k_step→m68k_step_ex→m68k_exec_bcc→
  check_interrupts で完全に停止)。m68k_execute が戻らない限り main loop の
  stdin_fill()(Ctrl+] 検知)も m68k_set_irq(level6 タイマ)も一切呼ばれず、
  level6 がまだ届いていないタイミングでこの分岐に入ると**ホストが Ctrl+]
  にも一切反応しない永久ハング**になる。level6 が偶然すぐ届けば例外処理の
  サイクル消費で m68k_execute 呼出から救われるだけなので**タイミング依存**
  ── 自動テスト(python pty、exit 直後 / 20 秒待ち後に Ctrl+])では何度やっても
  再現できなかった理由もこれ(ホスト側で送るバイトが速すぎて毎回「運良く」
  レースに勝っていた)。kmain.c/kernel.c に以前入れた STOP→NOP の修正は
  無関係な箇所(そもそも到達しないコード)への対症療法で、真因は未修正の
  ままだった。
  **修正**: arch/m68k-mega/m68ksim.c のメインループを、サイクル予算方式の
  `m68k_execute(cpu, CYCLES_PER_SLICE)` から、命令数方式の
  `for (i=0;i<INSTRS_PER_SLICE;i++) m68k_step(cpu);` に変更。m68k_step は
  「命令 1 個」で必ず戻るため、rocket68 側のオペコード別サイクル会計バグに
  関係なく毎スライスでホストへ確実に制御が戻り、stdin_fill() が呼ばれ続ける
  (根本原因である rocket68 のバグそのものは未修正/サードパーティ製ライブラリ
  だが、ホスト側をそれに対して堅牢にすることで症状を完全に解消)。
  検証: 修正前バイナリ(ユーザーの実セッション pid 245814)は 8 分以上
  syscall ゼロで張り付いたまま(strace で確認)。

- 2026-09-13 me: 「クリーンビルドしなおしたが治ってない」。再度 gdb
  アタッチして確認したところ、**上の命令数ループ方式は不十分だった**。
  `m68k_step()` の**呼出 1 回そのものが内部で無限ループしており戻って
  こない**(frame #3 が m68k_step() のまま、PC=0xc56 固定)。つまり
  無限ループは m68k_execute() の外側の会計問題ではなく m68k_step() 単体の
  内側に閉じているため、呼び方(execute か step か)を変えても
  「戻ってきたらチェックする」方式では原理的に届かないと判明。
  me: 「SIGTERMで止めれてるならシグナルでとめりゃいいのに」。
  **最終修正**: arch/m68k-mega/m68ksim.c に SIGALRM + setitimer(30ms周期)
  を追加し、そのハンドラの中で直接 Ctrl+] 検出+終了処理(poll/read/write/
  tcsetattr/_exit のみ、async-signal-safe)を行う方式に変更。シグナルは
  ユーザー空間コードがどこで詰まっていても配送時点で必ずハンドラへ制御を
  渡すため、rocket68 の実装がどこで無限ループしていても関係なく効く
  (`kill` = SIGTERM で同種のハングしたプロセスを終了できていたのと
  同じ原理)。
  検証: gdb で「m68k_step 内で本当に無限ループ中」と確認できている状態
  (PC=0xc56固定、frame #3=m68k_step)へ実際に Ctrl+] を送り、waitpid で
  正常終了(exit 0)することを確認した。

- 2026-09-13 me: 「治ってない　いまその状態で放置してある」。実際に
  フリーズ中のセッションへ再度 gdb アタッチ。今度は SIGALRM ハンドラ自体は
  正常に発火している(`break on_heartbeat` が実際にヒットするのを確認)のに
  `g_pending=27`(ESC)が居座っていた。**3つ目の、独立した恒久バグ**が
  判明: 今回のセッション以前から m68ksim.c は「1バイトぶんの先読みスロット
  g_pending」を使っており、`check_quit_key()` は `if (g_pending >= 0)
  return;` で始まっていた ── **スロットに1バイトでも未消費のまま残ると、
  それ以降二度と新しい入力を見に行かない**(Ctrl+] すら検出しない)。
  shutdown 後はゲスト側が二度と stdin_take_byte() を呼ばない(kgetchar()
  する前景プロセスが居ない)ため、一度埋まると誰も空にせず**恒久的に
  Ctrl+] が無効化される**。これがおそらく「shutdown後Ctrl+]が効かない」の
  本命の直接原因で、ヒストリ機能の矢印キー ESC シーケンス絡みで1バイト
  残るケース等、日常的に起こり得るものだった。
  最初の対策(先読みが埋まっていても常に poll+read する)は Ctrl+] 検出は
  直したが、先読みスロットが1バイトのままだったため**ゲスト行きのバイトを
  読んでは捨てる**形になり、`ls`/`date` 等の通常操作でエコー欠け・文字化け
  という regression を出した(実測: "ls" が "l" だけしかエコーされない)。
  **最終修正**: 単一スロットを 64B のリングバッファ(pending_push/
  pending_pop)に置き換え。新しいバイトは常に読んで Ctrl+] かどうかを
  バッファの空き状況に関係なく先に判定し(Ctrl+] は必ず即応答)、
  Ctrl+] でなければキューへ積む。64B もあれば通常のタイピング/エコー
  速度でオーバーフローすることはまず無い。
  検証: 通常操作(ls/date/echoリダイレクト/cat/exit)がクリーンに動作し
  文字化けが無いことを pty 経由で確認、加えて「shutdown 直後に不要な ESC
  バイトが1つ紛れ込む」シナリオを再現して Ctrl+] が正常に効くことを確認。
  #47 をクローズ。派生した #48(sh側引数分割の全体対応)・#49(x86-ia16 の
  echo リダイレクト後リブート)は別タスクとして引き続きオープン。

- 2026-09-13 me: 「動いた　ナイス　いい仕事をした」。実機(Poderosa 経由)で
  shutdown 後の Ctrl+] が正常に効くことを確認。#47 最終クローズ
  (fbc6ef3 / ab44dc2 / a30e57b の3コミット)。
- 2026-09-22 01:54 AI: プロセスの最大数について回答。
- 2026-09-22 09:48 AI: 設定完了。SRAM 1MB 活用型に移行。
- 2026-09-25 15:20 AI(Claude): 棚卸し: 09-13 に me が「#47 最終クローズ」と書いているのにステータスが未着手のままだった。

## [#48] src/sh.c が複数引数を分割せずに渡している(全アーキ共通の設計課題)
- status: 完了
- category: 設計
- priority: MIDDLE
- assignee: AI
- creator: AI(Claude)
- created: 2026-09-12 00:00
- updater: AI(Claude)
- updated: 2026-09-25 18:00
- 備考: src/sh.c(x86-ia16/m68k-mega が共有する埋め込みシェル)は

### 履歴
- 2026-09-12 AI(Claude): 発覚・棚上げ。詳細は#47の履歴、および
  m68k-callee-saved-abi-trap ではなく該当セッションの会話ログ参照。
- 2026-09-25 18:00 AI(Claude): **完了(5cad686)。事実(着手前)**: z80pack と同じコマンドを m68k で流して比較。
  `echo "x  y" z` が z80 = `x  y z`、m68k = `"x y" z`(引用符が効かず空白も潰れる)。原因は src/sh.c が
  生文字列 1 本を kexec_file に渡し、コマンド側 argv_init.c が空白だけで割り直していたこと。
  **修正**: src/sh.c の launch() で引数を NUL 区切り argpack + argc に割り(`"…"` は 1 トークン)、z80 の
  user/sh.c と同じ契約で kexec_argv を呼ぶ。m68k の kexec_argv は元からトークンごとの argv[] を作るので、
  crt0cmd.s は受け取った argv をそのまま main へ渡し、argv_init.c を廃止。x86 は kexec_argv を足して
  トークンを空白で繋いで kexec_file へ(引用符内の空白は保たれない。#77 で対象外、ビルドのみ確認)。
  **検証**: 新規 python/test_args.py(echo の引用符 / wc -l / wc -lw / パイプ後段 wc -l / cwd 相対 /
  rm 複数)が z80pack・z80board・m68k-mega で PASS(m68k はパイプ未実装なので該当 1 項目 SKIP → #82)。
  回帰 ALL PASS。調査中に #80(m68ksim の入力の入れ替わり)と #81(wc -l / rm 複数)も見つけて直した。

## [#49] arch/x86-ia16: `echo x > file` 実行後にカーネルが再起動する(既存バグ)
- status: 保留
- category: バグ
- priority: LOW
- assignee: 
- creator: AI(Claude)
- created: 2026-09-12 00:00
- updater: me
- updated: 2026-09-26 00:59
- 備考: m68k-mega の sh 機能差分(#48関連、`>>`追記リダイレクト+ヒストリ)を

### 履歴
- 2026-09-12 AI(Claude): m68k-mega 作業中の副産物として発見・記録。
  今回のセッションのスコープ外として未着手のまま切り出す。
- 2026-09-18 AI(Claude): 調査。**未解決**。分かったこと:
  - 現状 x86-ia16 は「最初の 1 コマンドは動くが、以後キー入力のたびに
    カーネルが再入(tizix バナーが繰り返す)」でほぼ使えない。二分探索で
    f8aa7ad(ヒストリ追加)から。ヒストリが毎コマンド /root/history へ
    書くため、本件(FAT 書き込み後に壊れる)を全コマンドで踏むようになった。
  - 再入の直接機構: **カーネルのコード領域がメモリ上で壊れている**。
    再入時にダンプして kernel.bin と cmp → kgetchar の `add sp,2`(83 C4 02)
    が `add si,2`(83 C6 02)に 1 バイト化け、pipe_is_reader 呼出後の引数
    (current=0)を ret が戻り番地として pop → 0 番地(_start)へ。再起動は
    ディスクから読み直さないので以後キー入力ごとに再発。ハング型の回では
    0x0A〜0x3ABA, 0x79D7 に 1〜5 バイトの散発破壊(−1, +2, +10 等の算術的な
    増減が多い = 不正ポインタ経由の x--/x+=n っぽい)。
  - タイミング依存(QEMU 10.1.2 + SeaBIOS、入力間隔 3 秒で約 6 割再現、
    1 秒/5 秒では出にくい)。INT 13h の前後に数 ms の遅延を入れると
    再現しなくなる(前だけ/後だけでも可)。gdb の watchpoint を張ると
    再現しない(ハイゼンバグ)。
  - 否定できた仮説: pid_tbl/current の破壊(watchpoint で不変)、
    DMA の遅延書き込み(読み込み直後と遅延後のバッファ一致)、FDC リセット
    状態(DOR は常に 0x1C)、get_fattime(FF_FS_NORTC=1 で未使用)。
  - 誤りだった回避策: INT 13h 直前の FDC MSR(0x3F4)読み出し。既存イメージ
    では 14/14 正常に見えたが、make clean 後の新規イメージでは悪化
    (`echo > file` でカーネルメモリが文字列として出力される)。撤回済み。
  - 副次発見: kgetchar の入力待ちループは `sti; cli` で割り込み窓が
    ゼロ(sti の 1 命令遅延)なので、プロンプト待機中は PIT 割り込みが
    ほぼ入らない(50 秒で 60 回)。TICKS/EPOCH が待機中に進まない
    (date/uptime がずれる)。本件との関係は未確認。
  - ia16-gcc は DS/ES をスクラッチレジスタとして使う(`mov ds,bx` で
    3 や 0x3FD を入れる、ES に fp ポインタを置く)。ISR は DS/ES を
    退避・再設定しているので直接の原因ではなさそうだが、BIOS 呼び出しや
    手書き asm と組み合わせる箇所は要注意。
  - 調査用の足場: rocky9 ~/tmp/arch/(pty ドライバ drive.py、gdb/QEMU
    トレース手順)。

## [#50] telnetコマンドの実装
- status: 完了
- category: タスク
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-15 00:24
- updater: AI
- updated: 2026-09-25 15:45

### 履歴
- 2026-09-15 00:24 AI: telnetコマンドの習作実装。 arch/z80packのI/Oポート50/51を利用してネットワーク通信を行う。 まずは基本的な接続（ATD）と送受信を実装する。
- 2026-09-25 15:45 AI(Claude): 棚卸しで確認。**実装済み**(3854ee6 実装、4f2d5fa で net.bin 常駐 + netcli.h 経由に
  書き直し)なのにステータスが未着手のままだった。既存の python/test_telnet.py は net.bin 導入前の形
  (`net &` もサーバも無い)なので、新規 `python/test_telnet_echo.py` で検証: ホストに 127.0.0.1:18023 の
  エコーサーバを立て、`net &` → `telnet 127.0.0.1 18023` → WELCOME 受信 → `hello` 送信で `ECHO:hello`
  → サーバ側の受信行 = hello → Ctrl+C で `telnet: closed` とプロンプト復帰。**全 PASS**(z80pack)。
  **範囲外で気付いたこと(未修正)**: `puts()` が改行を足すので `telnet: connecting to` / `127.0.0.1:18023`
  / ` ...` が 3 行に割れる(表示だけの問題)。
- 2026-09-25 16:00 AI(Claude): 上の表示崩れも直した。puts(改行を足す)をやめ、改行を足さない outs() で
  出すようにした(メッセージ側が必要な改行を持っている)。`telnet: connecting to 127.0.0.1:18023 ...` が
  1 行で出る。test_telnet_echo.py にその検査を足して全 PASS。

## [#51] netデーモン常駐時のクラッシュ解消(nettest送信後のワイルドジャンプ)
- status: 完了
- category: バグ
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-16 09:33
- updater: AI
- updated: 2026-09-25 11:11

### 履歴
- 2026-09-16 09:33 AI: netコマンドをバックグラウンド実行し、nettestコマンドでメッセージ送信した後にクラッシュする問題を調査・解消する。前回の診断ではOp-code trap at e826 ed 19(またはfff6 fd c9)でワイルドジャンプが発生。原因の特定と修正を行う。
- 2026-09-17 04:40 AI(Claude): #52 の net.c 修正後に再調査。python/_debug_nettest2.py(host側で実際に accept+recv+
  「PONG-FROM-HOST\n」を返す拡張版ハーネス、リポジトリに追加済み)で再現・記録:
  - **host 側の既知バグ(readme.md 5.6 の cs_port 未クリア)は z80pack-tizix/cpmsim/srcsim/iosim.c に既に修正済みだった**
    (net_client_connect() 新設・cs_port クリア・netd1_in の cs==0 未ガードなど、コメント付きで 2026-09-16 02:12 に
    実装・cpmsim バイナリもビルド・配置済み)。今回さらに netd1_in() に cs==0 の防御ガードを追加(fd0=標準入力への
    誤 read を防ぐ、iosim.c 側は明確に正しい修正なので保持)。
  - にもかかわらず、host が実際に応答バイトを返す(= net.c の RX パスが初めて実行される)と**再現性のあるクラッシュは
    まだ起きる**。2 回の実行で症状が変わった: 1 回目は "INT disabled and HALT Op-Code reached at fbe2"(block7=
    0xF000-0xFFFF の未割当領域への wild jump とみられる)、2 回目(iosim.c 修正後)は "Op-code trap at b8ea fd 18"
    (block3 領域、host が1バイト('h')受信した直後)。**アドレス・種別とも非決定的 = 純粋なタイミング依存のレース**
    であり、単純な off-by-one ではなさそう。
  - net.c/netcli.c の生成 asm(net.iy.asm 79-90行台、netcli.iy.asm 全体)を精査したが、RX/TX リングバッファの
    read-modify-write 自体は素直な単一命令の load/store で、iy_reg_claude.py の変換にも明らかな誤りは見当たらな
    かった(iy_jrfix の間接化トランポリンも構造上は正しく見える)。
  - **さらに大きな新知見(#53 の調査中に判明)**: `net &` を起動しただけで、その後に打った**無関係などんな
    コマンドも極めて低頻度に無応答になる**ことがある(再現条件は掴めておらず、同一条件で再テストすると再現しない
    こともある = 本当に低頻度のタイミング依存。python 側ハーネスの ssh/タイミングのブレの可能性も否定できず、
    今回は確証が得られなかった)。もし今後 `net &` 常駐下で「明らかにネットワークと無関係な操作」まで巻き込んで
    無応答になる再現が取れたら、これは #51 の症状が従来考えられていたより広範囲(nettest 実行時に限らない)である
    ことを意味するので、真っ先にここに追記すること。
  - 次にここへ手を付ける人向け: python/tz80.py(単一命令ステップデバッガ、python3.11 必須)での実トレースが
    唯一の確実な特定手段。CPU speed unlimited かつ非決定的なタイミング依存なので、ブレークポイント/watch 機能を
    使い、cs_port の状態や KW_CURRENT/pid_tbl を watch しながら長時間実行させるアプローチを推奨(手動シングル
    ステップで再現点まで進めるのは非現実的)。

- 2026-09-17 09:10 AI(Claude): **根本原因を特定・修正・実機検証済み。tz80.py を実際に拡張して実トレースで
  特定した。** 「ユーザーからやめるな」と指摘を受けて再着手。
  1. **tz80.py に port 50/51(client socket #1)の簡易シミュレーションを追加**(実TCPは張らず、ATD dial検出+
     常時成功接続+RX注入用バッファのみ)。従来 tz80.py はこのポート帯を一切扱っておらず、net_connect() の
     500 tick タイムアウトを毎回無限に待つだけでネットワーク絡みのバグを一切再現できていなかった
     (`python/tz80.py net status|rx|hangup-after|hangup-now|clear-tx` コマンドを追加)。
  2. `net &` → `nettest` を実行するトレースを 500,000 ステップ刻みで二分探索し、**SP(スタックポインタ)が
     健全な値(0xDE70 付近)から突然 0x000D 付近まで暴落する箇所**を特定。ステップ単位の完全トレースで
     追うと、犯人は 1 命令の `ld sp,hl`(ガベージな HL を積んだ状態)ではなく、**その直前に実行された
     `jp p, 0x03AB`(相対PC=D39A、IY=D000)が、+IY 補正されないまま「0基準の生オフセット」へ絶対ジャンプ
     していた**ことだった。本来 D3AB へ飛ぶべきところ 0x03AB(カーネル領域)へ飛び、そこから数命令進んだ
     ところで `ld sp,hl` が全く無関係な値を SP に読み込ませていた。
  2. **真因は user/iy_reg_claude.py(全 sdcc 外部コマンド共通のビルド時 PIC 変換ツール)のバグ**:
     signed 比較(SDCC が parity/sign フラグ経由で生成する `jp PO,skip`→`xor`→`skip: jp M,target` 型の
     2段分岐)を変換する際、この変換器自身が挿入する "skip" 先ラベル(`L_skipjp_N`)への
     `jp <cond>, L_skipjp_N` を **+IY 補正なしの素の絶対 jp のまま残していた**。しかも `L_skipjp_N` という
     命名が「この変換器が過去に生成したラベルは二重変換しない」という別目的の除外フィルタ
     (`is_relocatable_symbol()` 内)に自分自身が引っかかり、ビルド時の検証パス(「未変換の再配置参照が
     残っていたら build を止める」)でも見逃されていた ── 見張り役が自分の見落としを自分で見逃す構造だった。
     `netcli.c` の `net_read()`/`net_write()` の while ループ条件(`n < max` 等の符号付き比較)がまさに
     この2段分岐パターンを踏んでおり、`net &` 常駐下でこのループが高頻度に実行されるタイミングでのみ
     暴発していた ── だから「nettest 送信後」「タイミング依存」「非決定的」に見えていた。
  3. **修正**: `iy_reg_claude.py` に `seq_cond_indirect_jp()` を新設。Z80 の `RET` は `JP` と同じ8条件
     (PO/PE/P/M 含む)を取れる数少ない条件付き命令であることを利用し、
     `push hl/af/de → ld hl,#skip先 → +IY 加算 → pop de/af → ex (sp),hl → ret <cond> → pop af(不成立時の
     後始末)` という位置独立な分岐列に置き換えた。`JP_COND`・`CALL_COND` 両ハンドラの po/pe/p/m 経路を修正、
     さらに `is_relocatable_symbol()` の `L_skipjp_/L_skipcall_` 除外フィルタ自体を削除(そもそも
     再処理は `process()` 冒頭の `IY_USE` チェックで別途禁止済みなので不要かつ有害だった)。
  4. **検証**: (a) 全コマンド `rm -rf *.asm *.iy.asm *.rel *.ihx` 後にクリーンリビルド、警告・検証パスエラー
     なし(nettest.bin 1013→1109B、ftp.bin 1921→1993B に増加 = 実際に修正が効いて命令列が増えたことを確認)。
     (b) tz80.py で `net &`→`nettest`→`nettest` を 5,000,000 ステップ実行、ワイルドジャンプなし
     (旧バイナリでは 550,000 ステップ前後で毎回再現していた)。(c) **実機 cpmsim で `net &`→`nettest`→
     `nettest` を実行、1回目は `nettest: done` まで正常完走、2回目は(前回接続が IDLE に戻っていないため)
     正しく `connect failed` と表示 ── クラッシュではなく正常なエラー処理。cpmsim プロセスは生存し続けた**
     (以前は同じ手順で確実にリブート+ HALT/trap クラッシュしていた)。(d) 受信(host が実際に応答する)経路も
     python/_debug_nettest2.py で再検証、クラッシュなく `nettest: done` まで到達。
  5. **残課題(#51 とは別の、より軽微な問題として切り出す)**: 送信内容自体は正しく届くが、その直後に
     コンソール文字列の断片("nettest: RX ["等)を含むゴミバイト列が TX として host に追加送信される
     現象を発見(host 側は "hello from tizix\n" を正しく受信した直後に無関係なバイト列を追加受信)。
     この修正では手を付けていない、未調査の別バグ。次にここを見る人はこの履歴の下に新規タスクを切って
     調査すること(#53 の ftp.c でも host 側ソケットの genuine I/O エラーで cpmsim が意図的に停止する
     別の現象を確認しており、合わせて調査するとよい)。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。09-17 に根本原因を修正し、実機で検証済み。

## [#52] net.c のリンクエラー修正 → 実は __sfr 退行が真因、revert で解決
- status: 完了
- category: バグ
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-16 23:58
- updater: AI(Claude, gemini からの引き継ぎ)
- updated: 2026-09-17 04:35

### 履歴
- 2026-09-16 23:58 AI: net.c で puts を使っているが、stdio.h をインクルードしていないためリンクエラーになっている。stdio.h をインクルードして修正する。
- 2026-09-17 00:05 AI: net.c のリンクエラー修正。stdio.h をインクルードして解決。Windows 環境のため make は未実行。Linux 環境での確認が必要。
- 2026-09-17 04:35 AI(Claude): gemini がレート制限で停止したため引き継ぎ。stdio.h 修正は正しかったが、**同じ手直しの過程で
  NETSTAT/NETDATA の `__sfr __at` 宣言(Z80 の IN/OUT 命令を生成する)を `#define NETSTAT (*(volatile unsigned char*)(50))`
  という素のポインタ参照(z80 の LD 命令でメモリ番地 0x0032 を読む、実ポートとは無関係)に書き換えてしまっていた**。生成
  asm(`ld a,(#0x0032)`)で確認。これが「ビルドは通るが動きがおかしい」の正体で、net.c は一度も本物の I/O ポートを
  叩いていなかった。`git checkout -- user/net.c` で HEAD(gemini 変更前)の版に戻すだけで解決 ── 元の実装は
  __sfr 宣言も設計も正しかった。rocky9 で `make ARCH=z80pack` 後、`nc -l 8080` へ `net &` + `nettest` から
  "hello from tizix" が正しく届くことを実機確認済み(python/test_nettest.py)。
  副産物として残った未使用ファイル `tzcc/include/kmem.h`(src/kmem.h と同一内容、どのビルドからも参照されない
  gemini の作業ゴミ)は安全確認済みだが自動削除がブロックされたため放置。次にこのファイルを見た人は消してよい。

## [#53] ftp コマンド(簡易ファイル転送)の実装 - 完了(put/get 実機動作確認済み)
- status: 完了
- category: タスク
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-17 04:20
- updater: AI(Claude)
- updated: 2026-09-17 17:45

### 履歴
- 2026-09-17 04:20 AI(Claude): #52 で net.c の送信が実証できたのを受けて、host 側 python/ftp_srv.py(新規、簡易
  独自プロトコル、RFC959 ではない。PUT/GET ヘッダ1行+生バイト列、1リクエスト=1TCP接続、host 側が能動的に close
  する設計 ── net_close() は host ソケットを実際には閉じないため)と guest 側 user/ftp.c(netcli.h 経由)を新規作成。
  Makefile に ftp を追加、ビルドは通る(1ブロック、4KB 未満)。
- 2026-09-17 04:45 AI(Claude): **未解決のまま時間切れで一旦区切り**。経緯:
  1. 当初 argv[0]/argv[1] で get/put とファイル名を受け取る版を書いたが、`ftp put <既存ファイル>` が
     dbg プリント1行も出さずに無応答になる事象を発見。
  2. `fopen(argv[0], ...)` (変数/非リテラル引数)が原因かと疑い、cp_dbg.c を一時的に書き換えて
     `fopen(argv[0],"r")` にしたところ**同じ症状が再現**(strcpy でローカルバッファへコピーしてから渡しても
     症状は変わらず)。一方 `fopen("HELLO.BIN","r")`(リテラル)は元から問題なく動く。この時点では
     「fopen に非リテラルを渡す」が真因と誤って結論した(cp_dbg.c は検証後 `git checkout` で元に戻し済み)。
  3. ftp.c をローカルファイル名固定(`FTP_FILE "FTPFILE"`、リテラルのみ)に書き換えたが**それでも無応答が再現**
     したため (2) の結論は誤りと判明。
  4. さらに切り分けたところ、**`net &` を起動した「後」であれば、ftp とは無関係などんなコマンド
     (`echo bye` 単体など)を打っても低頻度に無応答になる**ことを発見。ただし同一条件で再テストすると
     3 回中 3 回とも再現しないケースもあり、**非決定的なタイミング依存(#51 と同系統のレース)である可能性が高い**。
  5. 最終的に host 側サーバを実際に動かして `net &` → `ftp`(dbg プリント付き最小版、user/ftp.c 現状)を
     複数回実行したところ、**"dbg1 start"→"dbg2 opened"→"dbg3 connected" までは安定して到達**するが、
     続くファイル送信ループ(fgetc + net_write を1ループ内で交互に呼ぶ)の途中で**"dbg4 sent" が出ないまま
     プロセスが静かに終了し、host 側には1バイトも届かない**。OPHALT 等の派手なクラッシュ表示は出ない
     (#51 のクラッシュとは別の、より静かな異常終了に見える)。
  - **現状のファイル構成**(次の担当者向け):
    - `python/ftp_srv.py` : host 側サーバ、単体では動作確認積み(nc 相当として使える)。
    - `user/ftp.c` : 現在は get/put 引数無し・"FTPFILE" 固定・dbg puts 付きの最小実験版(put 相当の送信のみ)。
      本来の get/put 切替・任意ファイル名版は git 履歴には残していない(このセッションでは削除済み)。
    - `python/_debug_ftpput.py` : 再現用ハーネス(現状は未完成のまま放置、要整理)。
  - **推測される次の一手**: fgetc()(DRIVER 経由の間接呼出、IY非保存の可能性)と net_write()(IY 相対の
    ローカルコード呼出)を同一ループ内で交互に呼ぶこと自体がレースを踏みやすくしている可能性がある。
    (a) ループの外で一旦ファイル全体を小さな RAM バッファへ読み切ってから、読み終わった後にまとめて
    net_write() する(fgetc と net_write の時間的な分離)、(b) net.c 側のポーリング頻度を落とす、
    (c) #51 と合わせて python/tz80.py での実トレースに委ねる、のいずれかを次回試すこと。
  - get(受信)は未着手のまま。
- 2026-09-17 09:15 AI(Claude): **#51 の根本修正(iy_reg_claude.py の JP_COND/CALL_COND バグ)後に再検証**。
  ftp.c の送信ループがサイレント終了していた症状は、まさにこの #51 と同一バグ(netcli.c の while ループの
  符号付き比較で踏む)が原因だった可能性が高い。修正後の再テストでは `ftp`(最小 dbg 版、引数なし固定
  "FTPFILE")が `dbg1 start`→`dbg2 opened`→`dbg3 connected` まで到達し、実際に送信を開始するところまで
  確認できた(以前は dbg1 すら出ずに無応答だった)。ただし送信の途中で host 側ソケットの genuine I/O エラー
  ("can't read client socket" → cpmsim 側が cpu_error=IOERROR で意図的に CPU 停止)に遭遇し、完走はしていない。
  これは #51 のようなメモリ破壊ではなく、host 側ソケットのライフサイクル管理(python/ftp_srv.py が処理後に
  能動的に close する設計と、iosim.c 側の再接続/エラー処理の間の未検証な組み合わせ)の問題と見られる。
  次の一手: python/ftp_srv.py 側で close 前に軽いディレイを置く、または iosim.c の netd1_in/netd1_out に
  read/write エラー時のリトライ猶予を足す、のいずれかを試すこと。「未完了、要デバッグ」から
  「送信は動くが host 切断タイミングでI/Oエラー停止、要デバッグ」に格下げ(改善はしている)。
  詳細は #54 も参照。
- 2026-09-17 17:45 AI(Claude): **完了。根本原因2つを特定・修正、put/get 双方向を実機で往復検証済み**。
  ユーザーから「安定するところまで直してコミット・クローズ」の指示を受けて再着手。
  1. **iosim.c の netd1_in/netd1_out が正常系の peer close まで致命的エラーにしていた**。
     `read(cs,&c,1)!=1`(EOF や read エラー)・`write(cs,...)!=1` を無条件に
     `cpu_error=IOERROR; cpu_state=STOPPED` で CPU 停止させていたが、これは peer が応答を送って
     接続を閉じた直後という**ごく普通のシナリオ**まで巻き込んでいた(`nets1_in()` の POLLHUP 検出は
     poll() の実装依存でタイミングによって後手に回ることがあり、その間隙で guest が readable と信じて
     読みに行くと EOF に当たる)。`atcli`/`extdial`(いずれも使い捨て検証用、削除済み)で実際に
     `example.com:80` へ繋いだ際にも同じエラーで cpmsim が停止するのを確認・再現した。
     **修正**: 両関数とも、致命的に扱うのをやめて `close(cs); cs=0; cs_port=0;` で通常の切断として
     処理するよう変更(`net_client_connect()` が接続失敗時に採っている「ログ警告のみで握り潰す」
     方針と揃えた)。また `write()` 側は peer close 後に書くと SIGPIPE でプロセスそのものが理由も
     分からず即死するリスクがあったため、`sim0.c` の `main()` 冒頭で `signal(SIGPIPE, SIG_IGN)` を
     追加(write() は EPIPE を返すだけになり、上記の graceful close 処理に任せられる)。
  2. **本命: `do_put()` が TX リングバッファの排出を待たずに `net_close()` していた**。`net.c` の
     常駐デーモンは TX リングを「1ループにつき1バイト」relay する設計(user/net.c 参照)。
     `net_write()` は「リングに積めたか」を返すだけで実際の送信完了を意味しない。`ftp put` が
     ファイル全体を書き終えた直後に `net_close()` すると、daemon が全バイトを吐き出し切る前に
     `NETCMD_CLOSE` を検知して即 `NETSTATE_IDLE` に戻ってしまい、**キューに積んだだけで実際には
     一度も送信されていない末尾データが失われる**。これが「`ftp put` は『N bytes』と成功報告するのに
     host 側(python/ftp_srv.py)には一切届かない」という症状の直接原因だった(host 側は単体テストでは
     正常動作を確認済みだったため、host 側ではなく guest 側 `ftp.c` のタイミング問題と判明)。
     **修正**: 送信ループの直後、`net_close()` を呼ぶ前に `getticks()` で約20 tick(≒0.2秒)待つ処理を
     追加。daemon に確実に複数回のスケジューラ周期を回してもらい、リングを完全に排出させてから
     close するようにした。
  3. **副次的な発見**: `python/ftp_srv.py` は完全同期(1接続ずつ、スレッド化なし)だったため、
     デバッグ中に「1リクエストが recv() でスタックすると以後の接続はTCPレベルではキューされる
     (guest 側の net_connect() は成功と誤認する)が実際には誰にも処理されない」という別の落とし穴
     を踏んだ。at_modem.py と同じスレッド化パターンに変更し、ソケットに15秒のタイムアウトも追加。
  4. **検証**: 実機 cpmsim で `net &` → `ftp put` → `rm` → `ftp get` → `cat` の一連を2回連続実行、
     両方とも host 側に正しいバイト数(16/16、23/23)で完全に届き、ローカルに書き戻した内容も
     元のファイルと一致することを確認。
  5. コミット・クローズ済み。get(受信)側は今回の修正では触っていないが、往復検証で問題なく
     動作していることを確認済み。
- 2026-09-18 00:10 AI(Claude): ユーザーから「本物のFTPプロトコルとは違うと分かる名前にしよう」との
  指摘を受けて `ftp` → `tzftp` に改名(`user/ftp.c`→`user/tzftp.c`、`python/ftp_srv.py`→
  `python/tzftp_srv.py`、既定ファイル名 `FTPFILE`→`TZFTPFILE`、既定ルートディレクトリ
  `ftp_root`→`tzftp_root`)。user/Makefile の COMMANDS も追随。上記履歴中の `ftp put`/`ftp get`
  等の記述は改名前の実行ログなのでそのまま残す(履歴は書き換えない方針)。
- 2026-09-18 00:20 me: `tzftp` 改名後、自分の手元でも動作確認できた。クローズとする。

## [#54] net.c の TX 中継が送信内容の後ろにゴミバイト列を追加送信する(#51 とは別の残存バグ)
- status: 完了
- category: バグ
- priority: MIDDLE
- assignee: AI
- creator: AI(Claude)
- created: 2026-09-17 09:20
- updater: AI
- updated: 2026-09-25 11:11

### 履歴
- 2026-09-17 09:20 AI(Claude): #51 修正後の実機検証中(python/_debug_nettest2.py 相当の拡張ハーネスで
  host 側が実際に recv ループする形にして確認)に発見。`nettest` が送る "hello from tizix\n"(17B)は
  host 側に正しく完全な形で届くが、**その直後に host 側が追加で以下のようなバイト列を受信し続ける**:
  ```
  b"hello from tizix\n\x00nettest: RX [\x00]\n\x00nettest: done\n\x00\xdd\xe5\xdd!\x00\x00\xdd9:i..."
  ```
  "nettest: RX [" や "nettest: done" といった**コンソール出力用の文字列リテラルの断片**が混ざっており、
  net.c の TX リングバッファ経由の中継(`NB(KW_NETTXBUF+t)`、固定カーネルアドレス 0x8D16 起点)が、
  本来 KW_NETTXH(送信すべきバイト数)で止まるはずが、何らかの理由で止まらず読み続けている疑い。
  netcli.c の net_write() は呼び出し時の len(17)分しか KW_NETTXH を進めないはずなので、KW_NETTXH が
  17 を超えて進む(=何か他のコードが KW_NETTXH/KW_NETTXT に書き込んでいる)可能性が高い。#51 修正で
  net.c/netcli.c 自体のコード生成は健全化しているはずなので、これは**別系統の原因**(カーネル側の
  他の処理が誤って同じアドレス帯に書き込んでいる、または host 側 iosim.c の別の未発見バグ)を疑うこと。
  crash はしない(#51 と違いシステムは無事)ので優先度は MIDDLE。次の一手:
  KW_NETTXH/KW_NETTXT(0x8C94/0x8C95)に mem watch を張って tz80.py で誰が想定外に書き込んでいるかを
  実トレースで特定するのが確実(#51 で確立した手法がそのまま使える)。
- 2026-09-17 13:05 AI(Claude): **根本原因を特定・修正・実機検証済み**。ユーザーの実機テストで
  「nettest は2回目でバグる」との指摘を受けて再着手。2つの独立した不具合が重なっていた:
  1. **nettest.c が net_close() を呼んでいなかった**ため、daemon 側の KW_NETSTATE が
     CONNECTED のまま戻らず、2回目の net_connect() が常に `connect failed` になっていた。
     nettest.c の末尾に `net_close();` を追加して解決(単純な追加漏れ)。
  2. **本命: [[iy-reg-claude-jpcond-bug]] の修正(seq_cond_indirect_jp)自体に新規バグがあった**。
     tz80.py で KW_NETTXH への書き込みを1件ずつロギングしたところ、`n`(送信済みバイト数)が
     `len`(=17)を超えても `while(n<len)` ループが一切終了せず、n=428 を超えても書き込みが続いて
     いることを確認。さらにフラグ単位で追跡すると、**`ret <cond>` が不成立だった場合の後始末に
     使っていた `pop af` が、スタックに積んだままの「未使用の実アドレス(ジャンプ先候補)」の
     下位ワードをそのまま AF レジスタへ pop してしまい、直前の signed 比較(`sub`/`sbc`)が
     セットした本物の判定フラグをゴミで上書きしていた**。この壊れたフラグが後続の第2段階の
     比較(`ret p`)にまで伝播し、毎回同じ側に倒れ続けてループが実質無限化していた。
     `netcli.c` の string 定数が rodata 上で連続配置されているため、over-run したコピーが
     "hello from tizix\n" の直後にある "nettest: RX [" 等の文字列リテラルをそのまま読み出し、
     ゴミとして host へ送っていた ── ユーザー報告の症状そのもの。
     **修正**: 後始末を `pop af` から `inc sp` × 2 に変更(Z80 の 16bit `INC rr` はレジスタ/
     フラグを一切変更しない。SDCC 自身も同じ目的でこのイディオムを使っている)。
  3. **ハマった点(ビルド)**: 修正後の再ビルドで `arch/z80pack/user/*.asm/*.rel/*.iy.asm` 等の
     中間ファイルだけを `rm` して `make` しても、**`nettest.bin` 自体が古いまま再生成されない**
     ことがあった(Make の依存関係が `.bin` 終端まで正しく辿れていない模様)。`nettest.bin` 等の
     最終 `.bin` も明示的に消してから `make` し直すことで解決。**中間ファイルの削除だけでは
     不十分**、この構成でコマンドを再ビルドする際は `.bin` も含めて消すこと。
  4. **検証**: tz80.py で修正前後を比較(修正前: n=428 でも終了せず、TXBUF に nettest 自身の
     文字列リテラルが混入 / 修正後: ちょうど n=17 で正しく終了)。実機 cpmsim で
     `net &`→`nettest`→`nettest` を実行、**両方とも `nettest: connecting...`→
     `connected (optimistic)`→`done` まで正常完走、host 側は両回とも
     `hello from tizix\n`(17バイトちょうど)のみを受信、ゴミなし**を確認。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。09-17 に修正し、実機で検証済み。

## [#55] カーネルビルドがヘッダの依存を追っていない(kmem.h を変えても .rel が再生成されない)
- status: 完了
- category: バグ(ビルド)
- priority: HIGH
- assignee: AI
- creator: AI(Claude)
- created: 2026-09-19 04:17
- updater: AI(Claude)
- updated: 2026-09-25 02:30

### 履歴
- 2026-09-19 04:17 AI(Claude): z80board 実機ブリングアップ中に発見。`src/kmem.h` の
  `IRQ_ON()`/`KYIELD()` を変更した後の `make ARCH=z80board boot.rom` で、
  `obj/builtin.rel` `dev.rel` `fatcmd.rel` `ff.rel` `ivthelpers.rel` が再コンパイルされず、
  **変更前のマクロ(z80board でも `IRQ_ON()` = `ei`)のまま ROM に入っていた**。
  `arch/common-sdcc.mk` のパターンルール `$(OBJDIR)/%.rel: $(SRCDIR)/%.c` は .c だけに
  依存しており、`#include` しているヘッダ(kmem.h / io.h / ff.h / kexec.h …)の変更を
  知らない。
  - **実害**: 割り込みを一切開けない前提(z80board)に対し、古い `ei` が残ったモジュールが
    混ざった ROM を焼いていた可能性がある。ROM の空き容量表示も実態と食い違った
    (古いオブジェクト混じりで「空き 39B」→ クリーンビルドすると 93B 超過)。
  - **当面の回避**: ヘッダを触ったら `make ARCH=<arch> clean` してからビルドする。
  - **対処案**: sdcc の `-MMD`(または `-M`)で .d を出して `-include $(OBJDIR)/*.d`。
    sdcc の依存出力がそのまま GNU make で読める形式か要確認。全アーキ共通の
    `arch/common-sdcc.mk` の変更なので z80pack(カーネル容量が逼迫)でも回帰確認すること。
  - 関連: #54 の「中間ファイルだけ消しても .bin が再生成されない」も同系統の依存漏れ
    (user 側)。まとめて見直すのがよい。
  - 補足: z80board 実機対応のコード/コミット内コメントで「#54」と書いた箇所は、
    task.md の #54(net.c TX ゴミ)とは無関係(番号の付け間違い)。z80board 実機
    ブリングアップ自体はタスク番号未採番。

- 2026-09-25 AI(Claude): **完了。** `arch/common-sdcc.mk` で `sdcc -MM` から
  `$(OBJDIR)/<name>.d`(`<rel> <d>: <c> <全ヘッダ>`)を作って `-include`。手書きの
  ヘッダ依存表は撤去(漏れていたのが本件)。.d 自身もヘッダに依存させてあるので
  #include を増やせば次回に追従する。アセンブラ(.s)は #include を使わないので対象外。
  z80pack / z80board とも `make clean` 後のフルビルドと回帰で確認。

## [#56] ビルトインを OS の心臓部だけにし、他は外部コマンドへ(uptime / tree / df)
- status: 完了
- category: 設計 / ROM 容量
- priority: MIDDLE
- assignee: AI
- creator: AI(Claude)
- created: 2026-09-19 05:00
- updater: AI(Claude)
- updated: 2026-09-25 02:30

### 履歴
- 2026-09-19 05:00 AI(Claude): z80board 実機でプロンプトと `ls` が動いた後、
  デバッグ構成(SDDBG=1)を外して機能を全部戻すと ROM(32KB)が約 805B 溢れた。
  **原因の内訳(クリーンビルドのモジュール別 _CODE+_HOME)**:
  ff.rel(FatFs)**20,484B = ROM の約 62%** / fatcmd 2,760 / vfs 1,484 /
  kexec 1,457 / pipe 1,358 / ivthelpers 945 / io 799 / sdcard 756 / dev 650 /
  crt0 464 / diskio 381 / builtin 316 / kernel 210 / init 136 / spi 110。
  ビルトイン自体は 1KB 程度で、主因は FatFs の大きさ。ffconf.h は既に
  LFN 無し・exFAT 無し・mkfs 無し・FS_TINY で、残りは SDCC のコード生成
  (32bit 演算、IY 予約、--sdcccall 0 のスタック渡し)による膨張。
  - **実施**: uptime / tree / df のビルトインを無効化(コードは残す):
    `src/builtin.c` の `BUILTIN_UPTIME_TREE` / `BUILTIN_DF`、実体側の
    `src/fatcmd.c` `FAT_DF`、`src/vfs.c` `VFS_DUMP`(tree の vfs_dump)を 0 に。
    呼び手が無くてもモジュール単位で ROM に残るので実体ごと無効化している。
    - z80board: SDDBG=0(デバッグログ無し、ps/kill あり)で **空き 103B**。
      Makefile の既定を SDDBG=0 に戻した。
    - z80pack: カーネル空きが約 32B → **932B**。cpmsim で ls / ps を確認。
    - 全アーキ共通の builtin.c なので、x86-ia16 / m68k-mega でも uptime/tree/df
      のビルトインは消えている(builtin_novfs.c は別で未変更)。
  - **残タスク**:
    1. /bin/uptime、/bin/tree、/bin/df を外部コマンドとして実装する。
       uptime は getticks(0x0044)か KW_EPOCH 系、tree は KW_VTREE を直接読む
       (メモリはフラット)、df は f_getfree をカーネルから露出する必要あり
       (drv_tbl か -g シンボル。[[commands-access-via-syscall]] の方針に従う)。
    2. さらに ROM を空ける案(z80board は空き 103B でまだ窮屈):
       - klog_write(約 670B)をカーネルから外し、カーネルは RAM リングに
         積むだけ・ファイルへの書き出しは rsyslog(ユーザー空間)が行う形へ。
         ディスクエラーの rsyslog 記録(ディスクドライバの中からは f_write
         できない)もこの仕組みで解決できる。
       - FatFs の内部 static 関数だけ `__sdcccall(1)`(レジスタ渡し)にする。
         外部コマンドが -g で直接呼ぶ公開 API(f_open 等)は sdcccall(0) の
         まま。ABI に触れるので慎重に。

- 2026-09-25 AI(Claude): **uptime / df を外部コマンド化。tree はしない(理由下記)。**
  - `/bin/uptime`(user/uptime.c、tzcc): getticks(0x0044)を TICK_HZ で秒に畳む。
    旧ビルトインと同じく 16bit tick なので 655 秒で一周する。
  - `/bin/df`(user/df.c、tzcc): カーネルに `kfs_df(sel)`(src/fatcmd.c、sel=0 総 KB /
    1 空き KB を long で返す)を足し drv_tbl[47] に載せた。外部コマンドは FatFs を
    直に叩かない方針([[commands-access-via-syscall]])どおり f_getfree はカーネル内。
    トランポリンは tzcc/tzcdf.s(使うコマンドだけ引くライブラリ。crt0_tizix に置くと
    全コマンドが 4B 太る)。long を戻り値で返す形にしたのは、tzcc の long 戻り値は
    実績がある(readdir_size)が long 配列への書き込み先を渡す形は未確認だったため。
  - **tree は外部化しない。** カーネルの VTREE を直接読むことになり「コマンドは syscall
    経由」の原則に反する。もともと「検証用・socket 段が入ったら不要」とされていた道具。
  - test_cmds_all.py を tzpaths 対応にして uptime / df の出力形を検査するようにした
    (以前は結果を見ておらず、しかも cpmsim 決め打ち。途中で走らせていた a / b は
    「kill されるまで出力し続ける」実演用で、後続のコマンドが届いていなかった)。

## [#57] ROM(カーネル)と SD(sh/DRIVER/コマンド)の版ずれを検出する
- status: 完了
- category: 安全策
- priority: MIDDLE
- assignee: AI
- creator: AI(Claude)
- created: 2026-09-19 11:56
- updater: AI(Claude)
- updated: 2026-09-25 02:30

### 履歴
- 2026-09-19 11:56 AI(Claude): z80board 実機で、新しい ROM(241e84f、spi.s 書き直し)
  だけを焼き SD を書き直さなかったところ、`ls` や `date` が**正しく動いた直後に
  `sh: ls: not found`** と出た。SD を同じビルドのイメージで書き直すと解消。
  - **原因**: sh.bin / DRIVER.BIN / 各コマンドは、ビルド時の `kernel.map` の番地へ
    `-g` で直接リンクされる(con_break、con_pending、kexec_argv、f_open …)。カーネル
    の変更で番地がずれると、古いバイナリは新しい ROM の中の別の関数の途中を呼び、
    無言でおかしな挙動になる。z80board ではリンク順(... spi sdcard ivthelpers -lio)
    の都合で、spi.s / sdcard.s を触るだけで io.lib の関数の番地がずれる。
    z80pack でもカーネルとディスクイメージが別に作られる限り同じ危険がある。
  - **対処案**: カーネルにビルド ID(例: kernel.map のハッシュや、ビルド時刻の
    16bit)を固定番地で持たせ、mkfatdisk / user ビルドが同じ ID を .BIN の先頭 32B
    ヘッダ(#38 の予約領域。'T','Z' マグの後ろに空きがある)に刻む。kexec が照合し、
    食い違えば起動時に「ROM と SD の版が違う」と警告する(実行は止めるか続けるか要相談)。
  - 当面の運用: **ROM と SD は必ずセットで焼く**。

- 2026-09-25 AI(Claude): **完了(警告のみ、止めない)。** 調べると SD 上でカーネルの
  番地に直接リンクしているのは **DRIVER.BIN だけ**(user/Makefile の FS_SYMS)。sh.bin や
  tzcc のコマンドは固定ベクタと drv_tbl しか使わない。よってビルド ID を刻む仕組みは
  作らず、kload_driver(src/kexec.c)がロード直後に drv_tbl[44] と自分の `con_break` の
  番地を比べる。con_break は **最後にリンクされる io.lib の中**なので、それより前が
  1 バイトでもずれれば食い違う。不一致なら `driver.bin != ROM` を出して続行
  (止めると SD を直す手段も無くなる。TK が「止めるべき」なら変える)。ROM +十数 B。

## [#58] rx(xmodem 受信)が z80board 実機で途中から進まなくなる
- status: 完了
- category: バグ
- priority: LOW
- assignee: AI
- creator: AI(Claude)
- created: 2026-09-19 11:56
- updater: AI(Claude)
- updated: 2026-09-25 21:00

### 履歴
- 2026-09-19 11:56 AI(Claude): ユーザー報告。rx(初期からある xmodem 受信)が実機で
  転送の途中から進まない(試験で /root/TMP.TXT を受信していた。その時点のカードの
  中身は ~/z80pack/sdcard_backup_20260919_1149.img に退避済み)。
  - **疑い**: z80board 実機はタイマ割り込みが無く(TMR_OUT 未結線)、`getticks()` は
    協調切り替え(KYIELD)1 回ごとに 1 tick 進むだけ。xmodem の「N 秒待って来なければ
    NAK」のタイムアウトが実時間ではなく**ループ回数**になり、途中で早くタイムアウト
    して再送要求を繰り返している可能性。drv_getc_timeout(user/driver.c)も
    getticks 基準。
  - タイマを配線すれば自然に直る見込み。先に確かめるなら、rx のタイムアウトを
    大きくして挙動が変わるかを見る。
  - 別件の可能性: 受信判定は 2026-09-19 に port 0x10 の RXF# 方式へ直した
    (e0f710c)。それ以前は port 0x00 を読んで 1 バイト消費していたので、それが
    原因だった可能性もある。直後の版で再試験すること。
- 2026-09-25 21:00 AI(Claude): **完了(49649eb)。z80boardsim で再現して直した。** 新しい python/test_rx.py(ホストが
  XMODEM-CRC の送信側になり 12 ブロックを送って cat で突き合わせる)で、**修正前(事実)**: z80board は
  1 ブロック目から NAK が続いて進まない(実機の症状と同じ)。z80pack は「成功」と出るのに中身が化けていた
  (先頭ブロックが消え 38 行、末尾に 0x1A)。
  **原因(コードから)**: (1) パケット受信中を di/ei で囲んでいた。z80board は受信バイトを ISR がリング
  (KW_RXBUF)へ積む方式(#59 以降)なので、割込み禁止の間は kbhit() が true にならない。上で疑っていた
  「タイマ無しでタイムアウトがループ回数になる」は、#59/#71 の後は当たらない。(2) 保留中の直前ブロックを、
  次のパケットを同じ buf へ読み込んだ **後** に書いていた。**修正**: di/ei を撤去し、保留分は次の
  パケットを読む前に書き出す。**修正後**: z80pack / z80board とも PASS、回帰に追加。
  z80board(sim)は 12 ブロックで 29.7 秒と遅い(z80pack は 0.4 秒)。sim の受信割込みの頻度による
  可能性が高いが未確認。**実機での確認は未実施**(TK の手元で `rx` を試してほしい)。

## [#60] m68k-mega を実機(生 MC68000 + Mega2560 バスホスト)で起動させる
- status: 完了
- category: 実機ブリングアップ
- priority: HIGH
- assignee: AI
- creator: AI(Claude)
- created: 2026-09-20 21:00
- updater: AI
- updated: 2026-09-26 12:15

### 履歴
- 2026-09-20 AI(Claude): SRAM 基板の調査完了(接触不良 3 件、回路は無罪)を受けて
  実機ブリングアップに着手。**M1 = SD カード無しでシェルまで**を目標に設定
  (SD モジュールは未接続)。結果: **実機の 68000 で tizix が起動し、
  `tizix` バナー → `[/root]#` プロンプト → pwd / ps / cd が動作**。
  FAT は `FAT Drive FAILED (continuing)` で正しく劣化する。

  **ソフト側の変更(このリポジトリ)**: `arch/m68k-mega/crt0.s` のみ。
  - 例外ベクタ 2-11 / 24-29 / 31 を個別スタブに張り替え、共通の例外レポータで
    `*** EXC nn PC=xxxxxxxx HALT ***` を **UART MMIO へ直書き**して停止する。
    従来は全部 `default_vector` の `bra.s` 無限ループで、実機では
    「電源を入れても何も出ない」無言ハングにしかならなかった。
    C にもスタックにも依存しない作り(例外フレームから PC を拾った直後に
    専用スタック exc_stk へ切替、con_putc は使わない)。
  - 検証: m68ksim 上で `illegal` 命令 → `*** EXC 04 PC=000005C2 HALT ***` を確認。
  - **副産物: rocket68(m68ksim)は奇数番地 word アクセスのアドレスエラーを
    再現しない**(素通しして続行する)。m68ksim が隠していた整列バグは実機で
    初めて出る。整列まわりは実機で確かめること。

  **Mega 側ファーム**(リポジトリ外: `d:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST\`)
  を「TIZIX HOST」として書き直した。旧 BUSPROBE は `bk/busprobe_20260920.ino.bk` と
  `%USERPROFILE%\fw_busprobe.hex` に退避。新 HEX は `%USERPROFILE%\fw_tizixhost.hex`。
  シリアルクライアントは同フォルダの `tizix.ps1`(1Mbps)。

  踏んだバグ 5 件(すべて Mega 側。SRAM も 68000 も無罪だった):
  1. **SRAM 転送で /UDS,/LDS を駆動していない**。現デコードは CE# = A20|DS
     なので DS を出さないと SRAM が選択されない。旧走行版スケッチも
     ユーザーの tizix68k_memtest.ino も同じ穴。→ BUSPROBE の作法へ統一。
  2. **前のファームで走行中の 68000 を載せたまま Mega だけ再起動すると、
     クロックが止まった CPU がバスを掴んだままになり転送が全滅**
     (読み返しが全部 0000)。リセット中に 20ms だけクロックを与えて
     バスを手放させる `cpu_force_tristate()` を追加。
  3. **/AS を見た瞬間に /UDS,/LDS を読むと早すぎる**(68000 は /AS が S2、
     DS は S4)。全部「偶数(UDS)アクセス」に誤判定し UART の DATA と STATUS が
     入れ替わる。→ DS が出るまで待ってから判定。
  4. **/DTACK を「/AS が戻るまで保持」にすると次サイクルを巻き込む**。
     落とすのが AVR の数命令ぶん遅れ、68000 が残った /DTACK で 0 ウェイト完走
     してしまい Mega が観測できない。→ 固定幅パルス(8 nop = 500ns)へ。
  5. **逆に「/AS が H に戻るのを待つ」のも破綻する**。サイクル間で /AS が H に
     なるのは 1 クロック(250ns)だけで、AVR のポーリング周期(~190ns)では
     一度逃すと次サイクルを同一サイクルと誤認し、以後ずれ続ける自己維持ループに
     なる。実測で 1 バスサイクル 0.7ms(本来の 1000 倍)まで低下した。
     → 観測をやめ、DTACK 後は固定 2us 待ち(`cycle_settle()`)。
  6. **DTACK パルス中に AVR の割り込みが入るとパルスが伸び、次のサイクルまで
     完走させてしまう**。1 文字消える(`continuing` → `coninuing`)。
     → パルスを cli/sei で囲みアトミックにした。

  診断の作り(再発時はこれを使う):
  - リセット直後のバスサイクルを 64 本トレースして吐く(`TRACE_BOOT`)。
    アドレス・R/W・レーン・データバスの標本が出るので、逆アセンブルと
    突き合わせれば「CPU が本当に何を実行しているか」が観測できる。
    2 秒おきの再武装(`TRACE_REPEAT`)で暴走位置も追える。
  - 転送後に **バイトレーン単位の verify**(UDS のみ / LDS のみ)と
    バイト書込テストを行う。「ワードでは通るがバイトで壊れる」を切り分ける。
  - `/AS` が戻らない時のピン状態ダンプ(`report_stuck`)。

### 残タスク
- a. **SD リレー(M2)**。Mega 側 SPI + plat.h の SD_* プロトコル実装。
  現状は STATUS に常時 `ERROR|DATA_RDY` を返すスタブで、FatFs を無限待ちに
  させずマウント失敗させているだけ。これが入れば /bin の外部コマンドが動く。
- b. **速度**。1 バスサイクルあたり固定 2us 待ち + 代行のオーバヘッドで、
  実効は 68000 換算で 100-200kHz 程度。`cycle_settle()` を詰める、
  MMIO 以外は待ちを短くする、などの余地がある。
- c. タイマ割込みは Mega Timer5 の 1Hz(カーネルも TICK_HZ=1)。
  プリエンプションの粒度が 1 秒なので、SD が入ったら 100Hz 化を検討。
- d. Mega 側ファームは tizix リポジトリの外にあり、BUSPROBE のソースは
  git 管理外の bk/ にしかない。置き場所の方針を決める。

### [#60] M2 完了 — 実機で SD カードが動作(2026-09-21)
- status: M2 完了(SD カードまで)
- updated: 2026-09-21

8GB SDHC を SPI(SCK=D52 / MOSI=D51 / MISO=D50 / CS=D53)で Mega にぶら下げ、
**実機の 68000 から FAT が読み書きできるようになった**。

  SD init ... OK (SDHC/blk addr)  sec0: EB 3C 90 6D 6B 66 73 2E 66 61 74 00
    -> looks like a FAT VBR (correct: raw image)
  tizix
  FAT Drive DETECTED
  [/root]# ls /bin        → 81 ファイル
  [/root]# echo hello-68k > /root/test.txt
  [/root]# cat /root/test.txt
  hello-68k
  [/root]# wc /root/test.txt
  1 1 10
  [/root]# mkdir /root/sub / rm /root/test.txt / ls /root  → すべて正常

`ls` は外部コマンドなので、**kexec が SD から ls1.bin を読んで生の 68000 で実行している**。
FatFs → SD 中継 → SPI → カードの経路が端から端まで通ったことになる。

実装(Mega 側ファーム、リポジトリ外):
- CMD0 / CMD8 / ACMD41 / CMD58 / CMD16 の初期化、CCS ビットで SDHC(ブロック)と
  SDSC(バイト)のアドレス方式を自動判別
- CMD17 読み / CMD24 書き。**512B を Mega 側でバッファ**し 1 トランザクションを
  1 バスサイクルで完結させる
- カード未接続でも STATUS に `ERROR|DATA_RDY` を返す(無いと FatFs が無限待ち)
- 起動時 sd_probe() がカード種別とセクタ0 を出し、FAT VBR か MBR かを判定
- シリアル出力は ASCII のみ(日本語は端末で化けた)

カードの作り方: `obj/disk.img`(1.4MB FAT12)を **raw で sector 0 から書く**。
ffconf.h が FF_LBA64=0 なので GPT は読めない。SFD(raw)か MBR+FAT のみ。

### 残課題
- **e. 速度**。1 コマンド約 15 秒。バスサイクルあたり固定 2us 待ち
  (`cycle_settle()`)+ 代行オーバヘッドで、68000 換算 100-200kHz 相当。
  ここを詰めるのが次の一番効く改善。
- f. `df` が m68k 用にビルドされておらず `not found`(#56 で外部化した分)。
- g. タイマが 1Hz(Mega Timer5 / TICK_HZ=1)なのでプリエンプション粒度が 1 秒。
- h. **plat.h の LBA が 24bit = 8GB が上限**。8GB カード(15,601,664 セクタ)は
  ぎりぎり収まるが、32GB 以上は上位が落ちて誤ったセクタを読む。
- i. 次は ESP32(A21=1 / 0x300000 を予約済み)。SPI は SD と共有、CS は別ピン。
  ESP-AT の SPI には handshake 線が要る(D20/D21/D38 が空き)。

- 2026-09-21 AI(Claude): **高速化フェーズ完了(M2 の続き)**。バスサイクル 121,533 →
  318,000/秒(2.6 倍)、`ls /bin` 10.65 → 4.2 秒。内訳は (1) /DTACK を固定幅パルスから
  68000 本来のハンドシェイク(アサート → /AS の H を観測 → ネゲート)へ戻した、
  (2) Arduino の `serialEventRun()` を自前 `for(;;)` で排除(1.9us = 全体の 23%)、
  (3) `always_inline` で call/ret 0.6us を回収、(4) IACK 判定を A20=1 側へ移動。
  **引き継ぎ文書の「支配項は cycle_settle() の 2us」は誤りだった**(定数インライン展開で
  実測 0.8us)。**地雷 4/5「DTACK は固定パルス・/AS の H を待つな」は撤回**。/AS を
  125ns 周期(8 回展開のインラインアセンブラ)+ 割り込み禁止で見れば取りこぼしは 0 回/4 秒。
  全経緯は `D:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST\HANDOFF_speed.md` の 9 章。
  ファームは tizix リポジトリ外、カーネル側は 1 行も変更していない。
- 2026-09-25 15:20 AI(Claude): 棚卸し: M1/M2(実機で SD の FAT まで)完了、高速化も済み。残課題があるので対応中。
- 2026-09-26 12:15 AI(Claude): **クローズ**(TK「詰め込みすぎなので残タスクを分解して登録しなおして #60 はクローズ」)。
  目的(実機で起動)は達成: M1 / M2、2026-09-26 に外部 sh(#90)・ls・パイプ・背景ジョブを実機で確認(#93)。
  残課題の行き先: b / e 速度 → #94、c / g タイマ 1Hz → #85、f df → 済み(8cc8298)、
  h LBA 24bit → **対応不要**(TK。FatFs が触るのは SD 先頭の 32MB イメージ内だけ)、
  d ファームの置き場所 → #95、i ESP32 → #96。

## [#61] 外部コマンドを PIC 化し、m68k-mega の「再配置なし・固定スロット」を解消する
- status: 完了
- category: 設計
- priority: HIGH
- assignee: AI
- creator: TK
- created: 2026-09-21 03:13
- updater: AI
- updated: 2026-09-21 03:29
- 備考: #60 の派生。本筋は z80 側(動的ブロック割当)との合流。#47/#50 の固定リンク方針を畳む

### 履歴
- 2026-09-21 TK: 本来は再配置が大前提なので、それを最新 z80pack 側と合わせるのが
  優先タスク。いまのままでは 1MB のメモリを使い切れない。
  68000 なら最初からフルアドレスで PIC なバイナリが出せるはず。予約レジスタがあれば話は早い。

- 2026-09-21 AI(Claude): 現状整理。

  **問題。** m68k-mega の kexec は `PROC_BASE(n) = 0x8000 + (n-1)*32KB` の**リンク時固定**で、
  再配置機構を持たない(src/kexec.c の `#ifdef ARCH_M68K_MEGA` 側)。このため
  (a) スロットを上限の 7 枠まで開けても 0x8000-0x40000 = **256KB しか使えない**
  (SRAM 1MB の 75% が永久に遊休。現在は 3 枠なので実際は 128KB = 87% 遊休)、
  (b) コマンドごとに**スロット数ぶんの .bin を再リンク**する必要がある
  (21 コマンド x 3 スロット = 63 本。`ls1.bin`/`ls2.bin`/`ls3.bin`)、
  (c) `src/kexec.c` の `PROC_NSLOT` と `arch/m68k-mega/Makefile` の `SLOTS`、
  `SLOT_ADDR_n` を手で同期させる約束事が残る。

  **z80/x86 側はどうなっているか。** `BLKBASE(n) = 0x8000 + n*0x1000` の 4KB ブロックを
  **実行時に空き探索**して置く(連続 nblk 個、MAX_PROC_BLK=4)。番地が実行時に決まるので
  再配置が要るが、それを iy_reg(sdcc 出力を後処理して全アドレスを IY 相対化)で
  実現している。**つまり分岐点は kexec.c の `#ifdef` 1 本であり、そこを畳めば合流する。**

  **68000 での解。tzcc の 68k 版は不要。**
  Z80 が iy_reg → tzcc と進んだのは、Z80 に PC 相対も汎用ベースレジスタ相対も無く、
  自前の後処理に頼るしかなかったため。68000 は gcc の正規のコード生成パスで PIC が出る。
  - `-mpcrel` … GOT を作らず 68000 の PC 相対を直接使う(暗黙に `-fpic`)。変位 16bit =
    イメージ全体が 32KB 以内なら中のどこへでも届く。`IMG_BUDGET` は 24KB なので**制約にならない**。
    予約レジスタ不要。手数は最小。
  - `-msep-data` + **A5** … gcc の m68k は `PIC_REG = A5` で、**予約レジスタは A5 が標準**
    (`-ffixed-a5` で固定可)。コードは PC 相対、データは A5 ベース。
    **IY(z80) ≡ A5(68000) の対応**だが、後処理スクリプトではなくコンパイラが出す。

  **A5 を選ぶ理由(速度).** 68000 の PC 相対は**リード専用**(`move.l d0,(d16,PC)` は無い)ため、
  `-mpcrel` だとグローバル変数への書き込みが毎回 `lea (var,PC),a0` + `move.l d0,(a0)` の
  2 命令になる。A5 相対なら `move.l d0,(var-base,a5)` の 1 命令。
  加えてコードとデータを別番地に置けるので 32KB の縛りからも外れる。
  A7=SP、A6 は `-fomit-frame-pointer` で空いているので、A5 を潰しても A0-A4/A6 + D0-D7 が残る。

  **好都合な点。** 偽コンテキストは `CTX_SIZE 0x42` = `D0-D7/A0-A6`(60B) + SR + PC で、
  **A5 は既にコンテキストの一員**。kexec が A5 スロットにベースを書くだけでよく、
  「崩すと起動直後に Address Error」な crt0.s の `movem.l` の並びには触らずに済む。

  **触る場所。**
  1. `arch/m68k-mega/Makefile` … `UCFLAGS` に PIC フラグ、`SLOTS`/`SLOT_ADDR_n`/
     3 重リンクルールを削除(.bin が 63 本 → 21 本)
  2. `arch/m68k-mega/user/cmd.ld` … `.text PROC_BASE : AT(0)` の VMA を 0 へ、
     `--defsym PROC_BASE=` を撤去
  3. `arch/m68k-mega/user/crt0cmd.s` … 手書きなので PIC 化は手作業(小さい)
  4. `src/kexec.c` … `base` を動的割当へ。**ここで z80 側の `#else` と合流させる**
  5. `src/kernel.c` / `src/kmem.h` … スロット数は据え置きでよい(下記)

  **先に確かめること(未検証).**
  - `$(LIBGCC)` をリンクしている(Makefile:29、32bit 除算等)。この libgcc は PIC 
    ビルドではない。葉の算術ルーチンが中心なので踏まない公算が高いが、**実測が必要**。
    A5 案なら `-ffixed-a5` でビルドされていない libgcc が A5 を壊さないかも要確認。
  - `-mpcrel` / `-msep-data` が共有 src/ のコードを通るか(`-include include/plat.h` のシム込み)。
  - まずは `ls.c` を 1 本コンパイルして、絶対アドレス参照が残らないかを見るだけでよい。

  **天井は 2 つあって別物。**
  - 番地の天井(32KB 固定リンク → 256KB) … **本タスクで外れる**
  - **スロット数の天井 8** … `pid_tbl[8]`/`sp_tbl[8]`/`out_route[8]`/`blocked[8]`/
    `wakepend[8]` が u8[8] 固定(z80/x86/m68k 共通)。**PIC では外れない。**
    1MB を本当に使い切るなら、本タスクの後に「1 プロセスの枠を広げる」か
    「テーブルを広げる」かを別途判断する。

  **リスク。** [[loadtime-reloc-forbidden]] の地雷(ロード時にイメージを書き換える方式で
  起動不能になった)とは別物で、PIC はロード時に何も書き換えない。ただし `base` が動的に
  なる以上 kexec の中心は書き換わるため blast radius は小さくない。**着手は他の作業の
  区切りで、単独のブランチで。**


- 2026-09-24 AI(Claude): **完了。PIC 化してスロット別リンクを撤廃した。**

  **選んだ手。** #61 が挙げていた 2 案のうち **`-mpcrel`**(68000 の PC 相対)を採った。
  `-msep-data` + A5 は「書き込みが 1 命令で済む」利点があるが、実測すると
  `-msep-data` は `R_68K_GOT16O` を吐く ── GOT の用意と A5 の初期化が要る。
  `-mpcrel` は **絶対再配置が 1 個も残らない**(ls.c で `R_68K_32` x32 →
  `R_68K_PC16` x29)。像が 32KB 以内なら PC 相対変位 16bit で全域に届き、
  実際の最大は vi.bin の 7784B なので制約にならない。手数が最小で確実。

  **libgcc という伏兵。** コマンドを -mpcrel にしても像に絶対参照が 2 個残った。
  原因は **libgcc.a 自身が -mpcrel でビルドされていない**こと:
    `_umodsi3.o: jsr __udivsi3` / `jsr __mulsi3`、`_divsi3.o: jsr __udivsi3`
  いずれも `R_68K_32`。68000 には 32bit 乗除算命令が無いので C の `*` `/` `%` は
  必ずここを通り、PIC が破れる。必要なシンボルを実測したところ
  **`__udivsi3`(12) / `__umodsi3`(12) / `__mulsi3`(2)** の 3 つだけだったので、
  `arch/m68k-mega/user/ulibgcc.s` に PIC 実装を書き、**コマンドのリンクから
  `$(LIBGCC)` を外した**(符号付き `__divsi3`/`__modsi3` も先に用意)。
  以後 libgcc のシンボルを要求すると undefined reference で止まる ── 黙って
  PIC が破れるより望ましい失敗の仕方。

  **触ったもの。**
  - `arch/m68k-mega/Makefile` … UCFLAGS に `-mpcrel`。`SLOTS`/`SLOT_ADDR_n`/
    31 重リンクルールを削除して単一ルールへ。`ulibgcc.o` を追加、`$(LIBGCC)` を除去。
  - `arch/m68k-mega/user/cmd.ld` … VMA を `PROC_BASE` から **0** へ。
  - `arch/m68k-mega/user/crt0cmd.s` … `lea sym(%pc)` / `bsr.w` へ(手書きなので手作業)。
  - `arch/m68k-mega/user/ulibgcc.s` … 新規。
  - `src/kexec.c` … ファイル名へのスロット番号挿入(`ls1.bin`)を撤去。

  **検証。**
  - **VMA 0 と 0xF8000 でリンクしたバイナリが 27 コマンド全てバイト単位で完全一致**
    (= 真に位置独立。1 バイトでも違えばそこが絶対参照)。
  - m68ksim: `ls /bin` が 27 本(スロット接尾辞なし)、`date` → 1970-01-01 00:00:03、
    `du /bin` → 34380、`wc /etc/rc` → 2 7 44。**除算ルーチンの実地検証**になっている。
  - `a &` + `b &` で `ABABAB...` の交互出力。**同一 PIC バイナリがスロット 1(0x8000)と
    2(0xC000)で同時に走る**。

  **効果。** .bin が 651 本 → 27 本。Makefile と kexec.c の間でアドレスを同期させる
  約束事が消滅。スロットを増やす代償は KW_* テーブルの大きさだけになった。

  **残り。** スロット数の天井(#61 が「別物」と書いていた方)。現在 31 枠 =
  0x8000〜0x88000(544KB)。SRAM 1MB を使い切るには 62 枠 + KW_* を 63 エントリへ。

## [#62] arch/z80board: ESP-WROOM-02 を SPI bit-bang で繋いで WiFi を実装する
- status: 対応中
- category: タスク
- priority: HIGH
- assignee: AI
- creator: TK
- created: 2026-09-21 11:55
- updater: AI
- updated: 2026-09-26 00:30
- 備考: z80pack のネットワーク実装(net.bin + netcli)を実機へ。I/O 番号は暫定 0x81(回路の空きデコードへ後で振り直す)

### 履歴
- 2026-09-21 TK: arch/z80board に spi bitbang で ESP-WROOM-02 による wifi を実装する。
  arch/z80pack のネットワーク実装を応用して実機に対応させてくれ。SD カードの次の
  io 番号でよい。あとで回路の空いてるデコード番号に合わせるので先に実装から。

- 2026-09-21 AI(Claude): 実装 + シミュレータ通し検証まで完了(実機は未検証)。

  **方針。** z80pack で動いている一式(`net &` 常駐デーモン + 共有リング KW_NET* +
  netcli.h)の **device 層だけを差し替える**。telnet / tzftp / atcli / ntpdate /
  nettest は 1 行も変えずに実機で動く。
    z80pack : port 50/51 = cpmsim の client socket #1(ATD dial-on-demand)
    z80board: port 0x81 = bit-bang SPI ごしの ESP-WROOM-02 に ESP-AT を話す

  **追加/変更したもの。**
  - `arch/z80board/include/hw.h` … `ESP_PORT 0x81`(暫定)。ビット割り当ては SD と
    同じ bit7=MOSI / bit6=CS / bit5=SCK / IN bit7=MISO、**加えて bit0 = ~RST**。
  - `arch/z80board/espspi.s` … 2 本目の bit-bang SPI(手書き PIC。spi.s の
    spi_transfer を流用、約 73T/ビット)。esp_cs / esp_rst / esp_xfer。
  - `user/espat.h` … SPI フレーミングの取り決め(CMD+ADDR+[DUMMY]+32B、
    ステータス 4B にマジック 0x5A)。Z80・シミュレータ・ESP ファームの 3 者が共有。
  - `user/netesp.c` … ESP-AT を喋る常駐デーモン。**ビルド名は net.bin**
    (user/Makefile が ARCH=z80board のときだけ net.c の代わりにリンク)。
    AT+CIPSTART / AT+CIPSEND / `+IPD,<len>:` / CLOSED の状態機械。
  - `user/wifi.c` (z80board のみ) … AT コマンドを 1 行通す小物。
    `wifi <ssid> <pass>` = AT+CWJAP / `wifi` = AT+CIFSR / `wifi AT+GMR` = 生。
  - `src/kmem.h` … `NETCMD_ATCMD`(z80board 専用)。**TX リング(127B)を
    AT コマンド行として渡す**(KW_NETHOST の 40B では SSID+パスワードが入らない)。
  - `z80pack-tizix/z80boardsim/srcsim/iosim.c` … port 0x81 の SPI スレーブ模擬。
    受けたバイト列を TCP で python/at_modem.py へ中継するだけ(AT は解釈しない)。
    接続先は環境変数 TZESP_MODEM(既定 127.0.0.1:8080)。
  - `arch/z80board/esp/tzesp_at/tzesp_at.ino` … ESP 側ファーム(SPI スレーブ ⇔
    ESP-AT 互換の解釈 + 実 TCP)。**実機未検証**。
  - `python/test_esp_net.py` … 通し試験。

  **なぜ ESP 側にも自前ファームが要るか。** ESP8266(ESP-WROOM-02)の純正 ESP-AT は
  **UART 専用**で、SPI/SDIO の AT インタフェースは ESP32 系しか持たない。そこで
  ESP 側に「SPI スレーブ ⇔ ESP-AT 互換」のスケッチを置き、喋る言葉を純正 ESP-AT の
  サブセットに揃えた。結果 **python/at_modem.py がそのまま ESP の代役**になり、
  Z80 側のコードを実機を待たずに検証できる(atcli.c が「将来の ESP-AT ドライバの
  プロトタイプ」として残されていた狙いどおりの形)。

  **シミュレータでの結果(z80boardsim + at_modem.py + 待受 9100)。**
    1. `net &`        → "net: ESP link ok"(AT / ATE0 / AT+CIPMUX=0)     PASS
    2. `wifi AT+GMR`  → AT パススルーの応答を表示                        PASS
    3. `telnet 127.0.0.1 9100` → +IPD 受信(PONG-FROM-HOST)              PASS
    4. 同上で送信     → AT+CIPSEND で相手へ届く                          PASS
  (3 は初回 FAIL に見えたがテスト側の照合ミス。telnet はコマンド行に残った改行を
   入力として読み、相手へ送りつつローカルにもエコーするので受信文字列の途中に
   改行が 1 個挟まる。照合を改行無視にして解消。)

  **残タスク。**
  - **I/O デコード番号の確定。** 0x81 は「SD の次」という指示どおりの暫定値で、
    現物の 74HC138 は A4〜A6 だけを見るため **0x81 は 0x80(SD)と同じ選択線**に
    なる。空いている番号へ振り直すこと。変更は hw.h / espspi.s(ESP_IO_PORT)/
    iosim.c の 3 箇所(それぞれコメントで相互参照済み)。
  - **ESP8266 のブートストラップ。** HSPI スレーブの CS は GPIO15 固定で、GPIO15 は
    起動時 Low でないと ESP が起動しない。Z80 のラッチは CS=High で待機するため、
    **ラッチ bit0 を ESP の ~RST へ配線**して Z80 からリセットを握れるようにした
    (netesp.c の esp_boot が「CS Low → ~RST Low → High」で起こす)。未配線でも
    bit0 は誰も見ないだけで害は無いが、その場合 ESP の起動順は運任せになる。
  - **未確認(実機 ESP が無いため)**: SPI スレーブのコマンド値(0x01-0x04)と
    読み出し時の DUMMY 1 バイト。ESP8266 core(hspi_slave.c)の実装に合わせたもので、
    合わなければ espat.h / iosim.c / .ino を揃えて直す。status 4B のバイト順は
    Z80 側がマジックを先頭・末尾の両方で探して吸収する。
  - **net.bin が 6669B = 2 ブロック。** sh(3)+ net(2)+ 前景コマンド(1)= 6 で
    block2..7 を使い切る。パイプと併用する余裕は無い。縮めるなら classify() の
    strcmp 連鎖を畳む(iy_reg のグルーが効いている)か tzcc 移行。
  - 3.3V/5V のレベル変換(MOSI/SCK/CS/MISO)。

- 2026-09-21 AI(Claude): TK 指摘「実機でタイマは出ているぞ」。netesp.c と
  doc/readme.md の記述を訂正(「GP5 未配線がありえる」→「実機で出ているが
  TICK_HZ=100 と一致しておらず、かつ getticks() が KYIELD で自走する」)。
  待ちを「反復回数 と tick の両方」で見る作りはそのまま残す ── tick が
  100Hz より速い分、反復回数の下限が実時間の保険として効く。

- 2026-09-21 AI(Claude): コミット。tizix 側 `1a1717b`(実装一式)+ `240e9c6`
  (ssh ヘルパ)。**z80pack-tizix フォークも git init した**(それまで版管理外):
  `18b5e7c` = #62 直前の状態を import(既存の cpmsim ATD 拡張 / z80boardsim
  一式を含む)、`7517923` = port 0x81 の ESP スレーブ模擬。ビルド生成物と
  ディスクイメージ(90MB 超)は .gitignore で除外。
- 2026-09-26 00:30 AI(Claude): **bit-bang UART の device 層を実装(tizix ba531ae / z80pack-tizix 093bcaf)。**
  2026-09-23 の TK 設計・実配線どおり、SD と同じラッチ 0x80 に相乗り(OUT bit0=TX / bit1=CTS / bit4=~RST、
  IN bit0=RX)、9600bps 8N1 + CTS 半二重。ESP は純正 ESP-AT のまま(自作ファーム不要)。
  - arch/z80board/espuart.s: esp_tx(1 ビット 831T)、esp_rxbuf(CTS=0 でまとめ受信、サンプル間隔 834T、
    残り 2 バイトで CTS=1)、esp_rst。ビット列の間は di。
  - user/netesp.c: SPI のフレームを UART に置き換え(AT の状態機械は同じ)。起動時に
    `AT+UART_CUR=9600,8,1,0,2` で CTS フロー制御を有効化(保存しない)。
  - **実機にも効くバグを修正**: spi.s の spi_close が 0x40(bit0-4=0)を出していて、SD を閉じるたびに
    ESP をリセット(~RST=0)・TX にブレーク・CTS=0 にしていた → 0x5F。crt0 の先頭でもラッチをアイドル値
    0xDF にする(TK メモの「ブート早期にアイドル値」)。
  - z80boardsim: t_states(T ステート)で波形を読み書きする UART 模擬を追加。実機と同じ 833T/ビットで
    解釈するので、Z80 側のビット幅ループが 8MHz で合っていることの確認になる(水晶が 8MHz ちょうどが前提)。
  **検証(sim)**: test_esp_net 4/4 PASS(net & のリンク / wifi AT+GMR / telnet の +IPD 受信 / CIPSEND 送信)。
  z80board の SD 経路 11 本・z80pack の回帰 ALL PASS。z80board ROM 空き 35B。
  **未解決**: (1) 実機での確認(TK。net & → wifi AT+CIFSR → telnet)。(2) ESP のフロー制御は
  `AT+UART_CUR` で毎回有効にしている。恒久化するなら `AT+UART_DEF=9600,8,1,0,2`(CTS 配線済みなので可)。
  (3) SPI 版(espspi.s / espat.h / esp/tzesp_at)は使わなくなったが残してある。消すかは TK 判断。

## [#63] SD の 1 セクタ転送が割り込み禁止で囲まれていない(プリミティブ操作の未保証)
- status: 完了
- category: バグ
- priority: HIGH
- assignee: AI
- creator: TK
- created: 2026-09-21 13:10
- updater: AI
- updated: 2026-09-25 02:30
- 備考: #62(ESP を SPI 共有バスにする)の前提条件。現状は共有相手が居ないので顕在化していない

### 履歴
- 2026-09-21 TK: SD カードの読み書きで割り込み無効・再有効がされていないのは
  プリミティブ操作を保証していないバグとみなす。

- 2026-09-21 AI(Claude): 確認した事実 ── arch/z80board/diskio.c と sdcard.s に
  `di`/`ei` は **1 つも無い**。つまり 1 セクタ転送(CS Low → 512B → CS High)の
  途中でプリエンプトされうる。今は SPI バスに他のマスタ/スレーブが居らず、
  中断されても再開すれば続きが流れるだけなので顕在化していないが、不変条件
  (トランザクションはアトミック)としては破れている。#62 で ESP を同じ SPI
  バスにぶら下げると即座に実害になる(相手が CS Low のまま他方のクロックを
  食う / MISO で 2 デバイスが同時駆動)。

  **直し方は 2 通りで、代償が違う。**
  1. `disk_read`/`disk_write` を `IRQ_OFF()`/`IRQ_ON()` で囲む。ROM は
     `di`/`ei` 各 1B × 2 箇所 = **4 バイト**(z80board の ROM 残りは 7 バイト
     なので収まる)。代償は **1 セクタ = 約 28ms の割り込み禁止**
     (512B × 8bit × 約 55T の spi_rx512、8MHz)。GP5 の周期が ≒512µs なので
     tick が数十個飛び、時計が遅れる。FT245 は ~RXF がレベル割り込みなので
     受信は失われず遅れるだけ。
  2. ロックバイト(KW_* に 1B、RAM はタダ)+ スピン中 KYIELD + プロセス kill
     時の解放。割り込みを止めないので時計は狂わないが、カーネル ROM を数十
     バイト食う = **今は入らない**。#56(ビルトイン外部化)で空けるのが前工程。

  WiFi 側(net.bin)は 1 フレーム(32B ≒ 2.5ms)ごとに同じ扱いが要る。

- 2026-09-25 AI(Claude): **実施(方法 1 = di/ei)。** #71 で ROM が空いたので入った。
  `arch/z80board/sdcard.s` の rw_common(`_disk_raw_rw` / `_disk_raw_rw32` の共通部)で、
  SDSC のアドレス換算から 1 セクタ転送までを割り込み禁止にする。起動途中など呼び出し元が
  すでに di のことがあるので、無条件に ei せず `ld a,i` の P/V(IFF2)で元へ戻す
  (Z84C00 は CMOS なので NMOS の ld a,i の不具合は無い。z80pack のコアも IFF2 を P/V に
  正しく反映している)。BC:DE(アドレス)と HL を壊さないよう op と IFF はスタックで持つ。
  代償(1 セクタ ≒ 28ms 割り込み停止 → tick が遅れる)は元の記録どおり。
  **実機での確認はまだ。** z80boardsim では読み書きとも正常。

## [#64] /etc/rc 移植とメモリチェック機能の実装
- status: 完了
- priority: MIDDLE
- assignee: 
- creator: AI
- created: 2026-09-22 10:58
- updater: AI
- updated: 2026-09-25 02:30
- 備考: /etc/rc 部分は #67 で完了。**残件はメモリチェックと free のみ**

### 履歴
- 2026-09-22 10:58 AI: - [ ] /etc/rc の移植 (@AI) - [ ] 起動時のメモリチェック実装と呼び出し (@AI) - [ ] free コマンドの実装 (@AI)
- 2026-09-22 11:00 AI: /etc/rc の作成まで完了。次セッションで実装へ。
- 2026-09-24 AI(Claude): #67 と突き合わせ。`/etc/rc の移植` は #67 で全 4 アーキ
  完了(rc は sh 側 `sh_execute()` で実行)。起動時メモリチェックと free コマンドは
  未実装のまま(user/free.c 無し)。本課題はこの 2 点に絞って継続。

- 2026-09-25 AI(Claude): **完了(z80 のみ)。**
  - **起動時メモリチェック**: crt0.s(z80pack / z80board 同じ手順)で pid 表の初期化直後に
    プロセス枠 block2..7(0xA000-0xFFFF)へ 1 バイトずつ 0x55 / 0xAA を書いて読み戻す。
    食い違ったブロックは `PID_BAD`(0xFB、src/kmem.h)にして二度と使わない
    (kexec は非 0 を使用中とみなし、sched_pick は PID_BAD を飛ばすよう両 crt0 に追加)。
    実機 8MHz で約 0.2 秒。アドレス線の短絡までは見ない。ROM はアセンブラで ~45B。
  - **`/bin/free`**(user/free.c、tzcc): pid 表を peek で読んで
    `used N (KB) free N (KB) largest free run N (KB)` とブロックのマップ
    (. 空き / # プロセス / p パイプ / x 不良)を出す。**「largest free run」が
    `no free block` になるかを決める数**(kexec は連続した空きにしか載せない)。
    sh.c も同じ表を直接読んでいる前例に倣った。
  - 副産物: メモリチェックを一時的に外した z80boardsim の起動で、DRIVER 内
    (0x96EA)の Op-code trap を 1 回見た。RAM の初期値に依存する別の潜在バグの
    可能性がある(未調査)。

## [#65] /etc/rc 実行機能実装(→ #67 に集約)
- status: 完了
- category: タスク
- priority: MIDDLE
- assignee: 
- creator: AI
- created: 2026-09-22 11:37
- updater: AI
- updated: 2026-09-25 15:20

### 履歴
- 2026-09-22 11:37 AI: /etc/rc 実行機能の実装 src/sh.c のメインループを分離し、init.c が /etc/rc を読み込んでシェルロジックへコマンドラインを渡せるようにする。
- 2026-09-22 11:42 AI: sh_execute 関数の骨格を定義。メインループの抽出作業を開始。
- 2026-09-22 14:42 AI: init.c で /etc/rc を読み込み実行するロジックを実装。LINE_MAX の定義を io.h に移動して共有。ssh 接続コマンドを確認済み。
- 2026-09-24 AI(Claude): 重複クローズ。init.c から rc を読む方式は採られず、
  rc 実行は src/sh.c の `sh_execute()`(起動時に 1 度だけ `/etc/rc` を流す)に
  入った。#67 で「klog_write は init()(ROM)、rc はシェル」と役割を確定し、
  4 アーキで検証済み。現行 src/init.c に rc の処理は無い。
- 2026-09-25 15:20 AI(Claude): 棚卸し: 重複クローズ(#67 に集約)。13:22 の UI 保存で規約外の値「重複クローズ(#67 に集約)」が未着手に正規化されたので、規約内の値で戻す。

## [#66] arch/m68k-mega: スロット別リンクをやめ、プリエンプティブ + ベースレジスタ方式へ改修する(→ #61 と重複。#61 に集約)
- status: 完了
- category: 改善
- priority: HIGH
- assignee: AI
- creator: TK
- created: 2026-09-23 09:30
- updater: AI
- updated: 2026-09-25 15:20
- 備考: z80pack では既に廃止済みの方式が m68k-mega に残っている。スロット数を増やすほどビルド時間とディスク消費が線形に増える

### 履歴
- 2026-09-23 TK: マルチタスクの仕組みが、ずいぶん前に z80pack で廃止した
  「スロット分コマンドを作る方式」になっている。z80pack と同じく
  プリエンプティブ(独自 PIC)に改修して、ベースレジスタ方式にすること。

- 2026-09-23 AI(Claude): 現状の確認。**src/kexec.c の `PROC_BASE(n)` に
  固定リンクした .bin をそのまま読み込むだけ(reloc 無し)** という作りで、
  arch/m68k-mega/Makefile が `SLOTS` の数だけ同じコマンドを別アドレスへ
  リンクし直し、`ls1.bin` `ls2.bin` … として全部ディスクに焼いている
  (`USERBINS = $(foreach c,$(TZPORT_CMDS),$(foreach s,$(SLOTS),...))`)。

  **何が問題か。**
  - **ビルド時間とディスク消費がスロット数に比例する。** コマンド 25 本 ×
    31 スロット = 775 個のバイナリ。スロットを増やすたびに再リンクが増える。
  - **Makefile と kexec.c の二重管理。** `SLOTS` / `SLOT_ADDR_n` と
    `PROC_NSLOT` / `PROC_BASE(n)` がずれると「n 番目に空いたスロットへ
    ロードしたのに別スロット用にリンクされたバイナリを実行する」事故になる
    (kexec.c のコメントに明記されている既知の罠)。
  - **同時実行数がリンク時に固定される。** 実行時に決められない。

  **あるべき形(z80pack 側で既に廃止済みの理由)。** 1 本のバイナリを
  任意の番地へ置けるようにし、スロット数をリンクから切り離す。68000 には
  A レジスタ相対のアドレッシングがあるので、**ベースレジスタ(A5 等)に
  プロセス基底を入れて全アクセスを相対化**すれば reloc 無しで再配置できる。
  あわせて**独自 PIC によるプリエンプティブ切り替え**へ移行する。

  **着手前に確認すべきこと(未調査)。**
  - z80pack 側がどう廃止したかの一次資料を読むこと。ここに書いた
    「ベースレジスタ方式」は TK の指示を書き取ったもので、実装は未確認。
  - m68k-elf-gcc でベースレジスタ相対を強制する手段(`-mpcrel` /
    `-fbaserel` 相当)が 68000 で使えるか。使えなければ crt0 と
    リンカスクリプト側の工夫が要る。
  - **[[loadtime-reloc-forbidden]] と同じ地雷**: kexec を触る変更は
    blast radius が大きい。起動不能にした前例があるので最後に回す。
  - 割り込みは現状 `TICK_HZ=1`(arch/m68k-mega/Makefile)。独自 PIC を
    入れるならここも変わる。**[[m68k-callee-saved-abi-trap]]** に注意。

  **関連する現状の制約(#47 系)。** `PROC_SIZE` = 16KB / `IMG_BUDGET` = 12KB /
  `PROC_NSLOT` = 31(0x8000〜0x88000 = 544KB)。実機 SRAM は 1MB あるので
  62 スロットまで伸ばせるが、それは `kwork` のテーブルを 63 エントリへ
  採り直す作業で、**この課題が済めばスロット数の上限という概念自体が消える**。
- 2026-09-25 15:20 AI(Claude): 棚卸し: 重複クローズ(#61 に集約)。13:22 の UI 保存で規約外の値が未着手に正規化されたので、規約内の値で戻す。

## [#67] /etc/rc と klog_write(rsyslog)を全 ARCH で揃える
- status: 完了
- category: 改善
- priority: HIGH
- assignee: AI
- creator: TK
- created: 2026-09-24 07:00
- updater: AI
- updated: 2026-09-24 08:00
- 備考: #61 と同じ日に実施。z80pack だけが正しく、他 3 アーキが取り残されていた

### 履歴
- 2026-09-24 TK: klog_write は rom 側が正解とする。z80pack/z80board もそこ直しておいて。
  arch/z80board も /etc/rc と klog_write、コマンド類が z80pack の世代まで移植できているか
  調べて、足りなければ実装してくれ。arch/x86-ia16 もあるので、最後に同じ対応をする。

- 2026-09-24 AI(Claude): **完了。4 アーキすべてで同じ骨格になった。**

  **方針(TK 判断)。** `klog_write` は**カーネル(ROM)側から呼ぶ**。シェルの
  外部化に巻き込むと「シェルが起動できない障害でログが残らない」ことになり、
  切り分けができなくなる。z80 の `init()` は元から正しかったので、x86/m68k を
  そちらへ合わせた。結果、全 ARCH で起動の骨格が同じ順序になる:

      init(): builtin_init(FAT mount + vfs_init) → バナー → klog_write → シェル
      （z80 はシェルが kexec_file("/bin/sh.bin")、x86/m68k は sh() 直呼び）

  `/etc/rc` の実行はシェルの仕事なので sh 側に残す。klog_write だけ init() へ
  動かすと FAT マウント前になるため、`builtin_init()` ごと sh() から持ち上げた。

  **見つかった欠落。どれも無言で失敗するので気づきにくい。**
  - **z80board と x86-ia16 のディスクイメージに `/etc` と `/var/log` が無い**
    (ルートに bin と root だけ)。rc は毎回空振りし、klog_write は
    「/var/log が無い」ので黙って何もしていなかった。z80pack の
    mkfatdisk.sh には元からあった。両者を z80pack と同じ形へ。
  - **klog_write が x86-ia16 だけ `#if !defined(ARCH_X86_IA16)` で除外**
    されていた。中身は FatFs(x86 も ff.o をリンク済み)と
    KW_EPOCH_SEC / KW_CURRENT しか使わずアーキ依存が無いので撤去。
  - **m68k の Makefile で `$(DISK_IMG)` が `etc/rc` に依存していなかった。**
    rc を書き換えてもイメージが焼き直されない ── 「rc に足したコマンドが
    実行されない」ように見えて中身が古いだけ、という追いにくい形で出る
    (実際に一度踏んだ)。
  - rsyslog が m68k / x86 に無かったので移植。**syscall 18 = klog** を両者へ
    追加(番号は m68k と x86 で揃えた)。x86 はセグメントがあるので
    `get_path()` で移送してから `klog_write()` へ渡す。

  **コマンド類の調査結果。**
  - **z80board は z80pack を上回っていた** ── 51 本すべて揃っており、加えて
    `wifi` がある。追加は不要だった。
  - m68k に足りないのは z80 固有の基盤/試験(blk/xblk/ovl*/spawn*/prx/ptx/rx/
    t2/wak/test1/driver/sh)と、ハードが無いネットワーク一式(net/telnet/
    tzftp/atcli/ntpdate/nettest)。`history` は sh 外部化が前提なので保留。
    実質の不足は `rsyslog` だけで、これは移植した。

  **検証。**
  - z80pack(cpmsim): 回帰なし。/var/log/message に "tizix boot"、`ls /` に
    bin/root/etc/var/dev、`ps` に外部 sh(block2)と net(block5、rc の `net &`)。
  - z80board(z80boardsim): "tizix boot"(init)+ "boot-ok"(rc の rsyslog)。
    rc の `net &` は **コメントアウト**してある ── ESP は配線を bit-bang UART へ
    変更中で、常駐させると未確定のハードを叩く。配線確定後に外すこと。
  - m68k-mega(m68ksim): 4 行の rc が全行実行され、"tizix boot" + "rc-ok"。
  - x86-ia16(qemu): ブートで "rc: started"、/var/log/message に 1 ブート
    あたり "tizix boot" + "rc-ok" が 1 組ずつ積まれる。

## [#68] 回帰スイートの test_vi / test_sh_hist が FAIL(今日以前から)
- status: 完了
- category: バグ
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-24 10:00
- updater: AI
- updated: 2026-09-25 02:30
- 備考: #67/#61 の作業中に発見。**今日の変更による回帰ではない**ことを確認済み

### 履歴
- 2026-09-24 AI(Claude): 切り分け結果を記録。

  `python/run_regress.sh --clean` で **test_vi と test_sh_hist が FAIL**。
  セッション開始時点の `3994cc8` の `src/` に戻して同条件(`net &` を外した
  状態)で回しても**完全に同じ 2 本が同じ内容で FAIL**するので、今日の
  #61/#67 の変更による回帰ではない。

  **test_vi = ブロック不足(物理的な制約)。** `sh: vi: no free block`。
      sh.bin  8593B + 0x140 → 3 枠(block2,3,4)
      vi.bin 11514B + 0x140 → 3 枠 + 追加ブロック 1 = 4 枠
      空き block5,6,7 = 3 枠 → 4 > 3
  z80 のプロセス枠は block2..7 の 6 個が上限(0x8000〜0xFFFF、64KB 空間の
  残り全部)。sh(3)+ vi(4)= 7 なので**同居できない**。
  打つ手は (a) sh を縮める (b) vi を縮める (c) バンク切替、のいずれか。
  [[vi-editor-design]] の「4 ブロック占有が残課題」と同じ話。

  **test_sh_hist = 履歴リングに期待した項目が入っていない。** ↑ で
  recall されるのがテストが打った `xA`..`xZ` ではなく、セットアップの
  `rm /root/history` になる(`↑ + BS + Z` の結果が `rm /root/historZ`)。
  つまり **recall 自体は動いていて、記録側が入っていない**。クリーン
  ディスクでも同じなので状態汚染ではない。[[sh-history-ring]](#45)の
  SHSTATE / リング書き込み側を追うこと。未調査。

- 2026-09-25 AI(Claude): **完了。両方ともテスト側の問題だった。**
  - **test_vi**: #69 で vi が 3 ブロックに入り解消。
  - **test_sh_hist**: sh は履歴を **実行の後** に書く(SD 書き込みで出力を待たせない、
    user/sh.c 706 行の意図的な設計)。テストは「書いてから実行」前提で、
    `rm /root/history` の行が最古として残ることを知らず、↑×20 で最古まで遡って Enter
    → **テスト自身が rm を実行して履歴を消し**、以降が連鎖的に FAIL していた。
    期待値を「実行後に記録」へ直し、最古(rm 行)に触る検査は ^C で捨てるようにした。
    z80pack / z80board とも PASS。
  - ついでに見つけた z80pack 側の原因: **FAT ディスクがほぼ満杯**だった。250KB に既定の
    2KB クラスタで mkfs していて、コマンド 70 本超で空き 20KB。test_vi の小さな
    ファイル十数個が書けずに空になり、undo 系が FAIL していた。
    mkfatdisk.sh を `-s 1`(512B クラスタ)に。

## [#69] vi をオーバーレイで 3 ブロックに収める
- status: 完了
- category: タスク / viシュリンク
- priority: HIGH
- assignee: AI
- creator: TK
- created: 2026-09-24 12:00
- updater: AI
- updated: 2026-09-25 15:20
- 備考: #36/#37 の続き。**2026-09-24 夜に達成: vi.bin 11514 → 7465B(像 2 + 追加 1 = 3 ブロック)、オーバーレイ 11 本、test_vi PASS**。結果は履歴末尾

### 履歴
- 2026-09-24 TK: 「オーバーレイをやってもらいたい。4 ブロックの消費は失敗してる、使えない」
  「オーバーレイはコマンドごとに差し替えるって意味」
  「i/a/o は可能な限り残したい(操作性に影響)」「初回で i を自動ロードしておいてほしい。
  i なら早いはまだツールとして成立する余地はある」「ngg/nyy/ndd は妥協できる」

- 2026-09-24 AI(Claude): **着手前の実測と設計。まだコードは書いていない。**

  **目的。** vi は像 3 枠 + 追加ブロック 1 = **4 枠**。sh が 3 枠なので
  `3 + 4 = 7 > 6` で **z80pack / z80board どちらでも起動できない**
  (`sh: vi: no free block`)。像を 2 枠に収めて合計 3 枠にする。

  **#37 が失敗した理由と、それが解消済みであること。**
  #37 は `:` コマンドを 1 本切り出して「core が 639B しか減らないのに
  オーバーレイが 2672B」で見送られた。真因は **オーバーレイから core の関数を
  呼ぶ経路が無かった**こと ── 切り出したコードが使うヘルパーを全部抱え込む。
  **2026-09-24 に `a43eb6c` で解消**(tzcc/ovlvec.s。固定番地サンク + 引数ブロック、
  置き場所はオーバーレイ領域の直下 OVLADDR-24)。python/test_ovl.py で
  「オーバーレイが core の prs/prnum を呼べた」を検証済み。
  **この機構があるので #37 の見積りはもう当てはまらない。**

  **実測した vi の関数別サイズ**(tzcc の map より。総コード 11095B + データ 387B):
  ```
  do_cmd  1666  main    1181  ex_line  837  do_ins  744
  status   463  del_line 442  do_undo  390  paste_line 372
  render   371  load_file 350 urecord  344  getkey  297
  save_file 265 ins_at   242  yank_line 220 render_l 217
  del_at   199  mvcur    196  line_at  196  move_line 189
  row_of_char 175 scroll 172  put_line 158  place    148
  line_tail 143 line_prev 126 line_head 104 line_next 92
  (上位 28 で 10299B。残り約 800B は小関数とライブラリ)
  ```

  **予算。**
  ```
  2 枠 8192 − 上端 0x140(320: argv+プール+偽コンテキスト+初期SP)
             − データ 387 − スタック 800(#37 で 290B は「全く足りない」と実測)
             − オーバーレイ領域(= 最大のオーバーレイ)
  ```

  **設計(TK 指示を反映)。**
  - **挿入モード(do_ins 744B)をオーバーレイにする。ただし起動時に先読みする。**
    さらに **1 本だけのキャッシュ**(「いま領域に何が載っているか」を 1 変数で
    覚える)を入れる。`i → 編集 → ESC → hjkl → i` の普通のリズムでは
    **一度も読み直さない**。yy/dd/p/u/: を挟んだときだけ入れ替わる。
  - オーバーレイ行き: 挿入モード / yy / dd / p / u / ex 実行 / save_file /
    load_file(起動時のみ)。**コマンドごとに 1 本**。
  - core に残す: main / do_cmd の受付骨格 / getkey / 描画一式 / 行プリミティブ /
    move_line / ins_at / del_at(共有ヘルパーとしてベクタ経由で呼ばれる)。
  ```
  core           5769 B
  領域            744 B  (最大 = 挿入モード)
  予算           5941 B
                 ─────
  余裕            172 B      ← 薄い
  + nGG/nyy/ndd を落とすと 100〜150B(10 倍の加算組み立てと while ループ)
                             → 余裕 300B 前後
  ```

  **★検証済みの否定的知見(同じ道を二度通らないこと)。**
  - **「分割すれば PIC グルーも減る」は誤り。** vi.c のコメントは
    「tzcc の switch は 1 ラベルにつき間接ジャンプ 15 命令」と書いているが、
    **作者は既に switch を if 連鎖へ畳んで潰してある**。実測 do_cmd 865 命令中
    `jp (hl)` は **0 個**、`jr` が 71 個。回収できる余分なグルーは無い。
    分割の効果は「移動した分だけ」。
  - **編集コマンドを全部諦めても 2 枠には入らない。** カーソル移動 + 挿入 +
    描画だけの core が 8229B(予算 5585B)。超過しているのは品揃えではなく
    **ディスパッチャ・挿入モード・描画・行プリミティブという土台**。
    → だから **do_ins をオーバーレイにするのが必須**。ここを常駐に戻すと
    約 430B 超過し、nGG/nyy/ndd を落としても閉じない。
  - **Z80 に相対 CALL は無い**(jr/djnz のみ ±128B)。オーバーレイ内の呼び出し
    箇所は全体に散るので、相対分岐だけで core へは届かない。
  - **tzcc は関数ポインタを生成できない**ので、親が実行時に `&helper` を表へ
    書き込む方式は採れない。リンカに解決させたサンクを置くのが実装可能な形。

  **進め方(余裕 300B で 11095B を再構成するので一気にやると必ず溢れる)。**
  1. **`do_cmd` の実測分解** ── どの if ブロックが何バイトか。骨格 300B の
     見積りを確定させる。**ここが 600B なら計画ごと見直し**(最初にやること)
  2. 挿入モードだけオーバーレイ化(起動時先読み + 1 本キャッシュ)。
     ここで 2 枠に入るか実測
  3. 入らなければ yy / dd / p を順にオーバーレイへ。**入った時点で止める**
  4. `nGG/nyy/ndd` は最後の調整弁

  **制約。** `user/vi.c` は **m68k-mega と共有**([[m68k-vi-port-technique]])。
  m68k にオーバーレイ機構は無いので、**分割後もm68k では全部を 1 本にリンク
  できる形**にすること(呼び出しをマクロで包み、z80 は callovl、m68k は直接
  call になるようにする)。ハードの都合を共有ソースへ漏らさない
  ([[design-principle-hw-isolation]])。

  **採らなかった安い道(捨てないこと)。** `sh` を **721B** 削れば
  `sh(2) + vi(4) = 6` で vi は 1 行も触らずに動く。sh.bin 8593B に対し
  2 枠の上限は 7872B。sh は今も sdcc + iy_reg で、グルーが 166 箇所・
  約 830B(sh.iymap: ld +IY 63 / jp 間接化 53 / call 間接化 50)あるので
  tzcc 移行で釣りが来る。**常駐が縮むので vi 以外の全作業でも 1 枠増える。**
  TK は「オーバーレイをやってもらいたい」と #69 を選択したが、
  **費用対効果では sh のほうが桁違いに安い**ことは記録しておく。

- 2026-09-24 TK 追記: 「オーバーレイは 40 個くらいになってもいいよ」

- 2026-09-24 AI(Claude): **40 個まで許容なら設計が変わる。i/a/o を常駐に
  戻せる可能性が出た。**

  オーバーレイ領域の大きさは **個数ではなく「最大のオーバーレイ」**で決まる。
  細かく刻めば領域が縮み、そのぶん core の予算が増える。
  ```
  領域 744B(挿入モードが最大)   → code 予算 5941B
  領域 300B(全部を 300B 以下に)  → code 予算 6385B   ★+444B
  ```
  一方 `i/a/o` を常駐に戻すと core は 5769 + 744 = **6513B**。
  予算 6385B に対し **128B 超過**まで詰まる。
  ここで TK が既に妥協を表明している **nGG/nyy/ndd(100〜150B)** を落とせば
  **ほぼ均衡**する。

  つまり選択肢が 2 つになった。**手順 2 でどちらを採るかを実測で決める。**
  ```
  案 A(領域 744): 挿入モードをオーバーレイ化 + 起動時先読み + 1 本キャッシュ
                  余裕 172B。i の初回は先読みで即座、普通のリズムで再読込ゼロ
  案 B(領域 300): i/a/o を常駐のまま、他を 40 本前後へ細かく刻む
                  nGG/nyy/ndd を落として概ね均衡。**操作性は案 A より確実**
  ```
  **案 B が成立するなら案 B。** 挿入モードが常駐なら、キャッシュの当たり外れを
  考えなくてよく、`dd` の直後の `i` も即座。ただし全オーバーレイを 300B 以下へ
  収める必要があり、`ex 実行`(約 600B)や `del_line`(442B)は
  さらに分割するか、その 2 本だけ領域を食う設計にする(その場合領域は
  最大値で決まるので案 A と同じになる ── **ex と del_line をどう刻むかが鍵**)。

  手順 1(do_cmd の実測分解)の結果を見てから A/B を決めること。

- 2026-09-24 夜 TK: 「追加ブロック側にスタックを置くならオーバーレイはつぶさない
  ように注意して配置が必要」

- 2026-09-24 夜 AI(Claude): **完了。案 B(挿入モード常駐)で 3 ブロックに入った。**
  ```
  vi.bin 11514 B(像 3 + 追加 1 = 4 枠)
      → 7465 B(像 2 + 追加 1 = 3 枠) + オーバーレイ 11 本(131〜665 B)
  領域 0x1D30..0x1FC9(最大 665 B、imgtop 0x2000 まで 55 B の余裕)
  sh(3) + vi(3) = 6 枠 → test_vi 全ケース PASS
  ```

  **★予算の前提が #69 の見積りと違っていた(ここが一番大きい)。**
  kexec は argv / 初期 SP を **プロセス全体の最上端 = 追加ブロック側** に置く
  (crt0_tizix: SP = base + nblk*0x1000 - 0x140、nblk は追加ブロック込み)。
  よって像側からスタック 800B を引く必要は無く、条件は
      (a) ファイル長 + 0x140 <= 8192(= 像 2 ブロック)  → core <= 7872 B
      (b) ___ovlbase + 最大オーバーレイ <= imgtop(0x2000)
  の 2 つだけ。[core 終端, imgtop) はファイルから読まれない空き RAM なので、
  そこをオーバーレイ領域にした。**スタックとの関係(TK 指摘)**: スタックは追加
  ブロックの上端から下へ伸び、途中に本文 2288B と undo 272B がある。領域に届くのは
  それを全部突き抜けたときだけ。(a)(b) は ovllink.py がビルド時に検査し、
  破ればビルドが落ちる(黙って重ならない)。

  **設計: C は割らない。後段で割る(TK の「コマンドごとに差し替え」を関数単位で)。**
  - `tzcc/ovlsplit.py`(新規): tzcc の出力 .s を関数単位に切り、`tzcc/ovl/vi.ovl`
    に書いた関数群を `.area _OVk` へ移す。群の先頭が入口。core から入口への
    call(tizix.c の IY グルー 16B)を `ld a,#k` + `___ovlcall` 経由(12B)に
    書き換える。**越境参照はビルドエラー**(core→オーバーレイ内ラベル /
    オーバーレイ→別オーバーレイ / オーバーレイ→core のコード内ローカルラベル)。
  - **単一モジュールのまま割る**ので、変数(var_*)・文字列・tzc_vb は全部 core の
    _DATA に残る → オーバーレイは core の大域変数も関数も **グルー無しで**直接触れる。
    a43eb6c の ovlvec サンク(固定番地 + 引数ブロック)は vi には不要だった。
    オーバーレイ専用の文字列リテラルだけはオーバーレイ側へ移す(core の _DATA を空ける)。
  - `tzcc/tzcovl.s`(新規ライブラリ): ___ovlcall。k が載っていなければ
    `/bin/vi<NN>.ovl` を領域へ fread し、領域の先頭へ **jp**(スタックは呼出側の
    まま)。**1 本キャッシュ**(同じ k が続けば読まない)。読めなければ "ovl?" で終了。
  - `tzcc/ovllink.py`(新規): 同じ .rel を area の番地だけ変えて N+2 回リンクし、
    core .bin と各 .ovl を切り出す。**core のバイト列が全リンクで同一か**を比較する
    (ずれたら「別の core 向けに貼られたオーバーレイ」になる)。
  - `make tizixovlcmd CMD=vi XBLK=1` / tizixcmds の `TIZIX_OVLCMDS ?= vi:1`。
    mkfatdisk.sh(z80pack / z80board)は `*.ovl` も /bin へ載せる。
  - **m68k は無改造でそのまま 1 本にリンクされる**(オーバーレイ化は z80 のビルドだけ)。

  **割り方(tzcc/ovl/vi.ovl)。常駐は 挿入モード(i/a/o/O)・移動(h/j/k/l/0/$)・
  x・描画・undo の記録(urecord)。それ以外を 11 本に刻んだ。**
  ```
   1 vi_init            530   起動時のみ(main から括り出し)
   2 load_file          352   起動時のみ
   3 ex_line            605   ':' の 1 行入力
   4 ex_run save_file   444   ':' の実行(ex_line から分けた)
   5 do_undo            390   u
   6 op_dd              665   dd / 3dd(del_line を取り込み)
   7 op_yy yank_line    288   yy
   8 paste_line         372   p
   9 go_line line_at    427   G / gg / 12G / 12gg
  10 mv_end             131   e
  11 cnt_digit          139   カウント接頭辞の数字
  ```
  C 側の変更は **関数の括り出しだけ**(vi_init / ex_run / op_dd / op_yy / go_line /
  mv_end / cnt_digit)。dd は yank の写しを自前で持つ(dd と yy は別オーバーレイで、
  オーバーレイどうしは呼び合えないため)。

  **core を詰めた副作用の改善(全コマンドに効く)。**
  - `peep.py` に **`&&` / `||` の 0/1 化を分岐へ畳む** collapse_andor を追加。
    vi で 19 箇所。collapse_bool と同じ定型パターンで、LaeN/LoeN への参照が全部
    `ld hl,#1` 直後の jr で、落ちてくる直前が `ld hl,#0` のときだけ畳む。
  - `_mul` / `_div` を crt0_tizix.s から **ライブラリ tzcmuldiv.s へ**(-74B/本)。
    crt0_tizix.rel は全コマンドが明示リンクするので、乗除算を書かないコマンドまで
    抱えていた。
  - `peek` / `poke` / `callovl` を tzcshare.s から **tzcpeek.s へ分離**(-57B)。
    sdld はモジュール単位で引くので、語の peekw/pokew しか使わない vi まで抱えていた。
  - vi の status() の msgid switch を if 連鎖へ(効果は 4B だけだった)。

  **検証。** `python/run_regress.sh --build`: test_sh_hist(#68、既知)以外
  **全 PASS**(test_vi / test_ovl / test_xblk / test_spawn / coreutils 一式)。
  m68k-mega はビルドが通ることまで確認(m68k の自動テストは無い)。

  **ついでに見つけた既存の穴(修正済み)。** a43eb6c で ovlmain に ovlvec サンクを
  足したが、**通常ビルド(tizixcmds)に OVLVEC が配線されていなかった**ため
  `make` 経由だと test_ovl が落ちる状態だった。`TIZIX_VCMDS ?= ovlmain:0x15E8` を追加。

  **残る制約。**
  - **net を常駐させると vi は起動できない**: sh(3) + net(1) + vi(3) = 7 > 6。
    いま z80pack の /etc/rc は `# net &` でコメントアウトされているので実害は無い。
    解くなら #69 冒頭に書いた「sh を 721B 削る」(sh(2) にする)が本筋。
  - 領域の余裕は 55B。**オーバーレイを太らせる変更は ovllink.py が落とす**ので
    黙って壊れはしないが、6(dd)と 3(ex_line)が上限に近い。
  - オーバーレイの読み込みは 1 回 fopen + fread。cpmsim では体感できないが、
    z80board の SD で u / dd / yy / p / : / G を交互に打つと毎回読み直しになる。
    **i / a / o / 移動 / x は常駐なので読まない**(TK 指示どおり)。

- 2026-09-25 TK: 「vi 自体のテストは合格」。クローズ。
- 2026-09-25 15:20 AI(Claude): 棚卸し: TK 合格済み。13:22 の UI 保存で規約外の値「完了(TK 合格 2026-09-25)」が未着手に正規化されたので戻す。

## [#70] tzcc に間接呼び出しの組み込みマクロを入れる(CALLI / FNADDR)
- status: 完了
- category: 改善 / viシュリンク
- priority: MIDDLE
- assignee: AI
- creator: TK
- created: 2026-09-24 12:30
- updater: AI
- updated: 2026-09-25 16:50
- 備考: #69 の余裕が足りないときの打ち手。単独でも価値あり。セルフホスト向けの本物の関数ポインタとは別物

### 履歴
- 2026-09-24 TK: 「tzcc に関数ポインタを実装するのは、簡略した方法なら難易度が
  下がらないか。ansi c の書き方は取らずに、独特の組み込みマクロにするとか」
  「tzcc はアセンブラコードを出すまでが役割なので、特定のマクロが呼ばれた場合、
  jp とスタック設定だけやれば実現できる」

- 2026-09-24 AI(Claude): **generator.c を読んで裏を取った。実装は小さい。**

  **要点。ANSI で重いのは宣言子(`int (*fp[3])(int,char*)`)の再帰パースと
  型検査であって、コード生成ではない。** tzcc は「アセンブラを吐くまで」が
  役割なので、特定のマクロを命令テンプレートへ写すだけなら型系に一切触らない。
  `readme.md`:285 が「関数ポインタ ← 次(セルフホスト向け)」としているのは
  **本物のほう**で、これとは別物。後で本物を入れる邪魔にもならない。

  **すでに半分できている。** 間接呼び出しの機構は `callovl`(tzcc/tzcshare.s)
  として動いている ── 2026-09-24 に #36 の実証で実際に core の関数を呼んだ。
  足りないのは「関数のアドレスを C から取る」ことと「引数を好きな数だけ渡す」こと。

  **実装箇所は generator.c:719-772 の `NODE_CALL` 一箇所。**
  ```c
  case NODE_CALL:
      /* 引数を逆順に評価して push          ← 既存 */
      fprintf(out, "    call _%s\n", node->value);   /* ← ここだけ差し替える */
      /* pop af × 積んだ数                  ← 既存 */
  ```
  `CALLI(addr, a, b, c)` のときは:
  ```
      args[1..] を逆順に push          … 既存のループをそのまま使う
      generate_asm(args[0])            … 呼び先を HL へ
      call ___sdcc_call_hl             … 差し替えるのはこの 1 行
      pop af × (arg_count - 1)         … 既存
  ```
  **★呼び先は引数の後に評価すること。** HL は引数評価にも使うので、先に
  HL へ入れると潰れる。
  **引数の積み方は tzcc 自身のループを流用する**ので、`callovl` の
  「引数 1 個」制限(asm 手書き側の都合)が消える。1 つの case の中で 15 行程度。

  **`FNADDR(name)` のほう。** `ld hl,#_name` を吐いて既存の IY 加算経路に
  乗せるだけ。**着手前に確認すべき唯一の点**は、tzcc が再配置シンボル参照へ
  IY を足している箇所(vi.s に `push iy` / `add hl,de` が出ているので必ずある)を
  **関数シンボルでも同じように通るか**。

  **★設計を縛る罠。`FNADDR` は「絶対番地」(IY 加算済み)を返す形にすること。**
  リンク時オフセットを返して変数へ代入させると、既知の穴
  ──「配列アドレスを変数へ代入すると +IY されない」([[process-split-shared-mem]])──
  を踏む。絶対番地なら `callovl` の契約(「addr は絶対番地」)にもそのまま合う。

  **手に入らないもの。** 型検査は無い(引数の個数・型を間違えても素通り)。
  組み込み前提なら許容範囲という判断。

  **これで何ができるか。**
  - **実行時ディスパッチ表**。vi の `do_cmd` が `if` 連鎖なのは
    「switch は 1 ラベル 24B」かつ「関数ポインタが無い」ため。キー → `FNADDR` の
    表にできれば骨格が縮む → **#69 の薄い余裕(172〜300B)に直接効く**
  - **固定番地のサンクが不要になる**。今日 `ovlvec.s` で手書きしたものを、
    親が `pokew(vec, FNADDR(helper))` で埋められる(TK の元の案「親が書き換える」)
  - オーバーレイから core の任意の関数を、引数の個数を気にせず呼べる

  **リスク。** tzcc は **coreutils 21 本すべてのコンパイラ**なので blast radius は
  小さくない。`CALLI`/`FNADDR` は既存の式に影響しない純粋な追加だが、
  `python/run_regress.sh --clean` を通してから。

  **依存関係。** #69 の前提条件では**ない**(固定番地方式で進められる)。
  **#69 の手順 1(do_cmd の実測分解)の結果、余裕が足りないと分かった時点で
  前倒しする**のが想定。

- 2026-09-24 夜 AI(Claude): **#69 は固定番地サンクも CALLI も使わずに達成した**
  (ovlsplit.py の「単一モジュールのまま area を割る」方式で、オーバーレイから
  core の関数・変数へ直接届くため)。本課題は #69 の打ち手としては不要になった。
  単独の価値(実行時ディスパッチ表で do_cmd の骨格を縮める等)は残るので未着手のまま置く。
- 2026-09-25 16:50 AI(Claude): **完了。** 上の案のとおり generator.c の NODE_CALL 1 箇所。
  - `FNADDR(name)` → `ld hl, #_name`(--tizix-user では tizix.c が自動で +IY → 絶対番地。確認済み)。
  - `CALLI(addr, a, b, ..)` → 引数を既存ループで積み、**その後で**呼び先を hl へ評価して
    `call ___sdcc_call_hl`。tizix.c は ___sdcc_call_hl を再配置せず素通しし、呼び出し後に IX を張り直す
    (既存の固定ベクタ call と同じ経路)。引数 1 個の評価は gen_arg_hl() に括り出して共用。
  - CP/M 用 crt0.s に `___sdcc_call_hl:: jp (hl)` を追加(tizix は 0x0050 のカーネルベクタ)。
  **検証(事実)**: 既存 33 コマンド(coreutils + vi / ovlmain / xblk)の .s を変更前後の tzcc で生成して
  **全一致**(str_ ラベルはノード番地由来で実行ごとに変わるので出現順に振り直して比較)。新規
  user/calli.c(0 / 1 / 3 引数、CALLI の入れ子、FNADDR の表で実行時ディスパッチ)が cpmsim で期待どおり
  (`python/test_calli.py` PASS、回帰に追加)。`run_regress.sh --clean` ALL PASS。使い方は tzcc/readme.md。
  **範囲外で気付いたこと(未修正)**: tzcc/test_all.sh は古く、`make crt0.rel` のルールが無くて冒頭で止まる
  (ビルド先が arch/z80pack/ へ移った後に追従していない)。しかも冒頭の `make clean` が tzcc の中間物を
  消す(次の make で再生成されるので実害は小さい)。

## [#71] z80board のカーネル ROM が溢れて `make ARCH=z80board` が通らない
- status: 完了
- category: バグ
- priority: HIGH
- assignee: 
- creator: AI
- created: 2026-09-24 19:00
- updater: AI
- updated: 2026-09-25 11:11
- 備考: #69 の検証中に発見。**HEAD(67dee5c)でも同じく落ちる = #69 の変更とは無関係**

### 履歴
- 2026-09-24 AI(Claude): `make ARCH=z80board` が boot.rom の生成で
  `error: size of the buffer is too small.`(makebin)で止まる。
  #69 の作業ツリーを使わず、`git worktree` で HEAD を別に展開して同じビルドを
  しても同じエラー。src/ は #69 で触っていない。
  #67 の時点では z80boardsim で検証済みだったので、その後の src/ 変更
  (候補: b3f7659 init への klog_write / builtin_init 集約、eaa3d67 kexec の
  ブロック数ループ化)で ROM 32KB を超えたと推測。**未確認**。
  ROM が作れないので z80board の user/ 再生成(tzcc-cmds)にも到達しない
  (arch/z80board/user/vi.bin は 07:42 の旧版 11514B のまま)。
  [[z80-kernel-size-ceiling]] / task.md 2775 行付近の「ROM が約 805B 溢れた」と同系統。

- 2026-09-24 AI(Claude): **原因を実測で確定。** 推測だった上の候補を置き換える。
  ```
  モジュール別 _CODE(z80pack → z80board)
    sdcard.rel     0 → 744   SD の SPI モードプロトコル(ソフト実装)
    spi.rel        0 → 173   bit-bang SPI
    crt0.rel     391 → 517   ROM 直置きのリセット/IM1/ソフトベクタ・RAM 初期化
    diskio.rel   315 → 389   FDC ポート叩き → SD 呼び出し
    io.rel       792 → 834   FT245 コンソール
    init.rel     135 → 138
    その他(ff/fatcmd/vfs/pipe/kexec/…)は同一
    合計       30637 → 31799  (+1162)
  置き場所の差  z80pack: 0x0120..0x7F00(NSEC=254)/ z80board: 0x0080..0x8000 → z80board が +416B 広い
  z80pack 空き 708B → z80board 708 + 416 - 1162 = -38B
  ```
  差の 8 割(917B)は **SD ドライバ(sdcard.s + spi.s)**。cpmsim の FDC は
  エミュレータ側がセクタ転送を丸ごとやるので diskio.c は OUT/IN を数回打つだけ
  だが、実機はコマンド送出・応答待ち・データトークン・CRC 読み捨てを
  ビットバンギングで全部ソフトでやる。**構造的な差で、どちらかが太ったのではない。**

  **溢れた引き金は eaa3d67(kexec のブロック数ループ化)の +45B。**
  過去リビジョンを同じフラグで単体コンパイルして比較(-DARCH_Z80BOARD):
      kexec.c  d888827: 1382 / b3f7659: 1382 / eaa3d67: 1427 / HEAD: 1427
      init / kernel / io / builtin は 4 リビジョンとも同一
  d888827(#59)時点の Makefile 記録は「空き 21B」。21 - 45 = -24 で、
  残り 14B は未計測のモジュール(ff/fatcmd/vfs/pipe/dev/diskio/crt0 のどれか)。
  z80pack は 708B 余っているので同じ +45B を吸収して気づかなかった。
  **共有 src/ を 1 バイト増やすたびに z80board だけが先に溢れる構造**なので、
  z80pack のビルドだけでは検出できない(#55 と同じく回帰に `make ARCH=z80board` の
  サイズ確認を入れるべき)。

- 2026-09-25 AI(Claude): **ROM を空けた + 回帰に z80board の ROM 検査を入れた。**
  TK「実機にしかない実装をしたんだから増えるよね」── 差の大きさは構造上当然で、
  問題は「共有 src/ の変更に一番先に溢れる z80board を普段のビルドで見ていない」こと。
  - `arch/z80board/sdcard.s` の重複を畳んだ(**カードへ流れるバイト列は 1 バイトも
    変えていない**)。sd_ffn / sd_cmd_r1 に集約、各コマンドの全レジスタ push/pop・
    no-op デバッグ呼び出しを撤去。sdcard 744 → 453B、spi.s の未使用 spi_begin/end
    削除で 173 → 143B。ROM -38B → 空き 283B(その後の #57/#64/#63/#56 で 39B)。
  - `python/run_regress.sh` 末尾に **z80board ROM guard**(`make -C arch/z80board
    boot.rom`)。z80pack だけの回帰でも z80board の溢れで FAIL になる。
  - 畳む途中で気付いた旧来の食い違い(**挙動は変えていない**): wait_for_response_r1 は
    コメントでは「タイムアウトで 0xFF」だが、実コードは移植当初から **0x00(= R1 成功)**
    で戻っていた。直すと ACMD41 のリトライ回数と読み出しのタイムアウト経路が変わるので
    実機確認とセットで。

  **★z80board 実機で危険だった潜在バグを 2 件見つけて直した(z80boardsim で z80pack の
  回帰スイートを回したら出た。TIZIX_ARCH=z80board で tzpaths が z80boardsim を起動する)。**
  1. **`_disk_raw_rw`(/dev/fda・/dev/fdb の生ブロック)の引数オフセットが 1 バイト
     ずれていた。** sdcc は char 引数を 1 バイトで積む(obj/dev.asm の `push af / inc sp`
     で確認)のに「各引数 2B」と仮定していた。op は呼び出し元のゴミを読むので、
     **/dev を読むだけでゴミのバッファ(ROM)をゴミのセクタへ書き得た。** sim では
     sdcard.img の先頭が ROM のコードで上書きされ 1.97TB に膨れた。実機で
     `dd if=/dev/fdb` を打っていれば SD を壊していたはず。ついでに SDSC の ×512 も
     16bit 版に効くようにした(32bit 版と後半を共有)。
  2. **KYIELD が tick を進めていた。** kyield_entry が isr_timer と tick++ を共有して
     いた(タイマ未結線時代 #54 の名残)。#59 で実タイマが入った後も残り、getticks()
     (中で KYIELD)を回すだけで時間が進んでいた → `getc_timeout(10)` が 10 回
     ポーリングで抜けて **カーソルキー ESC [ x が 3 つの別キーに化ける**(vi / sh の
     ↑↓)、**sleep N がほぼ即座に終わる**。kyield_entry の合流先を isr_save へ(同サイズ)。
- 2026-09-25 11:11 AI(Claude): 棚卸しで完了へ。09-25 に ROM を空けて make ARCH=z80board が通るようになり、回帰に ROM guard も入れた。

## [#72] z80boardsim: `ptx 40 | prx` がときどき止まる / 連続実行中のリセット
- status: 完了
- category: バグ(調査)
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-25 02:40
- updater: AI
- updated: 2026-09-25 15:10
- 備考: #71 の検証で z80boardsim に z80pack の回帰スイートを初めて通して見えた

### 履歴
- 2026-09-25 AI(Claude): **事実(実測)**
  - `TIZIX_ARCH=z80board` で test_pwd_cd / test_sed / test_spawn / test_5b_pipe を
    3 回ずつ: **11/12 PASS**。落ちたのは test_5b_pipe の 1 回で、`ptx 5 | prx` は通り
    `ptx 40 | prx` が **出力ゼロのまま止まり、以後シェルも応答しない**。
  - 手で打つ `ptx 3 | prx` / `echo aaa | sed s/aaa/bbb/` は正常。
  - 23 時台の最初の z80board 実行(sdcard.s を畳んだだけの段階)では test_5b_pipe は
    PASS。**KYIELD が tick を進めなくなった修正(#71)の後から揺らぎ始めた**。
  - 全 13 本を続けて回した回では、途中で **z80board がリセット**(出力に BOOTING NOW)
    して pwd/sed/spawn が落ちた回があった。単独 12 回の計測では 1 回も起きていない。
  - メモリチェック(#64)を一時的に外した起動では DRIVER 内 0x96EA で Op-code trap。
  **推測(未確認)**
  - パイプの待ち/起床(proc_block / proc_wake / KW_WAKEPEND)が、以前は KYIELD の
    tick++ で「たまたま」回っていた箇所を持っている可能性。
  - 連続実行でのリセットは、前のテストの sim がまだ死にきらないうちに次を起動して
    sdcard.img を 2 プロセスで触っている、というハーネス側の可能性(未確認)。
  **次の一手**: tz80 相当の観測(ptx 40 | prx が止まった瞬間の pid_tbl / KW_BLOCKED /
  KW_WAKEPEND / kpipe ヘッダを z80boardsim から読む)。回避策で塞がず観測から。
- 2026-09-25 14:40 AI(Claude): #75 のトレースを入れて観測。**事実**:
  - 今のビルド(86f1dff 以降)で z80boardsim(トレース有効)10 起動 × `ptx 40 | prx` 15 回 = 150 回、
    1 度も止まらず、リセットも無し。
  - 調べる途中で **別の実バグを 2 件** 見つけて直した:
    1. **z80board crt0 が SP を張る前に push/call していた**(86f1dff、#75 の記録参照)。sim は起動時
       レジスタが乱数なので回ごとに違う番地へ push していた。元の「たまに止まる / リセット」との
       因果は**未確認**(sim のメモリはゼロ初期化で、push 8B が KW 域に当たる確率は小さい)。
    2. **park 中のプロセスを kill / Ctrl+C すると blocked[n]=1 が残る**(d380900)。kill / Ctrl+C は
       pid_tbl を 0 にするだけ。次にそのブロックへ載った単体コマンドが sched_pick に永久に飛ばされる。
       `sleep 3 | prx` → Ctrl+C → `echo` で**修正前は z80pack / z80board とも echo が止まる**(FAIL)、
       修正後 PASS。kexec がブロックを渡す前に落とす。回帰に test_pipe_kill を追加。
       次もパイプの reader なら writer の最初の proc_wake が偶然落とすので見えにくかった。
  - 起動時の初期化は問題なし(両 crt0 が 0x851A..0x853D の 36B をクリアしている。一度「初期化
    されていない」と書きかけたのは grep の見落としで誤り)。
  - 未解決のまま: 元の症状が上の 2 件で説明できるかは未確認。z80board で回帰を 2 周連続 +
    test_5b_pipe 10 回を流して見る(結果は次の行)。
- 2026-09-25 15:10 AI(Claude): **完了扱い(元の症状は再現せず)。事実**: 上の 2 件を直した状態で
  `TIZIX_ARCH=z80board` の回帰 15 本を **2 周連続 30/30 PASS**(リセット無し)、test_5b_pipe 単独
  **10/10 PASS**、トレース有効の `ptx 40 | prx` 150/150。元の「ときどき止まる / 連続実行中のリセット」
  がこの 2 件のどちらで起きていたかは**未確認**。再発したら `TZSIM_TRACE=1` で起動しておき、止まった
  ところで `kill -USR1` を送って pid_tbl / blocked / wakepend / kpipe を見る(#75)。

## [#73] リリース版ツリーに configure を同梱し、依存をコマンドラインで解決する
- status: 完了
- category: 配布 / 初見の人の躓き対策
- priority: MIDDLE
- assignee: AI
- creator: TK
- created: 2026-09-25 03:30
- updater: AI
- updated: 2026-09-25 21:30
- 備考: 2026-09-13 の「autoconf 移行は見送り」を、リリース版に限って方針変更([[root-makefile-dispatcher]])。**TK: 着手は全体に安定感が出てから(2026-09-25)**。**配布の主軸は #74 Docker に決定(同日)。本件は Docker を使わない人向けの補助**

### 履歴
- 2026-09-25 TK: 「リリース版のツリーでは autoconf を入れてコマンドラインで解決するように
  したい。sdcc 同梱はデカくなりすぎるのと、Makefile をいじらせるのは利用者のメンタルを削る」。
  背景: SDCC は版ごとの非後方互換で立ち上げ時にかなり苦しんだ([[sdcc-long-term-reliability]])。
  初めて触る人が依存性で躓く不親切さを排除したい。

- 2026-09-25 AI(Claude): 案(未着手・TK 確認待ち)。
  - configure.ac はリポジトリに置き、**生成した configure はリリース tarball にだけ入れる**
    (利用者は autoconf 不要)。
  - configure の出力は **config.mk 1 本だけ**。各 Makefile は `-include config.mk` + `?=` の
    既定値で読む → 開発ツリー(config.mk 無し)の `make ARCH=…` は今までどおり。Makefile 本体を
    生成物にしない。
  - 検査: sdcc(`--with-sdcc=PATH`、**4.5.0 以外なら止めて入手先と z80 ポートだけの
    ビルド手順を表示**。同梱しない)、sdasz80/sdldz80/makebin、gcc(tzcc)、python3、mtools、
    mkfs.fat、`--with-z80pack=DIR`(cpmsim / z80boardsim。無ければ「ビルドは可、run/回帰は不可」
    と表示して続行)、`--with-arch=`(既定 z80pack)と各アーキのクロスツール。
  - **足りないものは名前と入れ方を 1 行ずつ全部並べてから終了**(1 個直すと次が出る、を避ける)。
  - 最初の一歩: tzcc/Makefile の `SDCC_BIN = ~/z80pack/sdcc/4.5.0/bin` 直書き
    (TK の環境以外では最初の make で落ちる)を config.mk 経由へ。

- 2026-09-25 21:30 AI(Claude): 実装。上の案どおり(autoconf 2.69、rocky9 に既存)。
  - `configure.ac` + `config.mk.in` をリポジトリに置く。生成物(configure / config.mk / config.log /
    config.status)は .gitignore。**configure をリリース版ツリーに入れる作業は #22 の梱包で**。
  - config.mk はツールの bin ディレクトリを `TZ_EXTRA_PATH` にまとめて `export PATH` するだけ。ルート
    Makefile が `-include config.mk`、run_regress.sh も読む(arch/ と tzcc を直に make するため)。
    config.mk が無い開発ツリーは今までどおり。Makefile 本体は生成しない。
  - 検査: sdcc(**4.5.0 以外は止める**、入手先と z80 だけのビルド例を表示)/ sdasz80・sdldz80・sdar・makebin /
    gcc / python3(3.6 以上)/ mcopy・mmd・mdir / mkfs.fat。任意(警告して続行): m68k-elf-gcc、
    z80pack-tizix フォーク(既定 ../z80pack-tizix)、arch/ のシミュレータ、receive。
    足りないものは全部並べてから止める。
  - `make sims`: フォークから cpmsim / z80boardsim(と receive)を作って arch/ へ置く。
  - **検証中に見つけた罠**: cpmsim(z80pack 1.37)は起動時に補助ポート用の `receive` を ./receive か PATH
    から exec し、**無いとプロセスグループごと SIGQUIT で落ちる**。TK の環境では ~/bin/receive が
    あったので誰も気づいていなかった(PATH から ~/bin を外すと tzcc runtest が PASS=1 FAIL=44)。
    configure が srctools を PATH に足し、make sims が arch/z80pack/receive も置く。
  - 案にあった `--with-arch=` は入れなかった(アーキの選択は make ARCH= のまま。configure は全アーキ分を
    一度に見て、無いものは「そのアーキだけ作れない」と出す方が単純)。
  - 検証: `./configure` の後、`env PATH=/usr/bin:/bin:/usr/sbin sh python/run_regress.sh --build`
    (sdcc / m68k-elf / receive を PATH から外した状態)で 3 アーキ + tzcc runtest が ALL PASS。
    足りない場合の表示(sdcc 無し + m68k 無し + フォーク無し)も確認。

## [#74] Docker 版: `docker run` 1 回でビルドと回帰まで通る環境を用意する
- status: 保留
- category: 配布 / 初見の人の躓き対策
- priority: LOW
- assignee: AI
- creator: TK
- created: 2026-09-25 03:45
- updater: me
- updated: 2026-09-26 01:00
- 備考: **配布の主軸(TK 2026-09-25)**。#73 は補助。着手は全体に安定感が出てから

### 履歴
- 2026-09-25 TK: 「docker バージョンいいね。タスクに入れとこう」。

- 2026-09-25 AI(Claude): 案(未着手)。
  - Rocky Linux 9 ベースの Dockerfile(いまの開発環境 rocky9 と同じ系統にして差を減らす)。
  - イメージ内で SDCC 4.5.0 を **公式リリース tarball + sha256 で固定し、z80 ポートだけ**
    ビルドする([[sdcc-long-term-reliability]]。リポジトリには同梱しない)。
    z80pack-tizix フォーク(cpmsim / z80boardsim)も **固定コミット**からビルド。
    gcc(tzcc)、python3、mtools、dosfstools、m68k / ia16 のクロスツールも入れる。
  - 使い方の目標: `docker run --rm -v $PWD:/tizix tizix make ARCH=z80pack` と
    `… sh python/run_regress.sh --clean` がそのまま通る。
  - 実機へ焼く手順(SD への dd、ROM ライタ)とは相性が悪いので、そこはホスト側の手順として
    残す([[sd-card-write-procedure]])。Docker はビルドと sim での回帰まで。
  - #73 の configure が入れば、コンテナ内でも同じ configure を通す(二重管理にしない)。

- 2026-09-25 TK: 「Docker 配布を中心に進めるわ」。**配布の主軸は Docker**。#73(configure 同梱)は
  Docker を使わない人向けの補助に格下げ。コンテナ内でも configure を通す案は、#73 を実装する
  場合に限る(Docker 側が先行してよい)。
- 2026-09-25 19:10 AI(Claude): 着手を見送った理由(事実)。(1) rocky9 に docker / podman / buildah が無く、
  Dockerfile を書いても検証できない(入れるには sudo でのシステム変更が要る)。(2) イメージが取り込む
  z80pack-tizix フォークと tizix はどちらもリモートが無く、配布用ツリー(GitHub の別ツリー)への配置は
  TK の判断待ち。前提は整った: 2026-09-25 時点で z80pack / z80board / m68k-mega の回帰と
  アーキ横断テスト(test_ls_format / test_args)が全 PASS。
- 2026-09-25 19:20 TK: 「#74 は最後でよい」。優先度を LOW にして順番を最後へ。
- 2026-09-26 02:50 TK: rocky9 に Docker(実体は podman 5.8.2 の docker 互換)を入れた。Docker 操作と、必要なら sudo を許可。
- 2026-09-26 02:50 AI(Claude): 着手。docker/Dockerfile・docker/build.sh・docker/entrypoint.sh を作り、ビルド中。
  - 2 段ビルド(builder で作って runtime へ /opt だけ写す)。ベースは quay.io/rockylinux/rockylinux:9。
  - SDCC 4.5.0 は公式ソース(sha256 d5030437…)で z80 系だけ、m68k-elf は binutils 2.30 + gcc 8.3.0(sha256 固定、
    configure は開発機 ~/m68k と同じ)。フォークは build.sh が **コミット済み HEAD を git archive** で渡す(093bcaf)。
  - tizix のソースはイメージに入れず /tizix へマウント。entrypoint がシミュレータ・receive・rocket68 を
    ツリーに無ければ置く。既定 CMD は `sh python/run_regress.sh --build`。PATH に sdcc / m68k / srctools(receive)。
  - 途中で見つけた穴: **rocket68(m68ksim の CPU コア、github.com/habedi/rocket68、MIT)は tizix のリポジトリに無く**
    (.gitignore)、m68k の Makefile も librocket68.a を作るルールを持たずビルド済みの lib/ に頼っていた → ルールを追加。
    イメージには手元のコピーを入れる。**未解決(TK 判断、#22)**: 公開ツリーでは rocket68 を同梱するか、固定コミットで取るか。
  - **未解決**: 公開ツリーから使う人はフォーク(z80pack-tizix)も要る。フォークの公開先は #22 の判断待ち。
- 2026-09-26 07:10 AI(Claude): 完了。イメージ localhost/tizix(1.36GB)。新しく clone したツリーで
  `podman run --rm -v /tmp/tzc:/tizix:Z tizix`(= `run_regress.sh --build`)が z80pack / z80board / m68k-mega /
  tzcc runtest まで回る。落ちたのは test_ls_format の 1 件のみで、これは #92(Docker とは無関係の不安定な失敗)。
  イメージを作る途中で直したもの:
  - SDCC 4.5.0 をソースから作ると Rocky 9 の boost 1.75 を拒否される(SDCC bug #3772)→ 開発機と同じ公式バイナリ配布物
    (sha256 3395722e…、~/z80pack/sdcc の手元の物と一致)。
  - cpmsim は -static でリンクする → glibc-static(gcc のキャッシュを壊さないよう別の RUN)。
  - フォークに receive のビルド済みバイナリが **実行権なしで** コミットされていて make が作り直さない → 消してから作る。
  - run_regress.sh が「m68ksim が既にある」ことを条件に m68k 区間を**黙って飛ばしていた**(新しい clone で発覚)→ 作ってから
    回す・飛ばすときは理由を出す(z80board 区間も同様)。
  - entrypoint の `cp -a` で rocket68 がホストから消せない uid のまま残った → 所有者を引き継がない。
  rocky9 のストレージに中間イメージ(<none>、計 3GB 強)が残っている。gcc のビルド(1 CPU で約 1 時間)のキャッシュ
  なので消していない。空きが要るなら `podman image prune`。
- 2026-09-26 TK: 方針変更。**ベースは Ubuntu 24.04**、ビルドは **AWS EC2**(大阪、c7i.2xlarge、$0.4494/h、**最大 6 時間で
  必ず止めること**)、配布は **Docker Hub の tkdevelopeross/tizix**(概要欄は TK が作成済み、short description
  「tizix - a small preemptive multitasking OS for MMU-less CPUs」)。ECR は使わない。
- 2026-09-26 11:20 AI(Claude): 対応中に戻す(Docker は後回し、TK)。ここまで:
  - docker/Dockerfile を Ubuntu 24.04 版に(gcc 8.3.0 / binutils 2.30 は gcc-11 で作る。strip・SDCC の z80 以外を削る段込み)。
    docker/ec2-launch.sh(起動・鍵・SSH は起動元 IP だけ・6 時間の停止)。
  - EC2(大阪 c7i.2xlarge)で `docker build` 成功(9 分)。tizix:ubuntu24 = 638MB(圧縮後 167MB、Rocky 版は 1.36GB)。
    コンテナの全体回帰は z80pack 区間 19 本が FAIL なし(test_ls_format 含む)まで見て、途中で中断。
  - **push はまだ**。EC2 は **停止済み**(イメージはディスクに残る)。rocky9 の見張りも消した。
  - 6 時間停止の仕組みは **AWS 側ではなく**(1)インスタンス内の systemd タイマー(期限をディスクに記録、毎分確認して
    poweroff、shutdown-behavior=stop)と(2)起動元の見張り。OS が固まると(1)は効かず、rocky9 が落ちると(2)も効かない。
    確実にするなら CloudWatch アラームの停止アクション(IAM 権限の追加が要る)。
  - **再開時の注意**: 期限は初回起動基準なので、停止中のこのインスタンスを start すると 1 分ほどで電源が落ちる。
    再開は terminate → ec2-launch.sh で立て直す(ビルド 9 分)。push は `echo "$DOCKER_HUB_PAT" | ssh … docker login
    -u tkdevelopeross --password-stdin`(トークンはファイルに書かない)、タグ ubuntu24 / 日付 / latest、push 後 logout。
  - rocky9 には pkill / pgrep が無い(exit 127)。プロセスを止めるときは ps + kill。
- 2026-09-26 19:00 TK: 「残りトークンからみて、今回のリリースは見送る。TODO 行き・LOW」。保留・LOW に(EC2 は停止中、push はしていない)。

## [#75] z80boardsim にクラッシュ前後のトレースを吐かせる(#72 の観測手段)
- status: 完了
- category: 開発環境 / デバッグ
- priority: HIGH
- assignee: AI
- creator: TK
- created: 2026-09-25 04:00
- updater: AI
- updated: 2026-09-25 14:20
- 備考: 対象は z80pack-tizix フォーク(~/z80pack/z80pack-tizix/z80boardsim/srcsim)。#72 の前に入れる

### 履歴
- 2026-09-25 TK: 「z80board シミュレータのデバッグ機能が弱そうだな。クラッシュ時 前後のログを
  吐くようにしたいな」。

- 2026-09-25 AI(Claude): 案(未着手)。今夜の #72(`ptx 40 | prx` がたまに止まる / 連続実行中の
  リセット / 0x96EA の Op-code trap)は、事後の観測手段が無く推測を重ねる形になった。
  - **常時記録(軽く)**: 直近 N 命令(PC・命令バイト・主要レジスタ・SP・IFF)、直近 N 回の
    I/O アクセス(ポート・値・R/W)、割り込み受理(タイマ / ~RXF)をリングバッファへ。
    `TZSIM_TRACE=<N>` で有効化。
  - **書き出しのきっかけ**(`z80boardsim_trace.log`):
    1. Op-code trap
    2. 起動後に PC が 0x0000 へ来た(実機ならリセット相当)
    3. ROM 領域 0x0000-0x7FFF への書き込み(実機では効かないが sim は RAM 扱いで通す =
       実機との食い違いの源。検出するだけでも価値がある)
    4. SIGUSR1(止まっている最中に外から吐かせる)。pid 表 0x8400 / KW_BLOCKED /
       KW_WAKEPEND / kpipe ヘッダも一緒にダンプ
  - **「後」も取る**: 2・3 は致命的でないので、きっかけ後さらに M 命令記録してから書き出す
    (前後が 1 本のログに揃う)。
  - 関連: python/tz80.py(cpmsim 互換の 1 命令ステップ、[[tz80-debugger]])は z80board の
    I/O を持たないので、sim 側に入れるのが近道。
- 2026-09-25 14:20 AI(Claude): **完了。** z80pack-tizix `d5daf42` + tizix `86f1dff`(crt0 修正)。
  **使い方**: `TZSIM_TRACE=1 ./z80boardsim -x obj/kernel.ihx` → cwd に `z80boardsim_trace.log`(追記)。
  止まっている最中に `kill -USR1 <pid>` で即書き出し。`TZSIM_TRACE_AFTER=<N>`(既定 256)で
  非致命のきっかけの後に進める命令数。
  - 記録: 命令履歴 = z80core の his[](**HISIZE 100 → 4096**。コアが元々毎命令書いている)、
    I/O(in/out)+ タイマ割り込み要求のリング 1024。
  - きっかけ: Op-code trap / INT のバス異常 / I/O エラー / HALT(停止時)、起動後の PC=0x0000、
    ROM 領域 0x0000-0x7FFF への書き込み、SIGUSR1。PC=0 と ROM 書き込みは種類ごとに 4 回まで。
  - 出力: 理由、現在のレジスタ(IFF/IM 含む)、古い順の命令履歴(命令バイト + 逆アセンブル +
    AF..SP)、**I/O はそれを実行した命令行の直下に差し込む**、履歴より古い I/O、0x8300-0x85FF
    (cmdtbl / pid_tbl / KW_*)、スタック 32B。
  - 実装は z80board 側のファイルだけ(memory.h / iosim.c / simctl.c / disas.c / sim.h + tztrace.c)。
    z80core は cpmsim と共用なので触っていない。無効時の費用は memrdr/memwrt の分岐 1 個。
  **検証(事実)**: きっかけを必ず踏む合成 ihx(ROM 書込 → jp 0 → ED 00)で 3 種とも書き出し、
  履歴末尾が `JP Z,0020` → `0020 ED 00 ???` で止まることを確認。本物のカーネルで SIGUSR1 書き出し、
  I/O が `OUT (01),A` の直下に `OUT port 01 <- 20` と正しく付く。トレース無効で z80board の
  test_cmds_all / test_ls_format / test_vi / test_sh_hist PASS。
  **★初回の実走でさっそく実バグを 1 件検出**: z80board の crt0 が **SP を張る前に push / call**
  していた(ブリングアップのカウントダウン表示。`ld sp,#0x9000` は後ろの crt0_bringup_done に
  あった)。sim は SP を乱数で初期化するので、ある回は SP=0x0E70 で ROM 領域への push として出た。
  実機 Z80 はリセット後 SP=FFFF のことが多いので表面化していなかったと推測(未確認)。
  `ld sp` を `start:` の `di` 直後へ移した(バイト数不変、ROM free 34B のまま)。修正後、トレース
  有効で 8 回起動して 8 回ともきっかけ無し。回帰 ALL PASS。
  次: #72(`ptx 40 | prx` の停止)をこのトレースで観測する。

## [#76] ls の表示をアーキ間で揃え、ディレクトリの後ろに / を付ける
- status: 完了
- category: 改善 / 表示
- priority: MIDDLE
- assignee: AI
- creator: TK
- created: 2026-09-25 04:20
- updater: AI
- updated: 2026-09-25 13:50

### 履歴
- 2026-09-25 TK: 「ls コマンドの結果で、ディレクトリの後ろに / を入れる」を追加。
  「arch によって表示フォーマットがなんかズレてる」。

- 2026-09-25 AI(Claude): 着手前のメモ(未調査)。
  - ls は z80 系が tzcc の user/ls.c、m68k-mega は同じ user/ls.c を arch 側 stdio.h で
    ビルド、x86-ia16 も tzport 経由 ── 共有ソースのはずなのに表示がずれるなら、
    readdir の戻り値(0=end / 1=file / 2=dir)やサイズ(readdir_size)の実装が
    アーキ側で食い違っている可能性が先に疑わしい(推測)。
  - z80pack の ls は Step 10(3864b9e)で「dir を / 付きで表示」済みとメモリにある
    ([[vfs-unification-progress]])。**どのアーキのどの表示(ls / ls -l)で / が
    欠けているかを、4 アーキで同じディレクトリを ls して並べるところから始める。**
  - 回帰: test_vfs_step10 に「ls の出力形式」の検査があるので、アーキ間で同じ期待値を
    通せるようにする(TIZIX_ARCH で z80board は回せる)。
- 2026-09-25 13:10 AI(Claude): 着手。3 アーキで同じ ls を流して並べた(事実): z80pack と z80board は一致。
  m68k だけ (1) ディレクトリに / が付かず -l で <DIR> でなく 0、(2) ls /dev が cannot open、
  (3) /etc から ls .. で dev/ が出る(z80 は出ない)。原因: m68k/x86 は z80 の kdir_*(DEVFS 対応・
  種別を返す)を使わず sysfile.c で fsb_*dir を直に叩き、しかも readdir が種別を捨てて常に 1 を返す。
  ルートの dev/ は ls.c が引数文字列 "/" と一致したときだけ足していたので、.. で来ると z80 は漏れる。
  方針: kdir を全アーキ共通にして sysfile から呼ぶ。dev/ はカーネルの kdir がルート列挙の末尾で返す。
- 2026-09-25 13:50 AI(Claude): **完了(4d549e7)。**
  - `src/fatcmd.c`: kdir_open_abs / kdir_read / kdir_size / kdir_close を全アーキ共通に(cwd 解決つきの
    kdir_open だけ z80 専用。cwd をカーネルが持つのは z80 だけのため)。状態 struct kdir の置き場所だけ
    アーキ差(z80 は KW_LSDIR、他は static)。ルートの列挙は FAT が尽きたら vtree root(idx 0)の子
    = /dev へ続ける(ルート以外は開始位置を VTREE 末尾にして何も出さない。文字列も分岐も足さない形)。
  - `arch/m68k-mega` / `arch/x86-ia16` の sysfile.c: readdir 系 syscall(8/9/10/12)を fsb_*dir 直叩き
    から kdir_* へ。種別 2=dir を返す。/dev も開ける。cwd=/ のとき sh(src/sh.c)は引数を絶対化せず
    ls の既定 "." が素通しで来るので、"." は "/" として扱う(これで m68k の `cd /; ls` も直った)。
  - `user/ls.c`: 引数文字列が "/" のときだけ dev/ を足す処理を撤去(`ls ..` で漏れていた)。
  **検証(事実)**: 新規 `python/test_ls_format.py`(10 項目)が z80pack / z80board(z80boardsim)/
  m68k-mega(m68ksim)で全 PASS。run_regress.sh に追加して ALL PASS。z80board ROM の空き 52B → 34B。
  x86-ia16 はビルドのみ確認(実行は #77/#49 のため未確認)。
  **残ったアーキ差(意図どおり)**: ルート直下の並び順(m68k は root/ が先)はディスクイメージの
  作成順で、表示ロジックの差ではない。
  **範囲外で気付いたこと(未修正)**: src/sh.c のコメントに文字化け(U+FFFD)が 71 箇所ある(4a92976 より前から)。
  x86-ia16 の sysfile.c で klog_write が暗黙宣言の警告(既存)。fsbackend_fat.c の fsb_*dir は
  使う側がいなくなった(削除はしていない)。

## [#77] x86-ia16: 背景ジョブが走らない・date / vi が無い(リリース対象外)
- status: 保留
- category: バグ / 移植
- priority: LOW
- assignee: 
- creator: TK
- created: 2026-09-25 05:00
- updater: AI
- updated: 2026-09-25 15:20
- 備考: **TK 判断: x86-ia16 は「いろいろ変」なので後回し、当面リリースから外す**。既知の #49 も未解決

### 履歴
- 2026-09-25 TK: 実行ログ(抜粋)
  ```
  tizix stage2 / stage2: entering kernel / FAT Drive DETECTED / tizix / rc: started
  [/root]# ls            → HELLO.TXT HISTORY
  [/root]# cat /var/log/message → "tizix boot" / "rc-ok"
  [/root]# a &           → A を 1 文字出したきり
  [/root]# b &           → B を 1 文字出したきり
  [/root]# date          → sh: date: not found
  (^D)  shutdown         → その後に BABABABA… がまとめて出る
  ```
  「x86-ia16 はいろいろ変だな。後回しにして当面はリリースを見送る。vi もない」。

- 2026-09-25 AI(Claude): 事実と推測を分けて記録。
  **事実(ログから)**: 起動・/etc/rc・ls・cat・rsyslog は正常。背景ジョブ a / b は起動直後に
  1 文字出したきりで、**sh が終わった後にまとめて走る** = sh がプロンプトで入力を待つ間、
  背景プロセスに CPU が回っていない。date は /bin に無い。vi は x86 へ未移植。
  **推測(未確認)**: x86 の sh はカーネル内蔵(src/sh.c)で、入力待ちが BIOS 呼び出し等で
  割り込みを止めたまま回っている、または入力待ちループが他へ譲らない。#49(タイミング依存の
  コード破壊)と関係する可能性もある。
  **着手時の最初の一手**: 入力待ち(src/io.c の kgetchar / readline 経由)の間に PIT の
  tick が進んでいるか、sched が呼ばれているかを観測する(qemu の gdbstub か monitor で
  tick カウンタを覗く)。
- 2026-09-25 15:20 AI(Claude): 棚卸し: TK 判断「後回し・当面リリース対象外」。規約外の値「未着手(後回し)」が UI 保存で未着手になったので、規約内の「保留」にする。
- 2026-09-26 01:10 AI(Claude): 途中まで調べたところで TK「x86-ia16 は今は対応不要」。コードは変えていない。
  **事実(QEMU で再現)**: `a &` の後に sh がプロンプトで入力待ちの間、4 秒で A は 0 文字。キーを打つと
  その瞬間だけ 1 文字出る(`pAs`)。kgetchar の待ちループは毎周 cli → sti しており、x86 の KYIELD は空。
  **推測(未確認)**: sti の直後の 1 命令分しか割込みの窓が無く、QEMU(TCG)ではほぼ取られない。
  **再開時の案**: x86 の KYIELD を `sti; hlt` にする(proc_block の x86 版と同じ)。ただし kgetchar /
  getticks は int 0x80(IF=0 でアトミックな前提、専用の syscall スタック)の中からも呼ばれるので、
  そこで sti; hlt すると syscall の途中で _isr08 が切り替える。syscall スタックが 1 本なので要設計。

## [#78] m68k-mega の ps にコマンド名・引数を出し、z80 と同じ表示に揃える
- status: 完了
- category: 改善 / アーキ間の差
- priority: MIDDLE
- assignee: AI
- creator: TK
- created: 2026-09-25 05:15
- updater: AI
- updated: 2026-09-25 12:40
- 備考: #76(ls の表示のずれ)と同じ「アーキ間で表示が揃っていない」系

### 履歴
- 2026-09-25 TK: m68ksim のログ(抜粋)
  ```
  [/root]# a &   /  b &   → A B が交互に出る(背景ジョブは正常に走っている)
  [/root]# ps
  BLK ST
  0 run (sh)
  1 rdy
  2 rdy
  ```
  「m68k-mega ps コマンドが古いな。user 以下分離しちゃってる?」

- 2026-09-25 AI(Claude): **user/ の分離ではない。** 事実:
  - ps はカーネル内蔵(src/builtin.c cmd_ps)。`#if !defined(ARCH_X86_IA16) &&
    !defined(ARCH_M68K_MEGA)` で分岐していて、m68k / x86 は `BLK ST` だけの古い形が残っている。
  - CMD/ARGS 列の元は KW_CMDNAME / KW_CMDARGS(src/kmem.h、0x830B〜 の固定番地の表、
    block 0..7 の 8 枠)で、**書くのは z80 の kexec_argv だけ**。m68k の kexec は名前を
    記録していないので出せない(ソースのコメントにも「空欄になる」とある)。
  - 外部コマンドは m68k も共有 user/*.c を obj/tzport/ へ写してビルドしている(date.c だけ
    arch 側専用版)。ソースは z80 と同じ。
  **直し方の案**: (1) m68k の kexec で、起動したコマンド名と引数をスロットごとの表に記録する
  (m68k は M68K_NSLOT = 62 枠なので z80 の固定番地の表ではなく、カーネルの配列にする)。
  (2) cmd_ps の分岐をなくし、z80 と同じ `BLK ST CMD ARGS` に揃える(名前の取り出しだけ
  アーキ差として関数に閉じ込める。[[design-principle-hw-isolation]])。
  x86-ia16 は #77 で当面対象外なので後回し。

- 2026-09-25 12:40 AI(Claude): **完了(97ae6b6)。** 上の案 (1)(2) のとおり。
  - `src/kexec.c`: z80 の kexec_argv に埋まっていた名前/引数の書き込みを
    `ps_note()` に括り出し、z80 と m68k-mega の両方から呼ぶ(書き方は 1 本)。
  - `src/kmem.h`: m68k-mega の kwork に KW_CMDNAME/KW_CMDARGS を追加。表の大きさは
    M68K_NSLOT から導く。kwork の大きさは `KWORK_SIZE`(0x640)が唯一の定義で、
    溢れたら `src/kernel.c` の `kw_used_fits` でビルドが止まる。
  - `src/builtin.c`: cmd_ps の z80/m68k 分岐を撤去。違いは走査範囲(PS_FIRST: z80=2 /
    m68k=1)だけ。x86-ia16 は表が無いので旧 `BLK ST` のまま(#77)。
  **検証(事実)**:
  - m68ksim(新規 `python/test_ps_m68k.py`、7 項目 PASS):
    ```
    BLK ST CMD ARGS
    0 run (sh)
    1 rdy sleep 30
    2 rdy a
    ```
    kill 1 / kill 2 のあとの ps ではジョブ行が消える。
  - z80pack 回帰 `run_regress.sh` ALL PASS(13 本 + dd ホスト比較 + DRIVER + z80board ROM guard)。
  - サイズ: z80 は共通関数化で **13B 縮んだ**(z80board ROM の空き 39 → 52B、z80pack 731 → 744B)。
  **範囲外で気付いたこと(未修正)**: `python/test_5a_waitwake.py` が `"0   run (sh)"`
  (sh 内蔵時代の ps 形式)を期待していて 1 件 FAIL する。回帰スイートには入っていない。
  今回の変更前から z80 の ps 形式は同じなので、テスト側が古い。
  test_ps_m68k は m68k のビルドが要るので run_regress.sh には入れていない。

## [#79] test_5a_waitwake が古い ps 形式を期待して FAIL する
- status: 完了
- category: テスト
- priority: LOW
- assignee: AI
- creator: AI
- created: 2026-09-25 15:30
- updater: AI
- updated: 2026-09-25 15:30

### 履歴
- 2026-09-25 15:30 AI(Claude): #78 の作業中に発見(事実): `python/test_5a_waitwake.py` が「シェル健在」の
  判定に `"0   run (sh)"`(sh がカーネル内蔵だった頃の ps 形式)を探していて、今の ps
  (`0 rdy (sh)` / sh は外部化されて別ブロック)では常に 1 件 FAIL していた。回帰スイートには未登録。
  判定を「ps が `BLK ST CMD ARGS` を返してプロンプトへ戻った」に変更。z80pack / z80board とも PASS
  (test_a_bg / test_a_fg も両方 PASS を確認)。run_regress.sh に追加。

## [#80] m68ksim: 速く入力すると文字が入れ替わる・欠ける
- status: 完了
- category: バグ
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-25 17:10
- updater: AI
- updated: 2026-09-25 17:10

### 履歴
- 2026-09-25 17:10 AI(Claude): #48 の調査で 1 行ずつまとめて送ったら発覚。**事実**: m68ksim で
  `wc -l /etc/rc` が `/etc/r` として実行され、落ちた `c` が次の行の頭に付いて `cgrep: not found`。
  **原因(コードから)**: check_quit_key() はメインループと SIGALRM ハンドラ(30ms ごと、#47 で
  rocket68 の自己分岐ハング対策として入れたもの)の両方から呼ばれ、どちらもリングの tail を書く。
  メインが read() で 1 文字取ってから pending_push() するまでの間にハンドラが割り込むと、次の文字を
  先に積んで順序が入れ替わる。tail の同時書き込みで欠けることもある。
  **修正**: メインが入力を扱う間(stdin_fill / stdin_take_byte)はフラグを立て、ハンドラはその間
  何もしない(ハンドラの目的は「メインが戻ってこないときの Ctrl+]」なので失われない)。
  **検証**: 同じコマンド列を 3 回流して文字の欠け・入れ替わり 0。test_ps_m68k / test_ls_format(m68k)PASS。

## [#81] wc が -l を解さない / rm が 2 個目以降を消さない / m68k で wc -l の -l が絶対化される
- status: 完了
- category: バグ
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-25 18:00
- updater: AI
- updated: 2026-09-25 18:00

### 履歴
- 2026-09-25 18:00 AI(Claude): #48 の比較で発見(事実): 全アーキで `wc -l /etc/rc` が `wc: cannot open -l` を
  出す(wc.c がオプションを解析していない)。全アーキで `rm a b` の b が残る(rm.c が argv[0] だけ)。m68k
  では加えて src/sh.c resolve_arg が「パス 1 個」グループの引数全体を 1 本のパスとして絶対化し、`-l` が
  `/root/-l` になる。**修正(5cad686)**: wc.c に -l/-w/-c(連結可、ファイル無しなら stdin)、rm.c を複数
  ファイル対応、resolve_arg は先頭フラグをそのまま写して残りを 1 トークンずつ絶対化。test_args.py で
  3 アーキ PASS。

## [#82] m68k-mega にカーネルパイプが無い(`A | B` が out of memory)
- status: 完了
- category: 移植
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-25 18:00
- updater: AI
- updated: 2026-09-25 19:00

### 履歴
- 2026-09-25 18:00 AI(Claude): 事実: m68k-mega で `cat /etc/rc | wc -l` 等のパイプはすべて
  `sh: pipe: out of memory`。arch/m68k-mega/pipestub.c(x86 からの転用)の pipe_setup が常に 0 を返す
  スタブのまま(ソースのコメントにも「m68k-mega はまだカーネルパイプ非対応」)。z80 の src/pipe.c は
  4KB ブロックと RAM_BASE 前提なので、m68k のスロット配置へ載せ替えが要る。test_args.py の該当項目は
  m68k で SKIP にしてある。
- 2026-09-25 19:00 AI(Claude): **完了(5748d15 + 次のコミット)。** z80 と同じ src/pipe.c を m68k でもリンクした。
  - kmem.h(m68k): KW_PIPE を kwork に置く。PROC_BLOCK_MIN/MAX = 1..NSLOT-1、BLOCK_ADDR(n) = n×32KB
    (kexec の PROC_BASE と同じ)。pipe.c の「ブロック < 8」の決め打ちは PROC_BLOCK_MAX に置き換え
    (z80 では同じ値なので z80 のコードは 4B 縮んだだけ)。
  - m68k スケジューラが pid!=0 なら何でも走らせていたので、パイプのバッファ枠(PID_PIPEBUF)と
    proc_block 中の枠を飛ばすようにした(z80 の sched_pick と同じ規則)。proc_wake の上限も直した。
  - 観測で見つけた: m68k の getchar syscall が `(unsigned char)kgetchar()` で EOF(-1)を 255 に化かし、
    `echo hi | cat` が `hi` の後に 0xFF を延々と出していた。syscall とユーザー側の getchar を直した。
  - m68k の kexec で out_route を CONSOLE に戻す(前の占有者がパイプの writer だった枠の再利用対策)。
  **検証(事実)**: test_args.py(m68k)10/10(`cat /etc/rc | wc -l` / `echo hi | wc -l` / `cat | head -1`)、
  `echo hi | cat` → `hi` の後にプロンプトへ戻る。z80pack 回帰 ALL PASS、z80board test_args PASS、
  x86 ビルド OK。m68k はタイマが 1Hz(実機 Mega Timer5 と同じ)なので、reader と writer の切り替えは
  秒単位で遅い(動作は正しい)。

## [#83] m68k-mega のタイマが 1Hz 固定(パイプ・背景ジョブが秒単位で待つ)
- status: 完了
- category: 改善 / エミュレーション
- priority: MIDDLE
- assignee: AI
- creator: TK
- created: 2026-09-25 19:40
- updater: AI
- updated: 2026-09-25 20:10

### 履歴
- 2026-09-25 19:40 TK: 「m68k エミュレーションの 1Hz はおかしい。せめて 60Hz とかで」。
- 2026-09-25 20:10 AI(Claude): **完了(9454223)。** arch/m68k-mega/Makefile の `TICK_HZ ?= 100` を唯一の定義に
  して、カーネル / コマンド / m68ksim に同じ値を渡す。m68ksim は単調時計で 1/TICK_HZ ごとにレベル6を
  上げる。ユーザー側 getticks の倍率は 100 固定から 100/TICK_HZ に(1Hz 前提で 100 倍していた)。
  **途中で見つけた**: syscall 中は割込み禁止なので、受理前に次の周期が来ると上げ直しで 1 回分消え、
  `sleep 2` が 4.5 秒かかった → 受理待ちの間の tick を数えて後から上げ直すようにした。
  **検証**: `sleep 2` = 2.03 秒、`sleep 5` = 5.04 秒、`echo hi | wc -l` 0.06 秒。test_args / test_ps_m68k /
  test_ls_format(m68k)PASS。
  **★実機**: Mega のファーム(Timer5)は 1Hz のまま(tizix の外)。実機に焼くときは `make TICK_HZ=1`
  (ファームを 100Hz にすれば不要)。値を変えたら make clean してから。

## [#84] tzcc のテスト: test_all.sh が動かない / make runtest が全件 FAIL
- status: 完了
- category: テスト
- priority: MIDDLE
- assignee: AI
- creator: TK
- created: 2026-09-25 19:40
- updater: AI
- updated: 2026-09-25 20:10

### 履歴
- 2026-09-25 19:40 TK: 「tzcc/test_all.sh 使わないなら消して、使うなら直して」。
- 2026-09-25 20:10 AI(Claude): **完了(6595c1a)。** test_all.sh は `make test` + `make runtest` の重複で、
  どこからも使われていないので削除。代わりに runtest を直した(事実: #70 より前の版でも PASS=1 FAIL=44)。
  原因は 2 つ: (1) run_rt.sh が tzcc/ 直下の ./cpmsim を見ていて存在しない → tizix の arch/z80pack/cpmsim
  を既定に(CPMSIM= で差し替え可)。(2) CRLF のテスト 5 本(ok_43..45 / ok_long1..2)は EXPECT に \r が
  付いて、見た目同じで FAIL → 注記から \r を落として読む。**make runtest: PASS=45 FAIL=0 SKIP=9**。
  `make test` の test99 だけは既存の FAIL(readme にも記載)のまま。
- 2026-09-25 22:00 AI(Claude): tzcc の全ターゲットを clean から回して残りを片付けた。本体(all / static)は元から通る。
  - `make test`: test99.c は float / double リテラルを使うライブラリ網羅テストで、tzcc は浮動小数点非対応。
    `// TEST: skip 理由` 注記を読んで飛ばすようにし、test99 に注記を付けた(書き換え禁止区間の外)。
    レシピの `[[ ]]` のため SHELL = /bin/bash を明示(/bin/sh が dash の環境対策)。
  - `make com` / `make tizix`: 統合前の tzcc/disks/*.dsk を見ていて落ちていた。tizix は本体の driveb.dsk の
    /bin/<name>.bin へ(tizixcmd と同じ)、tizixrun は本体の make run。com は CP/M ディスクが無ければ .COM を
    作るだけで止める(DISK= で指定すれば転送)。
  - **未解決(TK: x86 は今は不要)**: `make x86test` が ok_43..45 / ok_long1 で FAIL。run_rt.sh で直したのと
    同じ CRLF(EXPECT に \r)で、tests/run_x86.sh の sed に `| tr -d '\r'` を足せば直る見込み。
- 2026-09-25 22:10 AI(Claude): 訂正。TK「test99 はわざと ANSI C のテストにしてある」。test99.c の skip 注記は
  外した(b94a161)。`make test` が test99 で止まるのは **ANSI C 準拠の目標に未達** という意味で正しい状態。
  Makefile の `// TEST: skip` の仕組みは残した(注記を付けたファイルは現在無い)。

## [#85] 実機 m68k-mega: Mega のファーム(Timer5)を 1Hz → 100Hz にして焼く
- status: 完了
- category: 実機
- priority: MIDDLE
- assignee: AI
- creator: TK
- created: 2026-09-25 20:30
- updater: AI
- updated: 2026-09-26 16:30
- 備考: TK 帰宅後に Mega を接続してから実施(#83 の続き)

### 履歴
- 2026-09-25 20:30 TK: 「家に帰ったら mega つないで焼いてくれ。タスクにしておく」。
- 2026-09-25 20:30 AI(Claude): 準備メモ(事実はスケッチを読んで確認済み、実機では未確認)。
  - スケッチ: `d:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST\MEGA2560_68000_DTACK_TEST.ino`
    (独自 git、HEAD bd82e0e)。`timer_irq_start()` の Timer5 が CTC・1024 分周・`OCR5A = 15624` で 1Hz。
  - **変更案**: 100Hz = 64 分周(`CS51|CS50`)+ `OCR5A = 2499`(16MHz/64/2500 = 100Hz ちょうど)。
    実機のバス代行は遅いので、100Hz で割込み処理の負荷が重ければ 50Hz = 256 分周(`CS52`)+
    `OCR5A = 1249`(TICK_HZ は 100 を割り切る値にすること。getticks の倍率が 100/TICK_HZ のため)。
    コメント(35 行目・753 行目の「1Hz」)も直す。
  - **取りこぼし**: ISR は `g_irq_req = 1` の 1 フラグで、IPL を上げて受理(IACK)される前に次の周期が
    来ると 1 回分消える。m68ksim で同じ形の問題があり時計が 2 倍遅れた(#83)。100Hz では効くので、
    フラグをカウンタにして受理ごとに減らす形を検討する。
  - **手順**: (1) tizix を `make -C arch/m68k-mega clean && make -C arch/m68k-mega`(既定 TICK_HZ=100。
    50Hz にするなら `TICK_HZ=50`)→ `arch/m68k-mega/rom.h` をスケッチのフォルダへコピー。
    (2) スケッチの Timer5 を変更。(3) `build_and_flash.ps1`(既定 COM3)で書き込み。
    (4) `tizix.ps1 -Send "sleep 5`n"` などで起動確認し、`sleep 5` の実時間・`echo hi | wc -l`・
    `a &` / `b &` の交互出力・`ps` を見る。
  - 戻すとき: スケッチを 1Hz に戻し、tizix は `make TICK_HZ=1`。
- 2026-09-26 00:50 AI(Claude): 準備の続き(スケッチは触っていない。作業ツリーに TK の未コミット変更が
  あるため)。当てる差分の案(MEGA2560_68000_DTACK_TEST.ino):
  ```c
  /* timer_irq_start(): 1Hz → 100Hz */
  TCCR5B = (1 << WGM52) | (1 << CS51) | (1 << CS50);   /* CTC, 64 分周 */
  OCR5A  = 2499;                                       /* 16MHz/64/2500 = 100Hz */

  /* 取りこぼし対策: フラグ → カウンタ(IACK で 1 つ減らす) */
  volatile uint8_t g_irq_req = 0;    /* 未処理の tick 数 */
  static uint8_t g_ipl_on = 0;       /* IPL=6 を出している */
  ISR(TIMER5_COMPA_vect){ if(g_irq_req < 255) g_irq_req++; }
  static void periodic(void){
    if(g_irq_req && !g_ipl_on){ IPL_LV6(); g_ipl_on = 1; }
    housekeeping();
  }
  /* loop() の IACK 分岐: */
  IPL_NONE(); g_ipl_on = 0;
  cli(); if(g_irq_req) g_irq_req--; sei();
  ```
  tizix 側は既定の TICK_HZ=100 のまま `make -C arch/m68k-mega clean && make -C arch/m68k-mega` →
  rom.h をスケッチへ → build_and_flash.ps1。負荷が重ければ 50Hz(CS52 のみ + OCR5A=1249、TICK_HZ=50)。
- 2026-09-26 02:00 AI(Claude): TK の指示で **1Hz のまま**実機へ一式を焼いた(100Hz 化は未着手のまま)。
  tizix 917f6bb を TICK_HZ=1 でビルドし out/m68k-mega-hw-1hz/ へ(rom.h md5 67378d30… / disk.img a6c715a3…)。
  SD: rocky9 の /dev/sda(7.4G・usb・RM=1)へ dd、直読みの md5 一致・mdir / mtype 可・eject。
  Mega: スケッチの rom.h を差し替え(旧 rom.h は Claude の scratchpad に退避、md5 76bd26f9…)、avrdude で COM3 へ
  30674B 書き込み・verify OK。スケッチのソース(.ino)は触っていない。
  1 回目は avrdude が同期できずに固まった(PowerShell の 2>&1 で書き込みスクリプトが途中停止し、残った avrdude が
  sync を繰り返していた。止めて再実行で通った)。実機での起動確認はまだ(SD を Mega へ差し替えてから)。
- 2026-09-26 16:30 AI(Claude): **完了**。実機は 100Hz(ファーム・カーネル・SD のコマンドとも)で一貫した状態。
  - ファーム(272ed3b、#95 で tizix へ取り込んだスケッチ): Timer5 を 64 分周・OCR5A=2499 で 100Hz。割込み要求を
    フラグ → カウンタ(IACK ごとに 1 減らす)にして、割込み禁止の syscall 中にたまった tick を捨てない。
    UART の受信バッファを 64 → 256B(-DSERIAL_RX_BUFFER_SIZE=256 をコアとスケッチの両方に)。RAM 933B。
  - カーネル: 既定の TICK_HZ=100(#94 の KYIELD 入り、out/m68k-mega-hw-100hz/rom.h)。
  - **SD は COM3 経由の xmodem で書き換えた**(カードを抜かずに)。TICK_HZ で中身が変わるコマンドは 7 本
    (a / b / rx / sh / sleep / uptime / vi)。1Hz 用の古い rx.bin は 100Hz のカーネルの上だと待ち時間を 100 倍速く
    数えてタイムアウトする(NAK 連発で失敗)ので、**一時的に 1Hz のファーム + 1Hz のカーネルに戻して**実機の rx で
    /root へ受信(7 本とも大きさ一致・再送 0、約 350B/s)→ cp で /bin へ → 100Hz のファームを焼き戻した。
    送信側は Claude の scratchpad の xmsend.ps1(XMODEM-CRC 128B、PowerShell)。
  - 実測(実機): `sleep 5` 5,937ms(コマンド自体の約 0.9 秒を除いて 5.0 秒)→ **時計は正しい**。
    応答は 1Hz(yield 入り、#94)より遅い: `echo hi` 586 → 857ms、`ls /` 738 → 1,124ms、ビルトイン `ps` 430 → 640〜906ms。
    推測: 10ms ごとの割込み処理(IACK・全レジスタ退避・31 スロット走査)が遅いバス代行の上で重い。
    **TK の判断待ち**: 応答の速さ(1Hz + yield)と、時間の刻み・背景ジョブへの公平さ(100Hz)のどちらを取るか。
    中間の 50Hz も可(64→256 分周・OCR5A=1249、TICK_HZ=50)。割込み処理を軽くする(走査するスロット数を減らす等)手もある。
  - 途中で分かったこと: cp が非常に遅い(#97)、ポートを開くたびの "pppp" がキュー中のコマンドを壊す(#93 に追記)。
- 2026-09-26 17:00 TK: 「過学習はやめて、局所最適化はせずに妥協を選ぶ」。12.5Hz を表すために入れかけた作り替え
  (周期を 1/100 秒単位の TICK_CS で持つ、getticks の倍率をカーネル側へ移す、rom.h に周期を埋め込む)は捨てた。
  (AI がこれを「100Hz で確定」と書いたのは早合点。TK の意図は「周期だけ変えて、100Hz からの変化を見る」だった。)
- 2026-09-26 17:40 AI(Claude): **実機で周期だけを変えて測った**(ファームの Timer5 と カーネルの TICK_HZ のみ。SD は 100Hz 用のまま、
  測るのは時間を数えないコマンドだけ)。2 回の平均(ms)、1 文字のエコーはどの周期も約 15ms:

  | 周期 | ps | echo hi | ls / |
  |---|---|---|---|
  | 100Hz | 773 | 857 | 1,124 |
  | 50Hz | 586 | 717 | 921 |
  | **25Hz** | **375** | **539** | **717** |
  | 12Hz | 397 | 563 | 780 |
  | 6Hz | 445 | 605 | 831 |
  | (参考)1Hz・yield 入り | 430 | 586 | 738 |

  100 → 50 → 25Hz で `ls /` は約 200ms ずつ縮み、25Hz より下では逆に少し遅くなる(ばらつきも大きい)。**25Hz が谷**。
  1 tick = 40ms で 100 を割り切れるので、コマンド側の時間の換算(100 / TICK_HZ)にも誤差が出ない。
  **推奨: 25Hz(TK の判断待ち)**。採用するなら、ファームの OCR5A = 9999(64 分周のまま)、カーネル `make TICK_HZ=25`、
  SD の 7 本(a / b / rx / sh / sleep / uptime / vi)を 25Hz 用に入れ替える(xmodem、1Hz ファームで rx したのと同じ手順)。
  実機はいま 100Hz(ファーム・カーネル・コマンドとも)で一貫した状態に戻してある。
  測定: Claude の scratchpad の hzsweep.ps1 / hwmeasure.ps1。各周期のカーネルは out/m68k-mega-hz{50,25,12,6}/rom.h。
- 2026-09-26 18:30 TK: 「25Hz でいこう」。
- 2026-09-26 18:30 AI(Claude): **25Hz を採用、実機に適用**。m68k の Makefile の既定を `TICK_HZ ?= 25`、スケッチの OCR5A = 9999。
  m68ksim(25Hz)で cmds_all / ps_m68k / 5b_pipe / rx / args / sh_hist / ls_format PASS。
  SD: 25Hz 用の 7 本を実機の rx(100Hz 同士で受信)で /root へ → **mv で /bin へ**(cp は遅すぎる #97。古いものは .old に
  退避してから入れ替え、25Hz で起動を確認してから消した)。一式は out/m68k-mega-hw-25hz/(rom.h md5 6b16e3d3…)。
  実測(3 つそろった 25Hz): `sleep 5` 5,787ms(約 5.1 秒、時計は正しい)、`ps` 521〜622、`echo hi` 670〜690、`ls /` 862〜868ms、
  エコー 15〜30ms。**周期を振ったときの 25Hz(コマンドは 100Hz 用のまま)より 2〜3 割遅い** ── あのときは sh などの時間待ちが
  4 倍長く数えられ、待ちループの回る回数が少なかったためと推測(未検証、深追いしない)。そろった状態でも 100Hz(773 / 857 /
  1,124ms)より 2〜3 割速いので採用は妥当。

## [#86] z80boardsim の受信が 1 バイト / 10ms に律速される
- status: 完了
- category: 開発環境
- priority: LOW
- assignee: AI
- creator: AI
- created: 2026-09-25 23:00
- updater: AI
- updated: 2026-09-25 23:00

### 履歴
- 2026-09-25 23:00 AI(Claude): #58 の検証で rx 12 ブロックが z80board(sim)で 30 秒、z80pack で 0.4 秒。
  **原因(コードの比較)**: 実機は FT245 ~RXF がレベル信号で Z80_INT にワイヤード OR され、データがある間
  isr が即座に再入して吸い出す(arch/z80board/crt0.s)。z80boardsim は割込みをタイマ(10ms)でしか上げて
  いなかった。**修正(z80pack-tizix のコミット)**: データポートを読んだ後にまだ入力があれば割込みを上げ直す。
  z80board の test_cmds_all / test_ls_format / test_vi / test_sh_hist / test_args / test_pipe_kill /
  test_5b_pipe は PASS。**これで #87 が見えた**(test_rx は z80board で 1 ブロック目から失敗)。

## [#87] z80board: 受信リング 64B が 1 パケット(133B)の連続受信で溢れる(rx が実機でも失敗する見込み)
- status: 完了
- category: バグ / 設計
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-25 23:00
- updater: AI
- updated: 2026-09-25 23:40

### 履歴
- 2026-09-25 23:00 AI(Claude): **事実**: #86 で z80boardsim の受信を実機どおり速くしたら、test_rx(z80board)が
  1 ブロック目で「応答なし → NAK」を繰り返して失敗する。arch/z80board/crt0.s の isr は受信リング KW_RXBUF
  (64B、0x8D98)に **満杯チェックなしで** 積む(コメントに「満杯チェックは持たない(ROM 優先)」。kmem.h の
  説明「満杯側は直近 1 バイトを捨てる」とは食い違っている)。XMODEM の 1 パケットは 133B で、isr が詰める速さに
  rx(kbhit / getchar を 1 バイトずつ drv_tbl 経由で呼ぶ)が追いつかないと一周して上書きされる。
  **推測**: 実機も同じ(FT245 は USB から一気に届く)。#58 の修正後も実機の rx は失敗する見込み。未確認。
  **制約(事実)**: block0 に 256B の連続した空きが無い(リングの後ろ ~40B、他は 53B / 18B / 15B)。isr は
  1 ページ内の 8bit 添字計算が前提。z80board の ROM 空きは 27B。
  **未解決(打ち手の候補)**:
  1. リングを 256B のページ境界に移す(添字の `and` が要らなくなり ROM は減る)。場所の捻出が要る
     (KW_PATHS 768B の縮小、ネット用リングとの共用など)。
  2. カーネルに「受信リングからまとめて読む」入口を足し、rx はパケット本体をそれで読む(消費側を isr より
     速くする)。ROM が数十 B 要る。
  3. isr に満杯チェックを入れる(溢れても直近を捨てるだけにする)。これだけでは rx は直らない。
  実機で rx を試して、失敗の仕方(途中で NAK が続くか)を見てから選ぶのがよい。
- 2026-09-25 23:40 AI(Claude): **完了(上のコミット)。** 候補 1 を採用: 受信リングを 256B にしてページ境界 0x9F00(block1 = DRIVER 領域の末尾、DRIVER は 0xE70 まで)へ移した。添字が 8bit で自然に一周するので isr の and / add が消え **ROM は 12B 減った(空き 27 → 39B)**。run_regress.sh に「z80board の DRIVER は 0xF00 未満」のガードを追加。**検証**: z80board(sim)で test_rx 2/2 PASS(12 ブロック、中身一致)ほか 9 本 PASS、z80pack の回帰 ALL PASS。**未解決**: 実機での rx 確認(TK)。#8(block1 廃止)をやるときはリングの置き場所も一緒に考えること。

## [#88] リリース 3 アーキ(z80pack / z80board / m68k-mega)の使い勝手を揃える
- status: 完了
- category: リリース
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-26 02:00
- updater: AI
- updated: 2026-09-25 21:10

### 履歴
- 2026-09-26 02:00 AI(Claude): TK「x86-ia16 は今は対応不要。z80pack / z80board / m68k-mega の完成を優先」を受けて、
  3 アーキの差を洗って埋めた。
  - m68k: df / uptime / free / history を入れた(8cc8298, e100003)。df は syscall 19(kfs_df)、free は
    syscall 20(スロットの pid)+ arch 専用の free.c(スロット 30 × 32KB を z80 版と同じ形で表示)。uptime と
    history は共有版がそのまま動く(sh の ↑↓ ヒストリは元から動いており、/root/history の形式も同じ)。
  - m68k: `cat f | tail` が "needs FILE or pipe" だった。ユーザー側の pipe_tail が「常に 0」のスタブのまま
    だったので syscall 21 でカーネルの pipe_tail につないだ。
  - パイプの 4KB 打ち切りを z80 の sh は "out of memory" と表示していた → "sh: pipe: truncated at 4KB"。
    m68k の sh は何も出していなかったので同じ文言を出す。
  - ls FILE がどのアーキでも "cannot open" だった → ファイルなら名前(-l ならサイズ)を出す。
  - 回帰: z80board 区間(cmds_all / args / rx / esp_net)と m68k 区間(args / ls_format / ps)を run_regress.sh
    に追加。3 アーキとも 1 回の回帰で見る。
  **未解決**: m68k にネットワーク系(net / telnet / tzftp / ntpdate)と rx は無い(m68k-mega に通信ハードが無いため。
  実機の構成が決まってから)。
- 2026-09-25 21:10 AI(Claude): z80 のテストを m68k でそのまま回し(TIZIX_ARCH=m68k-mega、tzpaths に m68k-mega 分岐を追加)、
  出た差を埋めた。
  - `rm /root/history` の直後にヒストリが壊れる / `history` が自分自身を末尾に出す: m68k は行を**実行前**に
    履歴へ足していた(io.c の readline 内)。z80(user/sh.c)と同じく**実行後**に足すよう src/sh.c へ移した。
    消された直後の新規ファイルでリング位置(k / h_cnt)も 0 に戻す(src/fatcmd.c hist_add)。
  - `rm /dev/null` が "error 1": m68k の unlink が「FAT 以外」と「その他の失敗」を区別していなかった。
    syscall 11 を mkdir(14)と同じ 0 / 1 / 0xFF(FS_DENIED)にして "permission denied" に揃えた。
  - `cd NOPE` の文言 "no such directory" → z80 と同じ "no such dir"。
  - m68k に /dev/fda・/dev/fdb(生セクタ)が無く `dd if=/dev/fdb` が開けなかった。src/dev.c の kdev_*(z80 と
    同じ関数ポインタ無しの経路)を m68k でも有効にし、diskio.c に disk_raw_rw を足した。SD は 1 枚なので
    z80board と同じく fda / fdb は同じ媒体。
  - ptx / prx(パイプ試験用)を m68k にも入れた。
  - 回帰の m68k 区間に z80 共通テスト 8 本(pwd_cd / sh_hist / vfs_step8-10 / 5b_pipe / cmds_all / dev_dd)を追加。
    test_cmds_all の free 期待値だけ m68k 用に分けた(スロット 30 × 32KB)。
  m68k で意味を持たないので対象外にしたもの: spawn / ovl / xblk / blk・wak(z80 の 4KB ブロック機構)、
  vi のブロック不足チェック。

## [#89] m68ksim: パイプの出力が 1 バイト欠ける(`ptx 40 | prx` の "line-29" が "line-9")
- status: 完了
- category: バグ
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-25 21:10
- updater: AI
- updated: 2026-09-25 21:10

### 履歴
- 2026-09-25 21:10 AI(Claude): #88 で test_5b_pipe を m68k で回したら 2 回に 1 回、40 行のうち 1 行の先頭 1 文字が
  消えた。カーネルのパイプ(pipe_putc / pipe_getc は IRQ_OFF で囲まれている)ではなく m68ksim の UART 出力が原因:
  putchar + fflush(stdout) で書いていたが、心拍の SIGALRM(30ms)は SA_RESTART 無し(Ctrl+] 検出のため意図的)
  なので、pty が詰まって write が待たされている間に心拍が来ると EINTR で戻り、glibc がそのバイトを捨てる。
  write(2) を EINTR で再試行する形に変えた。直後に test_5b_pipe 6/6 PASS。実機(Mega の UART)には無関係。

## [#90] m68k-mega: sh がカーネルに同居している → z80 と同じ外部プロセス /bin/sh.bin へ分離
- status: 完了
- category: 構造
- priority: HIGH
- assignee: AI
- creator: TK
- created: 2026-09-25 22:20
- updater: AI
- updated: 2026-09-25 23:10
- 備考: TK「カーネルに sh が同居って Linux で考えたらどうよ」「最後の仕上げに sh と init を分離したい」。cwd もいずれカーネルの持ち物ではなくす(TK)

### 履歴
- 2026-09-25 22:20 TK: m68k の sh がカーネルに入っている。sh に分離してくれ。構造的におかしい。
- 2026-09-25 23:10 AI(Claude): 分離した。z80 と同じ user/sh.c をそのまま m68k でも /bin/sh.bin(6904B)として作り、
  カーネルの init(src/init.c)が起動・終了したら respawn する(z80 分岐に m68k を合流)。
  - user/sh.c: z80 で数値番地を直に触っていた 3 つ(pid 表 / 出力ルート / セッション状態)を SH_PID / SH_KILL /
    SH_ROUTE / SH_STATE の #ifndef 既定に包んだ。**z80 の sh.bin は md5 まで変わらない**ことを確認。
  - arch/m68k-mega/user/shvec.h: z80 の drv_tbl[27..44] と同じ名前の入口を TRAP #0 syscall 22..34 で出す。
    sysfile.c に 22..34(kexec_argv / builtin_try / builtin_is / redir / in / krun_pipe / kchdir / kgetcwd /
    con_break / kill / route)を追加。
  - cwd: z80 と同じくカーネルが持つ形に m68k も揃えた(kmem.h の kwork に KW_CWD / KW_PATHS、fatcmd.c の kpath
    ほかを m68k にも有効化、sysfile のパス入口 open / opendir / unlink / mkdir / rename に kpath)。
  - fopen "r+"(sh のヒストリのリング上書き)を m68k の tzstdio / sysfile / fsbackend_fat に通した。
  - カーネル内 sh 専用だった行編集ヒストリ(io.c / fatcmd.c)は x86 専用に戻した。src/sh.c は x86 だけが使う。
  - ps の slot 0 の表示 "(sh)" → "(init)"(z80 / m68k とも slot 0 はカーネル + init)。z80board ROM 残り 33B。
  - 回帰: 全体 ALL PASS(m68k 区間 11 本含む)。test_ps_m68k / test_cmds_all(free)は sh が slot 1 を使う形に更新。
  **次(TK の構想、未着手)**: init のカーネルからの分離(z80 / m68k とも init はまだ src/init.c)、cwd をカーネル
  からプロセスの持ち物へ。

## [#91] rx(xmodem 受信)を全アーキへ / 0x03 を含むデータで rx が kill される
- status: 完了
- category: コマンド / バグ
- priority: HIGH
- assignee: AI
- creator: TK
- created: 2026-09-25 23:10
- updater: AI
- updated: 2026-09-26 01:20
- 備考: TK「rx は全部に移植してくれ」

### 履歴
- 2026-09-26 01:20 AI(Claude): m68k-mega に rx を移植(共有 user/rx.c 無改造 + tzstdio に kbhit)。途中で 2 件の実バグ:
  - **m68ksim の入力リング 64B があふれる**: ホストが xmodem の 1 パケット(133B)を一度に送ると、ゲストが読み切る
    前にあふれて中身が欠け、block 1 から NAK。4KB に(Ctrl+] を常に即判定する方針はそのまま)。
  - **全アーキ共通: データ中の 0x03 を sh が Ctrl+C と取って rx を kill する**。block 3 はシーケンス番号そのものが
    0x03。rx より先に sh の前景待ち(con_break)が読むと kill される、というタイミング依存(m68k で 3 回中 2 回)。
    z80 はタイミングの差でたまたま助かっていただけ。
    → tty の raw(ISIG 無効)相当の「生モード」を入れた。前景プログラムが con_raw(1) を呼ぶと、その間 con_break は
    0x03 をデータとして通す。rx は開始時に 1、終了時に 0。異常終了しても sh が前景ジョブの後で con_raw(0) する
    (Unix のシェルが前景ジョブ後に端末モードを戻すのと同じ)。
    番地: z80 は KW_CONRAW=0x8544(crt0 のクリア範囲を 36→43B に延ばして初期化、命令は増えない)、m68k は kwork。
    入口: z80 は drv_tbl[48](**本体は DRIVER の drv_conraw**。z80board のカーネル ROM に 21B の余地が無かったため)、
    m68k は syscall 35(src/io.c の con_setraw)。
  - test_rx: z80pack / z80board / m68k-mega 各 3 回 = 9/9 PASS。
  - 解除の置き場所を途中で変えた: 最初は kexec(スロット再利用時)で解除していたが、z80board の ROM がちょうど
    満杯(0B)になったので、sh が前景ジョブの後で戻す形にした。z80board ROM 残り 13B。
  - 全体回帰: 1 回目は z80pack の test_ls_format が FAIL(#92、再現せず)、2 回目 ALL PASS。
  - run_regress.sh の z80board ROM ガードが、boot.rom が最新だと空きを表示しなかった → kernel.ihx から毎回数える。

## [#92] test_ls_format が全体回帰の中で 1 回だけ FAIL(その起動中 /etc/rc が開けない)
- status: 完了
- category: バグ(不安定)
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-26 02:20
- updater: me
- updated: 2026-09-26 13:23

### 履歴
- 2026-09-26 02:20 AI(Claude): #91 の全体回帰(z80pack 区間)で 1 回だけ観測。`ls /etc` には rc が出るのに、同じ起動の
  中で `ls /etc/rc` / `wc -c /etc/rc` / `ls -l /etc/rc` が 3 回とも "cannot open"。ディレクトリ反復(kdir)は通り、
  ファイルを開く経路(DRIVER の drv_open、fd 6 本)だけが全滅 → fd を使い切っていた疑い。直前は test_sh_hist。
  単体 2 回・test_sh_hist → test_ls_format の順で 2 回・全体回帰をもう 1 回、いずれも再現せず。
  sh のヒストリ(hist_add / hist_get / run_rc)は全経路で fclose している(読んで確認済み)。
  ログ: rocky9 の /tmp/ls_flake_1.log(再起動で消えるので要点はここに書いた)。
  **未解決**。次に見るなら: 失敗時に fd_table(0x8600、37B×6)の使用状況をダンプする仕掛けをテストに足す。
- 2026-09-26 07:10 AI(Claude): **2 回目を観測**(#74 の Docker、新しく clone したツリーで `--build` 直後の全体回帰)。症状は同じ
  (`ls /etc` には rc が出るのに、同じ起動で /etc/rc の open が 3 回とも失敗)。これで全体回帰 約 6 回中 2 回。
  再現の試み(いずれも PASS): 回帰と同じ順で test_cmds_all … test_sh_hist → test_ls_format を 2 周 /
  各周の頭で `make cleandisk && make` してから同じ順で 2 周。
  2 回とも「ビルド直後の最初の全体回帰」だったが、ディスクを作り直した再現では出ないので、それだけでは説明できない。
  **時期**: #91(rx の生モード。sh が前景ジョブのたびに drv_tbl[48] を呼ぶ・DRIVER に drv_conraw・crt0 のクリア 36→43B・
  con_break の判定)を入れてから出始めた。それ以前の全体回帰は同じ順で何度も ALL PASS。関係は未証明。
  次に見るなら: (1) sh の `con_raw(0)` を一時的に外して全体回帰を数回回し、出なくなるか(切り分け)。(2) 失敗時に
  fd_table(0x8600)と FatFs のロック表を覗けるよう、テスト側から `cat /etc/rc` 失敗時に kernel の状態を吐くコマンドを足す。
- 2026-09-26 19:00 TK: 「z80 なら優先度 HIGH」。落ちているのは z80pack の区間なので HIGH・対応中に。
- 2026-09-26 19:40 AI(Claude): **原因を特定して修正(完了)**。cpmsim は起動時に RAM を乱数で埋める(z80core/sim0.c の rand)。
  DRIVER は fd_table(0x8600)の初期化済みを fd_inited(0x86F0)== 0x5A で判定しており、約 1/256 の起動でゴミが 0x5A に
  なって fd_table のゴミがそのまま使われ、全 fd が「使用中」→ その起動ではどのファイルも開けなかった(ls /etc は kdir なので通る)。
  実機の電源投入時の RAM も不定なので実バグ。src/kexec.c の kload_driver で DRIVER を載せる前に fd_inited を 0 にした
  (z80board ROM 13 → 8B)。全体回帰 ALL PASS。

## [#93] 実機 m68k-mega で気づいた小さな点(ポート接続時の "pppp" / パイプの "out of memory" 表示)
- status: 完了
- category: 実機 / 表示
- priority: HIGH
- assignee: AI
- creator: AI
- created: 2026-09-26 02:40
- updater: AI
- updated: 2026-09-26 02:40

### 履歴
- 2026-09-26 02:40 AI(Claude): #85 で焼いた 1Hz 版(917f6bb)を実機で確認。起動 → init が /bin/sh.bin を起動 → `ps` に
  "0 rdy (init)" / "1 run sh"、`ls /`、`echo hi | wc -l` → 1、`ls /bin`(sh.bin / rx.bin を含む 36 本)、`sleep 3 &` まで動作
  (#90 の外部 sh 化は実機でも OK)。気づいた点:
  - **COM3 をリセット無し(DTR 無効)で開き直すたびに、sh の入力行に "pppp"(3〜4 文字)が入る**。tizix 側は受けたものを
    そのまま読んでいるだけ。Mega スケッチ側か USB シリアルの接続時の揺れと推測(未確認)。回避: 接続直後に改行を 1 つ送る。
  - パイプの左右どちらかが **見つからない** ときも sh は "sh: out of memory" と出す(krun_pipe の戻り 1 が「起動できない」を
    全部まとめているため。z80 も同じ)。not found と分けたい。
- 2026-09-26 16:30 AI(Claude): **"pppp" の実害を確認**(#85 の作業中)。実機の sh の入力キューにコマンドが溜まっている間に
  COM3 を開き直すと、そのたびに "p" が入力の途中へ混ざり、キュー中のコマンドが壊れて実行された(例: `cppppppppx.bin`、
  `ls -l /bin/sppppp…`)。回避: 実機にコマンドを溜めたままポートを開き直さない(1 回開いて最後まで見る)。
  出どころ(Mega スケッチか USB シリアルか)は未確認。
- 2026-09-26 19:00 TK: 優先度 HIGH に昇格。対応中に。
- 2026-09-26 19:40 AI(Claude): パイプの "out of memory" を修正(完了)。krun_pipe の戻り 1 は「起動できない」全部なので、
  user/sh.c が左右のコマンドのファイルの有無を確かめ、無ければ "sh: <cmd>: not found" と出す(カーネル側で戻り値を分けると
  z80board の ROM を削るため sh 側で)。"pppp" は出どころが未確認のまま #98 へ切り出した。

## [#94] m68k-mega実機 uartのバッファが重い
- status: 完了
- priority: HIGH
- assignee: AI
- creator: me
- created: 2026-09-26 09:09
- updater: AI
- updated: 2026-09-26 13:50

### 履歴
- 2026-09-26 09:09 me: m68k-mega実機 uartのバッファが重い 割り込み時間が遅い？
- 2026-09-26 12:15 AI(Claude): #60 の残課題 b / e(速度)をここへ統合。経緯: 2026-09-21 の高速化でバスサイクル 2.6 倍
  (`ls /bin` 10.65 → 4.2 秒、詳細はスケッチ側 HANDOFF_speed.md 9 章)。**推測(未測定)**: いまの重さの主因は
  タイマ 1Hz(#85)── sh は前景ジョブの終了を回り続けて待ち、子には 1 秒ごとの切替でしか CPU が回らない。
  1 文字ずつのバス代行(UART / SD)の遅さが上乗せ。まず 1Hz のまま実測(1 コマンドの所要時間・エコーの遅れ)してから
  #85 で 100Hz にして比べる。
- 2026-09-26 13:50 AI(Claude): 実測(実機、1Hz、COM3 から時刻を取って計測。スクリプトは Claude の scratchpad の hwmeasure.ps1):
  1 文字のエコー 約 15ms(**遅くない**)、ビルトイン `ps` 420〜470ms、外部コマンド `echo hi` 1,467ms・`ls /` 1,936ms。
  → 重いのは外部コマンド。原因: m68k のスケジューラはタイマ(1Hz)か exit でしかスロットを切り替えず、KYIELD が空。
  sh が子を起動しても次の tick まで自分のスロットで前景待ちを回り続け、init(slot 0)も同様に回る。
  **修正**: m68k の KYIELD を TRAP #1 に(crt0.s に trap1_handler、irq6 と同じ save/pick/restore で tick は進めない)。
  既存の待ちループ(con_break / kgetchar / getticks / init)がそのまま譲るようになる。proc_block の NOP スピンも KYIELD へ。
  **結果(実機、同じ 1Hz)**: `echo hi` 1,467 → 586ms、`ls /` 1,936 → 738ms(約 2.5 倍)、エコー・`ps` は変わらず。
  m68ksim: ps_m68k / 5b_pipe / cmds_all / rx / sh_hist PASS。実機には ROM だけ焼いた(out/m68k-mega-hw-1hz-yield、
  rom.h md5 9defce70…。SD のコマンドは 917f6bb から不変)。
  **残り**: ビルトインでも約 0.43 秒かかる。コマンドのたびの sh のヒストリ書き込み(SD、バス代行)が主と推測(未測定)。
  100Hz 化(#85)で残る切り替え待ちも詰まる見込み。

## [#95] m68k-mega の Mega ファーム(TIZIX HOST スケッチ)を arch/m68k-mega/arduino/ へ取り込む
- status: 完了
- category: 実機 / リポジトリ整理
- priority: MIDDLE
- assignee: AI
- creator: AI
- created: 2026-09-26 12:15
- updater: AI
- updated: 2026-09-26 14:00

### 履歴
- 2026-09-26 12:15 AI(Claude): #60 の残課題 d から分解。TK「ファームは arch/m68k-mega/arduino/ 以下においてくれ」。
  現在のスケッチは tizix の外(`D:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST\`、独自 git、HEAD bd82e0e +
  TK の未コミット変更)。書き込み手順(build_and_flash.ps1 / tizix.ps1)と、rom.h(tizix の make が作る生成物)の
  受け渡しも一緒に整理する。旧 BUSPROBE のソースは同フォルダの bk/ にしか無い。
- 2026-09-26 14:00 AI(Claude): 完了。`arch/m68k-mega/arduino/MEGA2560_68000_DTACK_TEST/`(.ino・README・HANDOFF_speed.md・
  build_and_flash.ps1・tizix.ps1・mega.ps1・bench.ps1・test_byte*.s)と `arduino/busprobe_20260920/`(旧 BUSPROBE)。
  元フォルダの作業ツリーをそのまま(TK の未コミット変更込み)。元フォルダは触っていない。bk/ の他の古い版は取り込まず。
  `make -C arch/m68k-mega` が rom.h をスケッチのフォルダにも置く(生成物なので .gitignore、build/ も)。
  取り込み先(\\rocky9\… の UNC パス)から build_and_flash.ps1 -BuildOnly が通ることを確認。arduino/README.md に使い方。

## [#96] m68k-mega に ESP32 をつなぐ(ネットワーク)
- status: 未着手
- category: 実機 / ネットワーク
- priority: HIGH
- assignee: TK / AI
- creator: AI
- created: 2026-09-26 12:15
- updater: AI
- updated: 2026-09-26 12:15
- 備考: TK「m68k に esp32 はこの土日でやりきる」

### 履歴
- 2026-09-26 12:15 AI(Claude): #60 の残課題 i から分解。当時のメモ: A21=1(0x300000)を ESP32 用に予約済み。
  SPI は SD と共有し CS は別ピン。ESP-AT の SPI には handshake 線が要る(D20 / D21 / D38 が空き)。
  z80board は 2026-09-23 に bit-bang UART 方式へ切り替えた(#62)ので、m68k もどちらでつなぐか最初に決める。

## [#97] m68k-mega 実機で cp が非常に遅い(1 秒あたり 1 桁バイト)
- status: 未着手
- category: 性能
- priority: LOW
- assignee: AI
- creator: AI
- created: 2026-09-26 16:30
- updater: AI
- updated: 2026-09-26 16:30

### 履歴
- 2026-09-26 16:30 AI(Claude): #85 の作業中に観測(実機、1Hz のとき)。664B の cp に 1〜2 分、6,956B の sh.bin に約 15 分(約 8B/s)。
  同じ実機の rx(xmodem)は 7,872B を 22 秒で書くので、SD の書き込み自体の遅さではない。
  原因(コードで確認、実機では未計測): user/cp.c は z80 の 4KB ブロックに収めるため **fgetc / fputc の 1 バイト逐次コピー**。
  m68k の tzstdio はそれを 1 バイトごとの read / write syscall にし、FatFs は **FF_FS_TINY=1**(src/ffconf.h、全ファイルで
  セクタ窓 1 枚を共有)なので、読み元と書き先が 1 バイトごとに窓を取り合い、そのたびに SD のセクタの読み直し・書き戻しが走る。
  直し方の候補: (1) m68k の tzstdio にファイルごとの小さなバッファ(z80 と違いメモリに余裕がある)、(2) cp に m68k 用の
  まとめ読み版(arch 側で差し替え、date.c / free.c と同じ手口)、(3) m68k だけ FF_FS_TINY=0。

## [#98] m68k-mega 実機: COM3 をリセット無しで開くたびに入力へ "pppp" が混ざる
- status: 未着手
- category: 実機 / シリアル
- priority: LOW
- assignee: AI
- creator: AI
- created: 2026-09-26 19:40
- updater: AI
- updated: 2026-09-26 19:40

### 履歴
- 2026-09-26 19:40 AI(Claude): #93 から切り出し。PowerShell の SerialPort(DtrEnable=false)で COM3 を開くたびに 3〜4 個の 'p'(0x70)が
  sh の入力に入る。キューにコマンドが溜まっているときに開き直すと、その途中に混ざってコマンドを壊した(#85 の作業中に実害)。
  出どころ(Mega スケッチ / ATmega16U2 の USB シリアル / .NET の制御線の揺れ)は未確認。回避: 接続直後に改行を 1 つ送る、
  コマンドを溜めたまま開き直さない(hwrun.ps1 / xmsend.ps1 はそうしている)。

## [#99] configure / Makefile の日本語を英語にする(利用者に見える出力から)
- status: 未着手
- category: リリース / ビルド
- priority: LOW
- assignee: AI
- creator: AI
- created: 2026-09-26 22:30
- updater: AI
- updated: 2026-09-26 22:30

### 履歴
- 2026-09-26 22:30 AI(Claude): TK 指摘「configure やら make に日本語が入ってる。今はもういいがタスクに」。README は英語化済みだが
  ビルド系は日本語のまま。非 ASCII を含む行数: configure.ac 43 / config.mk.in 3 / Makefile 16 / arch/common-sdcc.mk 45 /
  arch/z80pack 28 / arch/z80board 64 / arch/m68k-mega 63 / python/run_regress.sh 49(x86-ia16 はリリース対象外)。
  うち echo / AC_MSG_* など**利用者の端末に出る文言が 21 行**あり、これを先に英語へ。コメントは後回しでよい。
  configure は作り直し(autoconf)が要る。#73 の configure 同梱(dev に configure が入っていない)と一緒に片付けると手戻りが無い。

## [#100] make ARCH=*** run で依存関係が効かず、毎回コンパイルが走ることがある
- status: 未着手
- category: リリース / ビルド
- priority: LOW
- assignee: AI
- creator: TK
- created: 2026-09-26 22:40
- updater: AI
- updated: 2026-09-26 22:40

### 履歴
- 2026-09-26 22:40 AI(Claude): TK 指摘で登録。`make ARCH=*** run` で、ソースを変えていなくてもコンパイルが通り直すことがある
  (依存性チェックが効いていない)。原因は未調査。まず 2 回続けて `make ARCH=<arch>` → `make ARCH=<arch> run` を叩き、
  何が作り直されるかを `make -d` / `--trace` で 3 アーキそれぞれ確かめる。疑いどころ(未確認): 実在しないファイル名の
  ターゲット(.PHONY 扱いで毎回走る)、中間物(.asm/.iy.asm/.rel)の途中生成で時刻が逆転、disk 像を毎回作り直す規則、
  ルート Makefile から下位 make へ渡すときの再帰。
