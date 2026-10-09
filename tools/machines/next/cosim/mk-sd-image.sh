#!/usr/bin/env bash
# Build a partitioned FAT SD-card image from a card folder (TBBLUE.FW, machines/next/..., nextzxos/...).
# Usage: mk-sd-image.sh <card-folder> <out.img> [size-MB=256] [fat16|fat32=fat16]
# Needs mtools (brew install mtools). The image is sparse; one primary partition at 1 MiB.
set -euo pipefail
card=${1:?card folder}; out=${2:?output image}; mb=${3:-256}; fat=${4:-fat16}
[ -f "$card/TBBLUE.FW" ] || { echo "no TBBLUE.FW in $card" >&2; exit 1; }
command -v mformat >/dev/null || { echo "mtools missing: brew install mtools" >&2; exit 1; }
rm -f "$out"
python3 - "$out" "$mb" "$fat" <<'PY'
import sys, struct
out, mb, fat = sys.argv[1], int(sys.argv[2]), sys.argv[3]
total = mb * 2048                      # 512-byte sectors
start = 2048                           # 1 MiB aligned
size = total - start
ptype = 0x0C if fat == 'fat32' else 0x0E   # FAT32 LBA / FAT16 LBA
mbr = bytearray(512)
mbr[0x1BE:0x1BE + 16] = struct.pack('<B3sB3sII', 0, b'\xfe\xff\xff', ptype, b'\xfe\xff\xff', start, size)
mbr[510:512] = b'\x55\xaa'
with open(out, 'wb') as f:
    f.write(mbr)
    f.truncate(total * 512)
print(f"{out}: {mb} MB, partition @{start} len {size} type {ptype:#x}")
PY
size=$(( mb * 2048 - 2048 ))
opts=(-i "$out@@1M" -T "$size" -h 255 -s 63 -v NEXT)
if [ "$fat" = fat32 ]; then opts+=(-F); fi
mformat "${opts[@]}" ::
mcopy -s -m -Q -i "$out@@1M" "$card"/* ::/
mdir -i "$out@@1M" ::/ | tail -15
