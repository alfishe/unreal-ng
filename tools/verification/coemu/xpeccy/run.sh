#!/usr/bin/env bash
# Co-emulation runner: upstream Xpeccy, headless (see ../README.md for the contract).
#   XPECCY_UPSTREAM_DIR=<Xpeccy checkout> ./run.sh [machine...]
# Compiles the Xpeccy core, unmodified, from the checkout; runs the program on each machine built as a
# profile of that hardware with the default settings (see README.md). Upstream ships no ROMs besides
# 1982.rom: the ROM files come from XPECCY_ROMS (default: this repository's data/rom).
. "$(dirname "$0")/../common/common.sh"
coemu_init xpeccy "$@"

if [ -z "${XPECCY_UPSTREAM_DIR:-}" ] || [ ! -d "$XPECCY_UPSTREAM_DIR/src/libxpeccy" ]; then
	coemu_not_found "set XPECCY_UPSTREAM_DIR to an Xpeccy checkout (upstream, not xpeccy-plus)"
fi
ROMS=${XPECCY_ROMS:-$COEMU_ROOT/data/rom}

HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${XPECCY_BUILD:-$HERE/build}
if ! { cmake -S "$HERE" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DXPECCY_UPSTREAM_DIR="$XPECCY_UPSTREAM_DIR" &&
	ninja -C "$BUILD"; } > "$OUT/build.log" 2>&1; then
	for m in $MACHINES; do coemu_result "$m" error "build failed, see $OUT/build.log"; done
	coemu_finish
fi

for m in $MACHINES; do
	# what the program is loaded from (the machine itself is in ctharness.c's table)
	case $m in
		48k|128k|plus2|plus2a|plus3) media=$PROGRAM_TAP ;;
		*) media=$PROGRAM_TRD ;;
	esac
	if [ ! -f "$media" ]; then
		coemu_result "$m" skipped "no $(basename "$media") for this program"
		continue
	fi
	rm -f "$OUT/$m.bin"
	"$BUILD/ctharness" "$m" "$ROMS" "$media" "$PROGRAM_SYM" "$OUT/$m" "$MAX_FRAMES" > "$OUT/$m.log" 2>&1
	case $? in
		1) coemu_result "$m" error "the program did not finish in $MAX_FRAMES frames (DONE is not 1)"; continue ;;
		2) coemu_result "$m" error "$(tail -1 "$OUT/$m.log")"; continue ;;
		3) coemu_result "$m" error "$(grep -m1 'crashed' "$OUT/$m.log")"; continue ;;	# the program crashed
	esac
	coemu_compare "$m"
done
coemu_finish
