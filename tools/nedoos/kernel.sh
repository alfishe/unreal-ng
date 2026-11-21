#!/bin/sh
# Build a NedoOS kernel (the hobeta `nedoos.$C` and its body `code.c`) outside Windows, the way the tree's
# `src/mk<preset>.bat` + `src/kernel/build.bat` do. Recipe: .recipe/software/nedoos-build.md
#
# Usage: kernel.sh <nedoos-tree> <preset> [out-file]
#   preset   the suffix of src/mk<preset>.bat: pe26sd, evo, atm3sd, atm3hd, atm2hd, ...
#   out-file where to copy nedoos.$C (default: print its path only)
# Environment: SJASMPLUS, MHMT (default: sjasmplus and mhmt on PATH), SVNREVISION (default: none, as the
# releases built without svnversion)
set -eu

[ $# -ge 2 ] || { sed -n '2,12p' "$0"; exit 2; }
tree=$(cd "$1" && pwd)
preset=$2
out=${3:-}
sjasm=${SJASMPLUS:-sjasmplus}
mhmt=${MHMT:-mhmt}
bat="$tree/src/mk$preset.bat"
[ -f "$bat" ] || { echo "no $bat" >&2; exit 2; }

# The preset's settings: every `echo ...` line that writes _sdk\syssets.asm, as cmd would write it
# (rem lines and the svnversion FOR loop skipped)
syssets="$tree/src/_sdk/syssets.asm"
: > "$syssets"
tr -d '\r' < "$bat" | while IFS= read -r line; do
    case "$line" in
        [Rr][Ee][Mm]*|FOR*|for*) continue ;;
    esac
    case "$line" in
        *syssets.asm*)
            body=${line#*echo}
            body=${body%%>*}
            body=$(printf '%s' "$body" | sed 's/^[ ]//; s/[ ]*$//')
            printf '%s\n' "$body" >> "$syssets"
            ;;
    esac
done
[ -n "${SVNREVISION:-}" ] && printf ' define SVNREVISION %s\n' "$SVNREVISION" >> "$syssets"

cd "$tree/src/kernel"
rm -f code.c initcode.c syscode.c syscode.c.mlz 'nedoos.$C'
"$sjasm" --nologo --msg=war ngsinst.asm
"$sjasm" --nologo --msg=war main.asm
"$mhmt" -mlz syscode.c > /dev/null
cat initcode.c syscode.c.mlz > code.c
"$sjasm" --nologo --msg=war hobeta.asm
if [ -n "$out" ]; then
    cp 'nedoos.$C' "$out"
    echo "$out"
else
    echo "$tree/src/kernel/nedoos.\$C"
fi
