#!/usr/bin/env bash
# Co-emulation runner: SkoolKit's contention simulator (see ../README.md for the contract, README.md here).
#   SKOOLKIT_PYTHON=<a python that can import skoolkit> ./run.sh [machine...]
# SkoolKit simulates the 48K and the 128K; every other machine is skipped.
. "$(dirname "$0")/../common/common.sh"
coemu_init skoolkit "$@"
HERE=$(cd "$(dirname "$0")" && pwd)

# Discovery: SKOOLKIT_PYTHON, then python3, then the interpreter of tap2sna.py on PATH
has_skoolkit() { [ -n "$1" ] && "$1" -c 'import skoolkit' >/dev/null 2>&1; }
PY=""
if [ -n "${SKOOLKIT_PYTHON:-}" ]; then
	has_skoolkit "$SKOOLKIT_PYTHON" && PY=$SKOOLKIT_PYTHON
elif has_skoolkit python3; then
	PY=python3
elif T=$(command -v tap2sna.py 2>/dev/null); then
	I=$(head -1 "$T" | sed -n 's/^#! *//p' | awk '{print $1}')
	[ "$(basename "$I")" = env ] && I=$(head -1 "$T" | awk '{print $2}')
	has_skoolkit "$I" && PY=$I
fi
[ -n "$PY" ] || coemu_not_found "install SkoolKit (pip install skoolkit) or set SKOOLKIT_PYTHON to a python that has it"

DONE=$(coemu_sym DONE)
START=$(coemu_sym START)
END=$(coemu_sym PROBEEND)

for m in $MACHINES; do
	rm -f "$OUT/$m.bin" "$OUT/$m.log" "$OUT/$m.screen.txt"
	case $m in
		48k) sk=48 ;;
		128k) sk=128 ;;
		*) coemu_result "$m" skipped "SkoolKit simulates only the 48K and the 128K"; continue ;;
	esac
	if "$PY" "$HERE/skoolkit-run.py" --machine "$sk" --tap "$PROGRAM_TAP" --done "$DONE" --start "$START" \
		--end "$END" --max-frames "$MAX_FRAMES" --out "$OUT/$m" 2>> "$OUT/$m.log"; then
		coemu_compare "$m"
	else
		coemu_result "$m" error "$(tail -1 "$OUT/$m.log")"
	fi
done
coemu_finish
