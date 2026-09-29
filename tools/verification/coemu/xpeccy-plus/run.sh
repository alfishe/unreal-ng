#!/usr/bin/env bash
# Co-emulation runner: xpeccy-plus, headless (see ../README.md for the contract).
#   XPECCY_DIR=<xpeccy-plus checkout> ./run.sh [machine...]
# Compiles the xpeccy-plus core, unmodified, from the checkout; runs the program on its stock machines.
. "$(dirname "$0")/../common/common.sh"
coemu_init xpeccy-plus "$@"

if [ -z "${XPECCY_DIR:-}" ] || [ ! -d "$XPECCY_DIR/src/libxpeccy" ]; then
	coemu_not_found "set XPECCY_DIR to an xpeccy-plus checkout"
fi

HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${XPECCY_BUILD:-$HERE/build}
if ! { cmake -S "$HERE" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DXPECCY_DIR="$XPECCY_DIR" &&
	ninja -C "$BUILD"; } > "$OUT/build.log" 2>&1; then
	for m in $MACHINES; do coemu_result "$m" error "build failed, see $OUT/build.log"; done
	coemu_finish
fi

for m in $MACHINES; do
	# the stock machine (res/machines/<id>.conf) and what the program is loaded from
	case $m in
		48k) x=zx48; media=$PROGRAM_TAP ;;
		128k) x=zx128; media=$PROGRAM_TAP ;;
		plus2) x=zxplus2; media=$PROGRAM_TAP ;;
		plus2a) x=zxplus2a; media=$PROGRAM_TAP ;;
		plus3) x=zxplus3; media=$PROGRAM_TAP ;;
		pentagon) x=pent; media=$PROGRAM_TRD ;;
		scorpion) x=scorp; media=$PROGRAM_TRD ;;
		atm710) x=atm2; media=$PROGRAM_TRD ;;
		profi) x=profi; media=$PROGRAM_TRD ;;
		atm3) x=evo-baseconf; media=$PROGRAM_TRD ;;
		profscorp) coemu_result "$m" skipped "no stock machine: xpeccy-plus ships prof39f.rom, but no machine definition uses it"; continue ;;
		*) coemu_result "$m" skipped "not in this runner yet"; continue ;;
	esac
	if [ ! -f "$media" ]; then
		coemu_result "$m" skipped "no $(basename "$media") for this program"
		continue
	fi
	rm -f "$OUT/$m.bin"
	"$BUILD/ctharness" "$x" "$XPECCY_DIR/config/roms" "$media" "$PROGRAM_SYM" "$OUT/$m" "$MAX_FRAMES" > "$OUT/$m.log" 2>&1
	if [ $? -eq 3 ]; then		# the harness saw the program crash (see the log)
		coemu_result "$m" error "$(grep -m1 'crashed' "$OUT/$m.log")"
		continue
	fi
	coemu_compare "$m"
done
coemu_finish
