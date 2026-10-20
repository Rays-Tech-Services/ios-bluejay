#!/bin/bash
# Build the bluejay shim as one position-independent blob with Xcode's clang
# only (no cross linker), the way iOS16-on-A40/loader does: compile to
# assembly, fold every data section into .text, drop .globl so all calls are
# resolved by the assembler, and refuse any relocation (tools/mkbootimg.py).
#   shim/build.sh            -> shim/out/shim.o
# Then tools/mkbootimg.py packs it (with or without an XNU payload).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
CC=${CC:-clang}
$CC -target aarch64-none-elf -mcmodel=tiny -fno-pic -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-common -mgeneral-regs-only -mstrict-align -mcpu=cortex-a55 \
	-O2 -Wall -Wextra -Wno-unused-function -S shim.c -o out/shim.s
sed -E -e 's/^[[:space:]]*\.section[[:space:]]+\.(rodata|data|bss)[^,]*(,.*)?$/	.text/' \
	-e 's/^[[:space:]]*\.(data|bss)[[:space:]]*$/	.text/' \
	-e '/^[[:space:]]*\.(local|globl)[[:space:]]/d' \
	-e 's/^[[:space:]]*\.comm[[:space:]]+([^,]+),([0-9]+),([0-9]+)$/	.text\n	.balign \3\n\1:\n	.zero \2/' \
	out/shim.s > out/shim-flat.s
$CC -target aarch64-none-elf -c out/shim-flat.s -o out/shim.o
echo "shim/out/shim.o"
