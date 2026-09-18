"""
OPL4 PCM/Wavetable Synthesis Decoder

Decodes OPL4 Wave registers (PCM synthesis) into human-readable structures.
Based on Yamaha YMF278B datasheet and MoonSound documentation.

The PCM/Wave part provides:
- 24 channels of wavetable synthesis
- 12-bit or 16-bit samples
- 512KB sample ROM (YRW-801) + up to 4MB RAM
- Envelope generator (AR, D1R, DL, D2R, RC, RR)
- LFO (vibrato and tremolo)
- Pseudo-reverb effect
- Per-channel stereo panning

Wave registers (selected via port 0x7E, data via 0x7F):
  0x00-0x01  - Wave table header (unused in most contexts)
  0x02       - Memory access mode
  0x03-0x05  - Memory address (high, mid, low)
  0x06       - Memory data
  0x08-0x1F  - Wave number (per channel, 24 channels)
  0x20-0x37  - F-Number low 7 bits (per channel)
  0x38-0x4F  - Octave / F-Number high 3 bits / Pseudo-reverb (per channel)
  0x50-0x67  - Total Level / Level Direct (per channel)
  0x68-0x7F  - Key / Damp / LFO / Vibrato / AM / Pan (per channel)
  0x80-0x97  - Attack Rate / Decay 1 Rate (per channel)
  0x98-0xAF  - Decay Level / Decay 2 Rate (per channel)
  0xB0-0xC7  - Rate Correction / Release Rate (per channel)
  0xF8       - FM Mix control
  0xF9       - PCM Mix control
"""

from dataclasses import dataclass, field
from enum import IntEnum
from typing import Dict, Any, List, Optional, Tuple


class PanPosition(IntEnum):
    """PCM panning positions (4-bit field)"""
    CENTER = 0       # Equal both
    # Values 1-7: progressively more left
    # Values 8-15: progressively more right
    FULL_LEFT = 7
    FULL_RIGHT = 15


class LFOSpeed(IntEnum):
    """LFO frequency settings"""
    OFF = 0
    FREQ_0_168HZ = 1   # 0.168 Hz
    FREQ_2_019HZ = 2   # 2.019 Hz
    FREQ_3_196HZ = 3   # 3.196 Hz
    FREQ_4_206HZ = 4   # 4.206 Hz
    FREQ_5_215HZ = 5   # 5.215 Hz
    FREQ_5_888HZ = 6   # 5.888 Hz
    FREQ_6_224HZ = 7   # 6.224 Hz


LFO_FREQUENCY_HZ = [0, 0.168, 2.019, 3.196, 4.206, 5.215, 5.888, 6.224]


@dataclass
class PCMEnvelope:
    """PCM channel envelope generator state"""

    # Register 0x80+ch: AR/D1R
    attack_rate: int = 0            # AR - Attack rate (0-15)
    decay1_rate: int = 0            # D1R - Decay 1 rate (0-15)

    # Register 0x98+ch: DL/D2R
    decay_level: int = 0            # DL - Decay level (0-15)
    decay2_rate: int = 0            # D2R - Decay 2 rate (0-15)

    # Register 0xB0+ch: RC/RR
    rate_correction: int = 0        # RC - Rate correction (0-15)
    release_rate: int = 0           # RR - Release rate (0-15)

    def decode_reg80(self, value: int) -> None:
        """Decode register 0x80+ch"""
        self.attack_rate = (value >> 4) & 0x0F
        self.decay1_rate = value & 0x0F

    def decode_reg98(self, value: int) -> None:
        """Decode register 0x98+ch"""
        self.decay_level = (value >> 4) & 0x0F
        self.decay2_rate = value & 0x0F

    def decode_regB0(self, value: int) -> None:
        """Decode register 0xB0+ch"""
        self.rate_correction = (value >> 4) & 0x0F
        self.release_rate = value & 0x0F

    @property
    def decay_level_db(self) -> float:
        """Decay level in dB (3dB per step)"""
        return self.decay_level * 3.0

    def to_dict(self) -> Dict[str, Any]:
        return {
            'attack_rate': self.attack_rate,
            'decay1_rate': self.decay1_rate,
            'decay_level': self.decay_level,
            'decay_level_db': self.decay_level_db,
            'decay2_rate': self.decay2_rate,
            'rate_correction': self.rate_correction,
            'release_rate': self.release_rate,
        }


@dataclass
class PCMLFO:
    """PCM channel LFO state"""

    lfo_speed: int = 0              # LFO frequency (0-7)
    vibrato_depth: int = 0          # Vibrato depth (0-7)
    tremolo_depth: int = 0          # AM/Tremolo depth (0-7)

    @property
    def lfo_frequency_hz(self) -> float:
        """LFO frequency in Hz"""
        return LFO_FREQUENCY_HZ[self.lfo_speed & 7]

    @property
    def vibrato_cents(self) -> float:
        """Vibrato depth in cents (approximate)"""
        # Depth is exponential: 0=off, 1=3.4c, 2=6.7c, 3=13.5c, 4=26.8c, 5=53.7c, 6=107c, 7=214c
        if self.vibrato_depth == 0:
            return 0
        return 3.4 * (2 ** (self.vibrato_depth - 1))

    @property
    def tremolo_db(self) -> float:
        """Tremolo depth in dB"""
        # Depth: 0=off, 1-7 = increasing depth
        if self.tremolo_depth == 0:
            return 0
        return self.tremolo_depth * 1.5  # Approximate

    def to_dict(self) -> Dict[str, Any]:
        return {
            'lfo_speed': self.lfo_speed,
            'lfo_frequency_hz': self.lfo_frequency_hz,
            'vibrato_depth': self.vibrato_depth,
            'vibrato_cents': self.vibrato_cents,
            'tremolo_depth': self.tremolo_depth,
            'tremolo_db': self.tremolo_db,
        }


@dataclass
class PCMChannel:
    """Decoded state of one PCM/Wave channel"""

    channel: int = 0                # Channel number (0-23)

    # Register 0x08+ch: Wave number
    wave_number: int = 0            # Wave/sample number (0-511 for ROM, higher for RAM)

    # Register 0x20+ch: F-Number low
    # Register 0x38+ch: Octave/F-Number high/Pseudo-reverb
    f_number: int = 0               # F-Number (0-1023)
    octave: int = 0                 # Octave (-8 to +7, signed)
    pseudo_reverb: bool = False     # Pseudo-reverb enable

    # Register 0x50+ch: Total Level / Level Direct
    total_level: int = 0            # TL - Volume (0-127, 127=silent)
    level_direct: bool = False      # LD - Level direct mode

    # Register 0x68+ch: Key/Damp/LFO/VIB/AM/Pan
    key_on: bool = False            # Key-on
    damp: bool = False              # Damp (fast release)
    pan: int = 0                    # Pan position (0-15)

    # Envelope
    envelope: PCMEnvelope = field(default_factory=PCMEnvelope)

    # LFO
    lfo: PCMLFO = field(default_factory=PCMLFO)

    # Sample info (from wave header)
    sample_format: int = 0          # 0=8-bit, 1=12-bit, 2=16-bit
    loop_enabled: bool = False
    sample_start: int = 0
    loop_start: int = 0
    loop_end: int = 0

    @property
    def octave_signed(self) -> int:
        """Octave as signed value (-8 to +7)"""
        if self.octave >= 8:
            return self.octave - 16
        return self.octave

    @property
    def note_frequency(self) -> float:
        """Calculate actual frequency in Hz"""
        if self.f_number == 0:
            return 0.0
        # f = fnumber * 2^(octave) * sample_rate / 2^10
        # Assuming 44100 Hz base
        oct = self.octave_signed
        return self.f_number * (2 ** oct) * 44100 / 1024

    @property
    def volume_db(self) -> float:
        """Total level as attenuation in dB (0.375 dB per step)"""
        return self.total_level * 0.375

    @property
    def pan_position(self) -> str:
        """Human-readable pan position"""
        if self.pan == 0:
            return "Center"
        elif self.pan <= 7:
            return f"Left {(8 - self.pan) * 12.5:.1f}%"
        else:
            return f"Right {(self.pan - 7) * 12.5:.1f}%"

    @property
    def pan_left_level(self) -> float:
        """Left channel level (0.0-1.0)"""
        if self.pan == 0:
            return 1.0
        elif self.pan <= 7:
            return 1.0
        else:
            return 1.0 - (self.pan - 7) / 8.0

    @property
    def pan_right_level(self) -> float:
        """Right channel level (0.0-1.0)"""
        if self.pan == 0:
            return 1.0
        elif self.pan <= 7:
            return (8 - self.pan) / 8.0
        else:
            return 1.0

    @property
    def sample_format_name(self) -> str:
        """Human-readable sample format"""
        formats = ["8-bit", "12-bit", "16-bit", "Reserved"]
        return formats[self.sample_format & 3]

    def decode_reg08(self, value: int) -> None:
        """Decode register 0x08+ch (Wave number low)"""
        self.wave_number = (self.wave_number & 0x100) | value

    def decode_reg20(self, value: int) -> None:
        """Decode register 0x20+ch (F-Number low)"""
        self.f_number = (self.f_number & 0x380) | (value & 0x7F)

    def decode_reg38(self, value: int) -> None:
        """Decode register 0x38+ch (Octave/F-Num high/Reverb)"""
        self.pseudo_reverb = bool(value & 0x80)
        self.octave = (value >> 4) & 0x0F
        self.f_number = (self.f_number & 0x07F) | ((value & 0x07) << 7)

    def decode_reg50(self, value: int) -> None:
        """Decode register 0x50+ch (Total Level/LD)"""
        self.level_direct = bool(value & 0x80)
        self.total_level = value & 0x7F

    def decode_reg68(self, value: int) -> None:
        """Decode register 0x68+ch (Key/Damp/LFO/Pan)"""
        self.key_on = bool(value & 0x80)
        self.damp = bool(value & 0x40)
        self.lfo.lfo_speed = (value >> 3) & 0x07
        self.pan = value & 0x0F

    def decode_wave_header(self, header: bytes) -> None:
        """Decode wave table header (12 bytes)"""
        if len(header) < 12:
            return
        # Byte 0: format/loop
        self.sample_format = (header[0] >> 6) & 0x03
        self.loop_enabled = bool(header[0] & 0x20)
        # Bytes 1-3: sample start address
        self.sample_start = (header[1] << 16) | (header[2] << 8) | header[3]
        # Bytes 4-5: loop start offset
        self.loop_start = (header[4] << 8) | header[5]
        # Bytes 6-7: loop end offset
        self.loop_end = (header[6] << 8) | header[7]
        # Bytes 8-9: LFO/vibrato/AM
        self.lfo.vibrato_depth = (header[8] >> 5) & 0x07
        self.lfo.tremolo_depth = (header[8] >> 2) & 0x07

    def to_dict(self) -> Dict[str, Any]:
        return {
            'channel': self.channel,
            'wave_number': self.wave_number,
            'key_on': self.key_on,
            'damp': self.damp,
            'frequency': {
                'f_number': self.f_number,
                'octave': self.octave,
                'octave_signed': self.octave_signed,
                'hz': round(self.note_frequency, 2),
            },
            'level': {
                'total_level': self.total_level,
                'attenuation_db': self.volume_db,
                'level_direct': self.level_direct,
            },
            'pan': {
                'value': self.pan,
                'position': self.pan_position,
                'left_level': round(self.pan_left_level, 3),
                'right_level': round(self.pan_right_level, 3),
            },
            'effects': {
                'pseudo_reverb': self.pseudo_reverb,
            },
            'envelope': self.envelope.to_dict(),
            'lfo': self.lfo.to_dict(),
            'sample': {
                'format': self.sample_format_name,
                'loop_enabled': self.loop_enabled,
                'start_address': f'0x{self.sample_start:06X}',
                'loop_start': self.loop_start,
                'loop_end': self.loop_end,
            },
        }


@dataclass
class PCMGlobalState:
    """Global OPL4 PCM state"""

    # Memory access
    memory_mode: int = 0            # Memory access mode
    memory_address: int = 0         # Current memory address (22 bits)

    # Mix levels
    fm_mix_level: int = 0           # FM mix control (register 0xF8)
    pcm_mix_level: int = 0          # PCM mix control (register 0xF9)

    def decode_reg02(self, value: int) -> None:
        """Decode register 0x02 (Memory mode)"""
        self.memory_mode = value

    def decode_reg03(self, value: int) -> None:
        """Decode register 0x03 (Memory address high)"""
        self.memory_address = (self.memory_address & 0x00FFFF) | ((value & 0x3F) << 16)

    def decode_reg04(self, value: int) -> None:
        """Decode register 0x04 (Memory address mid)"""
        self.memory_address = (self.memory_address & 0x3F00FF) | (value << 8)

    def decode_reg05(self, value: int) -> None:
        """Decode register 0x05 (Memory address low)"""
        self.memory_address = (self.memory_address & 0x3FFF00) | value

    def decode_regF8(self, value: int) -> None:
        """Decode register 0xF8 (FM Mix)"""
        self.fm_mix_level = value

    def decode_regF9(self, value: int) -> None:
        """Decode register 0xF9 (PCM Mix)"""
        self.pcm_mix_level = value

    def to_dict(self) -> Dict[str, Any]:
        return {
            'memory': {
                'mode': self.memory_mode,
                'address': f'0x{self.memory_address:06X}',
            },
            'mix': {
                'fm_level': self.fm_mix_level,
                'pcm_level': self.pcm_mix_level,
            },
        }


class OPL4PCMDecoder:
    """
    Full OPL4 PCM/Wave register decoder.

    Maintains state for all 24 PCM channels plus global registers.
    """

    PCM_CHANNELS = 24

    def __init__(self):
        self.global_state = PCMGlobalState()
        self.channels: List[PCMChannel] = [
            PCMChannel(channel=i) for i in range(self.PCM_CHANNELS)
        ]

    def reset(self) -> None:
        """Reset all state to defaults"""
        self.global_state = PCMGlobalState()
        for ch in self.channels:
            ch.__init__(channel=ch.channel)

    def write_register(self, register: int, value: int) -> None:
        """Process a wave register write"""
        if register == 0x02:
            self.global_state.decode_reg02(value)
        elif register == 0x03:
            self.global_state.decode_reg03(value)
        elif register == 0x04:
            self.global_state.decode_reg04(value)
        elif register == 0x05:
            self.global_state.decode_reg05(value)
        elif register == 0xF8:
            self.global_state.decode_regF8(value)
        elif register == 0xF9:
            self.global_state.decode_regF9(value)
        elif 0x08 <= register <= 0x1F:
            ch = register - 0x08
            if ch < self.PCM_CHANNELS:
                self.channels[ch].decode_reg08(value)
        elif 0x20 <= register <= 0x37:
            ch = register - 0x20
            if ch < self.PCM_CHANNELS:
                self.channels[ch].decode_reg20(value)
        elif 0x38 <= register <= 0x4F:
            ch = register - 0x38
            if ch < self.PCM_CHANNELS:
                self.channels[ch].decode_reg38(value)
        elif 0x50 <= register <= 0x67:
            ch = register - 0x50
            if ch < self.PCM_CHANNELS:
                self.channels[ch].decode_reg50(value)
        elif 0x68 <= register <= 0x7F:
            ch = register - 0x68
            if ch < self.PCM_CHANNELS:
                self.channels[ch].decode_reg68(value)
        elif 0x80 <= register <= 0x97:
            ch = register - 0x80
            if ch < self.PCM_CHANNELS:
                self.channels[ch].envelope.decode_reg80(value)
        elif 0x98 <= register <= 0xAF:
            ch = register - 0x98
            if ch < self.PCM_CHANNELS:
                self.channels[ch].envelope.decode_reg98(value)
        elif 0xB0 <= register <= 0xC7:
            ch = register - 0xB0
            if ch < self.PCM_CHANNELS:
                self.channels[ch].envelope.decode_regB0(value)

    def load_wave_headers(self, wave_rom: bytes, wave_table_base: int = 0) -> None:
        """Load wave headers from sample ROM/RAM"""
        for ch in self.channels:
            if ch.wave_number > 0:
                header_offset = wave_table_base + (ch.wave_number * 12)
                if header_offset + 12 <= len(wave_rom):
                    ch.decode_wave_header(wave_rom[header_offset:header_offset + 12])

    def to_dict(self) -> Dict[str, Any]:
        """Export full state to dictionary"""
        return {
            'global': self.global_state.to_dict(),
            'channels': [ch.to_dict() for ch in self.channels],
        }


# MoonBlaster MFM pattern event decoder
class MFMEventDecoder:
    """
    Decode MoonBlaster MFM pattern events.

    Event byte values (from MSX Wiki and player analysis):
      0x00       - No event
      0x01-0x60  - Note on (C-0 to B-7, 96 notes)
      0x61       - Note off
      0x62-0x79  - Instrument 0-23
      0x7A-0xB9  - Volume (64 levels)
      0xBA-0xBC  - Stereo pan (3 positions)
      0xBD-0xCF  - Link/portamento (19 values)
      0xD0-0xE2  - Pitch bend (19 values)
      0xE3-0xEF  - Vibrato (13 values)
      0xF0-0xF6  - Detune (7 values)
      0xF7-0xF9  - Modulation (3 values)
      0xFA-0xFC  - Damp (3 values)
      0xFD       - Tempo change marker
      0xFE       - Position jump marker
      0xFF       - Empty row marker
    """

    NOTE_NAMES = ['C-', 'C#', 'D-', 'D#', 'E-', 'F-', 'F#', 'G-', 'G#', 'A-', 'A#', 'B-']

    @classmethod
    def decode(cls, value: int, channel: int, is_wave: bool = False) -> Dict[str, Any]:
        """Decode a single MFM event byte"""
        result: Dict[str, Any] = {
            'channel': channel,
            'raw': value,
            'raw_hex': f'0x{value:02X}',
        }

        if value == 0x00:
            result['type'] = 'none'
        elif value <= 0x60:  # 1-96: Note on
            note_num = value - 1
            octave = note_num // 12
            note = note_num % 12
            result['type'] = 'note_on'
            result['note'] = f'{cls.NOTE_NAMES[note]}{octave}'
            result['midi_note'] = note_num + 24  # C-0 = MIDI 24
            result['octave'] = octave
            result['semitone'] = note
        elif value == 0x61:  # Note off
            result['type'] = 'note_off'
        elif value <= 0x79:  # 0x62-0x79: Instrument
            result['type'] = 'instrument'
            result['instrument'] = value - 0x62
        elif value <= 0xB9:  # 0x7A-0xB9: Volume
            result['type'] = 'volume'
            result['volume'] = value - 0x7A  # 0-63
            result['volume_percent'] = round((value - 0x7A) / 63 * 100, 1)
        elif value <= 0xBC:  # 0xBA-0xBC: Stereo
            result['type'] = 'stereo'
            pan_value = value - 0xBA
            result['pan'] = pan_value
            result['pan_name'] = ['Left', 'Center', 'Right'][pan_value]
        elif value <= 0xCF:  # 0xBD-0xCF: Link/portamento
            result['type'] = 'link'
            result['link_value'] = value - 0xBD
        elif value <= 0xE2:  # 0xD0-0xE2: Pitch bend
            result['type'] = 'pitch_bend'
            pb = value - 0xD9  # Center at 0xD9
            result['pitch_bend'] = pb
            result['pitch_direction'] = 'up' if pb > 0 else ('down' if pb < 0 else 'center')
        elif value <= 0xEF:  # 0xE3-0xEF: Vibrato
            result['type'] = 'vibrato'
            result['vibrato_depth'] = value - 0xE3
        elif value <= 0xF6:  # 0xF0-0xF6: Detune
            result['type'] = 'detune'
            detune = value - 0xF3  # Center at 0xF3
            result['detune'] = detune
            result['detune_direction'] = 'sharp' if detune > 0 else ('flat' if detune < 0 else 'center')
        elif value <= 0xF9:  # 0xF7-0xF9: Modulation
            result['type'] = 'modulation'
            result['modulation'] = value - 0xF7
        elif value <= 0xFC:  # 0xFA-0xFC: Damp
            result['type'] = 'damp'
            result['damp'] = value - 0xFA
        elif value == 0xFD:
            result['type'] = 'tempo_change'
        elif value == 0xFE:
            result['type'] = 'position_jump'
        elif value == 0xFF:
            result['type'] = 'empty_row'
        else:
            result['type'] = 'unknown'

        return result
