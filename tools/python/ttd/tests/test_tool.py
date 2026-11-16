"""ttdtool.py commands on the fixtures; everything written goes to a temporary folder."""

import contextlib
import io
import os
import shutil
import time
from pathlib import Path

import pytest

import make_tool_outputs
import ttdtool
from ttdlib import TtdError
from ttdlib import folder as fld
from ttdlib.session import content_crcs, open_branched, read_branched


def run(*argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        code = ttdtool.main([str(a) for a in argv])
    return code, out.getvalue()


def loaded_lines(path: Path):
    data = fld.join_files(fld.join_list(path)) if path.is_dir() else path.read_bytes()
    return read_branched(open_branched(data))


def copy_folder(src: Path, dst: Path) -> Path:
    shutil.copytree(src, dst)
    return dst


# ---------------------------------------------------------------------------
# Reading commands

@pytest.mark.parametrize("name", ["two-branches.ttd", "deep-lineage.ttd", "folder", "folder-crashed"])
def test_info_tree_stats(fixtures, name):
    code, text = run("info", fixtures / name)
    assert code == 0 and "lines:" in text
    code, text = run("tree", fixtures / name)
    assert code == 0 and text.startswith("trunk")
    code, text = run("stats", fixtures / name, "--json")
    assert code == 0 and '"unique_bytes"' in text


def test_tree_shows_records(fixtures):
    code, text = run("tree", fixtures / "deep-lineage.ttd")
    assert "[3] edit 3" in text and "PINNED" in text and "by mcp:claude" in text and "(active)" in text
    assert "[7] side" in text


def test_stats_unique_bytes(fixtures):
    ld = ttdtool.load(fixtures / "deep-lineage.ttd")
    rows = {r["line"]: r for r in ttdtool.branch_stats(ld)}
    assert rows[6]["unique_bytes"] == rows[6]["bytes"]
    assert rows[3]["unique_bytes"] == sum(rows[l]["bytes"] for l in (3, 4, 5, 6))
    assert rows[0]["unique_bytes"] == sum(r["bytes"] for r in rows.values())


@pytest.mark.parametrize("name", ["two-branches.ttd", "deep-lineage.ttd", "mid-frame.ttd", "many-short.ttd",
                                  "folder"])
def test_validate_sound(fixtures, name):
    code, text = run("validate", fixtures / name)
    assert code == 0, text


def test_validate_crashed_folder_reports(fixtures):
    code, text = run("validate", fixtures / "folder-crashed")
    assert code == 1 and "branches.tdx: ends early" in text and "segment-0003.ttd: no complete part" in text


def test_validate_damaged(fixtures):
    for f in sorted((fixtures / "damaged").iterdir()):
        code, text = run("validate", f, "--json")
        assert code == 1, f.name


# ---------------------------------------------------------------------------
# Writing commands

@pytest.mark.parametrize("name", list(make_tool_outputs.OUTPUTS))
def test_tool_outputs_are_reproduced(fixtures, tmp_path, name):
    """The committed outputs (core-tests restore them as the originals) are what the tool writes today."""
    out = tmp_path / name
    code, text = run(*make_tool_outputs.command(name, out)[:-1])   # without --apply: a dry run
    assert code == 0 and "dry run" in text and not out.exists(), text
    code, text = run(*make_tool_outputs.command(name, out))
    assert code == 0, text
    assert out.read_bytes() == (fixtures / "tool-outputs" / name).read_bytes()


def test_extract_keeps_positions(fixtures, tmp_path):
    out = tmp_path / "e.ttd"
    assert run("extract", fixtures / "deep-lineage.ttd", "--branch", "5", "-o", out, "--apply")[0] == 0
    s, o = loaded_lines(out), loaded_lines(fixtures / "deep-lineage.ttd")
    assert sorted(s.lines) == [0, 1, 2, 3, 4, 5]
    for line in s.lines:
        assert content_crcs(s, line) == content_crcs(o, line)


def test_extract_refuses(fixtures, tmp_path):
    src = fixtures / "two-branches.ttd"
    assert run("extract", src, "--branch", "nope", "-o", tmp_path / "x.ttd", "--apply")[0] == 1
    assert run("extract", src, "--branch", "1", "-o", src, "--apply")[0] == 1          # in place
    (tmp_path / "exists.ttd").write_bytes(b"x")
    assert run("extract", src, "--branch", "1", "-o", tmp_path / "exists.ttd", "--apply")[0] == 1


def test_prune_file_criteria(fixtures, tmp_path):
    src = fixtures / "many-short.ttd"
    s = loaded_lines(src)
    now = max(l.info.created_micros for l in s.lines.values() if l.info) / 1e6 + 1

    def pruned(*criteria):
        out = tmp_path / f"p{len(list(tmp_path.iterdir()))}.ttd"
        code, text = run("prune", src, *criteria, "--now", now, "-o", out, "--apply")
        assert code == 0, text
        if "nothing to prune" in text:
            return set()
        return set(s.lines) - set(loaded_lines(out).lines)

    # many-short: lines 1-16 run-1, 17-24 run-2; qt made 5, 10, 15, 20; tags keep on 4, 11, 21; 6 and 18 pinned;
    # lengths 3 frames for 1, 4, 7, ... else 2; line k created 2 * (24 - k) days before the newest (+1 s)
    assert pruned("--run", "run-2") == {17, 19, 20, 21, 22, 23, 24}            # 18 pinned
    assert pruned("--run", "run-2", "--force") == set(range(17, 25))
    assert pruned("--tag", "keep") == {4, 11, 21}
    assert pruned("--created-by", "qt") == {5, 10, 15, 20}
    assert pruned("--created-by", "anal*", "--untagged", "--shorter-than", "3") == {2, 3, 8, 9, 12, 14, 17, 23, 24}
    assert pruned("--longer-than", "2") == {1, 4, 7, 10, 13, 16, 19, 22}
    assert pruned("--older-than", "40") == {1, 2, 3, 4}
    assert pruned("--not-lineage-of", "24", "--unpinned") == set(range(1, 24)) - {6, 18}
    assert pruned("--tag", "nothing-has-it") == set()


def test_prune_never_the_trunk_nor_pinned(fixtures, tmp_path):
    src = fixtures / "deep-lineage.ttd"
    out = tmp_path / "p.ttd"
    code, text = run("prune", src, "--tag", "deep", "-o", out, "--apply")
    # edit 1 and 2 are over the pinned edit 3: kept; 3 pinned; 4..6 go
    assert code == 0, text
    assert sorted(loaded_lines(out).lines) == [0, 1, 2, 3, 7]
    assert "kept:" in text
    code, _ = run("prune", src, "-o", tmp_path / "q.ttd")
    assert code == 2                                                            # no criterion


def test_prune_folder(fixtures, tmp_path):
    folder = copy_folder(fixtures / "folder", tmp_path / "rec")
    before = sorted(p.name for p in folder.iterdir())
    code, text = run("prune", folder, "--shorter-than", "5")
    assert code == 0 and "dry run" in text, text
    assert sorted(p.name for p in folder.iterdir()) == before
    code, text = run("prune", folder, "--shorter-than", "5", "--apply")
    assert code == 0, text
    assert not (folder / "segment-0002-b2.ttd").exists()
    index = fld.read_index((folder / fld.INDEX_FILE).read_bytes())
    assert index.deleted == {2}
    assert sorted(loaded_lines(folder).lines) == [0, 1]
    assert run("validate", folder)[0] == 0


def test_writes_refuse_a_live_recording(fixtures, tmp_path):
    folder = copy_folder(fixtures / "folder", tmp_path / "live")
    (folder / fld.OWNER_FILE).write_text(f"{os.getpid()}\n")
    for argv in (("prune", folder, "--run", "x", "--apply"), ("join", folder, "-o", tmp_path / "j.ttd", "--apply"),
                 ("reindex", folder, "--apply"), ("recover", folder, "-o", tmp_path / "r.ttd", "--apply")):
        code, text = run(*argv)
        assert code == 1 and "being recorded" in text, argv


def test_join_crashed_folder(fixtures, tmp_path):
    out = tmp_path / "j.ttd"
    code, text = run("join", fixtures / "folder-crashed", "-o", out, "--apply")
    assert code == 0 and "validated" in text, text
    assert run("validate", out)[0] == 0


def test_reindex(fixtures, tmp_path):
    folder = copy_folder(fixtures / "folder-crashed", tmp_path / "c")
    code, text = run("reindex", folder)
    assert code == 0 and "dry run" in text
    code, text = run("reindex", folder, "--apply")
    assert code == 0, text
    assert (folder / "branches.tdx.old").read_bytes() == (fixtures / "folder-crashed" / fld.INDEX_FILE).read_bytes()
    assert (folder / fld.INDEX_FILE).read_bytes() == fld.rebuilt_index(fixtures / "folder-crashed")
    assert run("reindex", folder)[1].count("the same as the folder's") == 1
    # The intact folder's index is what a rebuild writes (no records lost or reordered)
    assert run("reindex", fixtures / "folder")[0] == 0


def test_recover(fixtures, tmp_path):
    code, text = run("recover", fixtures / "folder-crashed")
    assert code == 0 and "recoverable:" in text and "trunk" in text
    out = tmp_path / "r.ttd"
    assert run("recover", fixtures / "folder-crashed", "-o", out, "--apply")[0] == 0
    assert out.read_bytes() == (fixtures / "tool-outputs" / "join-folder-crashed.ttd").read_bytes()
    # A flipped bit in a branch's file: that branch from there on is dropped, the rest kept
    folder = copy_folder(fixtures / "folder", tmp_path / "f")
    seg = folder / "segment-0002-b2.ttd"
    data = bytearray(seg.read_bytes())
    data[len(data) // 2] ^= 0x40
    seg.write_bytes(bytes(data))
    code, text = run("recover", folder, "-o", tmp_path / "r2.ttd", "--apply")
    assert code == 0, text
    assert 2 not in loaded_lines(tmp_path / "r2.ttd").lines
    assert run("join", folder, "-o", tmp_path / "j2.ttd", "--apply")[0] == 1      # the join refuses


def test_gc(tmp_path):
    root = tmp_path / "ttd"
    now = time.time()
    for name, age_days, owner in (("old-crashed", 10, "0"), ("young-crashed", 2, "0"),
                                  ("old-live", 30, str(os.getpid()))):
        d = root / name
        (d / "sub").mkdir(parents=True)
        (d / fld.OWNER_FILE).write_text(owner + "\n")
        (d / "segment-0000.ttd").write_bytes(b"x")
        (d / "sub" / "f").write_bytes(b"y")
        t = now - age_days * 86400
        for p in (d / "sub" / "f", d / "sub", d / "segment-0000.ttd", d / fld.OWNER_FILE, d):
            os.utime(p, (t, t))
    (root / "saved.ttd").write_bytes(b"z")
    os.utime(root / "saved.ttd", (now - 99 * 86400,) * 2)
    code, text = run("gc", "--root", root, "--older-than", "7", "--now", now)
    assert code == 0 and "old-crashed" in text and "young" not in text and "old-live" not in text
    assert (root / "old-crashed").exists()
    code, text = run("gc", "--root", root, "--older-than", "7", "--now", now, "--apply")
    assert code == 0, text
    assert not (root / "old-crashed").exists()
    assert (root / "young-crashed").exists() and (root / "old-live").exists() and (root / "saved.ttd").exists()


def test_flatten_without_emulator(fixtures, tmp_path):
    code, text = run("flatten", fixtures / "two-branches.ttd", "--branch", "1", "-o", tmp_path / "f.ttd",
                     "--url", "http://127.0.0.1:9", "--apply")
    assert code == 1 and "no emulator answers" in text
    assert not (tmp_path / "f.ttd").exists()


def test_flatten_calls_the_webapi(fixtures, tmp_path, monkeypatch):
    calls = []
    out = tmp_path / "flat.ttd"

    def fake(self, method, path, body=None):
        calls.append((method, path.split("?")[0], body))
        if path.endswith("/ttd/dump"):
            Path(body["path"]).write_bytes((fixtures / "deep-lineage.ttd").read_bytes())   # any sound file
            return {"ok": True, "path": body["path"]}
        return {"id": "emu-1", "session": "s-1", "machine": {"model": "PENTAGON"}}

    monkeypatch.setattr(ttdtool.WebApi, "call", fake)
    code, text = run("flatten", fixtures / "two-branches.ttd", "--branch", "1", "-o", out, "--apply")
    assert code == 0, text
    paths = [(m, p) for m, p, _ in calls]
    assert ("POST", "/api/v1/emulator/emu-1/ttd/load") in paths
    assert ("POST", "/api/v1/emulator/emu-1/ttd/branches/1/switch") in paths
    assert ("POST", "/api/v1/emulator/emu-1/ttd/dump") in paths
    assert ("DELETE", "/api/v1/emulator/emu-1") in paths
    dump = next(b for m, p, b in calls if p.endswith("/ttd/dump"))
    assert dump["lineage"] == "true"


def test_errors_are_ttd_errors(tmp_path):
    bad = tmp_path / "bad.ttd"
    bad.write_bytes(b"not a session")
    with pytest.raises(TtdError):
        ttdtool.load(bad)
    assert run("info", bad)[0] == 1
    assert run("info", tmp_path / "missing.ttd")[0] == 1
