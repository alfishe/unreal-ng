# Shared helpers for co-emulation runners (see ../README.md). Source it from a runner's run.sh:
#
#   . "$(dirname "$0")/../common/common.sh"
#   coemu_init <emulator-name> "$@"          # parses the machines, sets PROGRAM_*, OUT, MACHINES
#   BIN=$(coemu_find XYZ_BIN xyz xyz-sdl)     # discovery: $XYZ_BIN, then PATH
#   ... run each machine, write "$OUT/<machine>.bin" ...
#   coemu_compare <machine>                   # writes the machine's result from its dump
#   coemu_result <machine> skipped "no Scorpion in this emulator"
#   coemu_finish                              # exit status: see the README
#
# Everything a runner produces goes to $OUT (tools/verification/coemu/out/<emulator>/ unless OUT is set).

COEMU_DIR=$(cd "$(dirname "${BASH_SOURCE:-$0}")/.." && pwd)
COEMU_ROOT=$(cd "$COEMU_DIR/../../.." && pwd)

# The machine names every runner understands
COEMU_ALL_MACHINES="48k 128k plus2 plus2a plus3 pentagon scorpion profscorp atm710 atm3 profi"

coemu_init() {
	COEMU_NAME=$1
	shift
	# The program: PROGRAM names its files without the extension (<PROGRAM>.tap, .trd, .sym, -compare.py)
	PROGRAM=${PROGRAM:-$COEMU_ROOT/tools/verification/contention/ctprobe/ctprobe}
	PROGRAM_TAP=$PROGRAM.tap
	PROGRAM_TRD=$PROGRAM.trd
	PROGRAM_SYM=$PROGRAM.sym
	PROGRAM_COMPARE=$(dirname "$PROGRAM")/$(basename "$PROGRAM")-compare.py
	for f in "$PROGRAM_TAP" "$PROGRAM_SYM"; do
		[ -f "$f" ] || { echo "coemu: $f not found (PROGRAM=$PROGRAM)" >&2; exit 2; }
	done
	OUT=${OUT:-$COEMU_DIR/out/$COEMU_NAME}
	mkdir -p "$OUT"
	MAX_FRAMES=${MAX_FRAMES:-60000}
	if [ $# -gt 0 ]; then
		MACHINES="$*"
	else
		MACHINES=$COEMU_ALL_MACHINES
	fi
	for m in $MACHINES; do
		case " $COEMU_ALL_MACHINES " in
			*" $m "*) ;;
			*) echo "coemu: unknown machine '$m' (known: $COEMU_ALL_MACHINES)" >&2; exit 2 ;;
		esac
		rm -f "$OUT/$m.result"
	done
	COEMU_STATUS=0
}

# The address of a symbol in the program's .sym ("NAME equ #ABCD"), in decimal
coemu_sym() {
	v=$(sed -n "s/^$1 equ #\([0-9A-Fa-f]*\)$/\1/p" "$PROGRAM_SYM")
	[ -n "$v" ] || { echo "coemu: symbol $1 not in $PROGRAM_SYM" >&2; exit 2; }
	printf '%d\n' "0x$v"
}

# Discovery: the first of $<VAR> and the given names found on PATH (or an existing path); empty if none
coemu_find() {
	var=$1
	shift
	eval "given=\${$var:-}"
	if [ -n "$given" ]; then
		[ -e "$given" ] || command -v "$given" >/dev/null 2>&1 && { echo "$given"; return 0; }
		echo "coemu: $var=$given does not exist" >&2
		return 1
	fi
	for n in "$@"; do
		p=$(command -v "$n" 2>/dev/null) && { echo "$p"; return 0; }
	done
	return 1
}

# The emulator was not found: every machine is skipped, the runner exits 3
coemu_not_found() {
	for m in $MACHINES; do
		coemu_result "$m" skipped "$1"
	done
	echo "coemu[$COEMU_NAME]: $1" >&2
	exit 3
}

# coemu_result <machine> ok|wrong|error|skipped <detail>
coemu_result() {
	printf '%s %s\n' "$2" "$3" > "$OUT/$1.result"
	echo "coemu[$COEMU_NAME] $1: $2 $3"
	case $2 in
		error) COEMU_STATUS=2 ;;
		wrong) [ "$COEMU_STATUS" -eq 2 ] || COEMU_STATUS=1 ;;
	esac
}

# The machine's result from its dump ($OUT/<machine>.bin), through the program's compare script
coemu_compare() {
	dump=$OUT/$1.bin
	[ -s "$dump" ] || { coemu_result "$1" error "no dump"; return; }
	if [ ! -f "$PROGRAM_COMPARE" ]; then
		coemu_result "$1" error "no compare script $PROGRAM_COMPARE"
		return
	fi
	python3 "$PROGRAM_COMPARE" "$dump" "$PROGRAM_SYM" > "$OUT/$1.compare.txt" 2>&1
	rc=$?
	summary=$(tail -1 "$OUT/$1.compare.txt")
	case $rc in
		0) coemu_result "$1" ok "$summary" ;;
		1) coemu_result "$1" wrong "$summary" ;;
		3) coemu_result "$1" skipped "$summary" ;;
		*) coemu_result "$1" error "$summary" ;;
	esac
}

coemu_finish() {
	exit "$COEMU_STATUS"
}
