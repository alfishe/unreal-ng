"""HFE / SCP and the flux layer against the C++ reference (zxdisk, built by tools/build/build.sh).

Read: the Python parse of an HFE / SCP gives the tracks `zxdisk dump` reports for it.
Write: the Python serialize of the tracks `zxdisk dump` reports for a source image is byte-identical to the file
`zxdisk convert <source> out.hfe|.scp` writes.
Fixtures are generated with zxdisk at test time; the tests skip when the binary is absent.
The bigger sets (testdata, real HFE files, any folder of images) run through tools/verification/disk/parity.py.
"""

from __future__ import annotations

import json
import os
import struct
import subprocess
from pathlib import Path

import pytest

from conftest import REPO_ROOT
from diskconverter import flux
from diskconverter.flux import FluxPll, RawTrackData
from diskconverter.formats import hfe, scp

ZXDISK = Path(os.environ.get("ZXDISK") or REPO_ROOT / "cmake-build-agent-release" / "bin" / "zxdisk")
TESTDATA = REPO_ROOT / "testdata" / "loaders"

needs_zxdisk = pytest.mark.skipif(not ZXDISK.exists(), reason="zxdisk is not built")


def zxdisk_dump(path) -> dict:
    return json.loads(subprocess.run([str(ZXDISK), "dump", str(path)], check=True, capture_output=True).stdout)


def zxdisk_convert(source, target) -> None:
    subprocess.run([str(ZXDISK), "convert", str(source), str(target)], check=True, capture_output=True)


def dumped_tracks(dump: dict) -> dict:
    return {(t["c"], t["h"]): RawTrackData(t["encoding"], bytes.fromhex(t["raw"]), bytes.fromhex(t["clock"]),
                                           bytes.fromhex(t["weak"]) if t["weak"] else None)
            for t in dump["tracks"]}


def assert_read_matches(module, path) -> None:
    reference = zxdisk_dump(path)
    disk = module.read(str(path))
    assert (disk.cylinders, disk.sides) == (reference["cylinders"], reference["sides"])
    for key, want in dumped_tracks(reference).items():
        got = disk.tracks[key]
        assert got.encoding == want.encoding, key
        assert got.raw == want.raw, key
        assert got.clock == want.clock, key
        assert got.weak == want.weak, key


# region <Flux layer, no reference needed>

def test_mfm_encode_decode_round_trip_keeps_sync_marks():
    raw = bytes([0x4E] * 10 + [0x00] * 12 + [0xA1] * 3 + [0xFE, 0x01, 0x00, 0x05, 0x01] + [0x4E] * 20)
    clock = bytearray((len(raw) + 7) // 8)
    for offset in (22, 23, 24):
        clock[offset >> 3] |= 1 << (offset & 7)
    cells = flux.mfm_encode(raw, bytes(clock))
    assert len(cells) == 16 * len(raw)
    assert flux.mfm_decode(cells) == (raw, bytes(clock))


def test_fm_encode_decode_round_trip_keeps_marks():
    raw = bytes([0xFF] * 8 + [0x00] * 6 + [0xFE, 0x00, 0x00, 0x01, 0x01, 0xF7, 0xF7] + [0x00] * 6 + [0xFB])
    clock = bytearray((len(raw) + 7) // 8)
    for offset in (14, len(raw) - 1):
        clock[offset >> 3] |= 1 << (offset & 7)
    assert flux.fm_decode(flux.fm_encode(raw, bytes(clock))) == (raw, bytes(clock))


def test_pll_on_ideal_mfm_flux_and_mode_detection():
    pll = FluxPll("mfm")
    pll.add_intervals([4000, 6000, 8000, 4000] * 300)
    # an interval ends with its transition
    assert pll.bit_cells() == ("01" + "001" + "0001" + "01") * 300
    assert pll.current_cell_ns == 2000
    # DD MFM has 6 us intervals, DD FM only 4 and 8 us; nothing double density at all counts as MFM
    assert FluxPll.detect_mode([4000, 6000, 8000] * 10) == "mfm"
    assert FluxPll.detect_mode([4000, 8000] * 10) == "fm"
    assert FluxPll.detect_mode([2000, 3000] * 10) == "mfm"


def test_pll_trailing_time_to_the_index():
    pll = FluxPll("mfm")
    pll.add_intervals([4000, 2000])
    pll.add_trailing_time(4100)
    pll.add_trailing_time(900)
    assert pll.bit_cells() == "011" + "00"


def test_fm_index_mark_survives_encode_decode():
    fm_raw = bytes([0xFF] * 4 + [0x00] * 6 + [0xFC] + [0xFF] * 5)
    fm_clock = bytearray((len(fm_raw) + 7) // 8)
    fm_clock[10 >> 3] |= 1 << (10 & 7)
    assert flux.fm_decode(flux.fm_encode(fm_raw, bytes(fm_clock))) == (fm_raw, bytes(fm_clock))


def test_pll_tracks_a_slow_drive_like_cpp():
    """Hand-computed C++ arithmetic: round(2100 / 2000) = 1, error 100, adjustment 5 -> cell 2005, and so on"""
    pll = FluxPll("mfm")
    pll.add_intervals([2100, 2100, 4500])
    # 2100/2000 -> 1 cell, cell 2005; 2100/2005 -> 1 cell, error 95 -> +4 -> 2009; 4500/2009 = 2.24 -> 2 cells
    assert pll.bit_cells() == "1" + "1" + "01"
    assert pll.current_cell_ns == 2009 + (4500 - 2 * 2009) * 5 // 100

# endregion </Flux layer, no reference needed>


# region <Against zxdisk>

@pytest.fixture(scope="module")
def generated(tmp_path_factory):
    """HFE and SCP of the sources, written by zxdisk; (source dump, path) per (source name, extension)"""
    if not ZXDISK.exists():
        pytest.skip("zxdisk is not built")
    directory = tmp_path_factory.mktemp("flux")
    sources = {
        "atarin": TESTDATA / "trd" / "atarin.trd",                  # 80 x 2 TR-DOS
        "protected": TESTDATA / "td0" / "protected-sample.td0",    # FM and MFM tracks, protection
        "beta128": TESTDATA / "udi" / "beta128-empty.udi",         # 86 cylinders: HFE only, SCP refuses
    }
    out = {}
    for name, source in sources.items():
        dump = zxdisk_dump(source)
        for ext in ("hfe",) if name == "beta128" else ("hfe", "scp"):
            target = directory / f"{name}.{ext}"
            zxdisk_convert(source, target)
            out[(name, ext)] = (dump, target)
    return out


CASES = [("atarin", "hfe"), ("protected", "hfe"), ("beta128", "hfe"), ("atarin", "scp"), ("protected", "scp")]


@needs_zxdisk
@pytest.mark.parametrize("name,ext", CASES)
def test_write_is_byte_identical_to_cpp(generated, name, ext):
    dump, reference = generated[(name, ext)]
    module = hfe if ext == "hfe" else scp
    assert module.serialize(dumped_tracks(dump), dump["cylinders"], dump["sides"]) == reference.read_bytes()


@needs_zxdisk
@pytest.mark.parametrize("name,ext", CASES)
def test_read_matches_cpp(generated, name, ext):
    assert_read_matches(hfe if ext == "hfe" else scp, generated[(name, ext)][1])


@needs_zxdisk
def test_hfe_v1_read_matches_cpp(generated, tmp_path):
    dump, _ = generated[("protected", "hfe")]
    path = tmp_path / "v1.hfe"
    path.write_bytes(hfe.serialize_v1(dumped_tracks(dump), dump["cylinders"], dump["sides"]))
    assert_read_matches(hfe, path)


@needs_zxdisk
def test_hfe_v3_opcodes_and_weak_ranges_match_cpp(generated, tmp_path):
    """A C++-written v3 file with more NOP, SETINDEX, SETBITRATE, SKIPBITS and RAND opcodes spliced into its
    tracks, some of them cut short or with odd arguments"""
    _, source = generated[("atarin", "hfe")]
    data = bytearray(source.read_bytes())
    lut = hfe._u16(data, 0x12) * 512
    for cylinder, splice in ((0, [0xF0, 0xF1]), (1, [0xF2, 0x40, 0x00]), (2, [0xF3, 0x20]), (3, [0xF4, 0x30]),
                             (4, [0xF4, 0x08, 0xF4, 0x80]), (5, [0xF4, 0xFF])):
        start = hfe._u16(data, lut + cylinder * 4) * 512
        # opcodes are compared after bit reversal: store them reversed, in side 0 of the first block
        for index, byte in enumerate(splice):
            data[start + 40 + index] = flux.bit_reverse_bytes(bytes([byte]))[0]
    path = tmp_path / "opcodes.hfe"
    path.write_bytes(bytes(data))
    assert_read_matches(hfe, path)
    assert hfe.read(str(path)).tracks[(3, 0)].weak is not None


@needs_zxdisk
def test_scp_revolutions_merge_and_weak_match_cpp(generated, tmp_path):
    """The protected sample's SCP grown to 3 revolutions per track, one flux value changed in the second and
    third: majority vote, weak bytes and the 2-to-1 / 1-1-1 cases as C++ resolves them"""
    _, source = generated[("protected", "scp")]
    data = source.read_bytes()
    table = [struct.unpack_from("<I", data, 16 + 4 * t)[0] for t in range(168)]
    out = bytearray(data[:16]) + bytes(168 * 4)
    out[5] = 3
    for t, offset in enumerate(table):
        if not offset:
            continue
        count = struct.unpack_from("<I", data, offset + 8)[0]
        index_time = struct.unpack_from("<I", data, offset + 4)[0]
        ticks = list(struct.unpack_from(f">{count}H", data, offset + 16))
        revs = [list(ticks), list(ticks), list(ticks)]
        revs[1][200] += 80          # one more cell: shifts the second revolution from byte ~25 on
        revs[2][1000] += 40         # half a cell: a single disagreement further on
        new_offset = len(out)
        struct.pack_into("<I", out, 16 + 4 * t, new_offset)
        out += data[offset:offset + 4]
        data_start = 4 + 3 * 12
        for rev in revs:
            out += struct.pack("<III", index_time, len(rev), data_start)
            data_start += 2 * len(rev)
        for rev in revs:
            out += struct.pack(f">{len(rev)}H", *rev)
    path = tmp_path / "revs.scp"
    path.write_bytes(bytes(out))
    assert_read_matches(scp, path)
    assert any(track.weak for track in scp.read(str(path)).tracks.values())

@needs_zxdisk
def test_scp_refuses_more_cylinders_than_the_track_table(tmp_path):
    source = TESTDATA / "udi" / "beta128-empty.udi"
    result = subprocess.run([str(ZXDISK), "convert", str(source), str(tmp_path / "out.scp")], capture_output=True,
                            text=True)
    assert result.returncode != 0
    dump = zxdisk_dump(source)
    with pytest.raises(ValueError, match="track table holds 84"):
        scp.serialize(dumped_tracks(dump), dump["cylinders"], dump["sides"])


@needs_zxdisk
@pytest.mark.parametrize("ext", ["hfe", "scp"])
def test_round_trip_keeps_the_tracks(generated, ext):
    """What the emulator saves reads back as the same tracks (TR-DOS MFM and the protected FM / MFM mix)"""
    for name in ("atarin", "protected"):
        dump, path = generated[(name, ext)]
        disk = (hfe if ext == "hfe" else scp).read(str(path))
        for key, want in dumped_tracks(dump).items():
            got = disk.tracks[key]
            assert (got.encoding, got.raw, got.clock) == (want.encoding, want.raw, want.clock), (name, key)


def test_hfe_v3_keeps_weak_bytes():
    raw = bytes([0x4E] * 100 + [0x00] * 12 + [0xA1] * 3 + [0xFB] + [0xE5] * 256 + [0x4E] * 5878)
    clock = bytearray((len(raw) + 7) // 8)
    for offset in (112, 113, 114):
        clock[offset >> 3] |= 1 << (offset & 7)
    weak = bytearray(len(clock))
    for offset in range(200, 220):
        weak[offset >> 3] |= 1 << (offset & 7)
    tracks = {(0, 0): RawTrackData("mfm", raw, bytes(clock), bytes(weak))}
    back = hfe.parse(hfe.serialize(tracks, 1, 1)).tracks[(0, 0)]
    assert back.weak == bytes(weak)
    assert back.raw[:200] == raw[:200] and back.raw[220:] == raw[220:]
    assert back.clock == bytes(clock)

# endregion </Against zxdisk>
