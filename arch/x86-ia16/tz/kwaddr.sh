#!/bin/bash
# tz/kwaddr.sh [OBJDIR] - address of kwork in the tzcc-mixed kernel (read from the ELF the image was flattened from).
cd "$(dirname "$0")/.."
ia16-elf-nm "${1:-obj/tz}/kernel.elf" | sed -n 's/^0*\([0-9a-f]*\) [BbCc] kwork$/\1/p'
