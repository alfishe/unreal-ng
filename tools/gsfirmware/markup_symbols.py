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

The NeoGS firmware (gs-debugger N5): the NedoPC sources (docs/inprogress/2026-09-19-general-sound/materials/neogs/
ngs-firmware, SVN `ngs` /z80) assemble with AS (asl + p2bin, main_rom/build.bat) into the main ROM of
data/rom/neogs/full_ngs.rom (flash #10000), byte for byte. The loader copies it into RAM pages 0-1 and runs it from
there, so its labels are RAM0 (#0000) and RAM1 (#C000, MPAG 0); the bundle matches the flash page that holds the
copy (page 4). asl's MAP file gives the labels (segment CODE) and the EQUs (segment NOTHING: the variables of the RAM
window, from equ_ngs.a80). NEOGS-VS-GS.md compares each NeoGS routine with GS v1.05a.

Written to data/symbols/gs/:
  <id>.map        ROM0:ADDR / ROM1:C000.. / ADDR NAME (TYPE) ; comment  (LabelManager MAP format; the RAM window
                  without a page: the card CPU's view)
  manifest.json   the bundles, matched by the SHA-256 of the ROM's page 0 (data/symbols/README.md)
  REPORT.md       per ROM: matched / changed labels and the changed byte ranges
  NEOGS-VS-GS.md  each NeoGS label: same as GS NAME / changed from GS NAME / NeoGS only; the GS labels NeoGS drops

Usage:
  markup_symbols.py            rebuild and compare with the committed files (exit 1 on a difference)
  markup_symbols.py --write    rebuild and overwrite them

sjasmplus: --sjasmplus PATH, else $SJASMPLUS, else on PATH. asl and p2bin (AS, http://john.ccac.rwth-aachen.de:8000/as/):
--asl PATH (p2bin beside it), else $ASL, else on PATH.
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

NEOGS = ROOT / "docs/inprogress/2026-09-19-general-sound/materials/neogs/ngs-firmware/z80"
NEOGS_FLASH = ROOT / "data/rom/neogs/full_ngs.rom"
NEOGS_MAIN_ROM = 0x10000   # the main ROM's offset in the flash image
NEOGS_PAGE = NEOGS_MAIN_ROM // PAGE  # the flash page the bundle matches
NEOGS_REFERENCE = "gs105a"  # the GS firmware the comparison is against

DATA_DIRECTIVES = re.compile(r"^\s*(DB|DW|DEFB|DEFW|DEFS|DEFM|DS|DM|BYTE|WORD|ALIGN|INCBIN)\b", re.IGNORECASE)


def find_sjasmplus(given):
    for candidate in (given, os.environ.get("SJASMPLUS"), shutil.which("sjasmplus")):
        if candidate and Path(candidate).is_file():
            return candidate
    sys.exit("sjasmplus not found: pass --sjasmplus PATH or set SJASMPLUS")


def find_asl(given):
    for candidate in (given, os.environ.get("ASL"), shutil.which("asl")):
        if candidate and Path(candidate).is_file():
            p2bin = Path(candidate).with_name("p2bin")
            if not p2bin.is_file() and shutil.which("p2bin"):
                p2bin = Path(shutil.which("p2bin"))
            if p2bin.is_file():
                return str(Path(candidate).resolve()), str(p2bin.resolve())
    sys.exit("asl / p2bin not found: pass --asl PATH (p2bin beside it) or set ASL")


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


def build_neogs(tools, work):
    """Assemble the NeoGS main ROM (main_rom/build.bat) with asl; the 32K image, the folder and asl's MAP"""
    asl, p2bin = tools
    folder = work / "neogs"
    shutil.copytree(NEOGS, folder)
    main = folder / "main_rom"
    result = subprocess.run([asl, "-U", "-q", "-g", "MAP", "main_ngs.a80"], cwd=main, capture_output=True, text=True,
                            errors="replace")
    if result.returncode != 0 or not (main / "main_ngs.p").is_file():
        sys.exit(f"asl failed (NeoGS):\n{result.stdout}{result.stderr}")
    result = subprocess.run([p2bin, "main_ngs.p", "ref.rom", "-r", "$-$", "-k"], cwd=main, capture_output=True,
                            text=True, errors="replace")
    if result.returncode != 0:
        sys.exit(f"p2bin failed (NeoGS):\n{result.stdout}{result.stderr}")
    image = (main / "ref.rom").read_bytes()
    if len(image) != 2 * PAGE:
        sys.exit(f"NeoGS: the build is {len(image)} bytes, not 32K")
    return image, folder


def asl_symbols(map_path):
    """asl's MAP file: {segment: [(name, value)]} for the integer symbols"""
    segments = {}
    segment = None
    for raw in map_path.read_text(encoding="latin-1").splitlines():
        found = re.match(r"Symbols in Segment (\w+)", raw)
        if found:
            segment = segments.setdefault(found.group(1), [])
            continue
        parts = raw.split()
        if segment is not None and len(parts) >= 3 and parts[1] == "Int":
            segment.append((parts[0], int(parts[2], 16)))
    return segments


def definitions(folder):
    """NAME -> (file, line) of the line that defines it (a name at column 0), over every *.a80 under `folder`"""
    found = {}
    for path in sorted(folder.rglob("*.a80")):
        lines = path.read_bytes().decode("cp866").splitlines()
        for number, text in enumerate(lines, 1):
            name = re.match(r"([A-Za-z_.@?][\w.@?]*)", text)
            if name and name.group(1).upper() not in found:
                found[name.group(1).upper()] = (str(path.relative_to(folder)), number)
    return found


def neogs_labels(folder):
    """The NeoGS main ROM's labels (pages as in labels_of: 0 = low 16K, 3 = high 16K, 1 = the RAM window)"""
    segments = asl_symbols(folder / "main_rom" / "main_ngs.map")
    where = definitions(folder)
    cache = {}
    labels = []

    def describe(name, value, page, equ):
        file, line = where.get(name.upper(), ("", 0))
        text = source_line(folder, cache, file, line) if file else ""
        body = text.split(";", 1)[0]
        rest = body[len(name):] if body.upper().startswith(name.upper()) else body
        rest = rest.lstrip(":")
        if not rest.strip() and file:
            for following in range(line + 1, line + 6):
                nxt = source_line(folder, cache, file, following).split(";", 1)[0]
                if nxt.strip():
                    rest = nxt
                    break
        kind = "DATA" if equ or page == 1 or DATA_DIRECTIVES.match(rest) else "CODE"
        comment = ascii_comment(text.split(";", 1)[1]) if ";" in text else ""
        if not comment and file:
            previous = source_line(folder, cache, file, line - 1)
            comment = ascii_comment(previous) if previous.lstrip().startswith(";") else ""
        return {"name": name, "address": value, "page": page, "type": kind, "comment": comment}

    for name, value in segments.get("CODE", []):
        page = 0 if value < 0x4000 else 3 if value >= 0xC000 else 1 if value < 0x8000 else None
        if page is None:
            continue
        labels.append(describe(name, value, page, False))
    # The RAM window's variables are EQUs of equ_ngs.a80
    for name, value in segments.get("NOTHING", []):
        file = where.get(name.upper(), ("", 0))[0]
        if 0x4000 <= value < 0x8000 and file.endswith("equ_ngs.a80") and \
                re.search(r"\bEQU\b", source_line(folder, cache, file, where[name.upper()][1]), re.IGNORECASE):
            labels.append(describe(name, value, 1, True))
    labels.sort(key=lambda l: (l["page"] != 1, l["address"], l["name"]))
    return labels


def write_neogs_map(title, labels):
    lines = [f"; {title}: labels of the NedoPC sources, placed by tools/gsfirmware/markup_symbols.py",
             "; Format: [RAMn:]ADDR NAME (TYPE) ; comment  (the loader runs the main ROM from RAM: RAM0 = its low 16K",
             ";         at #0000, RAM1 = its high 16K at #C000 with MPAG 0; no prefix = the card's RAM window",
             ";         #4000-#7FFF). Generated: do not edit; see REPORT.md and NEOGS-VS-GS.md"]
    for label in labels:
        if label["page"] == 0:
            where = f"RAM0:{label['address']:04X}"
        elif label["page"] == 3:
            where = f"RAM1:{label['address']:04X}"
        else:
            where = f"{label['address']:04X}"
        line = f"{where:<10} {label['name']:<16} ({label['type']})"
        if label["comment"]:
            line += f" ; {label['comment']}"
        lines.append(line.rstrip())
    return "\n".join(lines) + "\n"


def address_names(labels):
    """CPU address -> the set of names labelling it (ROM and RAM window)"""
    names = {}
    for label in labels:
        names.setdefault(label["address"], set()).add(label["name"].upper())
    return names


def spans_with_bytes(labels, image):
    """Each ROM label: (name, type, offset, the bytes up to the next ROM label)"""
    out = []
    for name, start, end in label_spans(labels):
        kind = next(l["type"] for l in labels if l["name"] == name and rom_offset(l) is not None)
        out.append((name, kind, start, image[start:end]))
    return out


def same_code(ngs, gs, ngs_names, gs_names):
    """The bytes match, a 16-bit operand being equal when both name the same label (a moved routine's JP / CALL /
    LD rr,nn target)"""
    if len(ngs) != len(gs):
        return False
    i = 0
    while i < len(ngs):
        if ngs[i] == gs[i]:
            i += 1
            continue
        matched = False
        for j in (i - 1, i):
            if 0 <= j and j + 1 < len(ngs):
                a = ngs_names.get(ngs[j] | ngs[j + 1] << 8, set())
                b = gs_names.get(gs[j] | gs[j + 1] << 8, set())
                if a & b:
                    i = j + 2
                    matched = True
                    break
        if not matched:
            return False
    return True


def compare_neogs(ngs_labels, ngs_image, gs_labels, gs_image, title, gs_title):
    """NEOGS-VS-GS.md: each NeoGS ROM label in one of three classes; the GS labels NeoGS does not have"""
    ngs_names, gs_names = address_names(ngs_labels), address_names(gs_labels)
    gs_list = spans_with_bytes(gs_labels, gs_image)
    gs_spans = {name.upper(): (name, kind, start, data) for name, kind, start, data in gs_list}
    gs_exact = {span[0]: span for span in gs_list}
    gs_starts = sorted((start, name) for name, kind, start, data in gs_spans.values())

    def gs_label_at(offset):
        """The GS label whose routine holds `offset`"""
        holder = None
        for start, name in gs_starts:
            if start > offset:
                break
            holder = name
        return holder

    def gs_cpu(offset):
        return offset if offset < PAGE else 0xC000 + offset - PAGE

    rows = []
    counts = {"same": 0, "changed": 0, "only": 0}
    seen_gs = set()
    for name, kind, start, data in spans_with_bytes(ngs_labels, ngs_image):
        cpu = start if start < PAGE else 0xC000 + start - PAGE
        # The name as written, else in any case (asl -U and sjasmplus both keep case: TOCIP, TOcip, TOCip)
        twin = gs_exact.get(name) or gs_spans.get(name.upper())
        # From the GS label on, as many bytes as the NeoGS routine has (a label NeoGS adds inside splits it)
        if twin and same_code(data, gs_image[twin[2]:twin[2] + len(data)], ngs_names, gs_names):
            cls, ref = "same", twin[0]
        elif twin:
            cls, ref = "changed", twin[0]
        else:
            # GS may hold the bytes without the name: at the same place, else anywhere (4 bytes or more)
            at = start if gs_image[start:start + len(data)] == data else gs_image.find(data) if len(data) >= 4 else -1
            cls, ref = ("same", "") if at >= 0 else ("only", "")
        if ref:
            seen_gs.add(ref.upper())
        counts[cls] += 1
        if cls == "same" and not ref:
            holder = gs_label_at(at)
            text = f"same as GS bytes at `#{gs_cpu(at):04X}`" + (f" (in `{holder}`)" if holder else "")
            if holder:
                seen_gs.add(holder.upper())
        else:
            text = {"same": f"same as GS `{ref}`", "changed": f"changed from GS `{ref}`", "only": "NeoGS only"}[cls]
        rows.append(f"| `#{cpu:04X}` | `{name}` | {kind} | {len(data)} | {text} |")
    dropped = sorted({name for name, _, _, _ in gs_list if name.upper() not in seen_gs})
    report = [f"# {title} compared with {gs_title}", "",
              "Generated by `tools/gsfirmware/markup_symbols.py` (gs-debugger N5). A label's routine is its bytes up",
              "to the next label, compared with as many GS bytes from the GS label of the same name. **Same**: the bytes",
              "match, a 16-bit operand counting as equal when it names the same label in both (a moved routine's",
              "target); a label GS does not have counts as the same when its bytes are in the GS ROM (at the same",
              "address, else anywhere; the GS label that holds them is named). **Changed**: GS has the name, the bytes",
              "differ. **NeoGS only**: GS has neither the name nor the bytes.", "",
              f"- NeoGS ROM labels: {sum(counts.values())}: {counts['same']} same as GS, {counts['changed']} changed, "
              f"{counts['only']} NeoGS only",
              f"- GS ROM labels NeoGS has neither by name nor by bytes: {len(dropped)}", "",
              "| Address | NeoGS label | Type | Bytes | Class |", "|---|---|---|---|---|", *rows, "",
              "## GS labels NeoGS does not have", "",
              ", ".join(f"`{n}`" for n in dropped) if dropped else "None.", ""]
    return "\n".join(report), counts, len(dropped)


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


def generate(sjasmplus, asl):
    files = {}
    report = ["# General Sound firmware symbols: mark-up report", "",
              "Generated by `tools/gsfirmware/markup_symbols.py` (gs-debugger design §6.2). The sources are",
              "`docs/inprogress/2026-09-19-general-sound/materials/gs/gs-firmware/firmware`.", ""]
    manifest = {"format": "unreal-symbols-manifest", "version": 1,
                "note": "General Sound card firmware labels for the card CPU's store (gs, ms): a bundle applies when "
                        "the card ROM's page (page 0; NeoGS: flash page 4) has the listed SHA-256; the map's ROM0 / "
                        "ROM1 are the card's ROM pages, NeoGS's RAM0 / RAM1 the RAM pages its loader copies them to.",
                "bundles": []}
    page0 = {}
    kept = {}
    with tempfile.TemporaryDirectory(dir=ROOT / "scratch" if (ROOT / "scratch").is_dir() else None) as tmp:
        work = Path(tmp)
        builds = {variant: build(sjasmplus, work, variant) for variant in ("plain", "patched")}
        for firmware_id, rom_file, title, variant in FIRMWARES:
            reference, folder = builds[variant]
            rom = (ROMS / rom_file).read_bytes()
            if len(rom) != 2 * PAGE:
                sys.exit(f"{rom_file}: {len(rom)} bytes, not 32K")
            labels = labels_of(folder)
            kept[firmware_id] = (labels, rom, title)
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
        neogs_report, neogs_bundle, files["neogs.map"], files["NEOGS-VS-GS.md"] = neogs(asl, work, kept[NEOGS_REFERENCE])
        report += neogs_report
        manifest["bundles"].append(neogs_bundle)
    files["manifest.json"] = json.dumps(manifest, indent=2) + "\n"
    files["REPORT.md"] = "\n".join(report)
    return files


def neogs_version(folder):
    text = (folder / "main_rom" / "version.a80").read_text(encoding="latin-1")
    found = re.search(r'VERS_TXT\s+EQU\s+"([^"]+)"', text)
    return found.group(1) if found else "?"


def neogs(asl, work, reference):
    """The NeoGS main ROM: its report lines, bundle, map and the comparison with GS"""
    image, folder = build_neogs(asl, work)
    flash = NEOGS_FLASH.read_bytes()
    rom = flash[NEOGS_MAIN_ROM:NEOGS_MAIN_ROM + 2 * PAGE]
    ranges = changed_ranges(rom, image)
    if ranges:
        sys.exit(f"{NEOGS_FLASH.name}: the main ROM differs from the NeoGS build in {len(ranges)} ranges")
    title = f"NeoGS firmware v{neogs_version(folder)}"
    labels = neogs_labels(folder)
    rom_count = sum(1 for l in labels if rom_offset(l) is not None)
    gs_labels, gs_rom, gs_title = reference
    comparison, counts, dropped = compare_neogs(labels, rom, gs_labels, gs_rom, title, gs_title)
    report = [f"## {title} (`neogs/full_ngs.rom`, flash `#{NEOGS_MAIN_ROM:05X}`)", "",
              "- Reference build: main_rom/main_ngs.a80 with asl + p2bin (main_rom/build.bat)",
              f"- Labels: {len(labels)} ({rom_count} in ROM, {len(labels) - rom_count} in the RAM window), run from RAM "
              "pages 0-1",
              "- The ROM equals the build: yes, byte for byte",
              f"- Against GS v1.05a ([NEOGS-VS-GS.md](NEOGS-VS-GS.md)): {counts['same']} same, {counts['changed']} "
              f"changed, {counts['only']} NeoGS only; {dropped} GS labels dropped",
              "- The loader (flash `#00000`, loader_ngs/) is not marked up: it runs at boot only, from the RAM window",
              ""]
    bundle = {"id": "gs:neogs", "title": title, "file": "neogs.map",
              "match": {"page": NEOGS_PAGE, "page_sha256": [hashlib.sha256(rom[:PAGE]).hexdigest()]},
              "note": f"data/rom/neogs/full_ngs.rom: the main ROM at flash #{NEOGS_MAIN_ROM:05X} (the copy the loader "
                      f"runs from RAM pages 0-1), image SHA-256 {hashlib.sha256(rom).hexdigest()}"}
    return report, bundle, write_neogs_map(title, labels), comparison


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--write", action="store_true", help="overwrite the committed files")
    parser.add_argument("--sjasmplus", help="the sjasmplus binary")
    parser.add_argument("--asl", help="the asl binary (p2bin beside it)")
    args = parser.parse_args()
    files = generate(find_sjasmplus(args.sjasmplus), find_asl(args.asl))
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
