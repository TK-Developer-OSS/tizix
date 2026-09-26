#!/usr/bin/env python3
# sched_harness.py - tizix scheduler regression harness (kosarev z80)
#
#   cpmsim では「out 中に SIGALRM(タイマ) が割り込む」ホスト I/O アーティファクト
#   で切替ISRが域外PCへ飛ぶことがある(実機では起きない)。kosarev は OUT が
#   Python コールバック=host syscall 無しなのでこの現象が原理的に出ない。
#   よってスケジューラ本体の健全性(切替でPCが域外へ飛ばないこと)をここで確認する。
#
#   usage: python3 sched_harness.py [kernel.ihx] [rand|dense] [N]
#   PASS 条件: WILD PC が一度も出ないこと。
import sys, random, z80

IHX  = sys.argv[1] if len(sys.argv) > 1 else 'kernel.ihx'
MODE = sys.argv[2] if len(sys.argv) > 2 else 'rand'
N    = int(sys.argv[3]) if len(sys.argv) > 3 else 200000

# ---- load Intel HEX ----
img = bytearray(0x10000)
for line in open(IHX):
    line = line.strip()
    if not line.startswith(':'): continue
    n = int(line[1:3],16); addr = int(line[3:7],16); typ = int(line[7:9],16)
    if typ == 0:
        img[addr:addr+n] = bytes(int(line[9+2*i:11+2*i],16) for i in range(n))

m = z80.Z80Machine(); m.set_memory_block(0, bytes(img)); m.pc = 0x0100

console = bytearray(); timer = [False]
m.set_input_callback(lambda p: 0)         # CON_STAT=0 -> shell idles polling at prompt
def out(a, v):
    p = a & 0xFF
    if   p == 0x01: console.append(v & 0xFF)   # CON_DATA
    elif p == 0x1B: timer[0] = True            # timer enable (out (27),1)
m.set_output_callback(out)

# 有効PC域: カーネルコード(0x0100-0x0C00) と ISR トランポリン(0x0038)
def valid(pc): return (0x0100 <= pc < 0x0C00) or (0x0038 <= pc <= 0x003B)

random.seed(1)
ints = 0; from_pc = {}
for t in range(N):
    m.ticks_to_stop = 1 if MODE == 'dense' else random.choice([1,3,7,13,29,61,127])
    m.run()
    if m.halted:
        print('HALT at tick %d pc=0x%04X' % (t, m.pc)); break
    if not valid(m.pc):
        print('*** WILD PC 0x%04X tick %d ints=%d sp=0x%04X iy=0x%04X ix=0x%04X'
              % (m.pc, t, ints, m.sp, m.iy, m.ix)); break
    if timer[0]:
        pc_before = m.pc                       # 割り込まれた地点(受理前)
        if m.on_handle_active_int():
            ints += 1; from_pc[pc_before] = from_pc.get(pc_before, 0) + 1
else:
    print('PASS: mode=%s chunks=%d ints=%d, interrupted at %d distinct PCs'
          % (MODE, N, ints, len(from_pc)))
    top = sorted(from_pc.items(), key=lambda x: -x[1])[:8]
    print('  hot interrupted-from PCs:', ['0x%04X:%d' % (a,c) for a,c in top])

txt = bytes(console).decode('ascii', 'replace')
print('--- console (%d bytes) ---' % len(console)); print(txt[:200])
