#!/usr/bin/env bash
# Run the program on every co-emulation runner (every <emulator>/run.sh here) and print one table.
#   ./run-all.sh [machine...]
# Environment variables are passed to the runners (PROGRAM, MAX_FRAMES, XPECCY_DIR, ...). See README.md.
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/common/common.sh"
MACHINES=${*:-$COEMU_ALL_MACHINES}
OUTROOT=${OUT:-$HERE/out}
mkdir -p "$OUTROOT"

EMULATORS=""
for runner in "$HERE"/*/run.sh; do
	emu=$(basename "$(dirname "$runner")")
	[ "$emu" = common ] && continue
	EMULATORS="$EMULATORS $emu"
	echo "== $emu"
	OUT=$OUTROOT/$emu "$runner" $MACHINES
done

{
	printf '| Machine |'
	for emu in $EMULATORS; do printf ' %s |' "$emu"; done
	printf '\n|:--|'
	for emu in $EMULATORS; do printf ':--|'; done
	printf '\n'
	for m in $MACHINES; do
		printf '| %s |' "$m"
		for emu in $EMULATORS; do
			r=$OUTROOT/$emu/$m.result
			if [ -f "$r" ]; then
				read -r status detail < "$r"
				case $status in
					ok) cell=ok ;;
					*) cell="$status: $detail" ;;
				esac
			else
				cell="-"
			fi
			printf ' %s |' "$cell"
		done
		printf '\n'
	done
} > "$OUTROOT/summary.md"
echo
cat "$OUTROOT/summary.md"

# The contention probe's check-by-check matrix (matrix.py): $OUTROOT/matrix.html
if [ -z "${PROGRAM:-}" ] || [ "$(basename "$PROGRAM")" = ctprobe ]; then
	python3 "$HERE/matrix.py" --out-dir "$OUTROOT" || echo "coemu: matrix.py failed" >&2
fi
