"""Python vs C++ (zxdisk dump / convert) on a few fixtures; skipped when the zxdisk binary is not built.
The full run over every fixture is tools/verification/disk/parity.py."""

from __future__ import annotations

import importlib.util
import json

import pytest

from conftest import REPO_ROOT

_TOOL = REPO_ROOT / "tools" / "verification" / "disk" / "parity.py"
_spec = importlib.util.spec_from_file_location("disk_parity", _TOOL)
parity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(parity)

ZXDISK = parity.find_zxdisk(None)
FIXTURES = [
    "testdata/sound/The_Viewer1.0.trd",
    "testdata/loaders/td0/trdos-sample.td0",
    "testdata/machines/atm/ballq_gr.scl",
    "testdata/machines/profi/CPM.UDI",
]


@pytest.mark.skipif(ZXDISK is None, reason="zxdisk is not built (tools/build/build.sh zxdisk)")
@pytest.mark.parametrize("fixture", FIXTURES)
def test_dump_and_convert_match_zxdisk(fixture, scratch):
    image = REPO_ROOT / fixture
    if not image.exists():
        pytest.skip(f"{fixture} is not in this checkout")
    result = parity.check_image(str(image), ZXDISK, True, str(scratch))
    assert result["problems"] == [], json.dumps(result, indent=1)


def test_compare_dumps_reports_first_difference():
    track = {"c": 0, "h": 1, "encoding": "mfm", "length": 4, "raw": "00112233", "clock": "00", "weak": "",
             "sectors": []}
    a = {"format": "trd", "cylinders": 1, "sides": 2, "tracks": [dict(track)]}
    b = {"format": "trd", "cylinders": 1, "sides": 2, "tracks": [dict(track, raw="00112244")]}
    assert parity.compare_dumps(a, a) is None
    assert parity.compare_dumps(a, b) == {"c": 0, "h": 1, "field": "raw", "offset": 3, "zxdisk": "33",
                                          "python": "44", "zxdisk_length": 4, "python_length": 4}
