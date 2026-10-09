#!/usr/bin/env bash
# MAME specnext back end of run-ref.sh:  run-mame.sh <sd-image> <frames> <out-dir>
# The env COSIM_* set by run-ref.sh (COSIM_TRACE, COSIM_DUMP, COSIM_FRAMES, ...) are read by the patched driver.
# The boot ROM is the FPGA 3.01.00 boot ROM (jnext/roms/nextboot.rom == MAME boot-30100.bin, CRC ccbd55ba), looked
# up in $NEXT_COSIM_CARDS/mame-roms/tbblue/ (copied there by build-ref.sh).
set -euo pipefail
img=${1:?sd image}; frames=${2:?frames}; out=${3:?out-dir}
cards=${NEXT_COSIM_CARDS:-/Volumes/TB4-4Tb/Projects/emulators/cosim-cards}
build=${NEXT_COSIM_BUILD:-/Volumes/TB4-4Tb/Projects/emulators/build}
bin="$build/mame/src/specnext"
[ -x "$bin" ] || { echo "$bin missing: run build-ref.sh mame" >&2; exit 1; }
[ -f "$cards/mame-roms/tbblue/boot-30100.bin" ] || { echo "$cards/mame-roms/tbblue/boot-30100.bin missing" >&2; exit 1; }

work="$out/mame-work"
rm -rf "$work"; mkdir -p "$work"
# the patched driver exits by itself at the end of frame <frames> and saves the screen into the snapshot directory
"$bin" specnext_ks2 -bios v30100 -rompath "$cards/mame-roms" -hard1 "$img" \
    -video none -sound none -nothrottle -skip_gameinfo -seconds_to_run $((frames / 50 + 600)) \
    -nvram_directory "$work" -cfg_directory "$work" -snapshot_directory "$work" -state_directory "$work" \
    -diff_directory "$work" -comment_directory "$work" >"$out/run.log" 2>&1 || true
png=$(find "$work" -name '*.png' | sort | tail -1 || true)
[ -n "$png" ] && cp "$png" "$out/screen.png"
rm -rf "$work"
