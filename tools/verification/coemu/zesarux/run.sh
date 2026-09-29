#!/usr/bin/env bash
# Co-emulation runner: ZEsarUX, headless, driven over its remote protocol ZRCP (see ../README.md).
#   ZESARUX_BIN=<zesarux binary> ./run.sh [machine...]
# The machines run at the same time, each its own ZEsarUX on its own ZRCP port, in real time (see README.md).
. "$(dirname "$0")/../common/common.sh"
coemu_init zesarux "$@"

BIN=$(coemu_find ZESARUX_BIN zesarux) || coemu_not_found "install ZEsarUX (zesarux on PATH) or set ZESARUX_BIN"
case $BIN in /*) ;; *) BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN") ;; esac
HERE=$(cd "$(dirname "$0")" && pwd)
# ZEsarUX changes to its own folder on start: the program's files need absolute paths
abspath() { echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
TAP=$(abspath "$PROGRAM_TAP")
TRD=$([ -f "$PROGRAM_TRD" ] && abspath "$PROGRAM_TRD")

DONE=$(coemu_sym DONE)
START=$(coemu_sym START)
END=$(coemu_sym PROBEEND)
PORTBASE=${ZESARUX_PORT:-10100}

# Stock settings, no config file, no video or sound. --accelerate-loading: top speed while the tape plays only
COMMON="--noconfigfile --vo null --ao null --nowelcomemessage --quickexit --accelerate-loading --enable-remoteprotocol"

# run_machine <machine> <zesarux machine id> <frame T-states> <port> <how: tape|trd|baseconf>
run_machine() {
	m=$1 id=$2 frame=$3 port=$4 how=$5
	log=$OUT/$m.log
	: > "$log"
	if [ "$how" = trd ] || [ "$how" = baseconf ]; then
		# TR-DOS: the Betadisk interface with the disk, write-protected, never written back to the file
		media="--enable-betadisk --enable-trd --trd-file $TRD --trd-write-protection --trd-no-persistent-writes"
		drive="--trdos"
		[ "$how" = trd ] || drive="--baseconf"
	else
		# The tape played through the ROM loader; ZEsarUX's autoload types LOAD "" / picks the menu's loader
		media="--realtape $TAP"
		drive=""
	fi
	echo "zesarux: $BIN $COMMON --remoteprotocol-port $port --machine $id $media" >> "$log"
	# shellcheck disable=SC2086
	"$BIN" $COMMON --remoteprotocol-port "$port" --machine "$id" $media < /dev/null >> "$log" 2>&1 &
	pid=$!
	# shellcheck disable=SC2086
	python3 "$HERE/zrcp.py" run "$port" --done "$DONE" --start "$START" --end "$END" --frame-tstates "$frame" \
		--max-frames "$MAX_FRAMES" --out "$OUT/$m" $drive >> "$log" 2>&1
	echo $? > "$OUT/$m.rc"
	for _ in 1 2 3 4 5 6 7 8 9 10; do
		kill -0 "$pid" 2>/dev/null || break
		sleep 0.5
	done
	kill "$pid" 2>/dev/null && { sleep 1; kill -9 "$pid" 2>/dev/null; }
	wait "$pid" 2>/dev/null
}

set -- $MACHINES
PORTS=$(python3 "$HERE/zrcp.py" freeports "$PORTBASE" $#)
set -- $PORTS
RUN=""
for m in $MACHINES; do
	case $m in
		48k) args="48k 69888" ;;
		128k) args="128k 70908" ;;
		plus2) args="P2 70908" ;;
		plus2a) args="P2A41 70908" ;;
		plus3) args="P341 70908" ;;
		pentagon) args="Pentagon 71680" ;;
		atm3) args="BaseConf 71680" ;;
		scorpion | profscorp) coemu_result "$m" skipped "ZEsarUX has no Scorpion"; continue ;;
		atm710) coemu_result "$m" skipped "ZEsarUX has no ATM Turbo (only the ZX-Evo BaseConf and TS-Conf)"; continue ;;
		profi) coemu_result "$m" skipped "ZEsarUX has no Profi"; continue ;;
		*) coemu_result "$m" skipped "ZEsarUX has no such machine"; continue ;;
	esac
	how=tape
	case $m in
		pentagon) [ -n "$TRD" ] && how=trd ;;
		atm3) how=baseconf ;;
	esac
	rm -f "$OUT/$m.bin" "$OUT/$m.rc" "$OUT/$m.screen.txt"
	echo "coemu[zesarux] $m: running on ZRCP port $1"
	run_machine "$m" $args "$1" "$how" &
	shift
	RUN="$RUN $m"
done
wait

for m in $RUN; do
	rc=$(cat "$OUT/$m.rc" 2>/dev/null)
	case $rc in
		0) coemu_compare "$m" ;;
		1) coemu_result "$m" error "DONE not set after $MAX_FRAMES frames, see $OUT/$m.log" ;;
		*) coemu_result "$m" error "$(grep -o 'zrcp: error: .*' "$OUT/$m.log" | tail -1 | cut -c14-) (see $OUT/$m.log)" ;;
	esac
	rm -f "$OUT/$m.rc"
done
coemu_finish
