#!/usr/bin/env bash
# Sprinter Sp2000 reference captures on MAME's `sprinter` driver, headless
# (see testdata/machines/sprinter/reference/README.md).
#   MAME_BIN=<mame binary with the sprinter driver> ./mame-capture.sh <mode> [VAR=value ...]
# mode: boot | loader | sync | palette (see mame-capture.lua). Extra VAR=value pairs go to the Lua script's environment
# (SPC_END, SPC_DUMP_AT, ...). Output: $SPC_OUT (default build/<mode>/ here).
# SPC_FLOP1 / SPC_FLOP2=<image> put a floppy in drive A / B (a 3.5" HD drive; SPC_FLOP_DRIVE: 35hd by default, or
# 35dd / 525qd). With a blank CMOS the BIOS boots the IDE master, then the alternative device, floppy B.
# SPC_HARD1=<image> puts a hard disk on the primary master (MAME -hard1, ata1:0: a CHD, e.g. the owner's MAME pack
# sp_hdd_sys.chd), SPC_HARD2 on the secondary master (-hard2, ata2:0). SPC_BIOS: the MAME BIOS set (default v3.04;
# v3.06 needs MAME_ROMPATH with MAME's own sprinter.zip, e.g. the MAME pack's roms/ folder).
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
MODE=${1:?usage: mame-capture.sh boot|loader|sync|palette [VAR=value ...]}
shift

MAME=${MAME_BIN:-mame}
case $MAME in
	*/*) MAME=$(cd "$(dirname "$MAME")" && pwd)/$(basename "$MAME") ;;
esac
"$MAME" -listfull sprinter > /dev/null 2>&1 || { echo "no sprinter driver in $MAME" >&2; exit 3; }

BUILD=${SPC_BUILD:-$HERE/build}
OUTDIR=${SPC_OUT:-$BUILD/$MODE}
mkdir -p "$OUTDIR" "$BUILD/run"
OUTDIR=$(cd "$OUTDIR" && pwd)

# ROMs: MAME_ROMPATH as given, else a rompath built from unreal-ng's own files by the coemu tool
if [ -n "${MAME_ROMPATH:-}" ]; then
	ROMPATH=$MAME_ROMPATH
else
	ROMPATH=$BUILD/roms
	rm -rf "$ROMPATH"
	python3 "$ROOT/tools/verification/coemu/mame/romset.py" "$MAME" "$ROMPATH" \
		"$ROOT/data/rom" -- sprinter
fi

export SPC_MODE=$MODE SPC_OUT=$OUTDIR SPC_ROM=${SPC_ROM:-$ROOT/data/rom/sprinter/sp2k-3.04.rom}
for kv in "$@"; do export "${kv?}"; done
END=${SPC_END:-600}

MEDIA=()
abspath() { echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
if [ -n "${SPC_FLOP1:-}" ]; then
	MEDIA+=(-beta:wd179x:0 "${SPC_FLOP_DRIVE:-35hd}" -flop1 "$(abspath "$SPC_FLOP1")")
fi
if [ -n "${SPC_FLOP2:-}" ]; then
	MEDIA+=(-beta:wd179x:1 "${SPC_FLOP_DRIVE:-35hd}" -flop2 "$(abspath "$SPC_FLOP2")")
fi
if [ -n "${SPC_HARD1:-}" ]; then
	MEDIA+=(-hard1 "$(abspath "$SPC_HARD1")")
fi
if [ -n "${SPC_HARD2:-}" ]; then
	MEDIA+=(-hard2 "$(abspath "$SPC_HARD2")")
fi

# A fresh CMOS each run (MAME starts from its own default contents), so runs repeat
rm -rf "$BUILD/run/nvram/sprinter" "$BUILD/run/cfg/sprinter.cfg"
cd "$BUILD/run"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
"$MAME" sprinter -bios "${SPC_BIOS:-v3.04}" -kbd "" -rompath "$ROMPATH" \
	-video none -sound none -window -nomaximize -nothrottle -skip_gameinfo -noreadconfig -noplugins \
	-cfg_directory "$BUILD/run/cfg" -nvram_directory "$BUILD/run/nvram" -snapshot_directory "$OUTDIR" -snapview native \
	${MEDIA[@]+"${MEDIA[@]}"} \
	-seconds_to_run $(( END / 48 + 10 )) -autoboot_script "$HERE/mame-capture.lua" 2>&1 | tee "$BUILD/mame-$MODE.log"
