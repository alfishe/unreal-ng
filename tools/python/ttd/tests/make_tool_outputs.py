#!/usr/bin/env python3
"""Write testdata/ttd/branches/tool-outputs/ with ttdtool.py (run after DISABLED_WriteBranchFixtures).

TTDBranchToolOutputs_Test (core-tests) loads these and checks each restores as its original; test_tool.py checks the
tool still writes the same bytes.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ttdtool  # noqa: E402

FIXTURES = Path(__file__).resolve().parents[4] / "testdata" / "ttd" / "branches"

# name -> ttdtool arguments (the output path is appended)
OUTPUTS = {
    "extract-deep-side-lineage.ttd": ["extract", "deep-lineage.ttd", "--branch", "side", "--lineage"],
    "extract-deep-edit3-subtree.ttd": ["extract", "deep-lineage.ttd", "--branch", "edit 3", "--subtree"],
    "prune-many-run1.ttd": ["prune", "many-short.ttd", "--run", "run-1"],
    "join-folder.ttd": ["join", "folder"],
    "join-folder-crashed.ttd": ["join", "folder-crashed"],
}


def command(name: str, out: Path):
    args = list(OUTPUTS[name])
    args[1] = str(FIXTURES / args[1])
    return args + ["-o", str(out), "--apply"]


def main() -> int:
    out_dir = FIXTURES / "tool-outputs"
    out_dir.mkdir(exist_ok=True)
    for name in OUTPUTS:
        target = out_dir / name
        if target.exists():
            target.unlink()
        if ttdtool.main(command(name, target)) != 0:
            print(f"{name}: failed", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
