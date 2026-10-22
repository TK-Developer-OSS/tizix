# x86-ia16 動作確認レポート(2026-10-19、Claude)

コミット `aa74cd3` 時点(4KB ブロックのプール + 可変スロット + コンソール統合)。

## 確認した項目と結果(QEMU)

| 項目 | 結果 |
|---|---|
| ビルド(`make ARCH=x86-ia16 clean all`) | 成功。kernel.bin 40284B(予算 77312B)。警告 80 件(主に tzstdio.h の -Wcomment) |
| 回帰試験 test_interactive.py(フロッピー起動) | 3 周連続 FAIL 0 |
| 回帰試験(HDD 起動 `TIZIX_BOOT=hdd`、QEMU `-drive if=ide -boot c`) | FAIL 0 |
| キーボード入力(8042 直読み、QEMU sendkey) | 成功(test_bios_kbd.py) |
| メモリ検出(`-m 16`) | 従来 639KB / 拡張 15232KB、ブロック 127、スロット 12 |
| メモリ検出(`-m 1`) | 拡張 0KB |
| 背景ジョブ | `a &` を 10 本同時に実行、free は slots 1-11 used 11 / blocks used 22 / free 105。12 本目は「no free block」 |
| パイプ | `echo hello \| cat`、`cat /etc/rc \| head -1`、`ls \| wc`、`tail -2 /etc/rc \| head -1`、`cat /etc/rc \| tail -1` が正しい |
| vi | 起動・編集・`:wq`・画面制御(ANSI)が正しい |
| cp | 100000B のファイルが約 1.2 秒、バイト一致 |
| 他アーキのビルド(共有 src/ と common-gcc.mk の変更の影響) | m68k-mega と z80pack の `make clean all` が成功(ビルドのみ。実行試験は未実施) |

## 余裕の測定(メモリ)
- カーネルセグメント: bss 終端 0xEFA2、スタック上限 0xFF00。**カーネルスタックの最大使用 ≈ 0.3KB / 余白 ≈ 3.9KB**(スタック余白を 0x5A で埋めて、`ls`/`cat`/`cp`/パイプ/vi/背景ジョブの後に走査して測定)。
- プロセス別 syscall スタック: **最大使用 ≈ 300B / 1024B**(ファイル操作・cp・mkdir・mv・rm・free・history・du・vi の後)。

## 未確認(実施していないこと)
- **実機**: 今回のイメージ(統合コンソール・ブロックプール・可変スロット)は、R40 では動かしていない(R40 は旧イメージで確認、以後お蔵入り予定)。Eee PC 1002HA は未入手。
- **SD カード起動**(HDD 経路の実 BIOS 動作、MBR 無しの SD を BIOS が起動するか)は未確認。QEMU の IDE ディスク起動のみ。
- **他アーキの実行試験**(z80pack の回帰、m68k-mega の動作)は未実施。ビルドのみ確認。
- **長時間の連続運転**(耐久)は未実施。
- スワップ(拡張メモリへの退避)は未実装(将来)。

## 既知の制限
- プロセススロットは最大 12(`X86_MAXSLOT`)。ブロック総数は従来メモリ(640KB)の範囲(`-m` を増やしても変わらない)。
- 実機で `vi` を抜けたあとの行頭の桁ずれ(#184)は QEMU で再現できず、LF を CR+LF として扱う防御を入れたが実機未確認。
