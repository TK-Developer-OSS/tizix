#!/bin/sh
# tools/run_test_boot.sh - rpi-pico の起動回帰(rp2040js)をビルドから回す
#   カウントダウンを 1 秒にしたテスト用ビルドを別ディレクトリに作り、test_boot.js で判定する。
#   (通常ビルドは 16 秒。エミュレータは実時間より遅いので、そのままだと数分かかる)
#   前提: PICO_SDK_PATH、arm-none-eabi-gcc、cmake、node、tools/bootrom.bin、tools/node_modules/rp2040js
#   使い方(arch/rpi-pico から): sh tools/run_test_boot.sh
set -e
cd "$(dirname "$0")/.."
B=${TEST_BUILD_DIR:-build-test}
[ -f "$B/CMakeCache.txt" ] || cmake -S . -B "$B" -DBOOT_COUNTDOWN_SEC=1 >/dev/null
make -C "$B" -j4 >/dev/null
[ -f sdcard.img ] || make sdcard.img >/dev/null 2>&1 || true
NODE_PATH=tools/node_modules exec node tools/test_boot.js "$B/kernel.uf2"
