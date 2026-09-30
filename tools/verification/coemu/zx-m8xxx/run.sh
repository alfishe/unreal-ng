#!/usr/bin/env bash
# Co-emulation runner: ZX-M8XXX (a JavaScript emulator), in headless Chrome (see ../README.md for the contract).
#   M8XXX_DIR=<ZX-M8XXX checkout> ./run.sh [machine...]
#   CHROME_BIN=<chrome or chromium binary> picks the browser (default: found on PATH or in /Applications)
#   M8XXX_ROMS=<folder with ZX-M8XXX's ROM files> (default: unreal-ng's data/rom, see README.md)
# driver.py serves the checkout (read only) over HTTP and opens harness.html in headless Chrome, which drives the
# emulator through its automation surface window.zxDebug. The machines run one after another: one browser at a time.
. "$(dirname "$0")/../common/common.sh"
coemu_init zx-m8xxx "$@"
HERE=$(cd "$(dirname "$0")" && pwd)

DIR=$(coemu_find M8XXX_DIR) || coemu_not_found "set M8XXX_DIR to a ZX-M8XXX checkout (github.com/Bedazzle/ZX-M8XXX)"
[ -f "$DIR/index.html" ] && [ -f "$DIR/M8XXX.md" ] ||
	coemu_not_found "M8XXX_DIR=$DIR is not a ZX-M8XXX checkout (no index.html and M8XXX.md)"

# The browser: CHROME_BIN, then PATH, then the standard macOS application folders
CHROME=$(coemu_find CHROME_BIN google-chrome google-chrome-stable chromium chromium-browser chrome) || CHROME=""
if [ -z "$CHROME" ] && [ -z "${CHROME_BIN:-}" ]; then
	for app in "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
		"/Applications/Chromium.app/Contents/MacOS/Chromium"; do
		[ -x "$app" ] && { CHROME=$app; break; }
	done
fi
[ -n "$CHROME" ] || coemu_not_found "install Google Chrome or Chromium, or set CHROME_BIN (ZX-M8XXX runs in a browser)"

# The ROMs, under the names ZX-M8XXX fetches (never the checkout's own roms/ folder, which git ignores).
# From unreal-ng's data/rom the Pentagon's is pentagon128k.rom: ZX-M8XXX wants the 32 KB 128 + 48 BASIC ROM,
# and data/rom/pentagon.rom is 64 KB, in unreal-ng's page order (service ROM, TR-DOS, 128, 48)
if [ -n "${M8XXX_ROMS:-}" ]; then
	ROMDIR=$M8XXX_ROMS PENTAGON_ROM=pentagon.rom
else
	ROMDIR=$COEMU_ROOT/data/rom PENTAGON_ROM=pentagon128k.rom
fi
ROMS=()
for r in 48.rom 128.rom plus2.rom plus2a.rom plus3.rom pentagon.rom scorpion.rom trdos.rom; do
	src=$r
	[ "$r" = pentagon.rom ] && src=$PENTAGON_ROM
	[ -f "$ROMDIR/$src" ] && ROMS+=(--rom "$r=$ROMDIR/$src")
done
[ -f "$ROMDIR/48.rom" ] || coemu_not_found "no 48.rom in $ROMDIR (set M8XXX_ROMS to a folder with ZX-M8XXX's ROM files)"

DONE=$(coemu_sym DONE)
START=$(coemu_sym START)
END=$(coemu_sym PROBEEND)
BUILD=$HERE/build
mkdir -p "$BUILD"

for m in $MACHINES; do
	rm -f "$OUT/$m.bin" "$OUT/$m.log" "$OUT/$m.screen.txt" "$OUT/$m.compare.txt"
	# The harness machine name -> ZX-M8XXX's machine id, and how the program is loaded
	case $m in
		48k) id=48k how=tape rom=48.rom ;;
		128k) id=128k how=tape rom=128.rom ;;
		plus2) id=+2 how=tape rom=plus2.rom ;;
		plus2a) id=+2a how=tape rom=plus2a.rom ;;
		plus3) id=+3 how=tape rom=plus3.rom ;;
		pentagon) id=pentagon how=trd rom=$PENTAGON_ROM ;;
		scorpion) id=scorpion how=trd rom=scorpion.rom ;;
		profscorp) coemu_result "$m" skipped "ZX-M8XXX's Scorpion has only its stock 64K ROM, no ProfROM"; continue ;;
		atm710) coemu_result "$m" skipped "ZX-M8XXX has no ATM Turbo"; continue ;;
		atm3) coemu_result "$m" skipped "ZX-M8XXX has no ZX-Evo"; continue ;;
		profi) coemu_result "$m" skipped "ZX-M8XXX has no Profi"; continue ;;
		*) coemu_result "$m" skipped "ZX-M8XXX has no such machine"; continue ;;
	esac
	[ -f "$ROMDIR/$rom" ] || { coemu_result "$m" error "no $rom in $ROMDIR (see README.md, ROMs)"; continue; }
	[ "$how" = trd ] && [ ! -f "$ROMDIR/trdos.rom" ] && [ "$m" = pentagon ] &&
		{ coemu_result "$m" error "no trdos.rom in $ROMDIR (see README.md, ROMs)"; continue; }
	media=$PROGRAM_TAP
	if [ "$how" = trd ]; then
		[ -f "$PROGRAM_TRD" ] || { coemu_result "$m" skipped "no $PROGRAM_TRD for TR-DOS"; continue; }
		media=$PROGRAM_TRD
	fi
	echo "coemu[zx-m8xxx] $m: running ZX-M8XXX machine $id ($how)"
	python3 "$HERE/driver.py" --m8xxx "$DIR" --chrome "$CHROME" --machine "$id" --media "$media" --type "$how" \
		--done "$DONE" --start "$START" --end "$END" --max-frames "$MAX_FRAMES" \
		--out "$OUT/$m" --profile "$BUILD/profile-$m" "${ROMS[@]}" > "$OUT/$m.log" 2>&1
	case $? in
		0) coemu_compare "$m" ;;
		1) coemu_result "$m" error "DONE not set after $MAX_FRAMES frames, see $OUT/$m.log" ;;
		*) coemu_result "$m" error "$(grep -o 'driver: error: .*' "$OUT/$m.log" | head -1 | cut -c16-) (see $OUT/$m.log)" ;;
	esac
done
coemu_finish
