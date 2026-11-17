"""Chunked streams (F3): a payload over the record limit is stored as records of the stream's chunked twin
(stream | 0x8000, every record but the last flagged continued) and read back whole.

- The ttdlib writer chunks over its record limit; the reader joins the chunks bit for bit, from the index and by
  scanning; a verbatim copy is byte-identical; a reader that does not know the twin skips it and never takes a
  chunk for the whole; a cut chunked stream is damage.
- The C++ writer's chunked session (testdata/ttd/v2/chunked/, TTDSessionFileChunks_Test.DISABLED_WriteChunkedFixture)
  reads as the C++ reader read it: the write journal's and the holder stream's chunk counts, sizes and CRC32C.
"""

import json
import random

import pytest

from conftest import PROJECT_ROOT
from ttdlib import TtdError
from ttdlib.session import known_stream
from ttdlib.ttdcontainer import (FLAG_CONTINUED, StreamDesc, chunked_twin, crc32c, is_chunked_twin, open_container,
                                 read_record)
from ttdlib.writer import ContainerWriter, copy_parts

CHUNKED = PROJECT_ROOT / "testdata" / "ttd" / "v2" / "chunked"
LIMIT = 1000


def streams(declare_twin: bool = True):
    out = [StreamDesc(1, 1, True, "pieces"), StreamDesc(7, 1, False, "write-journal")]
    if declare_twin:
        out.append(StreamDesc(chunked_twin(7), 1, False, "chunked-write-journal"))
    return out


def payload(n: int, seed: int, compressible: bool) -> bytes:
    if compressible:
        return bytes((i // 64 + seed) & 0xFF for i in range(n))
    return random.Random(seed).randbytes(n)


def write(big: int, compressible: bool):
    """Two parts: a small stream-1 payload, a stream-7 payload of @p big bytes, a stream-1 payload of the limit."""
    w = ContainerWriter(0, bytes(16), 1_759_500_000_000_000, 0, streams(), b"")
    w.record_limit = LIMIT
    payloads = []
    for part in range(2):
        for sid, data in ((1, payload(100, part * 3 + 1, True)), (7, payload(big, part * 3 + 2, compressible)),
                          (1, payload(LIMIT, part * 3 + 3, False))):
            w.add_record(sid, data)
            payloads.append(data)
        w.end_part(part * 50, 50)
    return w, payloads


def expect_payloads(c, payloads, chunks):
    assert [p.damaged for p in c.parts] == [False, False]
    k = 0
    for part in c.parts:
        assert len(part.records) == 3, "the chunked stream is one payload"
        assert len(part.physical) == 2 + chunks
        assert part.records[1].stream_id == 7
        assert len(part.records[1].chunks) == chunks
        assert not part.records[2].chunks, "a payload of exactly the limit is one record"
        assert all(r.raw_size <= LIMIT for r in part.physical)
        for rec in part.records:
            assert read_record(c, rec) == payloads[k], f"payload {k} is not bit-identical"
            k += 1


@pytest.mark.parametrize("compressible", [False, True])
def test_a_stream_over_the_limit_reads_back_whole(compressible):
    w, payloads = write(3500, compressible)
    c = open_container(w.finalize())
    assert c.finalized and not c.notes
    expect_payloads(c, payloads, 4)


def test_a_scanned_file_joins_the_chunks_too():
    w, payloads = write(2001, False)
    c = open_container(w.unfinalized())
    assert not c.finalized
    expect_payloads(c, payloads, 3)


def test_a_verbatim_copy_is_byte_identical():
    w, _ = write(3500, True)
    data = w.finalize()
    assert copy_parts(open_container(data), [0, 1]) == data


def test_a_reader_without_chunks_sees_only_the_twin():
    w, _ = write(3500, False)
    c = open_container(w.finalize(), known=lambda sid: not is_chunked_twin(sid))
    assert c.notes == ["ancillary stream 'chunked-write-journal' (id 32775) skipped"]
    assert all(r.stream_id != 7 for p in c.parts for r in p.physical)


def test_a_cut_chunked_stream_is_damage():
    w = ContainerWriter(0, bytes(16), 0, 0, streams(), b"")
    w.add_stored(chunked_twin(7), FLAG_CONTINUED, payload(500, 9, False), 500)
    w.add_record(1, payload(50, 1, True))
    w.end_part(0, 1)
    c = open_container(w.finalize())
    assert c.parts[0].damaged
    assert "is cut" in c.parts[0].damage


def test_an_undeclared_stream_over_the_limit_is_refused_with_the_sizes():
    w = ContainerWriter(0, bytes(16), 0, 0, streams(declare_twin=False), b"")
    w.record_limit = LIMIT
    with pytest.raises(TtdError, match="1500 bytes passes the record limit of 1000"):
        w.add_record(7, payload(1500, 1, False))


def test_the_cpp_chunked_session_reads_as_the_cpp_reader():
    want = json.loads((CHUNKED / "chunked.json").read_text())
    data = (CHUNKED / want["file"]).read_bytes()
    c = open_container(data, known=known_stream)
    assert c.finalized
    assert not any(p.damaged for p in c.parts)
    found = {}
    for part in c.parts:
        for rec in part.records:
            found[rec.stream_id] = (len(rec.chunks) or 1, read_record(c, rec))
    for sid, prefix in ((7, "write_journal"), (want["holder_stream"], "holder")):
        chunks, data = found[sid]
        assert chunks == want[f"{prefix}_chunks"] and chunks > 1
        assert len(data) == want[f"{prefix}_bytes"]
        assert crc32c(data) == want[f"{prefix}_crc32c"]
    assert all(r.raw_size <= want["record_limit"] for p in c.parts for r in p.physical)
