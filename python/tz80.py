#!/usr/bin/env python3.11
"""tz80 - 単一命令ステップ Z80 デバッガ(cpmsim 互換)。仕様: doc/tz80.md

CPU コアは kosarev の `z80` モジュール(python3.11 必須)。tz80 は cpmsim の
デバイス(コンソール port 0/1、FDC port 10-16、タイマ port 27 + IM1)を再現し、
状態をファイルに永続化して「1 プロセス = 1 命令」で駆動する。

  python3.11 python/tz80.py reset
  python3.11 python/tz80.py run --until 0x0100
  python3.11 python/tz80.py step        # 1 命令
  python3.11 python/tz80.py int         # 次ステップ冒頭で ISR 注入
  python3.11 python/tz80.py save booted / load booted
  python3.11 python/tz80.py mem 0x8400 128
  python3.11 python/tz80.py regs / stack / trace 40
"""
import os, sys, json, shutil, time
from collections import deque

import z80

HERE   = os.path.dirname(os.path.abspath(__file__))
STATE  = os.path.join(HERE, "tz80_state")
DISKS  = os.path.abspath(os.path.join(HERE, "..", "arch", "z80pack", "disks"))
BLOB   = os.path.join(STATE, "state.bin")     # 完全状態(regs+mem)。source of truth
REGS   = os.path.join(STATE, "regs.txt")      # 人間可読ビュー(編集可 → step 冒頭で反映)
MEMHEX = os.path.join(STATE, "mem.hex")
IOF    = os.path.join(STATE, "io.json")
TRACE  = os.path.join(STATE, "trace.log")
COUT   = os.path.join(STATE, "console.out")
SCREEN = os.path.join(STATE, "screen.log")   # 画面表示 + 全レジスタを ==== 区切りで蓄積
WATCH  = os.path.join(STATE, "watch.txt")
SNAP   = os.path.join(STATE, "snap")
DA_IMG = os.path.join(STATE, "drivea.img")    # セッション内の可変ディスクイメージ
DB_IMG = os.path.join(STATE, "driveb.img")

SPT       = 26          # cpmsim フロッピー sectors/track (128B)
SECSZ     = 128
FLOPPY_SZ = 77 * SPT * SECSZ

# state blob 内レジスタ offset(probe で確認。little-endian 16bit)
RO = {"bc":0, "de":2, "hl":4, "af":6, "pc":8, "sp":10,
      "ix":20, "iy":22, "alt_bc":24, "alt_de":26, "alt_hl":28}
MEM_OFF = 40           # 64KB メモリはここから

# --- ざっくり T-state テーブル(タイマ周期の目安。厳密不要) ---
def tstates(op0, op1):
    if op0 in (0xCD,):                 return 17   # call nn
    if op0 in (0xC9,):                 return 10   # ret
    if op0 in (0xC3,):                 return 10   # jp nn
    if op0 in (0x18,):                 return 12   # jr
    if 0x76 == op0:                    return 4
    if op0 in (0xED,):                 return 16
    if op0 in (0xDD, 0xFD):            return 12
    if op0 in (0xC5,0xD5,0xE5,0xF5):   return 11   # push
    if op0 in (0xC1,0xD1,0xE1,0xF1):   return 10   # pop
    if 0xB8 <= op0 <= 0xBF or op0 == 0xFE: return 4
    return 8

TIMER_PERIOD_DEFAULT = 40000   # ~4MHz / 100Hz

# cpmsim が「op-code trap」を出すバイト列(z80 モジュールは nop 扱いするものも含む)
def is_trap_opcode(b):
    # 未定義 DD/FD/ED プレフィクス系。厳密でなくてよい(ワイルドジャンプ検出用)
    if len(b) >= 2 and b[0] in (0xDD, 0xFD):
        if b[1] in (0x00, 0xDD, 0xFD, 0xED, 0xC9, 0x76, 0xD9):
            return True
    if len(b) >= 2 and b[0] == 0xED:
        if b[1] in (0x2B, 0x93, 0x00, 0x77, 0x7F, 0xFF):
            return True
    return False


def default_io():
    return {
        "cycles": 0, "ticks": 0, "last_tick_cycles": 0,
        "int_pending": 0, "tick_pending": 0,
        "timer_on": 0, "timer_auto": 1, "timer_period": TIMER_PERIOD_DEFAULT,
        "ei_armed": 0,                 # EI 実行直後は割込を 1 命令遅延
        "fdc": {"drive": 0, "track": 0, "sector": 1, "dmal": 0, "dmah": 0, "status": 0},
        "con_in": "",                  # コンソール入力キュー
        "steps": 0,
        "commit_disk": 0,
        # --- client socket #1 (port 50/51) の簡易シミュレーション ---
        #   実 TCP は張らない。iosim.c の ATD dial-on-demand + cs_port クリア済み
        #   挙動(2026-09-17 修正後)を模して、#51 のクラッシュ(net常駐下での
        #   非決定的なワイルドジャンプ)を tz80 上で再現するためのもの。
        "net_cs": 0,        # 0=未接続(ATD待ち) / 1=接続中
        "net_atbuf": "",    # 未接続時に netd1_out へ書かれたバイトを行バッファ
        "net_rx": "",       # host→guest として読ませたい未消費バイト列(console netrx で注入)
        "net_tx_log": "",   # guest→host に書かれたバイト列(観測用、netd1_out 経由)
        "net_hangup_after": 0,   # >0 なら、この回数だけ net_tx_log にバイトが書かれた後
                                  # 自動的に POLLHUP を模して net_cs=0 に戻す(#51 再現用)
        # --- スタック最深トラッカ(ワイルドジャンプ調査用)---
        "min_sp": 0x10000,   "min_sp_pc": 0,   "min_sp_iy": 0,   "min_sp_step": 0,
        "min_sp_k": 0x10000, "min_sp_k_pc": 0, "min_sp_k_step": 0,   # IY==0(カーネル文脈)限定
        "max_pdepth": -1, "max_pdepth_iy": 0, "max_pdepth_sp": 0,    # IY!=0(プロセス文脈)ブロック内最深
        "max_pdepth_pc": 0, "max_pdepth_step": 0, "max_pdepth_frombase": 0,
    }


# ============================================================ Machine wrapper

class Sim:
    def __init__(self):
        self.m = z80.Z80Machine()
        self.io = default_io()
        self.disk = {0: bytearray(FLOPPY_SZ), 1: bytearray(FLOPPY_SZ)}
        self.ib = z80.Z80InstrBuilder()
        self._last_out_char = None
        self._tbuf = []
        self._ring = deque(maxlen=400)     # 直近の (step, addr, irq)。quiet でも保持
        self.m.set_output_callback(self._out)
        self.m.set_input_callback(self._in)

    # ---- device callbacks -------------------------------------------------
    def _out(self, port, value):
        p = port & 0xFF
        v = value & 0xFF
        f = self.io["fdc"]
        if p == 1:                                   # console data out
            ch = chr(v) if 0 <= v < 256 else "?"
            with open(COUT, "a", encoding="latin-1") as fp:
                fp.write(ch)
            sys.stdout.write(ch)
            sys.stdout.flush()
            self._last_out_char = v
        elif p == 10: f["drive"] = v & 1
        elif p == 11: f["track"] = v
        elif p == 12: f["sector"] = v
        elif p == 15: f["dmal"] = v
        elif p == 16: f["dmah"] = v
        elif p == 13:                                # FDC op -> DMA trigger
            self._dma(v)
        elif p == 27:
            self.io["timer_on"] = 1 if v else 0
        elif p == 51:                                 # netd1_out: client socket #1 data
            self._net_out(v)
        # 他ポートは無視

    def _net_out(self, v):
        io = self.io
        ch = chr(v) if 0 <= v < 256 else "?"
        if not io["net_cs"]:
            # 未接続: ATD 行バッファへ(iosim.c netd1_out と同じ規約)
            if ch == "\r":
                return
            if ch == "\n":
                line = io["net_atbuf"]
                io["net_atbuf"] = ""
                if line.startswith("ATD"):
                    io["net_cs"] = 1     # net_client_connect() 相当。常に成功する簡易版
            else:
                io["net_atbuf"] += ch
        else:
            io["net_tx_log"] += ch
            if io["net_hangup_after"] and len(io["net_tx_log"]) >= io["net_hangup_after"]:
                # 実機の POLLHUP 検出(2026-09-17 修正後: cs_port もクリア)を模す
                io["net_cs"] = 0
                io["net_atbuf"] = ""

    def _in(self, port):
        p = port & 0xFF
        if p == 0:                                   # console status
            return 0xFF if self.io["con_in"] else 0x00
        if p == 1:                                   # console data
            q = self.io["con_in"]
            if not q:
                return 0
            self.io["con_in"] = q[1:]
            return ord(q[0]) & 0xFF
        if p == 14:                                  # FDC status
            return self.io["fdc"]["status"] & 0xFF
        if p == 50:                                   # nets1_in: client socket #1 status
            io = self.io
            status = 0
            if io["net_cs"]:
                if io["net_rx"]:
                    status |= 1
                status |= 2                            # 簡易版: 接続中は常に書込可
            else:
                status |= 2                            # 未接続中も ATD 受付のため書込可(iosim.c仕様)
            return status
        if p == 51:                                    # netd1_in: client socket #1 data
            io = self.io
            q = io["net_rx"]
            if not q:
                return 0
            io["net_rx"] = q[1:]
            return ord(q[0]) & 0xFF
        return 0

    def _dma(self, op):
        f = self.io["fdc"]
        drv = f["drive"] & 1
        lin = f["track"] * SPT + (f["sector"] - 1)
        off = lin * SECSZ
        dma = ((f["dmah"] & 0xFF) << 8) | (f["dmal"] & 0xFF)
        if off < 0 or off + SECSZ > FLOPPY_SZ or dma + SECSZ > 0x10000:
            f["status"] = 1
            return
        img = self.disk[drv]
        mem = self.m.memory
        if op == 0:                                  # read: disk -> mem
            mem[dma:dma + SECSZ] = img[off:off + SECSZ]
        else:                                        # write: mem -> disk(image)
            img[off:off + SECSZ] = bytes(mem[dma:dma + SECSZ])
        f["status"] = 0

    # ---- register access via state blob --------------------------------
    def _blob(self):
        return self.m.get_state_view()

    def get_reg(self, name):
        return getattr(self.m, name) if hasattr(self.m, name) else None

    def set_reg(self, name, val):
        if name in RO:
            sv = self._blob()
            o = RO[name]
            sv[o] = val & 0xFF
            sv[o + 1] = (val >> 8) & 0xFF
        elif hasattr(self.m, name):
            setattr(self.m, name, val)
        else:
            raise KeyError(name)

    # ---- persistence -----------------------------------------------------
    def load(self):
        os.makedirs(STATE, exist_ok=True)
        if os.path.exists(BLOB):
            data = open(BLOB, "rb").read()
            self._blob()[:] = data[:65576]
        if os.path.exists(IOF):
            loaded = json.load(open(IOF))
            merged = default_io()
            merged.update(loaded)
            self.io = merged
        for k, path in ((0, DA_IMG), (1, DB_IMG)):
            if os.path.exists(path):
                d = open(path, "rb").read()
                self.disk[k][:len(d)] = d
        # regs.txt が blob より新しければ手編集を反映
        if os.path.exists(REGS) and os.path.exists(BLOB):
            if os.path.getmtime(REGS) > os.path.getmtime(BLOB):
                self._apply_regs_txt()

    def save(self):
        os.makedirs(STATE, exist_ok=True)
        self.flush_trace()
        open(BLOB, "wb").write(bytes(self._blob()))
        json.dump(self.io, open(IOF, "w"), indent=1)
        if self.io.get("commit_disk"):
            open(DA_IMG, "wb").write(bytes(self.disk[0]))
            open(DB_IMG, "wb").write(bytes(self.disk[1]))
        else:
            open(DA_IMG, "wb").write(bytes(self.disk[0]))
            open(DB_IMG, "wb").write(bytes(self.disk[1]))
        self._write_regs_txt()

    def _apply_regs_txt(self):
        # レジスタ値(RO + PC/SP/AF...)は常に 16 進。メタ(CYCLES 等)は 10 進。
        META = {"cycles": "cycles", "ticks": "ticks", "int_pending": "int_pending",
                "timer_auto": "timer_auto", "timer_period": "timer_period"}
        for line in open(REGS):
            line = line.split("#", 1)[0].strip()
            if not line or "=" not in line:
                continue
            k, v = line.split("=", 1)
            k = k.strip().lower()
            v = v.strip()
            if k in RO:
                self.set_reg(k, int(v, 16))
            elif k == "halted":
                self.m.halted = bool(int(v, 16))
            elif k in META:
                try:
                    self.io[META[k]] = int(v, 10)
                except ValueError:
                    pass

    def _write_regs_txt(self):
        m = self.m
        L = []
        L.append("PC=%04X" % m.pc)
        L.append("SP=%04X" % m.sp)
        L.append("AF=%04X" % m.af)
        L.append("BC=%04X" % m.bc)
        L.append("DE=%04X" % m.de)
        L.append("HL=%04X" % m.hl)
        L.append("IX=%04X" % m.ix)
        L.append("IY=%04X" % m.iy)
        L.append("ALT_BC=%04X" % m.alt_bc)
        L.append("ALT_DE=%04X" % m.alt_de)
        L.append("ALT_HL=%04X" % m.alt_hl)
        L.append("HALTED=%d" % (1 if m.halted else 0))
        L.append("INT_DISABLED=%d   # IFF1(読取専用)" % (1 if m.int_disabled else 0))
        L.append("# --- 実行制御(レジスタではない) ---")
        L.append("CYCLES=%d" % self.io["cycles"])
        L.append("TICKS=%d" % self.io["ticks"])
        L.append("INT_PENDING=%d" % self.io["int_pending"])
        L.append("TIMER_AUTO=%d" % self.io["timer_auto"])
        L.append("TIMER_PERIOD=%d" % self.io["timer_period"])
        L.append("STEPS=%d" % self.io["steps"])
        fdc = self.io["fdc"]
        L.append("# FDC drive=%d track=%d sector=%d dma=%04X status=%d" %
                 (fdc["drive"], fdc["track"], fdc["sector"],
                  (fdc["dmah"] << 8) | fdc["dmal"], fdc["status"]))
        open(REGS, "w").write("\n".join(L) + "\n")

    # ---- disasm --------------------------------------------------------
    def disasm(self, addr):
        raw = bytes(self.m.memory[addr:addr + 4])
        try:
            ins = self.ib.build_instr(addr, raw)
            return raw[:ins.size], str(ins), type(ins).__name__
        except Exception:
            return raw[:1], "db 0x%02X" % raw[0], "?"

    # ---- interrupt ---------------------------------------------------
    def _maybe_irq(self):
        io = self.io
        want = io["int_pending"] or io["tick_pending"]
        if not want:
            return None
        if self.m.int_disabled:
            return None            # IFF=0: 保留
        pushed_from = self.m.pc
        if self.m.halted:
            self.m.halted = False
        fired = self.m.on_handle_active_int()
        if fired:
            tag = ">>> INT tick %d  (push %04X -> pc 0x0038)" % (io["ticks"], pushed_from)
            io["int_pending"] = 0
            io["tick_pending"] = 0
            self._trace(tag)
            return tag
        return None

    def _trace(self, line):
        self._tbuf.append(line)
        if len(self._tbuf) >= 20000:
            self.flush_trace()

    def flush_trace(self):
        if self._tbuf:
            with open(TRACE, "a") as fp:
                fp.write("\n".join(self._tbuf) + "\n")
            self._tbuf = []

    def dump_ring(self, n=120):
        """直近 n 命令を逆アセンブル付きで返す(quiet run 後の pre-crash 追跡)。"""
        out = []
        for s, a, irq in list(self._ring)[-n:]:
            raw, text, _ = self.disasm(a)
            out.append("#%08d  %04X: %-10s %-22s%s" %
                       (s, a, raw.hex().upper(), text, "   <<IRQ" if irq else ""))
        return "\n".join(out)

    # ---- one step -----------------------------------------------------
    def step(self, trace=True):
        io = self.io
        irq = self._maybe_irq()

        addr = self.m.pc
        mem = self.m.memory
        op0 = mem[addr]
        op1 = mem[(addr + 1) & 0xFFFF]

        self.m.ticks_to_stop = 1
        self.m.run()
        # kosarev z80 は ticks_to_stop で「プレフィクスバイト直後」でも停止する。
        # プレフィクス始まりの命令なら、PC が命令末尾/分岐先に達するまで進める。
        if op0 in (0xDD, 0xFD, 0xED, 0xCB):
            try:
                isize = self.ib.build_instr(addr, bytes(mem[addr:addr + 4])).size
            except Exception:
                isize = 2
            guard = 0
            while addr < self.m.pc < addr + isize and guard < 4:
                self.m.ticks_to_stop = 1
                self.m.run()
                guard += 1
        io["steps"] += 1
        io["cycles"] += tstates(op0, op1)
        self._ring.append((io["steps"], addr, 1 if irq else 0))

        # --- スタック最深トラッカ(SP 未初期化 0x0000 やベクタ域は除外)---
        sp = self.m.sp
        if sp >= 0x8000:
            if sp < io.get("min_sp", 0x10000):
                io["min_sp"] = sp; io["min_sp_pc"] = addr
                io["min_sp_iy"] = self.m.iy; io["min_sp_step"] = io["steps"]
            # カーネル文脈(IY==0)かつカーネルスタック帯(0x8000-0x9000)限定
            if self.m.iy == 0 and sp <= 0x9000 and sp < io.get("min_sp_k", 0x10000):
                io["min_sp_k"] = sp; io["min_sp_k_pc"] = addr
                io["min_sp_k_step"] = io["steps"]
        # --- プロセス文脈(IY!=0)のブロック内スタック最深: block base からの距離を追う ---
        iy = self.m.iy
        if iy >= 0x8000 and sp >= iy and sp < iy + 0x1000:
            depth = (iy + 0x1000) - sp            # ブロック頂上(base+0x1000)からの深さ
            if depth > io.get("max_pdepth", -1):
                io["max_pdepth"] = depth
                io["max_pdepth_iy"] = iy
                io["max_pdepth_sp"] = sp
                io["max_pdepth_pc"] = addr
                io["max_pdepth_step"] = io["steps"]
                io["max_pdepth_frombase"] = sp - iy   # base からの余白(小さいほど code に接近)

        if io["timer_auto"] and io["timer_on"]:
            per = io["timer_period"] or TIMER_PERIOD_DEFAULT
            while io["cycles"] - io["last_tick_cycles"] >= per:
                io["last_tick_cycles"] += per
                io["ticks"] += 1
                io["tick_pending"] = 1

        if trace:
            raw, text, kind = self.disasm(addr)
            rec = "#%08d  %04X: %-11s %-22s  T%d tick%d%s" % (
                io["steps"], addr, raw.hex().upper(), text,
                io["cycles"], io["ticks"], "   <<IRQ" if irq else "")
            self._trace(rec)
            return addr, raw, text, kind, irq, rec
        return addr, None, None, None, irq, None

    # ---- watch checks -----------------------------------------------
    def mem_watch_ranges(self):
        rs = []
        for spec in read_watch():
            t = spec.split()
            if t and t[0] == "mem" and len(t) >= 3:
                rs.append((int(t[1], 16), int(t[2], 16)))
        return rs

    def snap_ranges(self, ranges):
        return {r: bytes(self.m.memory[r[0]:r[1]]) for r in ranges}

    def check_watch(self, prev):
        hits = []
        for spec in read_watch():
            h = self._eval_watch(spec, prev)
            if h:
                hits.append("%s : %s" % (spec, h))
        return hits

    def _eval_watch(self, spec, prev):
        t = spec.split()
        m = self.m
        if not t:
            return None
        if t[0] == "mem" and len(t) >= 3:
            lo = int(t[1], 16); hi = int(t[2], 16)
            old = (prev or {}).get((lo, hi))
            if old is not None:
                cur = bytes(m.memory[lo:hi])
                if cur != old:
                    for i in range(len(cur)):
                        if cur[i] != old[i]:
                            return "write @%04X %02X->%02X" % (lo + i, old[i], cur[i])
            return None
        if t[0] == "sp":
            op = t[1]; val = int(t[2], 16)
            if op == "<" and m.sp < val:
                return "SP=%04X < %04X" % (m.sp, val)
            if op == ">" and m.sp > val:
                return "SP=%04X > %04X" % (m.sp, val)
            return None
        if t[0] == "pc-range":
            pc = m.pc
            ok = (0x0100 <= pc < 0x8000) or (0x9000 <= pc < 0x10000)
            return None if ok else ("PC=%04X out of code range" % pc)
        if t[0] == "pc-datazone":
            # カーネル DATA/スタック帯(0x8000-0x8FFF)で命令実行 = ワイルドジャンプ
            if 0x8000 <= m.pc < 0x9000:
                return "PC=%04X in DATA/stack zone" % m.pc
            return None
        if t[0] == "sp-deep":
            thr = int(t[1], 16)
            if m.sp < thr:
                return "SP=%04X < %04X (IY=%04X PC=%04X)" % (m.sp, thr, m.iy, m.pc)
            return None
        if t[0] == "opcode-illegal":
            raw = bytes(m.memory[m.pc:m.pc + 3])
            return ("illegal opcode %s @%04X" % (raw.hex(), m.pc)) if is_trap_opcode(raw) else None
        if t[0] == "halt-with-di":
            if m.halted and m.int_disabled:
                return "HALT while IFF=0 @%04X" % m.pc
            return None
        return None


# ============================================================ helpers

def read_watch():
    if not os.path.exists(WATCH):
        return []
    out = []
    for line in open(WATCH):
        line = line.split("#", 1)[0].strip()
        if line:
            out.append(line)
    return out


FLAGBITS = "SZ5H3PNC"
def fmt_flags(f):
    return " ".join("%s%d" % (FLAGBITS[i], (f >> (7 - i)) & 1) for i in range(8))

SEP = "=" * 50


def log_screen(text):
    with open(SCREEN, "a", encoding="latin-1") as fp:
        fp.write("\n" + SEP + "\n" + text.rstrip("\n") + "\n")


def fmt_show(sim, addr, raw, text, kind, irq, rec):
    """画面/ログ共通の 1 ブロックを文字列で返す。"""
    m = sim.m
    L = []
    if irq:
        L.append(irq)
    if rec:
        L.append(rec)
    L.append("  AF=%04X BC=%04X DE=%04X HL=%04X  IX=%04X IY=%04X SP=%04X PC=%04X" %
             (m.af, m.bc, m.de, m.hl, m.ix, m.iy, m.sp, m.pc))
    L.append("  AF'=%04X BC'=%04X DE'=%04X HL'=%04X  IFF=%d HALTED=%d" %
             (0, m.alt_bc, m.alt_de, m.alt_hl,
              0 if m.int_disabled else 1, 1 if m.halted else 0))
    L.append("  flags: " + fmt_flags(m.f))
    sp = m.sp
    st = " ".join("%04X" % (m.memory[(sp + 2 * i) & 0xFFFF] |
                            (m.memory[(sp + 2 * i + 1) & 0xFFFF] << 8))
                  for i in range(8))
    L.append("  [SP] " + st)
    dstr = "  next %04X: " % m.pc + sim.disasm(m.pc)[1]
    L.append(dstr)
    return "\n".join(L)


def show(sim, addr, raw, text, kind, irq, rec):
    block = fmt_show(sim, addr, raw, text, kind, irq, rec)
    print(block)
    log_screen(block)


def hexdump(mem, addr, length):
    for base in range(addr, addr + length, 16):
        row = bytes(mem[base:base + 16])
        hexs = " ".join("%02X" % b for b in row)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
        print("  %04X  %-47s  %s" % (base, hexs, asc))


# ============================================================ commands

def cmd_reset(args):
    os.makedirs(STATE, exist_ok=True)
    os.makedirs(SNAP, exist_ok=True)
    sim = Sim()
    # 実ディスクイメージをセッション用にコピー
    da = os.path.join(DISKS, "drivea.dsk")
    db = os.path.join(DISKS, "driveb.dsk")
    if os.path.exists(da):
        d = open(da, "rb").read()
        sim.disk[0][:len(d)] = d
    if os.path.exists(db):
        d = open(db, "rb").read()
        sim.disk[1][:len(d)] = d
    # boot: drivea セクタ0(128B) -> 0x0000, PC=0
    sim.m.memory[0:SECSZ] = sim.disk[0][0:SECSZ]
    sim.set_reg("pc", 0)
    sim.set_reg("sp", 0)
    sim.io = default_io()
    for f in (TRACE, COUT, SCREEN):
        open(f, "w").close()
    sim.save()
    print("reset: drivea sec0 -> 0x0000, PC=0. boot 開始点。")
    print("  次: run --until 0x0100  (boot.s がカーネルをロードして jp 0x0100)")


def cmd_step(args):
    n = 1
    if args and args[0].isdigit():
        n = int(args[0])
    sim = Sim(); sim.load()
    watches = read_watch()
    mranges = sim.mem_watch_ranges()
    verbose = n <= 8
    last = None
    done = 0
    for i in range(n):
        prev = sim.snap_ranges(mranges) if mranges else None
        r = sim.step()
        last = r
        done += 1
        block = fmt_show(sim, *r)
        log_screen(block)                     # 毎ステップ screen.log へ(==== 区切り)
        if verbose:
            print(block)
            print()
        hits = sim.check_watch(prev)
        if hits:
            note = "  !! WATCH: " + " | ".join(hits)
            log_screen(block + "\n" + note)
            print(note)
            break
        if sim.m.halted and not sim.io["timer_on"]:
            break
    sim.save()
    if not verbose and last:
        print(fmt_show(sim, *last))
        print("  (%d steps。全ステップは screen.log)" % done)


def cmd_run(args):
    mx = 3000000
    until = None
    nowatch = False
    quiet = False
    it = iter(args)
    for a in it:
        if a == "--max":
            mx = int(next(it), 0)
        elif a == "--until":
            until = int(next(it), 0)
        elif a == "--nowatch":
            nowatch = True
        elif a == "--quiet":
            quiet = True
    sim = Sim(); sim.load()
    reason = "max"
    watches = [] if nowatch else read_watch()
    mranges = [] if nowatch else sim.mem_watch_ranges()
    tr = not quiet
    r = None
    i = 0
    for i in range(mx):
        prev = sim.snap_ranges(mranges) if mranges else None
        r = sim.step(trace=tr)
        if until is not None and sim.m.pc == until:
            reason = "until %04X" % until
            break
        if watches:
            hits = sim.check_watch(prev)
            if hits:
                reason = "WATCH: " + " | ".join(hits)
                break
        if sim.m.halted and not sim.io["timer_on"]:
            reason = "HALT"
            break
    interesting = reason.startswith("WATCH") or reason == "HALT"
    if not tr or interesting:
        # 直近命令リングを trace.log + screen.log へ(quiet でも pre-crash が残る)
        ring = sim.dump_ring(150)
        sim._trace("--- ring (直近 %d 命令、stop: %s) ---\n%s" %
                   (min(150, len(sim._ring)), reason, ring))
    sim.save()
    a2 = sim.m.pc
    raw, text, kind = sim.disasm(a2)
    show(sim, a2, raw, text, kind, r[4] if r else None,
         "#%08d  now at %04X: %s" % (sim.io["steps"], a2, text))
    if interesting:
        print(sim.dump_ring(40))
    print("  stop: %s  (%d steps this run, total %d)" % (reason, i + 1, sim.io["steps"]))


def cmd_int(args):
    sim = Sim(); sim.load()
    if args and args[0] == "auto":
        sim.io["timer_auto"] = 1 if (len(args) > 1 and args[1] == "on") else 0
        sim.save()
        print("timer_auto =", sim.io["timer_auto"])
        return
    sim.io["int_pending"] = 1
    sim.save()
    print("int_pending=1 (次 step の冒頭で IM1 割込。IFF=0 なら保留)")


def cmd_regs(args):
    sim = Sim(); sim.load()
    print(open(REGS).read() if os.path.exists(REGS) else "(no regs.txt)")
    raw, text, kind = sim.disasm(sim.m.pc)
    print("next: %04X: %-11s %s" % (sim.m.pc, raw.hex().upper(), text))


def cmd_setreg(args):
    sim = Sim(); sim.load()
    name = args[0].lower()
    val = int(args[1], 0)
    sim.set_reg(name, val)
    sim.save()
    print("%s = %04X" % (name.upper(), val))


def cmd_mem(args):
    addr = int(args[0], 0)
    length = int(args[1], 0) if len(args) > 1 else 128
    sim = Sim(); sim.load()
    hexdump(sim.m.memory, addr, length)


def cmd_dis(args):
    addr = int(args[0], 0)
    n = int(args[1], 0) if len(args) > 1 else 16
    sim = Sim(); sim.load()
    for _ in range(n):
        raw, text, kind = sim.disasm(addr)
        print("  %04X: %-11s %s" % (addr, raw.hex().upper(), text))
        addr += max(1, len(raw))


def cmd_stack(args):
    n = int(args[0], 0) if args else 16
    sim = Sim(); sim.load()
    sp = sim.m.sp
    for i in range(n):
        a = sp + 2 * i
        w = sim.m.memory[a] | (sim.m.memory[a + 1] << 8)
        print("  %04X: %04X" % (a, w))


def cmd_save(args):
    name = args[0]
    d = os.path.join(SNAP, name)
    os.makedirs(d, exist_ok=True)
    for f in (BLOB, IOF, DA_IMG, DB_IMG):
        if os.path.exists(f):
            shutil.copy(f, d)
    print("saved snap:", name)


def cmd_load(args):
    name = args[0]
    d = os.path.join(SNAP, name)
    if not os.path.isdir(d):
        sys.exit("no such snap: " + name)
    for f in ("state.bin", "io.json", "drivea.img", "driveb.img"):
        s = os.path.join(d, f)
        if os.path.exists(s):
            shutil.copy(s, os.path.join(STATE, f))
    sim = Sim(); sim.load(); sim.save()
    print("loaded snap:", name)
    cmd_regs([])


def cmd_snaps(args):
    if os.path.isdir(SNAP):
        for n in sorted(os.listdir(SNAP)):
            print(" ", n)


def cmd_watch(args):
    if not args or args[0] == "list":
        for i, s in enumerate(read_watch()):
            print("  %d: %s" % (i, s))
        return
    ws = read_watch()
    if args[0] == "add":
        ws.append(" ".join(args[1:]))
    elif args[0] == "del":
        del ws[int(args[1])]
    elif args[0] == "clear":
        ws = []
    open(WATCH, "w").write("\n".join(ws) + ("\n" if ws else ""))
    for i, s in enumerate(ws):
        print("  %d: %s" % (i, s))


def cmd_net(args):
    """client socket #1(port50/51)の簡易シミュレーション状態を操作/表示する。
    #51(net常駐時のクラッシュ)再現用。実TCPは張らない。"""
    sim = Sim(); sim.load()
    io = sim.io
    if not args or args[0] == "status":
        print("  cs=%d atbuf=%r rx_pending=%d tx_log_len=%d hangup_after=%d" %
              (io["net_cs"], io["net_atbuf"], len(io["net_rx"]),
               len(io["net_tx_log"]), io["net_hangup_after"]))
        print("  tx_log: %r" % io["net_tx_log"])
        return
    if args[0] == "rx":
        txt = " ".join(args[1:]).replace("\\r", "\r").replace("\\n", "\n")
        io["net_rx"] += txt
        sim.save()
        print("queued %d bytes for guest RX" % len(txt))
        return
    if args[0] == "hangup-after":
        io["net_hangup_after"] = int(args[1], 0)
        sim.save()
        print("net_hangup_after = %d" % io["net_hangup_after"])
        return
    if args[0] == "hangup-now":
        io["net_cs"] = 0
        io["net_atbuf"] = ""
        sim.save()
        print("net_cs forced to 0 (simulated POLLHUP)")
        return
    if args[0] == "clear-tx":
        io["net_tx_log"] = ""
        sim.save()
        print("tx_log cleared")
        return
    print("tz80 net: unknown subcmd %r (status|rx <text>|hangup-after <n>|hangup-now|clear-tx)" % args[0])


def cmd_console(args):
    sim = Sim(); sim.load()
    if args and args[0] == "in":
        txt = " ".join(args[1:]).replace("\\r", "\r").replace("\\n", "\n")
        sim.io["con_in"] += txt
        sim.save()
        print("queued %d chars" % len(txt))
    else:
        print(open(COUT).read() if os.path.exists(COUT) else "")


def cmd_trace(args):
    n = int(args[0], 0) if args else 30
    if os.path.exists(TRACE):
        lines = open(TRACE).read().splitlines()
        print("\n".join(lines[-n:]))


def cmd_screen(args):
    if args and args[0] == "clear":
        open(SCREEN, "w").close()
        print("screen.log cleared")
        return
    if not os.path.exists(SCREEN):
        return
    txt = open(SCREEN, encoding="latin-1").read()
    if args and args[0].isdigit():          # 末尾 N ブロック
        blocks = txt.split("\n" + SEP + "\n")
        txt = ("\n" + SEP + "\n").join(blocks[-int(args[0]):])
    print(txt)


CMDS = {
    "reset": cmd_reset, "step": cmd_step, "run": cmd_run, "int": cmd_int,
    "regs": cmd_regs, "setreg": cmd_setreg, "mem": cmd_mem, "dis": cmd_dis,
    "stack": cmd_stack, "save": cmd_save, "load": cmd_load, "snaps": cmd_snaps,
    "watch": cmd_watch, "console": cmd_console, "trace": cmd_trace,
    "screen": cmd_screen, "net": cmd_net,
}


def main(argv):
    if not argv:
        argv = ["step"]
    cmd = argv[0]
    fn = CMDS.get(cmd)
    if not fn:
        sys.exit("tz80: unknown cmd %r\n  %s" % (cmd, " ".join(sorted(CMDS))))
    fn(argv[1:])


if __name__ == "__main__":
    main(sys.argv[1:])
