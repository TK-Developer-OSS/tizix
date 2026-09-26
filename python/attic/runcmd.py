#!/usr/bin/env python3
# runcmd.py ― tizix 外部コマンドをホスト上の Z80 で実行し出力を捕捉する。
#
#   実機/cpmsim 無しで、コマンド .bin の「ロード時 reloc + 実行 + I/O」を
#   ホストで再現する切り分け用ハーネス。カーネル入口(kputchar/time_get 等)は
#   スタブで代替し、DRIVER.BIN は実物を 0x9000 に載せる。
#
#   これで「iy_reg の reloc は正しいか」「drv_printf に値が届くか」を、
#   ボードや UART 抜きで機械的に確認できる。化けが出れば .bin 段階で
#   再現=コマンド側の問題、出なければカーネル統合(実 kputchar/IPL/ブロック
#   ロード)側の問題、と原因を切り分けられる。
#
#   依存: pip install z80   (Z80 命令エミュレータ)
#
#   使い方:
#     python3 runcmd.py date_dbg.bin                 # driver.bin 自動使用
#     python3 runcmd.py date_dbg.bin --epoch 1700000000
#     python3 runcmd.py a.bin --max-out 20           # 無限ループcoマンドは出力上限で停止
#     python3 runcmd.py hello.bin --driver driver.bin --base 0x2000
#
#   前提(crt0cmd.s / kexec.c と一致):
#     * コマンドは 0x0000 基準リンク。ブロック base へロードし iy=base、
#       base+0x20 を call。
#     * ロード時 reloc は現状 RELOC_OFF=0x29(call _main operand)に base 加算
#       のみ(kexec_file と同じ。ここが本丸の検証点)。
#     * カーネル入口(ISR 直後): 0x003B kexit / 0x003E kputchar /
#       0x0041 kgetchar / 0x0044 getticks / 0x0047 kprintf /
#       0x004A time_get / 0x004D time_set
#     * DRIVER(block1) 固定入口: 0x9000 putc / 0x9010 getc / 0x9020 printf
import sys, os, argparse, datetime

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmdbin")
    ap.add_argument("--driver", default="driver.bin")
    ap.add_argument("--base", default="0x2000")
    ap.add_argument("--epoch", type=int, default=1700000000)
    ap.add_argument("--reloc-off", default="0x29",
                    help="ロード時に base 加算する 16bit フィールドのオフセット(kexec RELOC_OFF)")
    ap.add_argument("--no-reloc", action="store_true", help="reloc を一切適用しない(生ロード)")
    ap.add_argument("--max-out", type=int, default=4096, help="出力バイト上限(無限ループ対策)")
    ap.add_argument("--max-steps", type=int, default=8000000)
    ap.add_argument("--raw", action="store_true", help="repr でなく生出力を表示")
    args = ap.parse_args()

    try:
        import z80
    except ImportError:
        sys.exit("z80 エミュレータが無い: pip install z80")

    BASE = int(args.base, 0)
    m = z80.Z80Machine(); mem = m.memory
    def w(a,b): mem[a] = b & 0xFF
    def ww(a,v): mem[a] = v & 0xFF; mem[a+1] = (v>>8) & 0xFF

    img = open(args.cmdbin, "rb").read()
    for i,b in enumerate(img): mem[BASE+i] = b

    if not args.no_reloc:
        # kexec_file と同じ: mkreloc packed(個数>0)なら全テーブル適用、
        #   さもなくば旧 iy_reg bin として単一 0x29 reloc にフォールバック。
        cnt    = mem[BASE+0] | mem[BASE+1]<<8
        tbloff = mem[BASE+2] | mem[BASE+3]<<8
        if cnt != 0 and 0x20 <= tbloff < 0x1000:
            for i in range(cnt):
                fo = mem[BASE+tbloff+2*i] | mem[BASE+tbloff+2*i+1]<<8
                a  = BASE+fo
                ww(a, (mem[a] | mem[a+1]<<8) + BASE)
            sys.stderr.write(f"[runcmd] reloc: mkreloc table {cnt} fields\n")
        else:
            off = BASE + int(args.reloc_off, 0)
            ww(off, (mem[off] | mem[off+1]<<8) + BASE)
            sys.stderr.write("[runcmd] reloc: legacy single 0x29\n")

    if os.path.exists(args.driver):
        drv = open(args.driver, "rb").read()
        for i,b in enumerate(drv): mem[0x9000+i] = b
    else:
        sys.stderr.write(f"注意: {args.driver} が無い。printf/putchar は動かない\n")

    # カーネル入口ベクタ → 高位のトラップ番地へ jp
    TRAP = {0x003B:0xF000, 0x003E:0xF010, 0x0041:0xF020, 0x0044:0xF030,
            0x0047:0xF050, 0x004A:0xF040, 0x004D:0xF060}
    for v,t in TRAP.items():
        w(v,0xC3); ww(v+1,t); w(t,0x76)          # jp t ; t: halt(トラップ印)
    RET_HALT = 0xF0F0; w(RET_HALT,0x76)
    for t in list(TRAP.values())+[RET_HALT]:
        m.set_breakpoint(t)

    out = bytearray()
    ticks = [0]
    m.iy = BASE; m.sp = 0xFFFE
    m.sp = (m.sp-2)&0xFFFF; mem[m.sp]=RET_HALT&0xFF; mem[m.sp+1]=RET_HALT>>8
    m.pc = BASE + 0x20

    def do_ret():
        lo=mem[m.sp]; hi=mem[m.sp+1]; m.sp=(m.sp+2)&0xFFFF; m.pc=lo|hi<<8

    steps = 0
    reason = "kexit"
    while True:
        m.run(); pc = m.pc; steps += 1
        if steps > args.max_steps: reason="max-steps"; break
        if pc == 0xF010:                              # kputchar: int 引数は HL で受ける
            out.append(m.hl & 0xFF)                    #   (実 kputchar と一致。A 読みは ABI バグを隠す)
            if len(out) >= args.max_out: reason="max-out"; break
            do_ret(); continue
        if pc == 0xF040:                              # time_get → HL=high, DE=low
            m.hl=(args.epoch>>16)&0xFFFF; m.de=args.epoch&0xFFFF; do_ret(); continue
        if pc == 0xF060:                              # time_set: nop
            do_ret(); continue
        if pc == 0xF030:                              # getticks → 増加カウンタ(HL)
            ticks[0]=(ticks[0]+10)&0xFFFF; m.hl=ticks[0]; do_ret(); continue
        if pc == 0xF020:                              # kgetchar → 0
            m.hl=0; do_ret(); continue
        if pc == 0xF050:                              # kprintf: 未対応(素通し)
            do_ret(); continue
        if pc in (0xF000, RET_HALT): reason="kexit"; break

    txt = out.decode("latin1")
    sys.stderr.write(f"[runcmd] stop={reason} steps={steps} out={len(out)}B base={hex(BASE)} epoch={args.epoch}\n")
    if args.raw:
        sys.stdout.write(txt)
    else:
        print(repr(txt))
        print("---")
        print(txt.replace("\r","<CR>").replace("\n","<LF>\n"), end="")

if __name__ == "__main__":
    main()
