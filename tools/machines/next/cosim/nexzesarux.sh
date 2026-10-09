#!/bin/bash
# usage: nexzesarux.sh <file.nex> <frames> <out.png>  - run a NEX on the patched ZEsarUX (tbblue, null video) and keep the screen
nex="$1"; frames="${2:-250}"; out="$3"
build=${NEXT_COSIM_BUILD:-/Volumes/TB4-4Tb/Projects/emulators/build}
img=${NEXT_COSIM_IMG:-/Volumes/TB4-4Tb/Projects/emulators/cosim-cards/full.img}
w=$(mktemp -d)
export COSIM_DUMP="$w" COSIM_FRAMES="$frames" COSIM_TRACE="$w/trace.txt" COSIM_KINDS=FRM
( cd "$build/zesarux/src" && timeout 300 ./zesarux --noconfigfile --nosplash --nowelcomemessage --disable-all-first-aid \
    --quickexit --vo null --ao null --emulatorspeed 10000 --machine tbblue --enable-mmc --mmc-file "$img" \
    --mmc-no-persistent-writes --enable-divmmc-ports "$nex" >"$w/run.log" 2>&1 ) || true
python3 -c "from PIL import Image; Image.open('$w/screen.bmp').save('$out')" 2>/dev/null || echo "no screen: $(tail -2 $w/run.log)"
rm -rf "$w"
