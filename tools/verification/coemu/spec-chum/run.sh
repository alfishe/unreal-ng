#!/usr/bin/env bash
# Co-emulation runner: spec_chum, headless, driven over its loopback HTTP API (see ../README.md).
#   SPEC_CHUM_DIR=<spec_chum checkout> ./run.sh [machine...]     # builds spec-chum-agent into build/
#   SPEC_CHUM_BIN=<spec-chum-agent binary> ./run.sh [machine...]
# ROMs: unreal-ng's own (data/rom), laid out under build/romroot/roms/ the way spec_chum looks for them.
. "$(dirname "$0")/../common/common.sh"
coemu_init spec-chum "$@"

HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=$HERE/build
CARGO=${CARGO:-$(command -v cargo || echo "$HOME/.cargo/bin/cargo")}
# Half the host's cores, at most 4: other builds share this machine
JOBS=$(( $(sysctl -n hw.ncpu 2>/dev/null || nproc) / 2 )); JOBS=$(( JOBS < 1 ? 1 : JOBS > 4 ? 4 : JOBS ))

# Discovery: SPEC_CHUM_BIN (a built spec-chum-agent), else SPEC_CHUM_DIR (a checkout, built here)
HINT="set SPEC_CHUM_DIR to a spec_chum checkout (github.com/mward-sudo/spec_chum) or SPEC_CHUM_BIN to a built spec-chum-agent"
if [ -n "${SPEC_CHUM_BIN:-}" ]; then
	BIN=$(coemu_find SPEC_CHUM_BIN spec-chum-agent) || coemu_not_found "$HINT"
else
	[ -n "${SPEC_CHUM_DIR:-}" ] && [ -f "$SPEC_CHUM_DIR/crates/agent_server/Cargo.toml" ] || coemu_not_found "$HINT"
	[ -x "$CARGO" ] || coemu_not_found "cargo not found (install Rust, or set CARGO or SPEC_CHUM_BIN)"
	mkdir -p "$BUILD"
	echo "coemu[spec-chum]: building spec-chum-agent (cargo -j $JOBS, log in $BUILD/cargo.log)"
	# The checkout is not changed: all build products go to build/target
	if ! (cd "$SPEC_CHUM_DIR" && CARGO_TARGET_DIR=$BUILD/target "$CARGO" build --release -j "$JOBS" \
		-p agent_server --bin spec-chum-agent) > "$BUILD/cargo.log" 2>&1; then
		for m in $MACHINES; do coemu_result "$m" error "cargo build failed, see $BUILD/cargo.log"; done
		coemu_finish
	fi
	BIN=$BUILD/target/release/spec-chum-agent
fi
case $BIN in /*) ;; *) BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN") ;; esac

# The ROMs, from unreal-ng's data/rom, where spec_chum's ROM catalog looks for them (roms/... under a root)
ROMS=$COEMU_ROOT/data/rom
ROOT=$BUILD/romroot
mkdir -p "$ROOT/roms/128" "$ROOT/roms/plus2" "$ROOT/roms/plus2a" "$ROOT/roms/plus3" "$ROOT/roms/pentagon" \
	"$ROOT/roms/scorpion"
cp "$ROMS/48.rom" "$ROOT/roms/spec48.rom"
cp "$ROMS/128.rom" "$ROOT/roms/128/spec128uk.rom"
cp "$ROMS/plus2.rom" "$ROOT/roms/plus2/plus2uk.rom"
cp "$ROMS/plus2a.rom" "$ROOT/roms/plus2a/plus2a.rom"
cp "$ROMS/plus3.rom" "$ROOT/roms/plus3/plus3.rom"
# Pentagon 128: its own 128 ROM (32K); TR-DOS 5.04TM, the TR-DOS page of unreal-ng's pentagon.rom
cp "$ROMS/pentagon128k.rom" "$ROOT/roms/pentagon/pentagon.rom"
dd if="$ROMS/pentagon.rom" of="$ROOT/roms/pentagon/trdos.rom" bs=16384 skip=1 count=1 2>/dev/null
# Scorpion ZS-256: pages 0-2 of scorpion.rom (128 BASIC, 48 BASIC, service monitor), page 3 is its TR-DOS
dd if="$ROMS/scorpion.rom" of="$ROOT/roms/scorpion/scorpion.rom" bs=16384 count=3 2>/dev/null
dd if="$ROMS/scorpion.rom" of="$ROOT/roms/scorpion/trdos.rom" bs=16384 skip=3 count=1 2>/dev/null

DONE=$(coemu_sym DONE)
START=$(coemu_sym START)
END=$(coemu_sym PROBEEND)

for m in $MACHINES; do
	case $m in
		48k) args="48k keyword" ;;
		128k) args="128k menu" ;;
		plus2) args="plus2 menu" ;;
		plus2a) args="plus2a menu" ;;
		plus3) args="plus3 plus3basic" ;;
		pentagon) args="pentagon menu" ;;  # the tape: TR-DOS does not run in spec_chum (README)
		scorpion) args="scorpion diskboot" ;;
		profscorp) coemu_result "$m" skipped "spec_chum's Scorpion has no ProfROM (it takes a 48 KB ROM)"; continue ;;
		atm710 | atm3) coemu_result "$m" skipped "spec_chum has no ATM Turbo or ZX-Evo"; continue ;;
		profi) coemu_result "$m" skipped "spec_chum has no Profi"; continue ;;
		*) coemu_result "$m" skipped "spec_chum has no such machine"; continue ;;
	esac
	set -- $args
	rm -f "$OUT/$m.bin" "$OUT/$m.scr" "$OUT/$m.screen.txt"
	echo "coemu[spec-chum] $m: running"
	python3 "$HERE/chum.py" run --bin "$BIN" --romroot "$ROOT" --model "$1" --load "$2" \
		--tap "$PROGRAM_TAP" --trd "$PROGRAM_TRD" --done "$DONE" --start "$START" --end "$END" \
		--max-frames "$MAX_FRAMES" --out "$OUT/$m" > "$OUT/$m.log" 2>&1
	rc=$?
	if [ -s "$OUT/$m.scr" ]; then
		python3 "$COEMU_DIR/mame/screentext.py" "$OUT/$m.scr" "$ROMS/48.rom" > "$OUT/$m.screen.txt" 2>/dev/null
		rm -f "$OUT/$m.scr"
	fi
	case $rc in
		0) coemu_compare "$m" ;;
		1) coemu_result "$m" error "DONE not set after $MAX_FRAMES frames, see $OUT/$m.log" ;;
		*) coemu_result "$m" error "$(grep -o 'chum: error: .*' "$OUT/$m.log" | tail -1 | cut -c14-) (see $OUT/$m.log)" ;;
	esac
done
coemu_finish
