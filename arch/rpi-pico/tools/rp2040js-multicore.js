/* tools/rp2040js-multicore.js - rp2040js 1.4.0 にデュアルコアを足す(本体は書き換えない)
 *
 *   rp2040js は Core0 だけを持ち、SIO の FIFO(0x50/0x54/0x58)も CPUID も SEV も実装していない。
 *   そのため multicore_launch_core1() が先へ進めない。ここでは次を足す:
 *     - 2 個目の CortexM0Core(bus は共有)。ブートロムのリセットから始め、Core0 の
 *       multicore_launch_core1 の FIFO 手順(0,0,1,vtor,sp,entry)でブートロムが Core1 を起こす
 *     - SIO: CPUID(実行中のコア)、FIFO_ST / FIFO_WR / FIFO_RD(各 8 段、WOF/ROE のスティッキ)
 *     - SEV: 相手コアの WFE を起こす(待っていなければ event を立てる)
 *     - 実行ループ: 2 コアを 1 命令ずつ交互に回す。PPB(NVIC / VTOR など)は rp.core の
 *       差し替えでコアごとに振り分ける。ペリフェラルの割込みは Core0 へだけ行く
 *   足りないもの(必要になったら足す): SIO の FIFO 割込み、Core1 側のペリフェラル割込み、
 *   コア別の補間器(interp)、ブートロムの BOOTSEL 等。
 */
'use strict';

const FIFO_ST = 0x50, FIFO_WR = 0x54, FIFO_RD = 0x58, CPUID = 0x00;
const FIFO_DEPTH = 8;
const ST_VLD = 1, ST_RDY = 2, ST_WOF = 4, ST_ROE = 8;

function attach(sim, { logger } = {}) {
  const rp = sim.rp2040;
  const core0 = rp.core;
  const core1 = new core0.constructor(rp);

  const state = { cur: 0, rx: [[], []], sticky: [0, 0] };

  /* 実行中のコアの SIO */
  const sio = rp.sio;
  const origRead = sio.readUint32.bind(sio);
  const origWrite = sio.writeUint32.bind(sio);
  sio.readUint32 = (offset) => {
    const c = state.cur;
    switch (offset) {
      case CPUID: return c;
      case FIFO_ST:
        return (state.rx[c].length ? ST_VLD : 0) |
               (state.rx[1 - c].length < FIFO_DEPTH ? ST_RDY : 0) | state.sticky[c];
      case FIFO_RD:
        if (state.rx[c].length) return state.rx[c].shift() >>> 0;
        state.sticky[c] |= ST_ROE;
        return 0;
    }
    return origRead(offset);
  };
  sio.writeUint32 = (offset, value) => {
    const c = state.cur;
    switch (offset) {
      case FIFO_WR:
        if (state.rx[1 - c].length < FIFO_DEPTH) state.rx[1 - c].push(value >>> 0);
        else state.sticky[c] |= ST_WOF;
        return;
      case FIFO_ST:
        state.sticky[c] &= ~(value & (ST_WOF | ST_ROE));   /* 書き込みで WOF / ROE をクリア */
        return;
    }
    origWrite(offset, value);
  };

  /* SEV: 相手コアの WFE を起こす。待っていなければ event を覚えさせる */
  const cores = [core0, core1];
  function sev() {
    for (const c of cores) {
      if (c.waiting) c.waiting = false;
      else c.eventRegistered = true;
    }
  }
  const base = logger || rp.logger;
  rp.logger = {
    debug() {},
    info(src, msg) { if (msg === 'SEV') sev(); },
    warn: (s, m) => base.warn(s, m),
    error: (s, m) => base.error(s, m),
  };

  /* ペリフェラルの割込みは Core0 へ(rp.core は実行中のコアに差し替わるため固定する) */
  rp.setInterrupt = (irq, value) => core0.setInterrupt(irq, value);

  return { core0, core1, state };
}

function run(sim, mc) {
  const { core0, core1, state } = mc;
  const rp = sim.rp2040;
  const clock = sim.clock;
  const cycleNanos = 1e9 / 125000000;       /* 125 MHz */
  core1.reset();                        /* ブートロムのリセットベクタから(ブートロムを読んだ後。Core1 は FIFO を待つ) */
  sim.stopped = false;
  function slice() {
    sim.executeTimer = null;
    for (let i = 0; i < 1000000 && !sim.stopped; i++) {
      const w0 = core0.waiting, w1 = core1.waiting;
      let c0 = 0, c1 = 0;
      if (!w0) { state.cur = 0; rp.core = core0; c0 = core0.executeInstruction(); }
      if (!w1) { state.cur = 1; rp.core = core1; c1 = core1.executeInstruction(); rp.core = core0; }
      if (w0 && w1) {
        const n = clock.nanosToNextAlarm;
        clock.tick(n);
        i += n / cycleNanos;
      } else {
        clock.tick(Math.max(c0, c1) * cycleNanos);   /* 2 コアは並列なので時間は長いほう */
      }
    }
    if (!sim.stopped) sim.executeTimer = setTimeout(slice, 0);
  }
  slice();
}

module.exports = { attach, run };
