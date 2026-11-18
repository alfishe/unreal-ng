#!/usr/bin/env python3
"""Classify the MGT images that the catalog's pass 1 left as unsupported_image.

Reads index/runs/runs-pass1.jsonl, opens each image from the pool, parses the MGT directory and prints a summary
plus a TSV (sha, title, path, fs, has +SYS, has AUTO, snapshot files, file types).
"""
import collections
import json
import os
import sys

ROOT = "/Volumes/TB4-4Tb/Data/zx-spectrum"
RUNS = os.path.join(ROOT, "index/runs/runs-pass1.jsonl")


def sector(img, order, cyl, side, sec):
    """512-byte sector (sec 1..10) of an 80-cylinder image."""
    if order == "mgt":
        track_index = cyl * 2 + side
    else:
        track_index = side * 80 + cyl
    off = (track_index * 10 + (sec - 1)) * 512
    return img[off:off + 512]


def directory(img, order):
    entries = []
    for cyl in range(4):
        for sec in range(1, 11):
            s = sector(img, order, cyl, 0, sec)
            for half in (0, 256):
                e = s[half:half + 256]
                if len(e) < 256:
                    continue
                entries.append(e)
    return entries


def main():
    rows = [json.loads(l) for l in open(RUNS)]
    rows = [r for r in rows if r.get("verdict") == "unsupported_image" and r.get("ext") == "mgt"]
    summary = collections.Counter()
    types = collections.Counter()
    out = []
    for r in rows:
        sha = r["sha"]
        path = os.path.join(ROOT, "pool/images", sha[:2], sha + ".mgt")
        if not os.path.exists(path):
            summary["missing in pool"] += 1
            continue
        img = open(path, "rb").read()
        if len(img) != 819200:
            summary["size %d" % len(img)] += 1
        ents = [e for e in directory(img, "mgt") if e[0] != 0]
        ftypes = [e[0] & 0x3F for e in ents]
        names = [e[1:11].decode("latin-1").rstrip() for e in ents]
        sam_types = sum(1 for t in ftypes if t >= 16)
        zx_types = sum(1 for t in ftypes if 1 <= t <= 11)
        sam_boot = img[(4 * 2 * 10) * 512 + 256:(4 * 2 * 10) * 512 + 260]
        is_sam_path = "Sam Coupe" in r["loc"] or "SAM" in r["loc"].split("/")[2:4]
        if not ents:
            fs = "empty-dir"
        elif sam_types > zx_types:
            fs = "sam"
        else:
            fs = "plusd"
        has_sys = any(t == 4 and n.upper().startswith("+SYS") for t, n in zip(ftypes, names))
        has_any_sys = any("SYS" in n.upper() for n in names)
        has_auto = any(n.upper().startswith("AUTO") for n in names)
        snaps = sum(1 for t in ftypes if t in (5, 9))
        for t in ftypes:
            types[t] += 1
        key = (fs, "sam-path" if is_sam_path else "zx-path", "+SYS" if has_sys else ("other SYS" if has_any_sys else "no SYS"),
               "AUTO" if has_auto else "no AUTO")
        summary[key] += 1
        out.append([sha, r["title"], r["loc"], fs, int(has_sys), int(has_auto), snaps, " ".join(map(str, ftypes)),
                    sam_boot.hex(), " | ".join(names[:6])])
    for k, v in sorted(summary.items(), key=lambda kv: -kv[1]):
        print(v, k)
    print("file types:", sorted(types.items()))
    with open(sys.argv[1] if len(sys.argv) > 1 else "mgt-classified.tsv", "w") as f:
        f.write("sha\ttitle\tlocation\tfs\thas_plus_sys\thas_auto\tsnapshots\ttypes\tsam_boot\tfirst_names\n")
        for row in out:
            f.write("\t".join(str(x) for x in row) + "\n")


if __name__ == "__main__":
    main()
