#!/bin/sh
# NEORV32 (VHDL) -> convert tizix_top to Verilog with GHDL and build it with tb.v on Verilator.
#   Usage: fpga/build_sim.sh [output directory]   (default /mnt/sda1/fpga/tizix-sim)
#     LOADER=1 builds the version that boots from the boot loader in IMEM (ROM) (the kernel arrives over the UART).
#     The boot loader uses the imem_image.vhd made by fpga/loader (make -C fpga/loader CLK_HZ=...).
#   The NEORV32 sources are in /mnt/sda1/fpga/src/neorv32 (commit 87c8c98).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
N32=${N32:-/mnt/sda1/fpga/src/neorv32}
OUT=${1:-/mnt/sda1/fpga/tizix-sim}
CLK=${CLK:-10000000}
mkdir -p "$OUT"; cd "$OUT"
make -s -C "$HERE/loader" CLK_HZ=$CLK >/dev/null
FILES=$(sed "s#\$NEORV32_HOME#$N32#" "$N32/rtl/file_list_core.f" | sed "s#$N32/rtl/core/neorv32_imem_image.vhd#$HERE/loader/imem_image.vhd#")
LDR=false; [ "$LOADER" = 1 ] && LDR=true
ghdl --synth --std=08 --ieee=synopsys --work=neorv32 --out=verilog -gCLOCK_FREQUENCY=$CLK -gLOADER=$LDR \
  $FILES "$HERE/tizix_top.vhd" -e tizix_top > tizix_top.v
verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style --top-module tb \
  "$HERE/tb.v" "$HERE/sdram_psram.v" "$HERE/sdram_model.v" tizix_top.v -Mdir vobj -o simv -j 4 > vbuild.log 2>&1
echo "built: $OUT/vobj/simv"
