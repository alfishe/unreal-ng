#!/usr/bin/env bash
# Co-emulation runner: FUSE (the Free Unix Spectrum Emulator), headless (see ../README.md for the contract).
#   FUSE_DIR=<fuse source tree> ./run.sh [machine...]   # builds a patched copy under build/
#   FUSE_BIN=<patched fuse binary> ./run.sh [machine...]
# FUSE has no scripting; coemu-frame-hook.patch adds a once-per-frame check (driven by COEMU_* environment
# variables) that dumps memory and exits. Emulation is otherwise stock FUSE.
. "$(dirname "$0")/../common/common.sh"
coemu_init fuse "$@"

HERE=$(cd "$(dirname "$0")" && pwd)
PATCH=$HERE/coemu-frame-hook.patch
MARKER=COEMU_DONE_ADDR

build_fail() {
	for m in $MACHINES; do coemu_result "$m" error "$1"; done
	coemu_finish
}

if [ -n "${FUSE_BIN:-}" ]; then
	BIN=$(coemu_find FUSE_BIN fuse) || coemu_not_found "FUSE_BIN=$FUSE_BIN does not exist"
	grep -q "$MARKER" "$BIN" 2>/dev/null ||
		coemu_not_found "FUSE_BIN=$BIN is not patched with fuse/coemu-frame-hook.patch; set FUSE_DIR to a FUSE source tree instead"
elif [ -n "${FUSE_DIR:-}" ] && [ -f "$FUSE_DIR/spectrum.c" ] && [ -x "$FUSE_DIR/configure" ]; then
	# Build a patched copy of the source tree; the tree itself is not touched
	BUILD=${FUSE_BUILD:-$HERE/build}
	SRC=$BUILD/$(basename "$(cd "$FUSE_DIR" && pwd)")
	BIN=$SRC/fuse
	if [ ! -x "$BIN" ] || [ "$PATCH" -nt "$BIN" ]; then
		echo "coemu[fuse]: building $SRC (log: $OUT/build.log)"
		{
			rm -rf "$SRC" && mkdir -p "$BUILD" && cp -R "$FUSE_DIR" "$SRC" &&
				(cd "$SRC" && make distclean >/dev/null 2>&1; patch -p1 < "$PATCH") &&
				(
					cd "$SRC" || exit 1
					# Homebrew's libspectrum.h includes gcrypt.h from the Homebrew prefix
					if command -v brew >/dev/null 2>&1; then
						p=$(brew --prefix)
						CPPFLAGS="${CPPFLAGS:-} -I$p/include"
						LDFLAGS="${LDFLAGS:-} -L$p/lib"
						export CPPFLAGS LDFLAGS
					fi
					./configure --with-null-ui --with-audio-driver=null --without-libxml2 --without-joystick &&
						make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
				)
		} > "$OUT/build.log" 2>&1 || build_fail "build failed, see $OUT/build.log"
		[ -x "$BIN" ] || build_fail "build produced no fuse binary, see $OUT/build.log"
	fi
else
	coemu_not_found "set FUSE_DIR to a FUSE source tree (fuse-emulator.sourceforge.net; needs libspectrum) or FUSE_BIN to a patched fuse"
fi

# FUSE runs in its own working folder (below), so every path it gets is made absolute
abspath() { echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
BIN=$(abspath "$BIN")
OUT=$(cd "$OUT" && pwd)
PROGRAM_TAP=$(abspath "$PROGRAM_TAP")
[ -f "$PROGRAM_TRD" ] && PROGRAM_TRD=$(abspath "$PROGRAM_TRD")

# The 48K ROM (for the screen as text): next to the binary, as FUSE itself looks for it
ROM48=${FUSE_ROM48:-$(dirname "$BIN")/roms/48.rom}

DONE_ADDR=$(coemu_sym DONE)
FROM=$(coemu_sym START)
TO=$(coemu_sym PROBEEND)

# A private HOME so no ~/.fuserc changes the stock settings
FUSE_HOME=$OUT/home
mkdir -p "$FUSE_HOME"

# FUSE ships the Sinclair / Amstrad ROMs only. The Pentagon and Scorpion ROMs (FUSE's file names: 128p-0/1,
# 256s-0..3, trdos) come from FUSE_ROMS (a folder with those files) or else from this repository's data/rom,
# split into 16K pages. They go under their stock names into FUSE's working folder, the first place FUSE looks
# for ROMs: naming them with --rom-* options would make them "custom ROMs", which turns autoload off
ROMS=$FUSE_HOME
clone_roms() {
	if [ -n "${FUSE_ROMS:-}" ]; then
		for f in 128p-0 128p-1 256s-0 256s-1 256s-2 256s-3 trdos; do
			[ -f "$FUSE_ROMS/$f.rom" ] || { echo "no $FUSE_ROMS/$f.rom"; return 1; }
			cp "$FUSE_ROMS/$f.rom" "$ROMS/" || return 1
		done
		return 0
	fi
	r=$COEMU_ROOT/data/rom
	python3 - "$r/pentagon128k.rom" "$r/scorpion.rom" "$r/trdos.rom" "$ROMS" <<-'EOF'
		import sys
		pent, scorp, trdos, out = sys.argv[1:]
		def split(src, names):
		    data = open(src, 'rb').read()
		    assert len(data) == 0x4000 * len(names), src + ': unexpected size'
		    for i, n in enumerate(names):
		        open(out + '/' + n + '.rom', 'wb').write(data[i * 0x4000:(i + 1) * 0x4000])
		split(pent, ['128p-0', '128p-1'])        # 128 BASIC, 48 BASIC
		split(scorp, ['256s-0', '256s-1', '256s-2', '256s-3'])  # 128, 48, service, TR-DOS
		split(trdos, ['trdos'])
	EOF
}
if clone_roms > "$OUT/roms.log" 2>&1; then
	CLONE_ROMS=1
else
	CLONE_ROMS=0
fi

for m in $MACHINES; do
	case $m in
		48k) x=48 ;;
		128k) x=128 ;;
		plus2) x=plus2 ;;
		plus2a) x=plus2a ;;
		plus3) x=plus3 ;;
		pentagon) x=pentagon ;;
		scorpion)
			# FUSE 1.6.0's auto-load does not start a TR-DOS disk on its Scorpion (it ends in 48 BASIC,
			# the program never loads), and the tape through the Scorpion menu gives "Tape loading error"
			coemu_result "$m" skipped "FUSE's auto-load does not boot a TR-DOS disk on its Scorpion"
			continue ;;
		*) coemu_result "$m" skipped "FUSE has no such machine"; continue ;;
	esac
	case $m in
		pentagon | scorpion)
			# Built-in Beta 128: autoload boots TR-DOS, which runs the disk's "boot"
			[ -f "$PROGRAM_TRD" ] || { coemu_result "$m" skipped "no $PROGRAM_TRD"; continue; }
			[ "$CLONE_ROMS" = 1 ] || { coemu_result "$m" skipped "no Pentagon/Scorpion ROMs, see $OUT/roms.log"; continue; }
			media=(--betadisk "$PROGRAM_TRD") ;;
		*)
			# The phantom typist types LOAD "" (48K) or picks the menu's tape loader (128K, +2, +2A, +3)
			media=(--tape "$PROGRAM_TAP") ;;
	esac
	rm -f "$OUT/$m.bin" "$OUT/$m.scr" "$OUT/$m.screen.txt"
	(
		cd "$FUSE_HOME" || exit 1
		HOME=$FUSE_HOME \
			COEMU_DONE_ADDR=$DONE_ADDR COEMU_DUMP_FROM=$FROM COEMU_DUMP_TO=$TO \
			COEMU_DUMP_FILE=$OUT/$m.bin COEMU_SCREEN_FILE=$OUT/$m.scr COEMU_MAX_FRAMES=$MAX_FRAMES \
			"$BIN" --machine "$x" --no-sound --speed 100000 --auto-load "${media[@]}"
	) > "$OUT/$m.log" 2>&1
	rc=$?
	if [ -s "$OUT/$m.scr" ] && [ -f "$ROM48" ]; then
		python3 "$HERE/screen-text.py" "$OUT/$m.scr" "$ROM48" "$OUT/$m.screen.txt" >> "$OUT/$m.log" 2>&1
	fi
	rm -f "$OUT/$m.scr"
	if [ ! -s "$OUT/$m.bin" ]; then
		if grep -q "DONE not set" "$OUT/$m.log"; then
			coemu_result "$m" error "DONE not set after $MAX_FRAMES frames, see $OUT/$m.log"
		else
			coemu_result "$m" error "fuse exited ($rc) without a dump, see $OUT/$m.log"
		fi
		continue
	fi
	coemu_compare "$m"
done
coemu_finish
