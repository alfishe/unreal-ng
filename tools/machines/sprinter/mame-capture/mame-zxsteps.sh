#!/usr/bin/env bash
# A scripted user session on MAME's `sprinter` driver (ZX-mode research, 2026-10-02;
# docs/inprogress/2026-09-28-sprinter/research-zx-mode.md §9). Types into DSS and the Spectrum mode, plays a tape,
# loads a snapshot, resets, and saves screenshots, all at fixed frames.
#   MAME_BIN=<zxsp binary> MAME_ROMPATH=<folder with sprinter.zip (v3.06), betadisk.zip, kb_ms_natural.zip>
#   SPC_HARD1=<hard disk CHD> ZXK_STEPS="<frame>|<action>|<arg>;..." ./mame-zxsteps.sh <name> <seconds> [mame args]
# Actions (mame-zxsteps.lua): keys <text with {ENTER}> | snap <png name> | play | stop | load <snapshot path> |
#   kbd | kbdonly <tag part, or =exact tag> | reset (soft reset) | state | end. ZXK_SNAP_EVERY=n: a PNG every n frames.
# DSS reads the PC keyboard (":kbd:ms_naturl"); the Spectrum mode reads MAME's own matrix ports (":"), so switch
# with "kbdonly|ms_naturl" before DSS typing and "kbdonly|=:" once the Spectrum menu is up.
# Output: build/zxsteps/<name>/ here (log.txt with the "zxsteps:" lines, PNGs). SPC_BIOS: default v3.06.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
NAME=${1:?usage: mame-zxsteps.sh <name> <seconds> [mame args]}
SECS=${2:?usage: mame-zxsteps.sh <name> <seconds> [mame args]}
shift 2
: "${MAME_BIN:?set MAME_BIN}" "${MAME_ROMPATH:?set MAME_ROMPATH}" "${SPC_HARD1:?set SPC_HARD1 (a CHD)}"
BUILD=${SPC_BUILD:-$HERE/build/zxsteps}
OUT=$BUILD/$NAME
mkdir -p "$OUT" "$BUILD/run"
OUT=$(cd "$OUT" && pwd)
# A fresh CMOS and no hard-disk diff from an earlier run
rm -rf "$BUILD/run/nvram" "$BUILD/run/cfg" "$BUILD/run/diff"
cd "$BUILD/run"
export ZXK_OUT=$OUT
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
"$MAME_BIN" sprinter -bios "${SPC_BIOS:-v3.06}" -rompath "$MAME_ROMPATH" \
	-video none -sound none -window -nomaximize -nothrottle -skip_gameinfo -noreadconfig -noplugins \
	-cfg_directory "$BUILD/run/cfg" -nvram_directory "$BUILD/run/nvram" -snapshot_directory "$OUT" -snapview native \
	-hard1 "$SPC_HARD1" "$@" \
	-seconds_to_run "$SECS" -autoboot_script "$HERE/mame-zxsteps.lua" > "$OUT/log.txt" 2>&1
echo "exit $?"
grep zxsteps "$OUT/log.txt"
