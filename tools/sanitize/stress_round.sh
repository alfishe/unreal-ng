#!/usr/bin/env bash
# One WebAPI stress round against a sanitizer unreal-qt: build it, launch it headless on its own ports,
# create / read / model-switch / delete instances from 8 threads (2 long-running neighbours) for N
# seconds, then report the sanitizer finding, or a hang (sampled), or a clean survival.
#
# Usage: tools/sanitize/stress_round.sh [--busy-gui] <seconds> <tag> [build-dir]
#   build-dir   default cmake-build-asan (tools/sanitize/configure.sh asan); cmake-build-tsan works too
#   --busy-gui  open the debugger, TTD, media, network, slots, traffic, MIDI, tape and log windows
#               (UNREAL_QT_STRESS_WINDOWS=all), and select half of the instances (CLI) before deleting them, so
#               the GUI is bound to what is removed and its refresh paths run during the removal
# Output: scratch/sanitize/<tag>/ (app.log, stress.log, asan.<pid> / tsan.<pid>, hang.txt)
# Guide: docs/testing/sanitizers.md
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
BUSY_GUI=
if [ "${1:-}" = "--busy-gui" ]; then BUSY_GUI=all; shift; fi
SECONDS_RUN=${1:-900}
TAG=${2:-round}
BUILD=${3:-cmake-build-asan}
OUT="$ROOT/scratch/sanitize/$TAG"
mkdir -p "$OUT"
BUILD_DIR="$ROOT/$BUILD" tools/build/build.sh unreal-qt > "$OUT/build.log" 2>&1 || { echo "BUILD FAILED: $OUT/build.log"; exit 1; }

# Own ports: another emulator may hold the defaults (8090 ...)
export UNREAL_WEBAPI_PORT=8190 UNREAL_MCP_PORT=8192 UNREAL_CLI_PORT=8865 UNREAL_GDB_PORT=2100 \
       UNREAL_DEZOG_PORT=12100 UNREAL_ZRCP_PORT=10100 QT_QPA_PLATFORM=offscreen
export ASAN_OPTIONS="${ASAN_OPTIONS:-halt_on_error=1:detect_leaks=0}:log_path=$OUT/asan"
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}:suppressions=$ROOT/tools/sanitize/tsan.supp:log_path=$OUT/tsan"
[ -n "$BUSY_GUI" ] && export UNREAL_QT_STRESS_WINDOWS="$BUSY_GUI" STRESS_SELECT_CLI_PORT=$UNREAL_CLI_PORT
"$BUILD/bin/unreal-qt.app/Contents/MacOS/unreal-qt" > "$OUT/app.log" 2>&1 &
APP=$!
for _ in $(seq 1 60); do curl -s -m 1 localhost:8190/api/v1/emulator > /dev/null && break; sleep 1; done
python3 "$ROOT/tools/sanitize/webapi_stress.py" 8190 "$SECONDS_RUN" 8 > "$OUT/stress.log" 2>&1

if kill -0 "$APP" 2>/dev/null; then
  # A hang: the server does not answer - sample every thread before stopping it
  if ! curl -s -m 10 localhost:8190/api/v1/emulator > /dev/null; then
    echo "APP HUNG: $OUT/hang.txt"; sample "$APP" 3 -file "$OUT/hang.txt" > /dev/null 2>&1
  fi
  kill "$APP"; sleep 2; kill -9 "$APP" 2>/dev/null
  echo "app survived"
else
  echo "app died"
  # The report is symbolized after the abort: wait for its summary line
  for _ in $(seq 1 60); do grep -q SUMMARY "$OUT"/*san.* 2>/dev/null && break; sleep 2; done
fi
tail -2 "$OUT/stress.log"
grep -h -m1 "ERROR: \|WARNING: ThreadSanitizer" "$OUT"/*san.* 2>/dev/null
grep -h -E "^    #[0-3] " "$OUT"/*san.* 2>/dev/null | head -8
