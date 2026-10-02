"""The sessions every Phase 1 experiment reads (see ../README.md, "Data")."""

import glob
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", ".."))
FIXTURES = os.path.join(ROOT, "testdata", "ttd")
RECORDED = os.path.join(ROOT, "scratch", "ttd-experiments", "sessions")


def sessions():
    """(name, path) of every input session: the fixture corpus, then the recorded matrix cases."""
    out = []
    for path in sorted(glob.glob(os.path.join(FIXTURES, "*.ttd"))):
        out.append(("fixture:" + os.path.splitext(os.path.basename(path))[0], path))
    for path in sorted(glob.glob(os.path.join(RECORDED, "*.ttd"))):
        name = os.path.splitext(os.path.basename(path))[0]
        out.append(("matrix:" + name.replace("ttd-bench-v1-", ""), path))
    if not any(n.startswith("matrix:") for n, _ in out):
        print("note: no recorded matrix sessions in", RECORDED, "- run common/record-datasets.sh")
    return out
