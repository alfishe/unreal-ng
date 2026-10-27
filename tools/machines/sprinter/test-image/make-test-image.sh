#!/bin/zsh
# A test copy of a Sprinter DSS hard disk (raw image) with files added and SYSTEM.BAT rewritten. See README.md.
#
#   make-test-image.sh -b base.img -o out.img [-c host-path:DOS-dir ...] [-r command ...] [-P]
#
#   -b  the raw system disk (the MAME pack's sp_hdd_sys.img: chdman extracthd of sp_hdd_sys.chd)
#   -o  the copy to write (a clone on APFS: no space until written)
#   -c  copy a host file or folder into a DOS folder of the partition, made if missing; repeatable
#       (-c testdata/.../dont_blink_test1:/DEMOS/DNTBLINK replaces the files of that name)
#   -r  a SYSTEM.BAT line to run after the PATH line; repeatable, in order (the lines replace the shipped ver / fn)
#   -P  stop at the DSS prompt: the shipped SYSTEM.BAT without fn (Flex Navigator); ignored with -r
set -eu

base=""
out=""
copies=()
runs=()
prompt=0
while getopts "b:o:c:r:Ph" opt; do
    case $opt in
        b) base=$OPTARG ;;
        o) out=$OPTARG ;;
        c) copies+=("$OPTARG") ;;
        r) runs+=("$OPTARG") ;;
        P) prompt=1 ;;
        h | *) sed -n '2,13p' "$0"; exit 1 ;;
    esac
done
if [ -z "$base" ] || [ -z "$out" ]; then
    sed -n '2,13p' "$0"
    exit 1
fi
for tool in mcopy mmd mtype python3; do
    command -v $tool > /dev/null || { echo "$tool not found (brew install mtools)"; exit 1; }
done
[ -f "$base" ] || { echo "$base: no such file"; exit 1; }

# The FAT16 partition: MBR entry 0 (DSS boots from it); its first LBA gives the mtools offset
lba=$(python3 -c "
import struct, sys
mbr = open(sys.argv[1], 'rb').read(512)
if mbr[510:512] != b'\x55\xaa' or mbr[446 + 4] not in (0x04, 0x06, 0x0E):
    sys.exit('no FAT16 partition in MBR entry 0')
print(struct.unpack('<I', mbr[446 + 8:446 + 12])[0])" "$base")
img="$out@@$((lba * 512))"

mkdir -p "$(dirname "$out")"
rm -f "$out"
cp -c "$base" "$out" 2> /dev/null || cp "$base" "$out"
echo "$out: a copy of $base, FAT16 at LBA $lba"

for c in "${copies[@]}"; do
    src=${c%%:*}
    dst=${c#*:}
    [ -e "$src" ] || { echo "$src: no such file or folder"; exit 1; }
    # Make the DOS folder and its parents
    folder=""  # not "path": zsh ties it to $PATH
    for part in ${(s:/:)dst}; do
        folder="$folder/$part"
        mmd -i "$img" "::$folder" 2> /dev/null || true
    done
    if [ -d "$src" ]; then
        mcopy -o -s -i "$img" "$src"/* "::$dst/"
    else
        mcopy -o -i "$img" "$src" "::$dst/"
    fi
    echo "  copied $src -> $dst"
done

# SYSTEM.BAT: CR LF lines; the shipped PATH line is kept, so DSS finds its tools
shipped=$(mtype -i "$img" ::SYSTEM.BAT | tr -d '\r')
bat="$(dirname "$out")/.SYSTEM.BAT.$$"
if [ ${#runs} -gt 0 ] || [ $prompt = 1 ]; then
    {
        printf '@echo off\r\n'
        print -r -- "$shipped" | grep -i '^set path=' | while IFS= read -r l; do printf '%s\r\n' "$l"; done
        if [ ${#runs} -gt 0 ]; then
            for r in "${runs[@]}"; do printf '%s\r\n' "$r"; done
        else
            print -r -- "$shipped" | grep -Eiv '^(@echo off|set path=.*|fn)$' | while IFS= read -r l; do printf '%s\r\n' "$l"; done
        fi
    } > "$bat"
    mcopy -o -i "$img" "$bat" ::SYSTEM.BAT
    rm -f "$bat"
fi
echo "SYSTEM.BAT:"
mtype -i "$img" ::SYSTEM.BAT | tr -d '\r' | sed 's/^/  /'
