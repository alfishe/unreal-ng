"""The branch-aware reader against the C++ reader (testdata/ttd/branches/expected.json, TTDBranchFixtures_Test).

For every fixture the lines, parents, forks and the content CRC of every checkpoint must be the C++ reader's; for
every damaged copy its verdict; for both folders the C++ join (size and CRC) and the C++ branches.tdx rebuild.
"""

import pytest

from ttdlib import TtdError
from ttdlib import folder as fld
from ttdlib.session import content_crcs, open_branched, read_branched, validate_branched
from ttdlib.ttdcontainer import crc32c, open_container
from ttdlib.writer import copy_parts

FILES = ["two-branches.ttd", "deep-lineage.ttd", "mid-frame.ttd", "many-short.ttd"]
FOLDERS = ["folder", "folder-crashed"]


def lines_of(data: bytes) -> dict:
    s = read_branched(open_branched(data))
    out = {}
    for line in s.lines.values():
        out[str(line.id)] = {"parent": line.parent, "fork_frame": line.fork_frame, "fork_t": line.fork_t,
                             "checkpoints": [list(x) for x in content_crcs(s, line.id)]}
    return out


def assert_lines(actual: dict, want: dict) -> None:
    assert sorted(actual, key=int) == sorted(want, key=int)
    for line, w in want.items():
        a = actual[line]
        assert (a["parent"], a["fork_frame"], a["fork_t"]) == (w["parent"], w["fork_frame"], w["fork_t"]), line
        assert a["checkpoints"] == w["checkpoints"], f"line {line}"


@pytest.mark.parametrize("name", FILES)
def test_files_read_as_the_cpp_reader(fixtures, expected, name):
    data = (fixtures / name).read_bytes()
    want = expected["files"][name]
    c = open_branched(data)
    assert c.finalized == want["finalized"]
    assert len(c.parts) == want["parts"]
    assert_lines(lines_of(data), want["lines"])
    assert validate_branched(data) == []


@pytest.mark.parametrize("name", FILES)
def test_copy_is_byte_identical(fixtures, name):
    data = (fixtures / name).read_bytes()
    c = open_branched(data)
    assert copy_parts(c, list(range(len(c.parts)))) == data


@pytest.mark.parametrize("name", FOLDERS)
def test_folder_join_is_the_cpp_join(fixtures, expected, name):
    joined = fld.join_files(fld.join_list(fixtures / name))
    want = expected["files"][name]
    assert len(joined) == want["joined_bytes"]
    assert crc32c(joined) == want["joined_crc32c"]
    assert_lines(lines_of(joined), want["lines"])


@pytest.mark.parametrize("name", FOLDERS)
def test_index_rebuild_is_the_cpp_rebuild(fixtures, expected, name):
    data = fld.rebuilt_index(fixtures / name)
    want = expected["index_rebuilt"][name]
    assert (len(data), crc32c(data)) == (want["bytes"], want["crc32c"])


def test_folder_index_reads(fixtures):
    index = fld.read_index((fixtures / "folder" / fld.INDEX_FILE).read_bytes())
    assert not index.damaged
    assert len(index.creates) == 2
    assert [s.line for s in index.segments] == [0, 1, 2, 0]
    cut = fld.read_index((fixtures / "folder-crashed" / fld.INDEX_FILE).read_bytes())
    assert cut.records < index.records


def test_damaged_copies_match_the_cpp_verdicts(fixtures, expected):
    assert expected["damaged"], "no damaged copies in expected.json"
    for name, want in expected["damaged"].items():
        data = (fixtures / "damaged" / name).read_bytes()
        if not want["loads"]:
            with pytest.raises(TtdError):
                read_branched(open_branched(data))
            continue
        c = open_branched(data)
        s = read_branched(c)
        assert c.finalized == want["finalized"], name
        assert len(c.parts) == want["parts"], name
        assert_lines(lines_of(data), want["lines"])
        assert s.notes or s.stopped_at or not c.finalized, f"{name}: damage not reported"


def test_branch_records(fixtures):
    s = read_branched(open_branched((fixtures / "deep-lineage.ttd").read_bytes()))
    names = {l.id: l.info.name for l in s.lines.values() if l.info}
    assert names[3] == "edit 3" and names[7] == "side"
    assert s.lines[3].info.pinned and s.lines[3].info.tags == ["deep"]
    assert s.lineage(7) == [0, 1, 2, 7]
    assert s.subtree(3) == [3, 4, 5, 6]
    assert s.active_line() == 6


def test_old_reader_still_reads_the_trunk(fixtures, expected):
    """A reader without branches (the analyzer's) reads the trunk of a branched file."""
    from ttdlib.ttdcontainer import read_session
    for name in FILES:
        data = (fixtures / name).read_bytes()
        s = read_session(open_container(data))
        trunk = [cp[0] for cp in expected["files"][name]["lines"]["0"]["checkpoints"]]
        assert [cp.frame for cp in s.checkpoints] == trunk, name
