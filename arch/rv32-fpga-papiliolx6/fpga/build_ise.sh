#!/bin/bash
# Build the bitstream for the Papilio Pro: GHDL (NEORV32 -> Verilog) -> ISE 14.7 (Wine) xst / ngdbuild / map / par / bitgen.
#   Usage: fpga/build_ise.sh [output directory]   (default /mnt/sda1/fpga/papilio-build)
#   ISE lives in /mnt/sda1/fpga/Xilinx/14.7, the Wine prefix is ~/fpga-wine. It starts its own Xvfb (:97) and stops it at the end.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
N32=${N32:-/mnt/sda1/fpga/src/neorv32}
CLK=${CLK:-64000000}       # system clock (Hz). Keep CLK_HZ of the loader, NEORV32 and papilio_top the same
# PLL: VCO = 32MHz x PLL_MULT (400-1000MHz), CLK = VCO / PLL_DIV. If not given, they are chosen from CLK
MHZ=$((CLK / 1000000))
if [ -z "$PLL_DIV" ]; then
  for d in 10 8 9 7 6 5 4 3 2 12 14 16 20; do
    if [ $(( (MHZ * d) % 32 )) -eq 0 ]; then m=$(( MHZ * d / 32 )); v=$(( m * 32 ))
      if [ $v -ge 400 ] && [ $v -le 1000 ]; then PLL_DIV=$d; PLL_MULT=$m; break; fi; fi
  done
fi
SD_PHASE=${SD_PHASE:-0}
OUT=${1:-/mnt/sda1/fpga/papilio-build}
mkdir -p "$OUT"; cd "$OUT"

# 1. the boot loader (32MHz) and NEORV32 as Verilog
make -s -B -C "$HERE/loader" CLK_HZ=$CLK UART_BAUD=${BAUD:-115200} >/dev/null
FILES=$(sed "s#\$NEORV32_HOME#$N32#" "$N32/rtl/file_list_core.f" | sed "s#$N32/rtl/core/neorv32_imem_image.vhd#$HERE/loader/imem_image.vhd#")
ghdl --synth --std=08 --ieee=synopsys --work=neorv32 --out=verilog -gCLOCK_FREQUENCY=$CLK -gLOADER=true \
  $FILES "$HERE/tizix_top.vhd" -e tizix_top > tizix_top.v 2> ghdl.log
cp "$HERE/papilio_top.v" "$HERE/sdram_psram.v" "$HERE/papilio_pro.ucf" .

# 2. ISE
export WINEPREFIX=$HOME/fpga-wine WINEARCH=win32 WINEDEBUG=-all DISPLAY=:97
export XILINX='F:\fpga\Xilinx\14.7\ISE_DS\ISE'
B='F:\fpga\Xilinx\14.7\ISE_DS\ISE\bin\nt'
Xvfb :97 -screen 0 1024x768x16 >/dev/null 2>&1 &
XPID=$!
trap 'kill $XPID 2>/dev/null' EXIT
sleep 2
cat > top.prj <<EOP
verilog work "tizix_top.v"
verilog work "sdram_psram.v"
verilog work "papilio_top.v"
EOP
cat > top.xst <<EOP
run
-ifn top.prj
-ifmt mixed
-ofn top
-ofmt NGC
-p xc6slx9-2-tqg144
-top papilio_top
-generics { CLK_HZ=$CLK PLL_MULT=$PLL_MULT PLL_DIV=$PLL_DIV SD_PHASE=$SD_PHASE }
-opt_mode speed
-opt_level 1
-iobuf yes
EOP
wine "$B\\xst.exe" -ifn top.xst -ofn top.syr > xst.log 2>&1
wine "$B\\ngdbuild.exe" -uc papilio_pro.ucf -p xc6slx9-2-tqg144 top.ngc top.ngd > ngdbuild.log 2>&1
wine "$B\\map.exe" -p xc6slx9-2-tqg144 -w -o top_map.ncd top.ngd top.pcf > map.log 2>&1
wine "$B\\par.exe" -w top_map.ncd top.ncd top.pcf > par.log 2>&1
wine "$B\\bitgen.exe" -w -g StartUpClk:CClk -g UnusedPin:PullUp top.ncd top.bit top.pcf > bitgen.log 2>&1
ls -la top.bit
