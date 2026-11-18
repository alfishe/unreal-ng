#!/usr/bin/env python3
"""Mark up the General Sound firmware ROMs with the labels of their sources.

The binaries are the truth, the sources supply the names (gs-debugger design §6.2). The in-tree sources
(docs/inprogress/2026-09-19-general-sound/materials/gs/gs-firmware/firmware) assemble with sjasmplus into:
  - gs105b.rom, byte for byte (make_rom.a80: the 1997 code plus patch/patch.a80);
  - gs104.rom, byte for byte (the same without patch/patch.a80).
gs105a.rom (the 2007 bug fixes) differs from v1.05b in a few bytes: it takes v1.05b's labels, and the labels whose
bytes (up to the next label) differ are listed as changed in the report.

sjasmplus's SLD output gives each label its value, its 16K page (device ZXSPECTRUM48: 0 = the low ROM at #0000,
1 = the card's fixed RAM window #4000-#7FFF, 3 = the high ROM at #C000) and marks EQUs (+equ, left out).

Written to data/symbols/gs/:
  <id>.map        ROM0:ADDR / ROM1:C000.. / ADDR NAME (TYPE) ; comment  (LabelManager MAP format; the RAM window
                  without a page: the card CPU's view)
  manifest.json   the bundles, matched by the SHA-256 of the ROM's page 0 (data/symbols/README.md)
  REPORT.md       per ROM: matched / changed labels and the changed byte ranges

Usage:
  markup_symbols.py            rebuild and compare with the committed files (exit 1 on a difference)
  markup_symbols.py --write    rebuild and overwrite them

sjasmplus: --sjasmplus PATH, else $SJASMPLUS, else on PATH.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "docs/inprogress/2026-09-19-general-sound/materials/gs/gs-firmware/firmware"
OUT = ROOT / "data/symbols/gs"
ROMS = ROOT / "data/rom"
PAGE = 0x4000

# id, ROM file, title, reference build ("patched" = make_rom.a80, "plain" = without the patch)
FIRMWARES = [
    ("gs104", "gs104.rom", "General Sound firmware v1.04", "plain"),
    ("gs105a", "gs105a.rom", "General Sound firmware v1.05a", "patched"),
    ("gs105b", "gs105b.rom", "General Sound firmware v1.05b", "patched"),
]
# The ROMs the sources build byte for byte (R1 for v1.05b)
EXACT = {"gs104", "gs105b"}

DATA_DIRECTIVES = re.compile(r"^\s*(DB|DW|DEFB|DEFW|DEFS|DEFM|DS|DM|BYTE|WORD|ALIGN|INCBIN)\b", re.IGNORECASE)


def find_sjasmplus(given):
    for candidate in (given, os.environ.get("SJASMPLUS"), shutil.which("sjasmplus")):
        if candidate and Path(candidate).is_file():
            return candidate
    sys.exit("sjasmplus not found: pass --sjasmplus PATH or set SJASMPLUS")


def build(sjasmplus, work, variant):
    """Assemble one reference build in its own copy of the sources; the 32K image and the SLD path"""
    folder = work / variant
    shutil.copytree(FIRMWARE, folder)
    main = (folder / "make_rom.a80").read_text(encoding="latin-1")
    if variant == "plain":
        main = main.replace('\tinclude "patch/patch.a80"', ";\tpatch left out: v1.04")
        if "patch left out" not in main:
            sys.exit("make_rom.a80: the patch include was not found")
    (folder / "make_ref.a80").write_text(main, encoding="latin-1")
    result = subprocess.run(
        [sjasmplus, "-isrc", "-ipatch", "-isrc/sgen", "--sld=ref.sld", "make_ref.a80"],
        cwd=folder, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"sjasmplus failed ({variant}):\n{result.stdout}{result.stderr}")
    image = (folder / "gsroml_").read_bytes() + (folder / "gsromh_").read_bytes()
    if len(image) != 2 * PAGE:
        sys.exit(f"{variant}: the build is {len(image)} bytes, not 32K")
    return image, folder


def source_line(folder, cache, file, line):
    if file not in cache:
        found = [p for p in folder.rglob(file)]
        cache[file] = found[0].read_bytes().decode("cp866").splitlines() if found else []
    lines = cache[file]
    return lines[line - 1] if 0 < line <= len(lines) else ""


def ascii_comment(text):
    text = text.strip().lstrip(";").strip()
    return text if text and all(32 <= ord(c) < 127 for c in text) else ""


def labels_of(folder):
    """The SLD's labels (no EQUs, no module markers): name, CPU address, page, type, comment"""
    labels = []
    cache = {}
    for raw in (folder / "ref.sld").read_text(encoding="latin-1").splitlines():
        fields = raw.split("|")
        if len(fields) < 8 or fields[6] != "L":
            continue
        parts = fields[7].split(",")
        name, traits = parts[1], parts[3:]
        if not name or "+equ" in traits or "+module" in traits or "+endmod" in traits:
            continue
        page, value = int(fields[4]), int(fields[5])
        if page not in (0, 1, 3):
            continue
        file, line = fields[0], int(fields[1].split(":")[0])
        text = source_line(folder, cache, file, line)
        body = text.split(";", 1)[0]
        rest = body[len(name):] if body.startswith(name) else body
        rest = rest.lstrip(":")
        if not rest.strip():
            # A label alone on its line: the next line with code says what it is
            for following in range(line + 1, line + 6):
                nxt = source_line(folder, cache, file, following).split(";", 1)[0]
                if nxt.strip():
                    rest = nxt
                    break
        kind = "DATA" if page == 1 or DATA_DIRECTIVES.match(rest) else "CODE"
        comment = ascii_comment(text.split(";", 1)[1]) if ";" in text else ""
        if not comment:
            comment = ascii_comment(source_line(folder, cache, file, line - 1)) \
                if source_line(folder, cache, file, line - 1).lstrip().startswith(";") else ""
        labels.append({"name": name, "address": value, "page": page, "type": kind, "comment": comment})
    labels.sort(key=lambda l: (l["page"] != 1, l["address"], l["name"]))
    return labels


def rom_offset(label):
    """The label's offset in the 32K image (None for the RAM window)"""
    if label["page"] == 0:
        return label["address"]
    if label["page"] == 3:
        return PAGE + label["address"] - 0xC000
    return None


def write_map(firmware_id, title, labels):
    lines = [f"; {title}: labels of the in-tree sources, placed by tools/gsfirmware/markup_symbols.py",
             "; Format: [ROMn:]ADDR NAME (TYPE) ; comment  (ROM0 = low 16K at #0000, ROM1 = high 16K at #C000;",
             ";         no prefix = the card's fixed RAM window #4000-#7FFF). Generated: do not edit; see REPORT.md"]
    for label in labels:
        if label["page"] == 0:
            where = f"ROM0:{label['address']:04X}"
        elif label["page"] == 3:
            where = f"ROM1:{label['address']:04X}"
        else:
            where = f"{label['address']:04X}"
        line = f"{where:<10} {label['name']:<16} ({label['type']})"
        if label["comment"]:
            line += f" ; {label['comment']}"
        lines.append(line.rstrip())
    return "\n".join(lines) + "\n"


def changed_ranges(rom, reference):
    ranges = []
    for i, (a, b) in enumerate(zip(rom, reference)):
        if a != b:
            if ranges and ranges[-1][1] == i - 1:
                ranges[-1][1] = i
            else:
                ranges.append([i, i])
    return ranges


def label_spans(labels):
    """Each ROM label with the image range up to the next ROM label"""
    rom_labels = sorted((rom_offset(l), l["name"]) for l in labels if rom_offset(l) is not None)
    spans = []
    for i, (start, name) in enumerate(rom_labels):
        end = rom_labels[i + 1][0] if i + 1 < len(rom_labels) else 2 * PAGE
        spans.append((name, start, max(start + 1, end)))
    return spans


def generate(sjasmplus):
    files = {}
    report = ["# General Sound firmware symbols: mark-up report", "",
              "Generated by `tools/gsfirmware/markup_symbols.py` (gs-debugger design §6.2). The sources are",
              "`docs/inprogress/2026-09-19-general-sound/materials/gs/gs-firmware/firmware`.", ""]
    manifest = {"format": "unreal-symbols-manifest", "version": 1,
                "note": "General Sound card firmware labels for the card CPU's store (gs, ms): a bundle applies when "
                        "the card ROM's page 0 has the listed SHA-256; the map's ROM0 / ROM1 are the card's ROM pages.",
                "bundles": []}
    page0 = {}
    with tempfile.TemporaryDirectory(dir=ROOT / "scratch" if (ROOT / "scratch").is_dir() else None) as tmp:
        work = Path(tmp)
        builds = {variant: build(sjasmplus, work, variant) for variant in ("plain", "patched")}
        for firmware_id, rom_file, title, variant in FIRMWARES:
            reference, folder = builds[variant]
            rom = (ROMS / rom_file).read_bytes()
            if len(rom) != 2 * PAGE:
                sys.exit(f"{rom_file}: {len(rom)} bytes, not 32K")
            labels = labels_of(folder)
            ranges = changed_ranges(rom, reference)
            if firmware_id in EXACT and ranges:
                sys.exit(f"{rom_file}: the {variant} build differs in {len(ranges)} ranges; it must be byte for byte")
            changed = [name for name, start, end in label_spans(labels)
                       if any(lo < end and hi >= start for lo, hi in ranges)]
            files[f"{firmware_id}.map"] = write_map(firmware_id, title, labels)
            digest = hashlib.sha256(rom[:PAGE]).hexdigest()
            if digest in page0.values():
                sys.exit(f"{rom_file}: page 0 is the same as another firmware's; the manifest cannot tell them apart")
            page0[firmware_id] = digest
            manifest["bundles"].append({
                "id": f"gs:{firmware_id}", "title": title, "file": f"{firmware_id}.map",
                "match": {"page": 0, "page_sha256": [digest]},
                "note": f"data/rom/{rom_file}, image SHA-256 {hashlib.sha256(rom).hexdigest()}"})
            rom_count = sum(1 for l in labels if rom_offset(l) is not None)
            report += [f"## {title} (`{rom_file}`)", "",
                       f"- Reference build: {'make_rom.a80' if variant == 'patched' else 'make_rom.a80 without patch/patch.a80'}",
                       f"- Labels: {len(labels)} ({rom_count} in ROM, {len(labels) - rom_count} in the RAM window)",
                       f"- The ROM equals the build: {'yes, byte for byte' if not ranges else 'no'}"]
            if ranges:
                total = sum(hi - lo + 1 for lo, hi in ranges)
                report.append(f"- Changed: {total} bytes in {len(ranges)} ranges; the labels over them are placed at the "
                              "same address (the patches keep routines in place) and listed below")
                report += ["", "| Range (image offset) | CPU address | Bytes |", "|---|---|---|"]
                for lo, hi in ranges:
                    cpu = lo if lo < PAGE else 0xC000 + lo - PAGE
                    report.append(f"| `#{lo:04X}-#{hi:04X}` | `#{cpu:04X}` | {hi - lo + 1} |")
                report += ["", "Labels over changed bytes: " + ", ".join(f"`{n}`" for n in changed)]
            report.append("")
    files["manifest.json"] = json.dumps(manifest, indent=2) + "\n"
    files["REPORT.md"] = "\n".join(report)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--write", action="store_true", help="overwrite the committed files")
    parser.add_argument("--sjasmplus", help="the sjasmplus binary")
    args = parser.parse_args()
    files = generate(find_sjasmplus(args.sjasmplus))
    if args.write:
        OUT.mkdir(parents=True, exist_ok=True)
        for name, text in files.items():
            (OUT / name).write_text(text, encoding="utf-8")
        print(f"wrote {len(files)} files to {OUT.relative_to(ROOT)}")
        return 0
    differs = [name for name, text in files.items()
               if not (OUT / name).is_file() or (OUT / name).read_text(encoding="utf-8") != text]
    for name in differs:
        print(f"differs: data/symbols/gs/{name}")
    print("ok" if not differs else f"{len(differs)} files differ (run with --write)")
    return 1 if differs else 0


if __name__ == "__main__":
    sys.exit(main())
