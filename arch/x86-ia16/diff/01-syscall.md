# 01-syscall: x86-ia16 と共通側システムコールの比較

## 1. 概要
本ドキュメントは、共通システムコールディスパッチャ (`src/sysfile.c` の case 1〜52) と x86-ia16 アーキテクチャ固有のシステムコールディスパッチャ (`arch/x86-ia16/sysfile.c`) ならびにユーザー側ライブラリ (`arch/x86-ia16/user/tzstdio.h`) の syscall 番号、引数マッピング、戻り値の幅、および実装状況の比較・差異分析をまとめたものである。

---

## 2. 表1: syscall 番号 / 共通側の意味 / x86 側にあるか / 備考

| 番号 | 共通側の意味 (`src/sysfile.c`) | x86 側にあるか | 備考 |
| :---: | :--- | :---: | :--- |
| 0 | exit | 独立処理 | arch側(crt0.s等)で直接処理 |
| 1 | putchar(a1) | あり | al 引数 |
| 2 | getchar | あり | 戻り値 al |
| 3 | getticks | あり | 戻り値 AX |
| 4 | open(path, flags) | あり | SI=path, AL=flags |
| 5 | close(fd) | あり | AL=fd |
| 6 | read(fd, buf, n) | あり | BX=fd, DI=buf, CX=n |
| 7 | write(fd, buf, n) | あり | BX=fd, DI=buf, CX=n |
| 8 | opendir(path) | あり | SI=path |
| 9 | readdir(dh, name, size) | 意味が違う | BX=dh, DI=name13。共通は 1/2/0 を返すのに対し x86 は 1/0 のみ |
| 10 | closedir(dh) | あり | AL=dh |
| 11 | unlink(path) | あり | SI=path |
| 12 | readdir_size() | あり | 直前 readdir エントリのサイズ下位16bit |
| 13 | seek(fd, offset, whence) | なし | x86 に seek なし (tzstdio.h に stub の ftell/fseek のみ) |
| 14 | mkdir(path) | あり | SI=path |
| 15 | rename(old, new) | あり | SI=old, DI=new |
| 16 | time_get() | あり | Unix 秒(32bit) |
| 17 | input_ready() | なし | **【不整合】** tzstdio.h は `syscall5(17)` を呼ぶが `sysfile.c` に case 17 が無い |
| 18 | klog(msg) | あり | SI=msg (`/var/log/message` 追記) |
| 19 | df(sel) | あり | AL=sel |
| 20 | slot(n) | あり | AL=n (pid表 n番、8以上は0xFFFF) |
| 21 | pipe_tail(want) | なし | tzstdio.h はダミー 0 返却、sysfile.c に case 無し |
| 22〜34 | 外部 sh 用 (kexec_argv, builtin_try, krun_pipe, kchdir, con_break, kill, route 等) | なし | tzstdio.h はスタブ、sysfile.c に case 無し (sh非対応) |
| 35 | con_raw(on) | なし | **【不整合】** tzstdio.h は `syscall5(35)` を呼ぶが `sysfile.c` に case 35 が無い |
| 36〜46 | ネットワーク (knet_connect, knet_state, read, write, dns 等) | なし | `PLAT_NET` 未実装 |
| 47〜51 | 郵便受け / sleep (mb_bind, mb_call, mb_recv, mb_reply, ksleep) | なし | mbox 未実装 |
| 52 | exitcode(n) | なし | スロット n の終了コード取得未実装 |

---

## 3. 表2: 引数の対応と戻り値の幅

x86-ia16 はレジスタ分割 (`ax` の `AH`=機能番号, `AL`=小引数, `BX`, `CX`, `SI`, `DI`) とセグメント跨ぎの `farcpy`/`farcpy_in` を使用しており、戻り値は基本的に 16bit (`unsigned`) です（一部 32bit 拡張や特殊戻り値あり）。

| 番号 | 機能名 | 引数対応 (共通 `a1, a2, a3` → x86 レジスタ) | 戻り値の幅 (x86 / 共通) |
| :---: | :--- | :--- | :--- |
| 1 | putchar | a1 → `AL` | 16bit (`AL` cast) / 32bit |
| 2 | getchar | 引数なし | 16bit / 32bit |
| 3 | getticks | 引数なし | 16bit (`AX`) / 32bit |
| 4 | open | a1=path → `SI`, a2=flags → `AL` | 16bit (`0xFFFF` = fail) / 32bit (`0xFFFFFFFF`) |
| 5 | close | a1=fd → `AL` | 16bit / 32bit |
| 6 | read | a1=fd → `BX`, a2=buf → `DI`, a3=n → `CX` | 16bit (実読込数) / 32bit |
| 7 | write | a1=fd → `BX`, a2=buf → `DI`, a3=n → `CX` | 16bit (実書込数) / 32bit |
| 8 | opendir | a1=path → `SI` | 16bit (`0xFFFF` = fail) / 32bit |
| 9 | readdir | a1=dh → `BX`, a2=name → `DI`, a3=size → (-) | 16bit (1=有/0=終) / 32bit (1=ファイル/2=ディレクトリ/0) |
| 10 | closedir | a1=dh → `AL` | 16bit / 32bit |
| 11 | unlink | a1=path → `SI` | 16bit (`0xFFFF` = fail) / 32bit |
| 12 | readdir_size | 引数なし | 16bit (下位16bit) / 32bit |
| 13 | seek | (なし) | なし / 32bit |
| 14 | mkdir | a1=path → `SI` | 16bit (`0xFF` / `1` / `0`) / 32bit |
| 15 | rename | a1=old → `SI`, a2=new → `DI` | 16bit / 32bit |
| 16 | time_get | 引数なし | 32bit (`unsigned` casting Unix秒) / 32bit |
| 17 | input_ready | (なし / tzstdio.h が呼ぶが未実装) | 未実装 (常時 0xFFFF 戻り) / 32bit |
| 18 | klog | a1=msg → `SI` | 16bit / 32bit |
| 19 | df | a1=sel → `AL` | 16bit / 32bit |
| 20 | slot | a1=n → `AL` | 16bit (`0xFFFF` 判定) / 32bit (`0xFFFFFFFF`) |
| 21..34 | (各種) | なし | 未実装 / 32bit |
| 35 | con_raw | a1=on → (tzstdio.h が呼ぶが未実装) | 未実装 / 32bit |
| 36..52 | (各種) | なし | 未実装 / 32bit |

---

## 4. 特記事項・重要箇所の指摘

1. **`tzstdio.h` とカーネル側の不整合（重要）**:
   - `arch/x86-ia16/user/tzstdio.h` の `input_ready()` は `syscall5(17, ...)` を呼び、`con_raw(int on)` は `syscall5(35, (unsigned long)on, ...)` を呼び出しています。
   - しかし、`arch/x86-ia16/sysfile.c` の `sys_call` には **`case 17` と `case 35` が存在しません**。
   - そのため、これらを呼び出すコード（vi のタイムアウト付き `getc_timeout` や `kbhit` 等）を実行すると、デフォルトの `return 0xFFFF;`（または無視）となり、想定通りに動作しない潜在的な不整合があります。

2. **seek (13) の欠如**:
   - x86-ia16 カーネルには `seek` (`case 13`) がありません。`tzstdio.h` でも `ftell` / `fseek` は `-1` を返すスタブとなっています。

3. **外部 sh 用 syscall (22〜34) および高度機能 (36〜52)**:
   - ネットワーク (36〜46)、郵便受け (47〜51)、終了コード (`exitcode` 52)、パイプ末尾処理 (`pipe_tail` 21) 等は x86-ia16 アーキテクチャでは未実装です。x86 は休止中/最小限のポートであるため、シェルや高度なネットワーク機能は対象外となっています。

4. **readdir (9) の挙動の違い**:
   - 共通側は `1`(ファイル) / `2`(ディレクトリ) / `0`(終端) を返しますが、x86-ia16 の `case 9` は `1`(エントリあり) / `0`(終端) を返します（ディレクトリ種別の区別なし）。
