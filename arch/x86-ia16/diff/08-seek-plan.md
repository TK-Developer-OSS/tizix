# arch/x86-ia16 移行: syscall 13(seek) 実装計画

## 1. 概要
x86-ia16 アーキテクチャにおけるシステムコール 13 (`seek(fd, offset, whence)`) の実装に向けた調査と設計文書。
共通カーネル側 (`src/sysfile.c` case 13) では既に `fsb_seek` / `fsb_size` / `kdev_seek` を用いた実装が存在しているが、x86-ia16 側の `arch/x86-ia16/sysfile.c` および `arch/x86-ia16/user/tzstdio.h` では未実装（スタブで `-1` を返却）となっているため、これを移行・実装する。

## 2. 必要な変更箇所の一覧 (ファイル・関数単位)
1. **`arch/x86-ia16/sysfile.c` (`sys_call` 関数内)**
   - `case 13:` の追加。
   - 引数処理: `BX` (fd), `CX` (offset または offset 下位), `AL` (whence), 必要に応じて `SI` (offset 上位)。
   - 生デバイス (`ufd_kind[u] == 3`) のシーク (`kdev_seek`) および FAT ファイル (`ufd_kind[u] == 1`) のシーク (`fsb_seek` / `fsb_size`) の処理を共通 `src/sysfile.c` と同様に実装。
2. **`arch/x86-ia16/user/tzstdio.h`**
   - `fseek` スタブの修正: `syscall5(13, ...)` を呼び出してシークを実行するように変更。
   - `ftell` / `fseek` の連携 (`f->pos` の更新)。

## 3. 32bit オフセットの 16bit レジスタ渡し設計 (2案と各長所短所)
x86-ia16 の syscall ABI (`crt0.s` / `usyscall.s`) を変更しない前提 (`_real_syscall5(func, al, bx, cx, si, di, cseg)` を使用) で、32bit の `offset` をどう渡すかの 2 案:

### 案 A: レジスタ分割方式 (CX = offset下位16bit, SI = offset上位16bit, BX = fd, AL = whence)
- **レジスタ割り当て**:
  - `BX` = `fd`
  - `CX` = `offset` の下位 16bit (`(unsigned)offset`)
  - `SI` = `offset` の上位 16bit (`(unsigned)(offset >> 16)`)
  - `AL` = `whence` (`SEEK_SET(0)`, `SEEK_END(2)` など)
- **長所**:
  - 4GB までのフル 32bit ファイルサイズ・オフセットを完全にサポート可能。
  - 将来の大きなファイルサイズに対応しやすい。
- **短所**:
  - 16bit レジスタを 2 つ (CX, SI) 消費するため、他の引数に使えるレジスタが減る。
  - ユーザー側 (`tzstdio.h` の `fseek`) でのレジスタ分割・結合コードがやや複雑になる。

### 案 B: 16bit オフセット限定方式 (CX = offset 16bit, BX = fd, AL = whence)
- **レジスタ割り当て**:
  - `BX` = `fd`
  - `CX` = `offset` (16bit, 符号付き/符号なし ~64KB)
  - `AL` = `whence`
- **長所**:
  - 既存の `read` / `write` (`CX` = `n`) と同様の 1 レジスタ渡しで完結し、実装が極めてシンプル。
  - tizix の全コマンドおよび既存ファイルシステムのエコシステムは 64KB 未満のファイルを主対象としているため、実用上十分。
- **短所**:
  - 64KB を超えるファイルへのシークや巨大オフセット指定が原理的に不可能 (切り捨てられるかエラーになる)。

## 4. 試験案 (seek を使うコマンドによる確認)
1. **`du` コマンド (`user/du.c`)**:
   - 実行: `du <filename>`
   - 検証内容: `fseek(fp, 0L, SEEK_END)` と `ftell(fp)` を組み合わせてファイルサイズを取得・表示する動作が正しく成功することを確認。
2. **テストプログラム (`user/test1.c`)**:
   - 検証内容: `fseek(f1, 0, SEEK_SET)` および `ftell` による位置確認、その後の `fgetc` による読み直しが正しく機能することを確認。
