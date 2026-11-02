/* tools/rp2040js-sdcard.js - rp2040js の SPI0 に SD カード(SPI モード)をつなぐ
 *
 *   arch/rpi-pico/diskio.c が使うコマンドだけに応える最小モデル:
 *     CMD0 / CMD8 / CMD55 / ACMD41 / CMD58 / CMD16 / CMD17(1 ブロック読み)/ CMD24(1 ブロック書き)
 *   カードはバイトアドレス方式(diskio.c は sector*512 を渡す)。書き込みはメモリ上のコピーだけで、
 *   イメージのファイルは変えない。CS は GPIO17(SIO の出力値)。
 *   マルチブロック・CRC・CMD9/10 などは持たない(diskio.c は使わない)。
 */
'use strict';

const CS_PIN = 17;

function attach(rp, image) {
  const spi = rp.spi[0];
  const img = Buffer.from(image);                 /* 書き込みはこのコピーへ */
  let queue = [];                                 /* これから MISO へ出すバイト */
  let cmd = [];                                   /* 組み立て中のコマンド(6 バイト) */
  let app = false;                                /* 直前が CMD55 */
  let wr = null;                                  /* CMD24 のデータ待ち { addr, got:[] , state } */
  const stats = { cmds: [], reads: 0, writes: 0, errors: 0 };

  const csLow = () => (rp.sio.gpioValue & (1 << CS_PIN)) === 0;

  function command(c, arg) {
    stats.cmds.length < 64 && stats.cmds.push((app ? 'A' : '') + 'CMD' + c);
    const wasApp = app;
    app = false;
    switch (c) {
      case 0:  return [0x01];
      case 8:  return [0x01, 0x00, 0x00, 0x01, 0xaa];
      case 55: app = true; return [0x01];
      case 41: return wasApp ? [0x00] : [0x04];
      case 58: return [0x00, 0x00, 0xff, 0x80, 0x00];
      case 16: return [0x00];
      case 17: {
        if (arg + 512 > img.length) { stats.errors++; return [0x04]; }
        stats.reads++;
        return [0x00, 0xfe, ...img.subarray(arg, arg + 512), 0x00, 0x00];
      }
      case 24:
        if (arg + 512 > img.length) { stats.errors++; return [0x04]; }
        wr = { addr: arg, got: [], state: 'token' };
        return [0x00];
      default:
        stats.errors++;
        return [0x04];                            /* illegal command */
    }
  }

  spi.onTransmit = (v) => {
    let out = queue.length ? queue.shift() : 0xff;
    if (!csLow()) {                               /* 選択されていない: 状態を捨てる */
      queue = []; cmd = []; wr = null;
      spi.completeTransmit(0xff);
      return;
    }
    if (wr) {
      if (wr.state === 'token') {
        if (v === 0xfe) wr.state = 'data';
      } else if (wr.state === 'data') {
        wr.got.push(v);
        if (wr.got.length === 514) {              /* 512 + CRC 2 */
          for (let i = 0; i < 512; i++) img[wr.addr + i] = wr.got[i];
          stats.writes++;
          wr = null;
          queue = [0x05];                         /* data accepted。次の読みで返る */
        }
      }
    } else if (cmd.length === 0) {
      if ((v & 0xc0) === 0x40) cmd.push(v);
    } else {
      cmd.push(v);
      if (cmd.length === 6) {
        const arg = ((cmd[1] << 24) | (cmd[2] << 16) | (cmd[3] << 8) | cmd[4]) >>> 0;
        queue = command(cmd[0] & 0x3f, arg);
        cmd = [];
      }
    }
    spi.completeTransmit(out);
  };

  return { img, stats };
}

module.exports = { attach };
