# ttdsession.ksy - the time-travel engine's session file (TTD schema 2).
#
# Written by core/src/debugger/ttd/engine/ttdcontainer.cpp (the container) and
# ttdsessionfile.cpp (the session in it); read by the same files and by the
# analyzer (tools/verification/ttd-analyzer/src/ttdcontainer.py), which is the
# machine-checked reader (tests/test_ttdcontainer.py against files the C++
# writer produced, testdata/ttd/v2/). Design:
# docs/inprogress/2026-09-25-ttd-v2-migration/phase-4-session-file-tdd.md.
#
# Schema 1 (the emulator's files until the engine replaces v1, Phase 5) stays
# described by core/src/debugger/ttd/ttd.ksy; at the switch-over this file
# becomes ttd.ksy and that one ttdschema1.ksy.
#
# Layout:
#   header   magic "TTDD", schema 2, flags, size, CRC, UUID, creation time,
#            zstd version, stream table, the session's tables
#   records  32-byte header + payload each; grouped into parts, every part
#            closed by a part-end record (stream 0)
#   index    part table, stream totals, index extra (finished files only)
#   trailer  24 bytes at the end: index offset, size, CRC, magic "TTDX", CRC
#
# Integrity (owner decision 2026-10-04): CRC32C (Castagnoli) everywhere. The
# header CRC covers the whole header with the CRC field read as zero; a record
# header's CRC its first 28 bytes; a payload's CRC the payload as stored. A
# file without a valid trailer is read by scanning records ("TREC" sync); an
# incomplete last part is dropped; a damaged part makes it and the parts that
# depend on it unreachable.
#
# Payloads flagged compressed are zstd frames (the frame records its content
# size). Kaitai has no zstd process: the stream payload types below describe
# the bytes after decompression (`payload_*`), to be applied by the reader.
#
# No compatibility promise before the release (owner decision 2026-10-03):
# the layout is amended in place, fixtures rewritten.

meta:
  id: ttdsession
  title: Unreal-NG time-travel session file (schema 2)
  file-extension: ttd
  endian: le
  license: GPL-3.0-or-later

seq:
  - id: header
    type: header
  - id: records
    type: record
    repeat: until
    repeat-until: _io.pos >= records_end
    doc: Every record up to the index (or to the end of an unfinished file)

instances:
  trailer:
    pos: _io.size - 24
    type: trailer
    doc: Valid only in a finished file (check magic and CRC)
  records_end:
    value: 'trailer.magic == [0x54, 0x54, 0x44, 0x58] ? trailer.index_offset : _io.size'
  index:
    pos: trailer.index_offset
    size: trailer.index_size
    type: index
    if: trailer.magic == [0x54, 0x54, 0x44, 0x58]

types:
  header:
    seq:
      - id: magic
        contents: 'TTDD'
      - id: schema_version
        type: u2
        valid: 2
      - id: flags
        type: u2
        doc: bit 0 converted from a schema-1 file (D31)
      - id: header_size
        type: u4
      - id: header_crc
        type: u4
      - id: uuid
        size: 16
      - id: created_micros
        type: u8
        doc: microseconds since 1970, UTC
      - id: zstd_version
        type: u4
      - id: reserved
        size: 20
      - id: stream_count
        type: u2
      - id: streams
        type: stream_desc
        repeat: expr
        repeat-expr: stream_count
      - id: session_tables_size
        type: u4
      - id: session_tables
        size: session_tables_size
        type: session_tables

  stream_desc:
    seq:
      - id: id
        type: u2
        doc: |
          0 container (part ends), 1 pieces, 3 checkpoints, 5 events,
          6 configuration, 7 write journal, 13 bus reads, 14 bus writes,
          15 bus vectors, 16 media (sector) reads; 0x0100-0x01FF frame-boundary
          streams; 0x0200-0x02FF reserved (branches); 0x0300-0x03FF reserved (groups)
      - id: layout_version
        type: u2
      - id: kind
        type: u1
        doc: 0 required (a reader that does not know it refuses the file), 1 ancillary (skipped)
      - id: name_size
        type: u1
      - id: name
        type: str
        size: name_size
        encoding: UTF-8

  session_tables:
    seq:
      - id: version
        type: u2
        valid: 1
      - id: snapshot_interval
        type: u4
        doc: a full reference table every this many checkpoints (rebuilt on load, not stored)
      - id: region_count
        type: vlq
      - id: regions
        type: region
        repeat: expr
        repeat-expr: region_count.value
        doc: the memory regions; device-state regions follow from the devices
      - id: device_count
        type: vlq
      - id: devices
        type: device
        repeat: expr
        repeat-expr: device_count.value

  region:
    seq:
      - id: id
        type: u2
      - id: name
        type: str_vlq
      - id: owner_type
        type: u2
      - id: owner_instance
        type: str_vlq
      - id: pieces
        type: u4
        doc: 4 KB pieces
      - id: bytes
        type: u4
      - id: dirty_granularity
        type: u4
      - id: block_pieces
        type: u4

  device:
    seq:
      - id: type
        type: u2
      - id: instance
        type: str_vlq
      - id: legacy_id
        type: u1
        doc: the schema-1 peripheral id (PeripheralId)
      - id: layout_version
        type: u2
      - id: state_size
        type: u4
      - id: variable_size
        type: u1
      - id: firmware_fingerprint
        type: u8
      - id: restore_after_count
        type: vlq
      - id: restore_after
        type: device_key
        repeat: expr
        repeat-expr: restore_after_count.value
      - id: time_field_count
        type: vlq
      - id: time_fields
        type: time_field
        repeat: expr
        repeat-expr: time_field_count.value
      - id: runs_behind_cpu
        type: u1

  device_key:
    seq:
      - id: type
        type: u2
      - id: instance
        type: str_vlq

  time_field:
    seq:
      - id: offset
        type: u2
      - id: width
        type: u1

  record:
    seq:
      - id: sync
        contents: 'TREC'
      - id: stream_id
        type: u2
      - id: flags
        type: u2
        doc: bit 0 payload zstd-compressed, bit 1 part-end record
      - id: part_index
        type: u4
      - id: sequence
        type: u4
      - id: stored_size
        type: u4
      - id: raw_size
        type: u4
      - id: payload_crc
        type: u4
      - id: header_crc
        type: u4
      - id: payload
        size: stored_size
        type:
          switch-on: stream_id
          cases:
            0: part_end

  part_end:
    doc: Stream 0, never compressed
    seq:
      - id: first_frame
        type: u8
      - id: frame_count
        type: u4
      - id: branch
        type: u2
      - id: record_count
        type: u4
      - id: record_offsets
        type: u8
        repeat: expr
        repeat-expr: record_count
      - id: dependency_count
        type: vlq
      - id: dependency_deltas
        type: vlq
        repeat: expr
        repeat-expr: dependency_count.value
        doc: the earlier parts this one needs, ascending, each as the difference from the one before
      - id: extra_size
        type: vlq
      - id: extra
        size: extra_size.value
        doc: byte 0 bit 0 - a file starts here (the loader restarts version numbers and journal positions)

  index:
    seq:
      - id: version
        type: u2
        valid: 1
      - id: part_count
        type: u4
      - id: parts
        type: index_part
        repeat: expr
        repeat-expr: part_count
      - id: stream_count
        type: u2
      - id: streams
        type: index_stream
        repeat: expr
        repeat-expr: stream_count
      - id: extra_size
        type: u4
      - id: extra
        size: extra_size

  index_part:
    seq:
      - id: part_end_offset
        type: u8
      - id: first_frame
        type: u8
      - id: frame_count
        type: u4
      - id: branch
        type: u2

  index_stream:
    seq:
      - id: id
        type: u2
      - id: records
        type: u8
      - id: stored_bytes
        type: u8
      - id: raw_bytes
        type: u8

  trailer:
    seq:
      - id: index_offset
        type: u8
      - id: index_size
        type: u4
      - id: index_crc
        type: u4
      - id: magic
        size: 4
      - id: trailer_crc
        type: u4
        doc: CRC32C of the first 20 bytes

  # --- Stream payloads, after decompression ---------------------------------

  payload_pieces:
    doc: |
      Stream 1: the piece versions this part introduces, in the order its
      checkpoints' changes name them. Versions are numbered from the file's
      start (from each part flagged as a file start in a joined file).
    seq:
      - id: versions
        type: piece_version
        repeat: eos

  piece_version:
    seq:
      - id: encoding
        type: u1
        doc: |
          0 full (a zstd frame of the 4 KB), 1 xor (a zstd frame of the XOR
          with the base), 2 zero (no payload), 3 ranges (the XOR's non-zero
          runs: u2 offset, u1 length, bytes; applied by XOR)
      - id: depth
        type: vlq
      - id: base_plus_one
        type: vlq
        doc: the version a difference applies to (its number + 1; 0 none)
      - id: content_crc
        type: u4
        doc: CRC32C of the decoded 4 KB
      - id: payload_size
        type: vlq
      - id: payload
        size: payload_size.value

  payload_checkpoints:
    doc: Stream 3
    seq:
      - id: count
        type: vlq
      - id: checkpoints
        type: checkpoint
        repeat: expr
        repeat-expr: count.value

  checkpoint:
    seq:
      - id: frame_delta
        type: vlq
        doc: from the previous checkpoint of the part (the first from 0)
      - id: flags
        type: u1
        doc: bit 0 baseline - a segment starts here, every piece stored whole (D41)
      - id: start
        type: u8
        doc: the frame's start in machine time
      - id: cpu
        size: 48
        doc: TTDCpuState (ttdcheckpoint.h)
      - id: chipset
        size: 120
        doc: TTDChipsetState (ttdcheckpoint.h)
      - id: unclaimed_count
        type: vlq
      - id: unclaimed_devices
        size: unclaimed_count.value
      - id: bus_read_cursor
        type: vlq
        doc: positions in the file's journals, from its first record
      - id: bus_write_cursor
        type: vlq
      - id: media_read_cursor
        type: vlq
      - id: bus_vector_cursor
        type: vlq
      - id: region_count
        type: vlq
      - id: regions
        type: region_changes
        repeat: expr
        repeat-expr: region_count.value

  region_changes:
    doc: Each listed piece takes the next version of the part's stream 1
    seq:
      - id: region
        type: vlq
      - id: count
        type: vlq
      - id: pieces
        type: vlq
        repeat: expr
        repeat-expr: count.value

  payload_events:
    doc: Stream 5
    seq:
      - id: count
        type: vlq
      - id: events
        type: event
        repeat: expr
        repeat-expr: count.value

  event:
    seq:
      - id: time_delta
        type: vlq
        doc: machine time from the previous event of the part
      - id: kind
        type: u2
        doc: TTDEventKind (engine/ttdeventlog.h)
      - id: cpu
        type: u2
      - id: args
        size: 16
      - id: payload_size_plus_one
        type: vlq
      - id: payload
        size: 'payload_size_plus_one.value == 0 ? 0 : payload_size_plus_one.value - 1'

  payload_ports:
    doc: |
      Streams 13-15 (IN, OUT, interrupt vectors), as columns. A time is the
      delta from the previous record of the same frame, or the time itself on
      a new frame
    seq:
      - id: count
        type: vlq
      - id: times
        type: port_time
        repeat: expr
        repeat-expr: count.value
      - id: ports
        type: u2
        repeat: expr
        repeat-expr: count.value
      - id: pcs
        type: u2
        repeat: expr
        repeat-expr: count.value
      - id: values
        type: u1
        repeat: expr
        repeat-expr: count.value

  port_time:
    seq:
      - id: frame_delta
        type: vlq
      - id: time
        type: vlq

  payload_media_reads:
    doc: Stream 16
    seq:
      - id: count
        type: vlq
      - id: reads
        type: media_read
        repeat: expr
        repeat-expr: count.value

  media_read:
    seq:
      - id: frame
        type: u8
      - id: t_in_frame
        type: u4
      - id: slot
        type: str_vlq
      - id: lba
        type: u8
      - id: size
        type: vlq
      - id: bytes
        size: size.value

  payload_configuration:
    doc: Stream 6
    seq:
      - id: entry_count
        type: vlq
      - id: entries
        type: config_entry
        repeat: expr
        repeat-expr: entry_count.value
      - id: media_change_count
        type: vlq
      - id: media_changes
        type: media_change
        repeat: expr
        repeat-expr: media_change_count.value

  config_entry:
    seq:
      - id: frame
        type: u8
      - id: field_count
        type: vlq
      - id: fields
        type: config_field
        repeat: expr
        repeat-expr: field_count.value

  config_field:
    seq:
      - id: name
        type: str_vlq
      - id: value
        type: u8
      - id: affects_restore
        type: u1

  media_change:
    seq:
      - id: slot
        type: str_vlq
      - id: format
        type: str_vlq
      - id: has_versions
        type: u1
      - id: checkpoint
        type: vlq
      - id: content_id
        type: u8
      - id: version
        type: u8

  payload_frame_stream:
    doc: |
      Streams 0x0100-0x013F (ancillary, D19): the copies a frame-boundary
      stream took in the part's frames. Each is the XOR with the stream's
      previous copy in the file, zstd-compressed; a full copy at least every 50
      frames, on a size change and at the file's start. Stream 0x0100 is the
      screenshot: u2 width, u2 height, u1 video mode, then the framebuffer (RGBA)
    seq:
      - id: count
        type: vlq
      - id: copies
        type: frame_copy
        repeat: expr
        repeat-expr: count.value

  frame_copy:
    seq:
      - id: frame_delta
        type: vlq
        doc: from the part's first frame
      - id: kind
        type: u1
        doc: 0 full, 1 the XOR with the previous copy
      - id: raw_size
        type: vlq
      - id: packed_size
        type: vlq
      - id: packed
        size: packed_size.value
        doc: a zstd frame of raw_size bytes

  payload_write_journal:
    doc: |
      Stream 7 (ancillary, D40), with the session's last part: the records in
      blocks of 2,048 in v1's column layout (EncodeWriteBlock,
      ttdwritejournal.cpp), then the segments the journal covers
    seq:
      - id: record_count
        type: vlq
      - id: blocks
        type: write_block
        repeat: expr
        repeat-expr: '(record_count.value + 2047) / 2048'
      - id: segment_count
        type: vlq
      - id: segments
        type: write_segment
        repeat: expr
        repeat-expr: segment_count.value

  write_block:
    seq:
      - id: size
        type: vlq
      - id: columns
        size: size.value

  write_segment:
    seq:
      - id: from
        type: u8
      - id: to
        type: u8

  # --- Basics ------------------------------------------------------------------

  vlq:
    doc: LEB128 unsigned (7 bits per byte, low first, bit 7 = more)
    seq:
      - id: groups
        type: u1
        repeat: until
        repeat-until: (_ & 0x80) == 0
    instances:
      value:
        value: >-
          (groups[0] & 0x7f)
          + (groups.size > 1 ? (groups[1] & 0x7f) << 7 : 0)
          + (groups.size > 2 ? (groups[2] & 0x7f) << 14 : 0)
          + (groups.size > 3 ? (groups[3] & 0x7f) << 21 : 0)
          + (groups.size > 4 ? (groups[4] & 0x7f) << 28 : 0)
          + (groups.size > 5 ? (groups[5] & 0x7f) << 35 : 0)
          + (groups.size > 6 ? (groups[6] & 0x7f) << 42 : 0)
          + (groups.size > 7 ? (groups[7] & 0x7f) << 49 : 0)
          + (groups.size > 8 ? (groups[8] & 0x7f) << 56 : 0)

  str_vlq:
    seq:
      - id: size
        type: vlq
      - id: value
        type: str
        size: size.value
        encoding: UTF-8
