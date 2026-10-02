#!/usr/bin/env bash
# Sprinter Sp2000 reference captures on MAME's `sprinter` driver, headless
# (see testdata/machines/sprinter/reference/README.md).
#   MAME_BIN=<mame binary with the sprinter driver> ./mame-capture.sh <mode> [VAR=value ...]
# mode: boot | loader | sync | palette (see mame-capture.lua). Extra VAR=value pairs go to the Lua script's environment
# (SPC_END, SPC_DUMP_AT, ...). Output: $SPC_OUT (default build/<mode>/ here).
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
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

# A fresh CMOS each run (MAME starts from its own default contents), so runs repeat
rm -rf "$BUILD/run/nvram/sprinter" "$BUILD/run/cfg/sprinter.cfg"
cd "$BUILD/run"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
"$MAME" sprinter -bios v3.04 -kbd "" -rompath "$ROMPATH" \
	-video none -sound none -window -nomaximize -nothrottle -skip_gameinfo -noreadconfig -noplugins \
	-cfg_directory "$BUILD/run/cfg" -nvram_directory "$BUILD/run/nvram" -snapshot_directory "$OUTDIR" -snapview native \
	-seconds_to_run $(( END / 48 + 10 )) -autoboot_script "$HERE/mame-capture.lua" 2>&1 | tee "$BUILD/mame-$MODE.log"
