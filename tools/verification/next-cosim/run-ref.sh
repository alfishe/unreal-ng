#!/usr/bin/env bash
# Run a reference emulator headless on a Next card and write the common trace + state dump + screenshot.
#
#   run-ref.sh <jnext|zesarux|mame> <card> <frames> <out-dir>
#
#   <card>     a card folder (TBBLUE.FW, machines/next/...) or a name under $NEXT_COSIM_CARDS (default
#              /Volumes/TB4-4Tb/Projects/emulators/cosim-cards); an SD image <card>.img is built next to it
#              (mk-sd-image.sh) the first time and whenever the folder is newer
#   <frames>   video frames to run, then dump and exit
#   <out-dir>  receives trace.txt, state.txt (regs, MMU slots, NR 00-FF), screen.png, run.log
#
# Environment (all optional):
#   NEXT_COSIM_BUILD   where build-ref.sh built the emulators (default /Volumes/TB4-4Tb/Projects/emulators/build)
#   COSIM_KINDS        NRW,NRR,POUT,PIN,MMU,IRQ,PCS,FRM (default: all but PCS)
#   COSIM_PCS=N        one PCS line every N instructions
#   COSIM_PCWIN=a:n,.. a PCS line for EVERY instruction in frames [a, a+n) (frames are 1-based)
#   COSIM_PORT_SKIP    low port bytes in hex to leave out of POUT/PIN, e.g. "eb,e7" (SPI data noise)
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
emu=${1:?emulator: jnext|zesarux|mame}; card=${2:?card}; frames=${3:?frames}; out=${4:?out-dir}
cards=${NEXT_COSIM_CARDS:-/Volumes/TB4-4Tb/Projects/emulators/cosim-cards}
build=${NEXT_COSIM_BUILD:-/Volumes/TB4-4Tb/Projects/emulators/build}

[ -d "$card" ] || card="$cards/$card"
[ -f "$card/TBBLUE.FW" ] || { echo "card $card has no TBBLUE.FW" >&2; exit 1; }
card=$(cd "$card" && pwd)
mkdir -p "$out"; out=$(cd "$out" && pwd)

img="$card.img"
if [ ! -f "$img" ] || [ -n "$(find "$card" -newer "$img" -print -quit)" ]; then
    "$here/mk-sd-image.sh" "$card" "$img" 1024 fat32 >"$out/mk-sd-image.log"
fi

export COSIM_TRACE="$out/trace.txt" COSIM_DUMP="$out" COSIM_FRAMES="$frames"
rm -f "$out/trace.txt" "$out/state.txt" "$out/screen.png" "$out/screen.bmp"

case "$emu" in
jnext)
    bin="$build/jnext/bld/jnext"
    [ -x "$bin" ] || { echo "$bin missing: run build-ref.sh jnext" >&2; exit 1; }
    "$bin" --headless --silent --sdcard "$img" --sdcard-readonly --rtc "2026-01-01 12:00:00" \
        --delayed-screenshot "$out/screen.png" --delayed-screenshot-frames "$frames" \
        --delayed-automatic-exit-frames $((frames + 1)) --log-level warn >"$out/run.log" 2>&1
    ;;
zesarux)
    bin="$build/zesarux/src/zesarux"
    [ -x "$bin" ] || { echo "$bin missing: run build-ref.sh zesarux" >&2; exit 1; }
    # the patched emulator exits by itself at the end of frame <frames>
    ( cd "$build/zesarux/src" && "$bin" --noconfigfile --nosplash --nowelcomemessage --disable-all-first-aid \
        --quickexit --vo null --ao null --emulatorspeed 10000 --machine tbblue \
        --enable-mmc --mmc-file "$img" --mmc-no-persistent-writes --enable-divmmc-ports \
        >"$out/run.log" 2>&1 ) || true
    if [ -f "$out/screen.bmp" ]; then
        python3 -c "from PIL import Image; Image.open('$out/screen.bmp').save('$out/screen.png')" 2>/dev/null \
            && rm -f "$out/screen.bmp" || true
    fi
    ;;
mame)
    "$here/run-mame.sh" "$img" "$frames" "$out"
    ;;
*)
    echo "unknown emulator $emu" >&2; exit 1 ;;
esac

[ -f "$out/state.txt" ] && head -3 "$out/state.txt" | cut -c1-200
[ -f "$out/trace.txt" ] && echo "trace: $(wc -l <"$out/trace.txt") events in $out/trace.txt"
ls "$out"
