#!/usr/bin/env node
/* tools/test_boot.js - rpi-pico の起動回帰(rp2040js)
 *
 *   node tools/test_boot.js [kernel.uf2]      ... arch/rpi-pico/ から
 *
 *   kernel.uf2 をエミュレータで回し、USB CDC(仮想シリアル)に出る文字列で
 *   起動の段階ごとに PASS / FAIL を判定する。全段階 PASS で終了コード 0。
 *   判定は期待文字列だけで行う(出力を眺めて「動いたことにする」ことはしない)。
 *
 *   前提: tools/bootrom.bin(RP2040 の B1 ブートロム 16KB。環境変数 RP2040_BOOTROM で指定も可)
 *         と rp2040js(npm i。tools/node_modules か NODE_PATH)。
 *   rp2040js 1.4.0 が持たないもの(2026-10-15 に確認):
 *     - SIO の FIFO(マルチコア)… tools/rp2040js-multicore.js で足した(Core1 を回す)
 *     - CYW43(Wi-Fi チップ)… `Failed to start CYW43` が出る
 *     - SD カード(SPI0)
 *   これらに依存する段階は FAIL のまま報告する(隠さない)。実機では全段階が通る想定で、
 *   実機の回帰は #128。
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { Simulator, USBCDC, ConsoleLogger, LogLevel } = require('rp2040js');

const uf2Path = process.argv[2] || path.join(__dirname, '..', '..', '..', 'kernel.uf2');
const bootromPath = process.env.RP2040_BOOTROM || path.join(__dirname, 'bootrom.bin');
const sdPath = process.env.SDCARD_IMG || path.join(__dirname, '..', 'sdcard.img');
const BUDGET_MS = Number(process.env.TEST_BUDGET_MS || 300000);   /* 実時間の上限 */

const ECHO_PROBE = 'abcdefghijklmnop';
/* ECHO_PROBE の後に打つ GPIO の確認(builtin の echo / cat と /dev/gpio。SD も外部コマンドも要らない)。
 *   GPIO5 に 1 を書いて読み戻す / SD の SPI ピン(16)は拒否される / 一覧にそのピンが出る */
const GPIO_PROBE = process.env.TEST_CMDS ? process.env.TEST_CMDS.split(';') : ['echo 1 > /dev/gpio5', 'cat /dev/gpio5', 'echo 1 > /dev/gpio16', 'cat /dev/gpio', 'ls /etc', 'cat /etc/rc'];   /* ]# の後に送り、そのままエコーされるはず */

/* 起動の段階。順序は問わず、全出力に at(正規表現も可)が現れれば PASS。
 *   bad = 現れたら FAIL(「initialized」と出しつつ内部では失敗している類を弾く) */
const STAGES = [
  { name: 'USB CDC バナー',            at: 'Dual-Core Wi-Fi & OS Boot' },
  { name: 'カウントダウン開始',          at: 'Boot countdown: ' },
  { name: 'カウントダウン完了(タイマ)', at: 'Countdown complete. Starting system' },
  /* kmain は cyw43_arch_init の失敗を握らず「initialized」と出すことがあるので、bad で弾く */
  { name: 'Wi-Fi 初期化(CYW43)',       at: 'CYW43 Wi-Fi STA mode initialized', bad: 'Failed to start CYW43' },
  { name: 'Core1 起動',                at: 'Tizix OS booting on Core 1' },
  { name: 'SD / FAT マウント',          at: 'Initializing SD card', bad: 'FAT Drive FAILED' },
  { name: 'カーネル起動(tizix)',        at: /\ntizix\r?\n/ },
  { name: 'シェル起動',                 at: 'Launching embedded Tizix shell' },
  /* 外部コマンドは plat_load がスタブの間は起動できない(`no free block`)。実装されるまで FAIL が正しい */
  { name: '外部コマンドの実行(rc の echo)', at: 'rc: started', bad: 'no free block' },
  { name: 'シェルのプロンプト',          at: ']# ' },
  /* プロンプトの後に 1 文字ずつ打って、エコーが欠けずに返るか(実機で 1 文字おきに落ちた不具合の回帰) */
  { name: '入力が欠けない(エコー)',      at: ECHO_PROBE },
  { name: 'GPIO 書いて読む(echo > /dev/gpio5)', at: /cat \/dev\/gpio5\r?\n1\r?\n/ },
  { name: 'GPIO SD ピンは拒否',         at: 'GPIO16-19 are the SD card SPI (refused)' },
  { name: 'GPIO 一覧',                  at: /pin  dir  val\r?\n0 +in / },
  { name: 'ls /etc(FAT: SD か内蔵 Flash)', at: /ls \/etc\r?\nrc  \d+/ },
  { name: 'cat /etc/rc',            at: /cat \/etc\/rc\r?\n[^]*rsyslog rc-ok/ },
];

function loadUF2(buf, flash) {
  let n = 0;
  for (let off = 0; off + 512 <= buf.length; off += 512) {
    const b = buf.subarray(off, off + 512);
    if (b.readUInt32LE(0) !== 0x0A324655 || b.readUInt32LE(4) !== 0x9E5D5157 ||
        b.readUInt32LE(508) !== 0x0AB16F30) continue;
    const addr = b.readUInt32LE(12), size = b.readUInt32LE(16);
    if (addr >= 0x10000000 && addr + size <= 0x10000000 + flash.length) {
      flash.set(b.subarray(32, 32 + size), addr - 0x10000000);
      n++;
    }
  }
  return n;
}

for (const f of [uf2Path, bootromPath]) {
  if (!fs.existsSync(f)) { console.error(`ERROR: ${f} がない`); process.exit(2); }
}

const sim = new Simulator();
const rp = sim.rp2040;
const mc = require('./rp2040js-multicore.js').attach(sim, { logger: new ConsoleLogger(LogLevel.Error) });   /* SEV / USB の情報ログは出さない */
const rom = fs.readFileSync(bootromPath);
rp.loadBootrom(new Uint32Array(rom.buffer, rom.byteOffset, rom.length / 4));
let sd = null;
if (fs.existsSync(sdPath)) {
  sd = require('./rp2040js-sdcard.js').attach(rp, fs.readFileSync(sdPath));
} else {
  console.error(`注意: ${sdPath} がない。SD カード無しで回す(FAT マウントは FAIL になる)`);
}
const blocks = loadUF2(fs.readFileSync(uf2Path), rp.flash);
if (blocks === 0) { console.error('ERROR: UF2 から書き込めたブロックが 0'); process.exit(2); }

let out = '';
const has = (at) => (at instanceof RegExp ? at.test(out) : out.includes(at));
const cdc = new USBCDC(rp.usbCtrl);
let probeSent = false;
cdc.onSerialData = (b) => {
  out += Buffer.from(b).toString('latin1');
  if (!probeSent && out.includes(']# ')) {          /* プロンプト後に 1 バイトずつ(実時間 20ms 間隔) */
    probeSent = true;
    probeAt = Date.now();
    [...ECHO_PROBE].forEach((ch, i) => setTimeout(() => cdc.sendSerialByte(ch.charCodeAt(0)), 300 + i * 20));
    /* 行を消してから GPIO の確認を 1 行ずつ(各行 1.5 秒おき。エミュレータは遅い) */
    GPIO_PROBE.forEach((line, k) => setTimeout(() => {
      for (const ch of '\r' + line + '\r') cdc.sendSerialByte(ch.charCodeAt(0));
    }, 2500 + k * 1500));
  }
};
cdc.onDeviceConnected = () => cdc.sendSerialByte('\r'.charCodeAt(0));

rp.core.PC = 0x10000000;
const t0 = Date.now();
let done = false;
function finish() {
  if (done) return;
  done = true;
  sim.stop();
  let fail = 0;
  console.log(`=== rpi-pico 起動回帰 (${path.basename(uf2Path)}, UF2 ${blocks} blocks) ===`);
  STAGES.forEach((s) => {
    const got = has(s.at), bad = s.bad && out.includes(s.bad);
    const ok = got && !bad;
    if (!ok) fail++;
    const why = ok ? '' : (bad ? `  ← 禁止文字列 ${JSON.stringify(s.bad)} が出た` : '  ← 到達せず');
    console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${s.name}   (期待: ${String(s.at)})${why}`);
  });
  if (sd && process.env.TEST_DEBUG)
    console.log(`  [debug] sd: reads=${sd.stats.reads} writes=${sd.stats.writes} errors=${sd.stats.errors} cmds=${sd.stats.cmds.slice(0,12).join(',')}`);
  if (process.env.TEST_DEBUG) {
    const h = (v) => '0x' + (v >>> 0).toString(16);
    for (const [i, c] of [mc.core0, mc.core1].entries())
      console.log(`  [debug] core${i}: PC=${h(c.PC)} SP=${h(c.SP)} LR=${h(c.LR)} waiting=${c.waiting} PM=${c.PM} IPSR=${c.IPSR}`);
    console.log(`  [debug] fifo core0 rx=${JSON.stringify(mc.state.rx[0].map(h))} core1 rx=${JSON.stringify(mc.state.rx[1].map(h))}`);
  }
  if (fail || process.env.TEST_FULL) {
    console.log('--- 最後の出力(末尾 400 文字) ---');
    console.log(out.slice(process.env.TEST_FULL ? 0 : -400));
  }
  console.log(`=== result: ${fail ? 'FAIL' : 'ALL PASS'} (${((Date.now() - t0) / 1000).toFixed(0)} s) ===`);
  process.exit(fail ? 1 : 0);
}

/* 最後の段階に着いたら少し待って(続きの出力を拾う)終了。実時間の上限(BUDGET_MS)でも終了 */
let lastSeen = 0;
let probeAt = 0;
const poll = setInterval(() => {
  const last = STAGES[STAGES.length - 1];
  if ((has(last.at) || (probeSent && Date.now() - probeAt > 40000)) && !lastSeen) lastSeen = Date.now();
  if ((lastSeen && Date.now() - lastSeen > 1500) || Date.now() - t0 > BUDGET_MS) {
    clearInterval(poll); finish();
  }
}, 200);
require('./rp2040js-multicore.js').run(sim, mc);
