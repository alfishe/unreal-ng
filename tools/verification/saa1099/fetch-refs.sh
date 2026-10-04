#!/bin/sh
# Fetch the pinned SAA1099 co-simulation references into refs/ (git-ignored).
# Idempotent: re-running only verifies the pinned revisions. Third-party code
# never enters the repository; the drivers compile it from refs/ only.
set -e
cd "$(dirname "$0")"
mkdir -p refs

# clone_pinned <dir> <repo> <commit>
clone_pinned() {
    if [ ! -d "refs/$1/.git" ]; then
        rm -rf "refs/$1"
        echo "cloning $1..."
        git clone --quiet "$2" "refs/$1"
    fi
    if ! git -C "refs/$1" cat-file -e "$3^{commit}" 2>/dev/null; then
        git -C "refs/$1" fetch --quiet origin
    fi
    git -C "refs/$1" -c advice.detachedHead=false checkout --quiet "$3"
    echo "$1 @ $(git -C "refs/$1" rev-parse HEAD)"
}

# fetch_raw <dest> <url> <sha256>
fetch_raw() {
    if [ ! -f "$1" ]; then
        mkdir -p "$(dirname "$1")"
        curl -sSfL -o "$1.tmp" "$2"
        mv "$1.tmp" "$1"
    fi
    got=$(shasum -a 256 "$1" | cut -d' ' -f1)
    if [ "$got" != "$3" ]; then
        echo "checksum mismatch for $1: $got (want $3)" >&2
        exit 1
    fi
    echo "$1 sha256 ok"
}

# SAASound (Dave Hooper). The SourceForge project (sourceforge.net/projects/saasound)
# hosts only the legacy CVS tree and links its development to this GitHub repository.
clone_pinned saasound https://github.com/stripwax/SAASound.git 3d92322030d1e5627cbf8295bed1efc872bad6eb

# MAME saa1099 device (Juergen Buchmueller, Manuel Abadia). Two files from a pinned
# commit instead of the whole MAME tree.
MAME_REF=f43983b62edf2b7d1dc8911ee4a2c08dba8a98de
MAME_RAW=https://raw.githubusercontent.com/mamedev/mame/$MAME_REF/src/devices/sound
fetch_raw refs/mame/saa1099.cpp "$MAME_RAW/saa1099.cpp" 7f2be89a2f91dbd52fdeb9adca575a87a3149b4d5201769156fe8b4845db96d9
fetch_raw refs/mame/saa1099.h "$MAME_RAW/saa1099.h" 2304fa294a182b2af9f9f0d8d02c3485c76a68b0c4a5f8af2709af385b9da755

# MiSTer SAM Coupe core: rtl/saa1099.sv (Sorgelig, after SAASound)
clone_pinned sam-coupe-mister https://github.com/MiSTer-devel/SAM-Coupe_MiSTer.git 9888045b41b0a9a51cf6a1c56dc7fde6d3e9d264

# rejunity tt06-psg-saa1099: independent Tiny Tapeout implementation
clone_pinned tt06-saa1099 https://github.com/rejunity/tt06-psg-saa1099.git 10c0983eac13a691430eb199127e0f1f695dc03c
