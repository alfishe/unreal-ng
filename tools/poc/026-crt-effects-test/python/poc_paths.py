"""Paths shared by the 026 scripts: no machine-specific absolute paths.

Output goes to <repo>/scratch/. The source image (352x288 ZX Spectrum 128K menu screenshot) is
taken from the first argument, else $CRT_SOURCE_IMAGE, else <repo>/scratch/128k.png.
"""
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
SCRATCH = REPO_ROOT / "scratch"


def source_image() -> Path:
    if len(sys.argv) > 1:
        return Path(sys.argv[1])
    return Path(os.environ.get("CRT_SOURCE_IMAGE", SCRATCH / "128k.png"))
