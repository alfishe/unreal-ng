#!/bin/sh
# Build the headless harness and run ctprobe on each stock xpeccy-plus machine.
#   XPECCY_DIR=<xpeccy-plus checkout> ./run.sh [machine...]
#   machines: zx48 zx128 zxplus2a zxplus3 (default: all four)
# Output in ./out: <machine>.bin (memory dump for ../../ctprobe-compare.py), .log, .screen.txt
set -e
cd "$(dirname "$0")"
if [ -z "$XPECCY_DIR" ]; then
	echo "set XPECCY_DIR to an xpeccy-plus checkout" >&2
	exit 2
fi
PROBE=${PROBE:-../..}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DXPECCY_DIR="$XPECCY_DIR" >/dev/null
ninja -C build >/dev/null
mkdir -p out
[ $# -gt 0 ] || set -- zx48 zx128 zxplus2a zxplus3
for m in "$@"; do
	case $m in zx48) o=48k;; zx128) o=128k;; zxplus2a) o=plus2a;; zxplus3) o=plus3;; *) o=$m;; esac
	./build/ctharness "$m" "$XPECCY_DIR/config/roms" "$PROBE/ctprobe.tap" "$PROBE/ctprobe.sym" "out/$o" 60000 | tee "out/$o.log" || true
	python3 "$PROBE/ctprobe-compare.py" "out/$o.bin" "$PROBE/ctprobe.sym" | tail -1 || true
done
