# 04-user: ユーザー側 (コマンド / libc / 入出力) の比較

## 1. 概要
本ドキュメントは、x86-ia16 アーキテクチャ固有のユーザー側基盤 (`arch/x86-ia16/user/tzstdio.h`, `usyscall.s`, `sysfile.c`) と共通側のユーザー側基盤 (`user/*.c`, `src/libc.c` / `src/libc/libc.c`, `src/sysfile.c`, `user/stdio.h` の syscall 1〜52 部分) との比較分析をまとめたものである。

---

## 2. (1) syscall 番号の対応表 (x86 実装 vs 共通側)

共通側のディスパッチャ (`src/sysfile.c`) の syscall 番号 1〜52 に対し、x86-ia16 側 (`arch/x86-ia16/sysfile.c`) の実装状況を以下に示す。

| 番号 | 共通側の意味 (`src/sysfile.c`) | x86 側実装状況 | 備考 (x86側での扱い) |
| :---: | :--- | :---: | :--- |
| 0 | exit | 独立処理 | arch側 (`crt0.s` 等) で直接処理 |
| 1 | putchar(a1) | あり | `AL` 引数 |
| 2 | getchar | あり | 戻り値 `AL` |
| 3 | getticks | あり | 戻り値 `AX` |
| 4 | open(path, flags) | あり | `SI=path`, `AL=flags` |
| 5 | close(fd) | あり | `AL=fd` |
| 6 | read(fd, buf, n) | あり | `BX=fd`, `DI=buf`, `CX=n` |
| 7 | write(fd, buf, n) | あり | `BX=fd`, `DI=buf`, `CX=n` |
| 8 | opendir(path) | あり | `SI=path` |
| 9 | readdir(dh, name, size) | 一部差異あり | `BX=dh`, `DI=name13` (共通側は 1/2/0 を返すのに対し x86 は 1/0) |
| 10 | closedir(dh) | あり | `AL=dh` |
| 11 | unlink(path) | あり | `SI=path` |
| 12 | readdir_size() | あり | 直前 readdir エントリのサイズ下位16bit |
| 13 | seek(fd, offset, whence) | なし | x86 に seek なし (`tzstdio.h` はスタブ) |
| 14 | mkdir(path) | あり | `SI=path` |
| 15 | rename(old, new) | あり | `SI=old`, `DI=new` |
| 16 | time_get() | あり | Unix 秒 (32bit) |
| 17 | input_ready() | なし | **【不整合】** `tzstdio.h` が呼び出すが `sysfile.c` に case 17 が無い |
| 18 | klog(msg) | あり | `SI=msg` (`/var/log/message` 追記) |
| 19 | df(sel) | あり | `AL=sel` |
| 20 | slot(n) | あり | `AL=n` (pid表 n番、8以上は0xFFFF) |
| 21 | pipe_tail(want) | なし | `tzstdio.h` はダミー 0 返却、sysfile.c に case 無し |
| 22〜34 | 外部 sh 用機能 (kexec_argv, builtin_try, krun_pipe, kchdir 等) | なし | `tzstdio.h` はスタブ、sysfile.c に case 無し |
| 35 | con_raw(on) | なし | **【不整合】** `tzstdio.h` が呼び出すが `sysfile.c` に case 35 が無い |
| 36〜46 | ネットワーク (knet_connect, net_read, dns 等) | なし | `PLAT_NET` 未実装 |
| 47〜51 | 郵便受け / sleep (mb_bind, mb_call, mb_recv, mb_reply, ksleep) | なし | mbox 未実装 |
| 52 | exitcode(n) | なし | スロット n の終了コード取得未実装 |

---

## 3. (2) `tzstdio.h` と共通 stdio/libc API の違い

`arch/x86-ia16/user/tzstdio.h` は、共通の `user/*.c` (tzcc 向け書式や共通 libc API を前提とするコマンド群) を x86-ia16 環境で無改造（または最小限のラッピング）でビルドするために用意された互換ヘッダである。共通の `user/stdio.h` や `src/libc.c` と比較して以下の違いがある：

1. **syscall5 ラッパのアーキテクチャ差異 (`usyscall.s`)**:
   - 共通の gcc 側 syscall5 (m68k/esp32 等のフラット32bit) と異なり、x86-ia16 版は 16bit セグメント環境を前提とし、レジスタ指定 (`AH=func`, `AL=al`, `BX`, `CX`, `SI`, `DI`) および `INT 0x80` 割り込みによるアセンブリスタブ (`_real_syscall5` / `syscall5`) を用いる。
   - 戻り値の `0xFFFF` (-1) を `0xFFFFFFFFUL` に拡張する `_sc_ext` マクロを含む。

2. **静的 (`static`) インライン実装による名前空間衝突回避**:
   - `tzstdio.h` 内の全関数 (`fopen`, `fread`, `fwrite`, `fclose`, `printf`, `puts` 等) はすべて `static` として定義されている。これは x86 ネイティブコマンド (`ls.c` 等が使う `user/stdio.h` + `ulibc.o`) とのシンボル衝突を防ぐためである。

3. **独自追加・スタブ関数群**:
   - Z80/tzcc 系の低レベルプリミティブや高度機能 (`proc_block`, `getbase`, `peek`, `poke`, `absmove`, `absmovd`, `absscan`, `absrscan`, `getxbase`, `getxsize`, `vi_xbuf` 等、特に `vi.c` や `sh.c` 等の移植性維持に必要な特殊関数) が `static` インラインスタブとして多数追加されている。

---

## 4. (3) 共通 `user/*.c` を無改造でビルドするために x86 側に足りないもの (最大5項目)

事実 (Fact) と推測 (Guess) を明確に区別し、共通 `user/*.c` を x86 側で完全に動作させるために不足している主要項目を挙げる。

1. **Seek システムコール (`seek` / syscall 13) 【事実】**:
   - カーネルディスパッチャ (`arch/x86-ia16/sysfile.c`) に case 13 が存在せず、`tzstdio.h` でも `fseek`/`ftell` はスタブ (-1返却) となっている。ランダムアクセスを伴うファイル操作コマンドは正常に動作しない。

2. **プロセス間パイプ・シェル実行系システムコール (syscall 21〜34) 【事実】**:
   - `krun_pipe`, `kexec_argv`, `kchdir`, `con_break`, `kill` 等の外部シェル連携・プロセス制御用 syscall (21〜34) が x86 カーネルに未実装であるため、共通のシェル (`user/sh.c`) や高度なパイプライン処理を行うコマンド群はビルド・実行できない。

3. **端末制御・非ブロッキング入力関連 syscall の不整合 (syscall 17, 35) 【事実】**:
   - `tzstdio.h` は `input_ready()` (syscall 17) や `con_raw()` (syscall 35) を呼び出してエスケープシーケンス判定や生モード切替を行うが、`arch/x86-ia16/sysfile.c` に該当の case が実装されていないため、`vi` などのインタラクティブコマンドでキー入力制御が正しく行われない不整合がある。

4. **ネットワーク・メッセージボックス機能 (syscall 36〜51) 【事実】**:
   - `PLAT_NET` 関連のソケット/DNS/HTTP系 syscall (36〜46) およびプロセス間通信用の郵便受け syscall (47〜51) が一切存在しないため、ネットワーク関連の共通コマンドはビルドまたは実行時に失敗する。

5. **16bit メモリモデルおよび大容量ヒープ制約下でのポインタ・メモリ管理機構 【推測】**:
   - x86-ia16 はセグメント分割モデルであり、ポインタが 16bit オフセットに限定されるか `farcpy` を経由する必要がある。共通 `user/*.c` の一部が仮定しているフラットな 32bit 連続メモリ空間や動的メモリ割り当て (`malloc` 等の大規模ヒープ) は、単なる `tzstdio.h` のスタブや小規模スタティックバッファ (`vi_xbuf`等) ではカバーしきれないメモリ制約上の壁に直面する可能性が高い。
