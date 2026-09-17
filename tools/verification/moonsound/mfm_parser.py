"""
MFM/MWM MoonBlaster Music File Parser

Parses MoonBlaster FM (.MFM) music files for MSX MoonSound (OPL4/YMF278B).

The layout below was reverse-engineered from the reference player shipped
with "MFM Music sample 2" (moonsound.bin + mfm_player.asm) and validated
against 14 sample songs plus a runtime OPL4 register capture of
CRYOGENT.MFM. Full derivation:
docs/file-formats/music/mfm-moonblaster.md

    file offset  size    contents
    ----------------------------------------------------------------------
    0x000        4       "MBMS" signature
    0x004        2       version bytes (0x10 0x01 in all observed files)
    0x006        718     track-info block, copied verbatim into the player
                         workspace (field map below)
    0x2D4        94      trailer: 50-byte metadata string, 36 config
                         bytes, 8-byte sample-kit name
    0x332        len+1   position table (len = song length; each entry is
                         a pattern index)
    ...          2*P     pattern pointer table, P = max(positions)+1;
                         little-endian, bits 15:14 = RAM bank,
                         bits 13:0 = offset relative to the track block
    ...          ...     pattern data (16 rows per pattern)

The track-info block starts at file offset 6: the reference loader skips
the 6-byte file header when it relocates the song to its work image at
0xC000, so block offset X lives at file offset X+6.

    block offset  file offset  contents
    ----------------------------------------------------------------------
    +0x000        0x006       song length (positions)
    +0x001        0x007       loop position (255 = no loop)
    +0x002        0x008       24 FM instruments, 23 bytes each
    +0x22A        0x230       tempo
    +0x22B        0x231       base frequency (hertz equalizer)
    +0x22C..      0x232..     per-channel configuration
    +0x245        0x24B       number of 4-op FM chains (0..6); the player
                              writes OPL4 bank-2 register 0x104 with the
                              connection mask {0,1,3,7,0F,1F,3F}[value]
                              and derives the 2-op voice count 18-2*value
    +0x246..      0x24C..     remaining channel configuration

FM instruments are 23 bytes: two 11-byte operator-pair patches plus one
extra byte. Each 11-byte patch stores interleaved modulator/carrier
fields (am-vib-eg-ksr-mult, total level, attack/decay, sustain/release,
waveform for both operators) followed by a feedback/connection byte. The
second patch holds the remaining operator pair of a 4-op instrument.

Patterns hold 16 rows. A row is either a single 0xFF byte (no events on
any of the 24 channels) or a packed record: one event byte for channel 0,
three bit-mask bytes covering channels 1..24 (MSB first, eight channels
per byte), then one event byte per set bit. Event values: 0 = nothing,
1..96 = note (player-verified), 97 = note off, 98..121 = instrument
0..23, 122..185 = volume; higher ranges are effects (MSX wiki values,
not re-verified in the player).

.MWM (MoonBlaster Wave) files share this container; wave-channel data is
not decoded by this parser yet.
"""

import argparse
import json
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import List, Optional, Dict, Any

MAGIC_MBMS = b'MBMS'

FILE_HEADER_SIZE = 6
TRACK_BLOCK_SIZE = 718
TRAILER_SIZE = 94
METADATA_SIZE = 50
KIT_NAME_SIZE = 8

FM_INSTRUMENT_COUNT = 24
FM_INSTRUMENT_SIZE = 23
FM_PATCH_SIZE = 11

PATTERN_ROWS = 16
# Channel 0 plus 24 mask-covered channels; the reference player clears a
# 25-byte step buffer (1 + ldir 24) per tick.
PATTERN_CHANNELS = 25
EMPTY_ROW = 0xFF

OPL4_FM_CHANNELS = 18

# Offsets inside the track-info block (file offset = block offset + 6).
BLOCK_SONG_LENGTH = 0x000
BLOCK_LOOP_POSITION = 0x001
BLOCK_INSTRUMENTS = 0x002
BLOCK_TEMPO = 0x22A
BLOCK_BASE_FREQUENCY = 0x22B
BLOCK_FOUR_OP_CHAINS = 0x245

# Connection masks the reference player writes to OPL4 bank-2 register
# 0x104, indexed by the number of 4-op chains (MBPlayer_init_opl4).
FOUR_OP_CONNECTION_MASKS = [0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F]

NOTE_NAMES = ['C-', 'C#', 'D-', 'D#', 'E-', 'F-', 'F#', 'G-', 'G#', 'A-', 'A#', 'B-']


@dataclass
class FMPatch:
    """One operator pair (2-op patch) of an FM instrument.

    The ksl_tl bytes are raw OPL4 register 0x40 values (KSL<<6 | TL);
    total_level/ksl expose the decoded halves.
    """
    mod_am_vib_eg_ksr_mult: int = 0
    car_am_vib_eg_ksr_mult: int = 0
    mod_ksl_tl: int = 0
    car_ksl_tl: int = 0
    mod_attack_decay: int = 0
    car_attack_decay: int = 0
    mod_sustain_release: int = 0
    car_sustain_release: int = 0
    mod_waveform: int = 0
    car_waveform: int = 0
    feedback_connection: int = 0

    @property
    def mod_total_level(self) -> int:
        return self.mod_ksl_tl & 0x3F

    @property
    def car_total_level(self) -> int:
        return self.car_ksl_tl & 0x3F

    def to_dict(self) -> Dict[str, Any]:
        result = asdict(self)
        result['mod_total_level'] = self.mod_total_level
        result['car_total_level'] = self.car_total_level
        return result


@dataclass
class FMInstrument:
    """OPL4 FM instrument: two operator-pair patches plus one extra byte."""
    index: int
    primary: FMPatch = field(default_factory=FMPatch)    # operators 1+2
    secondary: FMPatch = field(default_factory=FMPatch)  # operators 3+4 (4-op)
    extra: int = 0

    def is_empty(self) -> bool:
        return (self.primary.mod_ksl_tl == 0 and self.primary.car_ksl_tl == 0
                and self.primary.mod_am_vib_eg_ksr_mult == 0
                and self.primary.car_am_vib_eg_ksr_mult == 0
                and self.secondary.mod_ksl_tl == 0 and self.secondary.car_ksl_tl == 0
                and self.secondary.mod_am_vib_eg_ksr_mult == 0
                and self.secondary.car_am_vib_eg_ksr_mult == 0)

    def to_dict(self) -> Dict[str, Any]:
        return {
            'index': self.index,
            'primary': self.primary.to_dict(),
            'secondary': self.secondary.to_dict(),
            'extra': self.extra,
        }


@dataclass
class PatternEvent:
    """Single event decoded from a pattern row."""
    channel: int
    raw_value: int
    event_type: str = ''
    note: Optional[str] = None
    instrument: Optional[int] = None
    volume: Optional[int] = None
    effect: str = ''
    effect_param: Optional[int] = None

    def to_dict(self) -> Dict[str, Any]:
        result: Dict[str, Any] = {
            'channel': self.channel,
            'raw': self.raw_value,
            'type': self.event_type,
        }
        if self.note:
            result['note'] = self.note
        if self.instrument is not None:
            result['instrument'] = self.instrument
        if self.volume is not None:
            result['volume'] = self.volume
        if self.effect:
            result['effect'] = self.effect
        if self.effect_param is not None:
            result['effect_param'] = self.effect_param
        return result


@dataclass
class Pattern:
    """A pattern: 16 rows of up to 24 channel events."""
    index: int
    file_offset: int
    bank: int
    size: int = 0
    rows: List[List[Optional[PatternEvent]]] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return {
            'index': self.index,
            'file_offset': f'0x{self.file_offset:04X}',
            'bank': self.bank,
            'size': self.size,
            'rows': [[None if e is None else e.to_dict() for e in row]
                     for row in self.rows],
        }


@dataclass
class MFMFile:
    """Complete MFM file structure."""
    filename: str = ''
    warnings: List[str] = field(default_factory=list)

    # Header
    magic: str = ''
    version_major: int = 0
    version_minor: int = 0
    song_length: int = 0
    loop_position: int = 0

    # Trailer
    metadata: str = ''
    sample_kit: str = ''

    # Configuration (track-info block)
    tempo: int = 0
    base_frequency: int = 0
    four_op_chains: int = 0
    two_op_voices: int = 0
    connection_mask: int = 0

    # Instruments
    fm_instruments: List[FMInstrument] = field(default_factory=list)
    used_instruments: List[int] = field(default_factory=list)

    # Song structure
    position_table: List[int] = field(default_factory=list)
    pattern_count: int = 0
    pattern_pointers: List[int] = field(default_factory=list)
    patterns: List[Pattern] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return {
            'file': {
                'name': self.filename,
                'magic': self.magic,
                'version': f'{self.version_major}.{self.version_minor:02X}',
            },
            'metadata': {
                'text': self.metadata,
                'sample_kit': self.sample_kit,
            },
            'song': {
                'length': self.song_length,
                'loop_position': None if self.loop_position == 255 else self.loop_position,
                'tempo': self.tempo,
                'base_frequency': self.base_frequency,
            },
            'channels': {
                'fm_total': OPL4_FM_CHANNELS,
                'four_op_chains': self.four_op_chains,
                'two_op_voices': self.two_op_voices,
                'connection_mask_0x104': f'0x{self.connection_mask:02X}',
            },
            'instruments': {
                'fm': [i.to_dict() for i in self.fm_instruments if not i.is_empty()],
                'used': self.used_instruments,
            },
            'position_table': self.position_table,
            'pattern_count': self.pattern_count,
            'patterns': [p.to_dict() for p in self.patterns],
            'warnings': self.warnings,
        }


def decode_event(value: int, channel: int) -> PatternEvent:
    """Decode a channel event byte.

    Note values 1..96 are verified against mfm_player.asm; everything above
    97 follows the MSX wiki MoonBlaster command table (see
    docs/file-formats/music/mfm-moonblaster.md) and is not re-verified in
    the player disassembly.
    """
    event = PatternEvent(channel=channel, raw_value=value)

    if value == 0:
        event.event_type = 'none'
    elif value <= 96:
        note_num = value - 1
        event.event_type = 'note'
        event.note = f'{NOTE_NAMES[note_num % 12]}{note_num // 12}'
    elif value == 97:
        event.event_type = 'note_off'
    elif value <= 121:
        event.event_type = 'instrument'
        event.instrument = value - 98
    elif value <= 185:
        event.event_type = 'volume'
        event.volume = value - 122
    elif value <= 188:
        event.event_type = 'stereo'
        event.effect_param = value - 186
    elif value <= 191:
        event.event_type = 'unknown_0xBD_0xBF'
        event.effect_param = value
    elif value <= 207:
        event.event_type = 'pitch_bend'
        event.effect_param = value - 192
    elif value <= 226:
        event.event_type = 'vibrato'
        event.effect_param = value - 208
    elif value <= 239:
        event.event_type = 'detune'
        event.effect_param = value - 227
    elif value <= 246:
        event.event_type = 'special'
        event.effect_param = value - 240
    elif value <= 249:
        event.event_type = 'portamento'
        event.effect_param = value - 247
    else:
        event.event_type = 'speed'
        event.effect_param = value - 250

    return event


def parse_instrument(index: int, record: bytes) -> FMInstrument:
    """Decode one 23-byte FM instrument record."""
    def patch(raw: bytes) -> FMPatch:
        return FMPatch(
            mod_am_vib_eg_ksr_mult=raw[0],
            car_am_vib_eg_ksr_mult=raw[1],
            mod_ksl_tl=raw[2],
            car_ksl_tl=raw[3],
            mod_attack_decay=raw[4],
            car_attack_decay=raw[5],
            mod_sustain_release=raw[6],
            car_sustain_release=raw[7],
            mod_waveform=raw[8],
            car_waveform=raw[9],
            feedback_connection=raw[10],
        )

    return FMInstrument(
        index=index,
        primary=patch(record[0:FM_PATCH_SIZE]),
        secondary=patch(record[FM_PATCH_SIZE:2 * FM_PATCH_SIZE]),
        extra=record[2 * FM_PATCH_SIZE],
    )


def parse_rows(buf: bytes) -> List[List[Optional[PatternEvent]]]:
    """Decode up to 16 packed rows from one pattern's byte range."""
    rows: List[List[Optional[PatternEvent]]] = []
    pos = 0
    for _ in range(PATTERN_ROWS):
        row: List[Optional[PatternEvent]] = [None] * PATTERN_CHANNELS
        if pos >= len(buf):
            rows.append(row)
            continue
        first = buf[pos]
        pos += 1
        if first == EMPTY_ROW:
            rows.append(row)
            continue
        row[0] = decode_event(first, 0)
        if pos + 3 > len(buf):
            rows.append(row)
            break
        masks = buf[pos:pos + 3]
        pos += 3
        for group in range(3):
            for bit in range(8):
                channel = 1 + group * 8 + bit
                if masks[group] & (0x80 >> bit):
                    if pos >= len(buf):
                        break
                    row[channel] = decode_event(buf[pos], channel)
                    pos += 1
        rows.append(row)
    return rows


def parse_mfm(filepath: Path) -> MFMFile:
    """Parse an MFM/MWM file."""
    mfm = MFMFile(filename=filepath.name)
    data = filepath.read_bytes()

    if len(data) < FILE_HEADER_SIZE + TRACK_BLOCK_SIZE + TRAILER_SIZE:
        raise ValueError(f'File too small: {len(data)} bytes')

    if data[0:4] != MAGIC_MBMS:
        raise ValueError(f'Invalid magic: {data[0:4]!r}, expected {MAGIC_MBMS!r}')
    mfm.magic = MAGIC_MBMS.decode('ascii')
    mfm.version_major = data[4]
    mfm.version_minor = data[5]

    block = data[FILE_HEADER_SIZE:FILE_HEADER_SIZE + TRACK_BLOCK_SIZE]
    trailer = data[FILE_HEADER_SIZE + TRACK_BLOCK_SIZE:
                   FILE_HEADER_SIZE + TRACK_BLOCK_SIZE + TRAILER_SIZE]

    mfm.song_length = block[BLOCK_SONG_LENGTH]
    mfm.loop_position = block[BLOCK_LOOP_POSITION]
    mfm.tempo = block[BLOCK_TEMPO]
    mfm.base_frequency = block[BLOCK_BASE_FREQUENCY]
    mfm.four_op_chains = block[BLOCK_FOUR_OP_CHAINS]
    if mfm.four_op_chains > 6:
        mfm.warnings.append(
            f'four-op chain count {mfm.four_op_chains} out of range 0..6')
        mfm.four_op_chains = 6
    mfm.two_op_voices = OPL4_FM_CHANNELS - 2 * mfm.four_op_chains
    mfm.connection_mask = FOUR_OP_CONNECTION_MASKS[mfm.four_op_chains]

    for i in range(FM_INSTRUMENT_COUNT):
        start = BLOCK_INSTRUMENTS + i * FM_INSTRUMENT_SIZE
        mfm.fm_instruments.append(
            parse_instrument(i, block[start:start + FM_INSTRUMENT_SIZE]))

    mfm.metadata = trailer[:METADATA_SIZE].decode('ascii', errors='replace').rstrip()
    mfm.sample_kit = (trailer[TRAILER_SIZE - KIT_NAME_SIZE:]
                      .decode('ascii', errors='replace').rstrip())

    pos_base = FILE_HEADER_SIZE + TRACK_BLOCK_SIZE + TRAILER_SIZE
    position_table = list(data[pos_base:pos_base + mfm.song_length + 1])
    if len(position_table) < mfm.song_length + 1:
        raise ValueError(f'File truncated in position table: {len(data)} bytes')
    mfm.position_table = position_table
    mfm.pattern_count = max(position_table) + 1

    ptr_base = pos_base + mfm.song_length + 1
    if ptr_base + 2 * mfm.pattern_count > len(data):
        raise ValueError(f'File truncated in pattern pointer table: {len(data)} bytes')
    pointers = [data[ptr_base + 2 * i] | (data[ptr_base + 2 * i + 1] << 8)
                for i in range(mfm.pattern_count)]
    mfm.pattern_pointers = pointers
    offsets = [(p & 0x3FFF) + FILE_HEADER_SIZE for p in pointers]

    # Pattern sizes come from the next pointer in memory order; the player
    # itself only ever enters a pattern through the pointer table.
    bounds = sorted(set(offsets))
    for i, start in enumerate(offsets):
        nxt = [b for b in bounds if b > start]
        end = nxt[0] if nxt else len(data)
        pattern = Pattern(index=i, file_offset=start, bank=pointers[i] >> 14,
                          size=end - start)
        pattern.rows = parse_rows(data[start:end])
        mfm.patterns.append(pattern)

    used = set()
    for pattern in mfm.patterns:
        for row in pattern.rows:
            for event in row:
                if event is not None and event.event_type == 'instrument':
                    used.add(event.instrument)
    mfm.used_instruments = sorted(used)

    if any(p >> 14 for p in pointers):
        mfm.warnings.append(
            'pattern data spans multiple RAM banks; file offsets assume bank 0')
    if offsets != sorted(offsets):
        mfm.warnings.append('pattern pointers are not stored in index order')

    return mfm


def print_summary(mfm: MFMFile) -> None:
    print(f'{mfm.filename}:')
    print(f'  Magic     : {mfm.magic} (version {mfm.version_major}.{mfm.version_minor:02X})')
    print(f'  Metadata  : {mfm.metadata}')
    print(f'  Sample kit: {mfm.sample_kit}')
    loop = 'none' if mfm.loop_position == 255 else mfm.loop_position
    print(f'  Song      : {mfm.song_length + 1} positions (0..{mfm.song_length}), '
          f'loop at {loop}, tempo {mfm.tempo}')
    print(f'  Channels  : {OPL4_FM_CHANNELS} FM = '
          f'{mfm.four_op_chains} four-op chains + {mfm.two_op_voices} two-op voices '
          f'(reg 0x104 <- 0x{mfm.connection_mask:02X}), 0 wave')
    print(f'  Patterns  : {mfm.pattern_count}')
    defined = [i for i in mfm.fm_instruments if not i.is_empty()]
    print(f'  Instruments: {len(defined)} defined, used: {mfm.used_instruments}')
    for warning in mfm.warnings:
        print(f'  WARNING   : {warning}', file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Parse MoonBlaster .MFM/.MWM music files')
    parser.add_argument('files', nargs='+', type=Path,
                        help='.MFM/.MWM files to parse')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='dump full JSON to stdout')
    parser.add_argument('-o', '--output', type=Path,
                        help='write JSON to this file (or directory for batches)')
    args = parser.parse_args()

    status = 0
    for path in args.files:
        try:
            mfm = parse_mfm(path)
        except (OSError, ValueError) as exc:
            print(f'{path.name}: ERROR: {exc}', file=sys.stderr)
            status = 1
            continue
        print_summary(mfm)
        if args.verbose:
            print(json.dumps(mfm.to_dict(), indent=2))
        if args.output:
            target = (args.output / f'{path.stem}.json'
                      if args.output.is_dir() else args.output)
            target.write_text(json.dumps(mfm.to_dict(), indent=2))
            print(f'  JSON      : {target}')
    return status


if __name__ == '__main__':
    sys.exit(main())
