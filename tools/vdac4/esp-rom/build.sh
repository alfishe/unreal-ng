#!/bin/bash
# Build the VDAC4 ESP HLE "ROM": the libc / libm / libgcc functions the board's firmware exports to app ELFs,
# taken from the Espressif toolchain's esp32s3 newlib and libgcc and linked at a fixed address, so the Xtensa
# interpreter runs the same code the board runs. Names the toolchain does not provide, newlib's system calls,
# __getreent, the malloc family (the firmware's heap_caps), float division and square root are host traps at
# kEspTrapBase + 4 * index.
#
# Usage: tools/vdac4/esp-rom/build.sh <xtensa-esp-elf toolchain dir>
# Output: core/src/emulator/platforms/tsconf/vdac4/esp/esprom.cpp (generated, checked in)
#
# Docs: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §8.4
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
TC="$(cd "${1:?toolchain dir}" && pwd)"
LIBS="$TC/xtensa-esp-elf/lib/esp32s3"
GCCLIB="$(ls -d "$TC"/lib/gcc/xtensa-esp-elf/*/esp32s3 | head -1)"
OUT="$ROOT/scratch/vdac4-esp-rom"
mkdir -p "$OUT"

ROM_BASE=0x40800000
TRAP_BASE=0x40000000

TRAPS=()
UNDEF=()
i=0
for s in $(cat "$HERE/traps.txt"); do
    TRAPS+=(--defsym "$s=$((TRAP_BASE + i * 4))")
    i=$((i + 1))
done
for s in $(cat "$HERE/exports.txt"); do
    grep -qw -- "$s" "$HERE/traps.txt" || UNDEF+=(-u "$s")
done

"$TC/bin/xtensa-esp32s3-elf-gcc" -O2 -mlongcalls -ffp-contract=off -c "$HERE/romshim.c" -o "$OUT/romshim.o"
"$TC/bin/xtensa-esp32s3-elf-ld" -o "$OUT/rom.elf" -nostdlib --gc-sections -Ttext=$ROM_BASE -e 0 \
    "${TRAPS[@]}" "${UNDEF[@]}" "$OUT/romshim.o" "$LIBS/libc.a" "$LIBS/libm.a" "$GCCLIB/libgcc.a"
# The FPU's approximation steps (DIV0.S, NEXP01.S, DIVN.S, SQRT0.S, ...) are not emulated: __divsf3 and
# __ieee754_sqrtf are traps (IEEE division / square root on the host, correctly rounded as the board's sequence)
if "$TC/bin/xtensa-esp32s3-elf-objdump" -d "$OUT/rom.elf" |
    grep -qE $'\t(div0|recip0|sqrt0|rsqrt0|nexp01|mksadj|mkdadj|addexp|addexpm|const|divn)\.s\t'; then
    echo "rom.elf uses FPU approximation instructions the interpreter does not run" >&2
    exit 1
fi
"$TC/bin/xtensa-esp32s3-elf-objcopy" -O binary -j .text -j .rodata -j .data "$OUT/rom.elf" "$OUT/rom.bin"
"$TC/bin/xtensa-esp32s3-elf-nm" "$OUT/rom.elf" > "$OUT/rom.nm"
"$TC/bin/xtensa-esp32s3-elf-size" -A "$OUT/rom.elf" > "$OUT/rom.size"
python3 "$HERE/emit.py" "$HERE" "$OUT" "$ROOT/core/src/emulator/platforms/tsconf/vdac4/esp/esprom.cpp" \
    "$ROM_BASE" "$TRAP_BASE" "$("$TC/bin/xtensa-esp32s3-elf-gcc" --version | head -1)"
