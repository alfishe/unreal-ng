#!/bin/bash
# usage: nexcmp.sh <file.nex> [frames]   -> prints the number of pixels differing by more than 40 per channel
N="$1"; F="${2:-300}"
B=$(cd "$(dirname "$0")/../../.." && pwd)
W=/tmp/claude-501/cmp; mkdir -p $W
J=/Volumes/TB4-4Tb/Projects/emulators/build/jnext/bld/jnext
IMG=/Volumes/TB4-4Tb/Projects/emulators/cosim-cards/full.img
rm -f $W/ref.png $W/frame.rgba
if [ "${REF:-jnext}" = zesarux ]; then
  "$(dirname "$0")/nexzesarux.sh" "$N" "$F" $W/ref_full.png
  python3 -c "
from PIL import Image
a=Image.open('$W/ref_full.png').convert('RGB')           # 704x608: ZEsarUX's window, 2x; crop calibrated on tm.nex (pixel-identical)
a.crop((32,48,32+640,48+512)).save('$W/ref.png')"
else
timeout 120 $J --headless --silent --sdcard $IMG --sdcard-readonly --delayed-screenshot $W/ref.png --delayed-screenshot-frames $F --delayed-automatic-exit-frames $((F+5)) --experimental-nex-v1.3 "$N" >/dev/null 2>&1
fi
UNREAL_NEX="$N" UNREAL_NEX_FRAMES=$F UNREAL_NEX_OUT=$W $B/cmake-build-agent-release/bin/core-tests --gtest_filter='LoaderNexRun*' >/dev/null 2>&1
python3 - "$N" <<'PY'
import struct,sys,os
from PIL import Image, ImageChops
W='/tmp/claude-501/cmp/'
if not os.path.exists(W+'ref.png') or not os.path.exists(W+'frame.rgba'):
    print(os.path.basename(sys.argv[1]),'MISSING', os.path.exists(W+'ref.png'), os.path.exists(W+'frame.rgba')); sys.exit()
b=open(W+'frame.rgba','rb').read(); w,h=struct.unpack('<II',b[:8])
a=Image.frombytes('RGBA',(w,h),b[8:8+w*h*4]).convert('RGB'); r=Image.open(W+'ref.png').convert('RGB')
if a.size!=r.size: print(os.path.basename(sys.argv[1]),'SIZE',a.size,r.size); sys.exit()
d=ImageChops.difference(a,r)
n=sum(1 for p in d.getdata() if max(p)>40)
print(os.path.basename(sys.argv[1]), n, 'of', w*h)
a.save(W+'ours_last.png'); r.save(W+'ref_last.png')
PY
