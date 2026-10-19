# x86-ia16: asm が ES/DS を壊していないかの監査(2026-10-18、Claude)

背景: ia16-gcc は ES/DS を値の置き場に使い、関数呼び出しをまたいで保持する前提(呼ばれた側が保存する)。
`bios13_rw` が ES を壊して戻ったため、FatFs の `f_write` が ES に置いた `bw` ポインタが 0x1000 に化け、
カーネル 0x1000 番地を書き換えていた(#182、修正済み)。同種の問題を grep で全数確認した
(Gemini に依頼したがレート制限で停止したため Claude が実施)。

| ルーチン / asm | ES/DS を変更 | 戻る前に復帰 | 判定 |
|---|---|---|---|
| proc.s `farcpy` | ES | push/pop es | 問題なし |
| proc.s `farcpy_in` | DS, ES | push/pop ds, es | 問題なし |
| bios13.s `bios13_rw` | ES(=DS) | push/pop es(**今回追加**) | 修正済み |
| bios13.s `bios13_reset` | なし | — | 問題なし |
| console_vga.c `vwrite` / `vread` | ES | push/pop es | 問題なし |
| console_vga.c `vscroll` | DS, ES | push/pop ds, es | 問題なし |
| crt0.s `_isr08` / `_isr80` | DS, ES | 割り込みフレームから pop | 問題なし(sys_call は DS=ES=KSEG で呼ぶ) |
| crt0.s `_start`、stage1.s、stage2.s | DS, ES | 起動時のみ(戻らない) | 対象外 |
| user/usyscall.s、user/crt0cmd.s | なし | — | 問題なし |

結論: 残る問題は 0 件。今後 asm を足すときは **ES/DS を変えたら必ず元に戻す**(BIOS 呼び出しの前に ES を張る場合も、戻る前に復帰)。
