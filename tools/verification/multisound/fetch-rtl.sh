#!/usr/bin/env bash
# Fetches the ZX-MultiSound CPLD source (the oracle of MultiSoundLogic) at a pinned commit into refs/ (git-ignored)
# and derives the two testbench variants:
#   refs/top.v          pristine cpld/rtl/top.v
#   refs/top-pro.v      current firmware; fm*_ena '1'bz' written as 1'b1 (see below)
#   refs/top-classic.v  the same plus the unofficial issue #11 patch (ctrlMask = classic)
# The 1 MB / 2 MB GS RAM builds are the same file with or without +define+GS_RAM_2MB (build.sh).
#
# Why the fm*_ena edit: the RTL assigns 1'bz to an 'output reg' inside a clocked block (an open-drain style pin).
# Verilator has no Z inside registers and would turn it into 0, so "FM on" would read as "FM muted". The edit keeps
# the register and encodes "released" as 1; the testbench reports fm = mute when the pin is 0. Nothing else changes.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REFS="$HERE/refs"
COMMIT=d7f3ac293724e1b69cbe9af9894311de4e07e4d1       # 2026-06-17, "d7f3ac2"
SHA256=b46839bad27b03740526839e5cca395dbec5a44e2c39db6a44693013dc9474ba
URL="https://raw.githubusercontent.com/UzixLS/zx-multisound/$COMMIT/cpld/rtl/top.v"

mkdir -p "$REFS"
if [[ ! -f "$REFS/top.v" ]] || ! shasum -a 256 "$REFS/top.v" | grep -q "^$SHA256 "; then
    echo "fetching $URL"
    curl -fsSL -o "$REFS/top.v.tmp" "$URL"
    actual="$(shasum -a 256 "$REFS/top.v.tmp" | cut -d' ' -f1)"
    if [[ "$actual" != "$SHA256" ]]; then
        echo "error: top.v SHA-256 $actual, expected $SHA256" >&2
        rm -f "$REFS/top.v.tmp"
        exit 1
    fi
    mv "$REFS/top.v.tmp" "$REFS/top.v"
fi
echo "refs/top.v at $COMMIT (SHA-256 verified)"

python3 - "$REFS" <<'PY'
import sys
from pathlib import Path

refs = Path(sys.argv[1])
src = (refs / "top.v").read_text()

def replace_once(text, old, new, what):
    count = text.count(old)
    if count != 1:
        sys.exit(f"error: {what}: expected one match, found {count}")
    return text.replace(old, new)

def release_as_one(text):
    count = text.count("1'b0 : 1'bz;")
    if count < 2:
        sys.exit("error: fm*_ena 1'bz assignments not found")
    return text.replace("1'b0 : 1'bz;", "1'b0 : 1'b1; // tb: 1'bz (released) encoded as 1")

control = """    else if (port_fffd && ioreq_wr && zxd[7:4] == 4'b1111) begin
        ym_chip_sel <= zxd[0];
        ym_get_stat <= ~zxd[1];
        fm1_ena <= zxd[2]? 1'b0 : 1'bz;
        fm2_ena <= zxd[2]? 1'b0 : 1'bz;
    end
"""
# Issue #11 (zxshock's patch, applied to the current signal names and the current, non-inverted chip select):
# five compared bits while the SAA DIP is off, four while it is on. The SAA clock block is unchanged.
classic = """    else if (port_fffd && ioreq_wr && zxd[7:3] == 5'b11111 && saa_ena == 1'b0) begin // tb: issue #11 patch
        ym_chip_sel <= zxd[0];
        ym_get_stat <= ~zxd[1];
        fm1_ena <= zxd[2]? 1'b0 : 1'bz;
        fm2_ena <= zxd[2]? 1'b0 : 1'bz;
    end
    else if (port_fffd && ioreq_wr && zxd[7:4] == 4'b1111 && saa_ena == 1'b1) begin // tb: issue #11 patch
        ym_chip_sel <= zxd[0];
        ym_get_stat <= ~zxd[1];
        fm1_ena <= zxd[2]? 1'b0 : 1'bz;
        fm2_ena <= zxd[2]? 1'b0 : 1'bz;
    end
"""
replace_once(src, control, control, "control byte block")
(refs / "top-pro.v").write_text(release_as_one(src))
(refs / "top-classic.v").write_text(release_as_one(replace_once(src, control, classic, "control byte block")))
print("refs/top-pro.v, refs/top-classic.v derived")
PY
