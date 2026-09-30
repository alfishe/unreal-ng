#!/usr/bin/env bash
# Co-emulation runner: MAME, headless (see ../README.md for the contract and README.md here).
#   MAME_BIN=<mame binary> [MAME_ROMPATH=<rompath>] ./run.sh [machine...]
# Loads the program on MAME's stock Spectrum drivers the way a user would, driven by coemu.lua.
. "$(dirname "$0")/../common/common.sh"
coemu_init mame "$@"

MAME=$(coemu_find MAME_BIN mame) || coemu_not_found "mame not found: install MAME or set MAME_BIN"
case $MAME in
	*/*) MAME=$(cd "$(dirname "$MAME")" && pwd)/$(basename "$MAME") ;;  # MAME runs from build/run
esac

HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${MAME_BUILD:-$HERE/build}
mkdir -p "$BUILD/run"

driver_of() {
	case $1 in
		48k) echo spectrum ;;
		128k) echo spec128 ;;
		plus2) echo specpls2 ;;
		plus2a) echo specpl2a ;;
		plus3) echo specpls3 ;;
		pentagon) echo pentagon ;;
		scorpion) echo scorpio ;;
		profscorp) echo scorpiontb ;;
		atm710) echo atmtb2plus ;;
		atm3) echo pentevo ;;
		profi) echo profi ;;
	esac
}

# A driver MAME has but this runner cannot load the program on (see README.md)
not_runnable() {
	case $1 in
		profi) echo "MAME's profi driver is marked not working (a Scorpion board with a Profi ROM, no Profi video): with unreal-ng's Profi ROM (-bios v7, the only one found) the screen stays black, there is no loader to type into" ;;
	esac
}

# The drivers this MAME has
DRIVERS=""
for m in $MACHINES; do
	d=$(driver_of "$m")
	[ -n "$d" ] || { coemu_result "$m" skipped "not in this runner"; continue; }
	if "$MAME" -listfull "$d" > /dev/null 2>&1; then
		DRIVERS="$DRIVERS $d"
	fi
done

# ROMs: MAME_ROMPATH as given, else a rompath under build/ made from unreal-ng's own ROM files
ROMSTATUS=$BUILD/romstatus.txt
: > "$ROMSTATUS"
if [ -n "${MAME_ROMPATH:-}" ]; then
	ROMPATH=$MAME_ROMPATH
elif [ -n "$DRIVERS" ]; then
	ROMPATH=$BUILD/roms
	rm -rf "$ROMPATH"
	python3 "$HERE/romset.py" "$MAME" "$ROMPATH" "$COEMU_ROOT/data/rom" "$COEMU_ROOT/testdata" -- $DRIVERS \
		> "$ROMSTATUS" 2> "$OUT/romset.log" || echo "romset.py failed, see $OUT/romset.log" >&2
fi

DONE_ADDR=$(coemu_sym DONE)
START_ADDR=$(coemu_sym START)
END_ADDR=$(coemu_sym PROBEEND)
SECONDS_TO_RUN=$(( MAX_FRAMES / 50 + 30 ))

for m in $MACHINES; do
	d=$(driver_of "$m")
	[ -n "$d" ] || continue  # reported above
	case " $DRIVERS " in
		*" $d "*) ;;
		*) coemu_result "$m" skipped "no driver $d in this MAME"; continue ;;
	esac
	why=$(not_runnable "$m")
	[ -z "$why" ] || { coemu_result "$m" skipped "$why"; continue; }
	biosargs=()
	if [ -z "${MAME_ROMPATH:-}" ]; then
		rs=$(sed -n "s/^$d //p" "$ROMSTATUS")
		case $rs in
			ok\ *) ;;
			*) coemu_result "$m" skipped "ROMs for $d: ${rs:-not found} (set MAME_ROMPATH)"; continue ;;
		esac
		# The BIOS romset.py found complete: the default one when it can
		bios=$(echo "$rs" | sed -n 's/.*bios=\([^ ]*\).*/\1/p')
		[ -z "$bios" ] || [ "$bios" = - ] || biosargs=(-bios "$bios")
	fi

	# Media: the .trd through TR-DOS on the Beta Disk machines, the tape everywhere else
	media=tape
	mediaargs=(-cass "$PROGRAM_TAP")
	case $m in
		pentagon | scorpion | profscorp | atm710)
			if [ -f "$PROGRAM_TRD" ] && "$MAME" "$d" -listmedia 2>/dev/null | grep -q '(flop1)'; then
				media=disk
				mediaargs=(-flop1 "$PROGRAM_TRD")
			fi
			;;
	esac

	rm -f "$OUT/$m.bin" "$OUT/$m.screen.txt" "$BUILD/run/$m.scr" "$BUILD/run/$m.rom"
	(
		cd "$BUILD/run" &&
		COEMU_MACHINE=$m COEMU_MEDIA=$media COEMU_DONE=$DONE_ADDR COEMU_START=$START_ADDR COEMU_END=$END_ADDR \
		COEMU_MAX_FRAMES=$MAX_FRAMES COEMU_BIN=$OUT/$m.bin COEMU_WORK=$BUILD/run/$m \
		SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
		"$MAME" "$d" "${biosargs[@]}" -rompath "$ROMPATH" "${mediaargs[@]}" \
			-video none -sound none -window -nomaximize -nothrottle -skip_gameinfo -noreadconfig -noplugins \
			-cfg_directory "$BUILD/run/cfg" -nvram_directory "$BUILD/run/nvram" \
			-seconds_to_run "$SECONDS_TO_RUN" -autoboot_script "$HERE/coemu.lua"
	) > "$OUT/$m.log" 2>&1
	rc=$?

	if [ -s "$BUILD/run/$m.scr" ] && [ -s "$BUILD/run/$m.rom" ]; then
		python3 "$HERE/screentext.py" "$BUILD/run/$m.scr" "$BUILD/run/$m.rom" > "$OUT/$m.screen.txt" 2>/dev/null
	fi
	if [ -s "$OUT/$m.bin" ]; then
		coemu_compare "$m"
	elif grep -q 'coemu.lua: reset' "$OUT/$m.log"; then
		coemu_result "$m" error "the machine reset while the program ran ($media): its memory was cleared before DONE was set"
	elif grep -q 'coemu.lua: timeout' "$OUT/$m.log"; then
		last=$(grep -v '^ *$' "$OUT/$m.screen.txt" 2>/dev/null | tail -1 | sed 's/^ *//')
		coemu_result "$m" error "DONE not 1 after $MAX_FRAMES frames ($media); screen ends: '$last'"
	else
		coemu_result "$m" error "mame exited ($rc) without a dump: $(grep -v '^ *$' "$OUT/$m.log" | tail -1)"
	fi
done
coemu_finish
