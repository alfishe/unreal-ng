"""
OPL4 FM Synthesis Decoder

Decodes OPL4 FM registers (OPL3-compatible) into human-readable structures.
Based on Yamaha YMF278B datasheet and OPL3 (YMF262) documentation.

The FM part of OPL4 is fully OPL3-compatible:
- 18 channels (or 6 four-operator + 6 two-operator)
- 36 operators total
- 8 waveforms per operator
- Rhythm mode (6 percussion sounds)

Register layout (per bank, 0xC4/0xC6 select):
  0x01       - Test / Waveform select enable
  0x02-0x03  - Timer 1/2
  0x04       - Timer control / IRQ
  0x05       - OPL3 mode enable (bank 1 only)
  0x08       - CSW / Note-sel
  0x20-0x35  - AM/VIB/EG-type/KSR/Multiple (per operator)
  0x40-0x55  - KSL/Total Level (per operator)
  0x60-0x75  - Attack Rate / Decay Rate (per operator)
  0x80-0x95  - Sustain Level / Release Rate (per operator)
  0xA0-0xA8  - F-Number low 8 bits (per channel)
  0xB0-0xB8  - Key-on / Block / F-Number high 2 bits (per channel)
  0xBD       - Rhythm mode / AM depth / VIB depth (bank 1 only)
  0xC0-0xC8  - Feedback / Connection / Panning (per channel)
  0xE0-0xF5  - Waveform select (per operator)
  0x104      - 4-op connection enable (bank 2, OPL4-specific)
"""

from dataclasses import dataclass, field
from enum import IntEnum
from typing import Dict, Any, List, Optional


class Waveform(IntEnum):
    """OPL3/OPL4 waveform types"""
    SINE = 0
    HALF_SINE = 1
    ABS_SINE = 2
    PULSE_SINE = 3
    SINE_EVEN = 4
    ABS_SINE_EVEN = 5
    SQUARE = 6
    DERIVED_SQUARE = 7


class PanPosition(IntEnum):
    """Stereo panning positions"""
    CENTER = 0  # Both speakers
    LEFT = 1    # Left only
    RIGHT = 2   # Right only
    MUTE = 3    # Neither (muted)


WAVEFORM_NAMES = [
    "Sine",
    "Half-Sine",
    "Abs-Sine",
    "Pulse-Sine",
    "Sine (even harmonics)",
    "Abs-Sine (even harmonics)",
    "Square",
    "Derived Square"
]

PAN_NAMES = ["Center", "Left", "Right", "Mute"]


@dataclass
class FMOperator:
    """Decoded state of one FM operator (slot)"""

    # Register 0x20+offset: AM/VIB/EG-type/KSR/Multiple
    tremolo: bool = False           # AM - Tremolo (amplitude modulation)
    vibrato: bool = False           # VIB - Vibrato (frequency modulation)
    sustain: bool = False           # EG-type - Sustaining sound (1=sustain, 0=decay)
    ksr: bool = False               # KSR - Key scale rate
    multiple: int = 0               # MULT - Frequency multiplier (0-15)

    # Register 0x40+offset: KSL/TL
    key_scale_level: int = 0        # KSL - Key scale level (0-3)
    total_level: int = 0            # TL - Total level / attenuation (0-63)

    # Register 0x60+offset: AR/DR
    attack_rate: int = 0            # AR - Attack rate (0-15)
    decay_rate: int = 0             # DR - Decay rate (0-15)

    # Register 0x80+offset: SL/RR
    sustain_level: int = 0          # SL - Sustain level (0-15)
    release_rate: int = 0           # RR - Release rate (0-15)

    # Register 0xE0+offset: WS
    waveform: int = 0               # WS - Waveform select (0-7)

    @property
    def waveform_name(self) -> str:
        return WAVEFORM_NAMES[self.waveform & 7]

    @property
    def multiple_ratio(self) -> float:
        """Actual frequency multiplier ratio"""
        mult_table = [0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15]
        return mult_table[self.multiple & 0xF]

    @property
    def attenuation_db(self) -> float:
        """Total level in dB (0.75 dB per step)"""
        return self.total_level * 0.75

    def decode_reg20(self, value: int) -> None:
        """Decode register 0x20+offset"""
        self.tremolo = bool(value & 0x80)
        self.vibrato = bool(value & 0x40)
        self.sustain = bool(value & 0x20)
        self.ksr = bool(value & 0x10)
        self.multiple = value & 0x0F

    def decode_reg40(self, value: int) -> None:
        """Decode register 0x40+offset"""
        self.key_scale_level = (value >> 6) & 0x03
        self.total_level = value & 0x3F

    def decode_reg60(self, value: int) -> None:
        """Decode register 0x60+offset"""
        self.attack_rate = (value >> 4) & 0x0F
        self.decay_rate = value & 0x0F

    def decode_reg80(self, value: int) -> None:
        """Decode register 0x80+offset"""
        self.sustain_level = (value >> 4) & 0x0F
        self.release_rate = value & 0x0F

    def decode_regE0(self, value: int) -> None:
        """Decode register 0xE0+offset"""
        self.waveform = value & 0x07

    def to_dict(self) -> Dict[str, Any]:
        return {
            'envelope': {
                'attack_rate': self.attack_rate,
                'decay_rate': self.decay_rate,
                'sustain_level': self.sustain_level,
                'release_rate': self.release_rate,
            },
            'level': {
                'total_level': self.total_level,
                'attenuation_db': self.attenuation_db,
                'key_scale_level': self.key_scale_level,
            },
            'modulation': {
                'tremolo': self.tremolo,
                'vibrato': self.vibrato,
            },
            'frequency': {
                'multiple': self.multiple,
                'multiple_ratio': self.multiple_ratio,
            },
            'sustain_mode': self.sustain,
            'key_scale_rate': self.ksr,
            'waveform': self.waveform,
            'waveform_name': self.waveform_name,
        }


@dataclass
class FMChannel:
    """Decoded state of one FM channel"""

    channel: int = 0                # Channel number (0-17)
    bank: int = 0                   # Bank (0=0xC4, 1=0xC6)

    # Operators (2 for 2-op, 4 for 4-op)
    operators: List[FMOperator] = field(default_factory=lambda: [FMOperator(), FMOperator()])

    # Register 0xA0+ch: F-Number low
    # Register 0xB0+ch: Key-on/Block/F-Number high
    key_on: bool = False            # Key-on flag
    block: int = 0                  # Block/octave (0-7)
    f_number: int = 0               # F-Number (0-1023)

    # Register 0xC0+ch: FB/Connection/Panning
    feedback: int = 0               # Feedback level (0-7)
    connection: int = 0             # Connection type (0=FM, 1=additive)
    pan_left: bool = True           # Left output enable
    pan_right: bool = True          # Right output enable

    # 4-op mode
    is_4op: bool = False            # Part of 4-op pair
    is_4op_primary: bool = False    # Primary channel of 4-op pair
    paired_channel: Optional[int] = None  # Paired channel for 4-op

    @property
    def pan_position(self) -> str:
        if self.pan_left and self.pan_right:
            return "Center"
        elif self.pan_left:
            return "Left"
        elif self.pan_right:
            return "Right"
        return "Mute"

    @property
    def note_frequency(self) -> float:
        """Calculate actual frequency in Hz (assuming 49716 Hz master clock)"""
        if self.f_number == 0:
            return 0.0
        # f = fnumber * 2^(block-1) * 49716 / 2^19
        return self.f_number * (2 ** (self.block - 1)) * 49716 / (2 ** 19)

    @property
    def connection_type(self) -> str:
        return "Additive" if self.connection else "FM"

    @property
    def feedback_modulation(self) -> str:
        """Feedback as modulation depth description"""
        if self.feedback == 0:
            return "None"
        fb_table = ["π/16", "π/8", "π/4", "π/2", "π", "2π", "4π"]
        return fb_table[self.feedback - 1] if self.feedback <= 7 else f"FB{self.feedback}"

    def decode_regA0(self, value: int) -> None:
        """Decode register 0xA0+ch (F-Number low)"""
        self.f_number = (self.f_number & 0x300) | value

    def decode_regB0(self, value: int) -> None:
        """Decode register 0xB0+ch (Key-on/Block/F-Number high)"""
        self.key_on = bool(value & 0x20)
        self.block = (value >> 2) & 0x07
        self.f_number = (self.f_number & 0x0FF) | ((value & 0x03) << 8)

    def decode_regC0(self, value: int) -> None:
        """Decode register 0xC0+ch (Feedback/Connection/Panning)"""
        self.pan_right = bool(value & 0x20)
        self.pan_left = bool(value & 0x10)
        self.feedback = (value >> 1) & 0x07
        self.connection = value & 0x01

    def to_dict(self) -> Dict[str, Any]:
        result: Dict[str, Any] = {
            'channel': self.channel,
            'bank': self.bank,
            'key_on': self.key_on,
            'frequency': {
                'f_number': self.f_number,
                'block': self.block,
                'hz': round(self.note_frequency, 2),
            },
            'output': {
                'pan': self.pan_position,
                'pan_left': self.pan_left,
                'pan_right': self.pan_right,
            },
            'algorithm': {
                'connection': self.connection,
                'connection_type': self.connection_type,
                'feedback': self.feedback,
                'feedback_depth': self.feedback_modulation,
            },
            'operators': [op.to_dict() for op in self.operators],
        }
        if self.is_4op:
            result['four_op'] = {
                'enabled': True,
                'is_primary': self.is_4op_primary,
                'paired_channel': self.paired_channel,
            }
        return result


@dataclass
class FMGlobalState:
    """Global OPL4 FM state (non-channel registers)"""

    # Register 0x01: Test / Waveform enable
    waveform_enable: bool = True    # WS - Waveform select enable
    test_bits: int = 0              # Test register bits

    # Register 0x04: Timer control
    timer1_mask: bool = False       # Timer 1 IRQ mask
    timer2_mask: bool = False       # Timer 2 IRQ mask
    timer1_start: bool = False      # Timer 1 start
    timer2_start: bool = False      # Timer 2 start
    irq_reset: bool = False         # IRQ reset

    # Register 0x05: OPL3 mode (bank 1)
    opl3_mode: bool = True          # OPL3 mode enable (always true for OPL4)

    # Register 0x08: CSW / Note-sel
    csw: bool = False               # Composite sine wave mode
    note_sel: bool = False          # Note select

    # Register 0xBD: Rhythm mode (bank 1)
    am_depth: bool = False          # AM depth (0=1dB, 1=4.8dB)
    vib_depth: bool = False         # Vibrato depth (0=7cents, 1=14cents)
    rhythm_mode: bool = False       # Rhythm mode enable
    bass_drum: bool = False         # BD key-on
    snare_drum: bool = False        # SD key-on
    tom_tom: bool = False           # TT key-on
    cymbal: bool = False            # CY key-on
    hi_hat: bool = False            # HH key-on

    # Register 0x104 (bank 2): 4-op connection
    four_op_mask: int = 0           # 4-op channel enable mask (6 bits)

    # Timer values
    timer1_value: int = 0           # Timer 1 preset
    timer2_value: int = 0           # Timer 2 preset

    @property
    def four_op_channels(self) -> List[int]:
        """List of primary channels in 4-op mode"""
        channels = []
        pairs = [(0, 3), (1, 4), (2, 5), (9, 12), (10, 13), (11, 14)]
        for i, (primary, secondary) in enumerate(pairs):
            if self.four_op_mask & (1 << i):
                channels.append(primary)
        return channels

    def decode_reg01(self, value: int) -> None:
        """Decode register 0x01"""
        self.waveform_enable = bool(value & 0x20)
        self.test_bits = value & 0x1F

    def decode_reg04(self, value: int) -> None:
        """Decode register 0x04"""
        self.irq_reset = bool(value & 0x80)
        self.timer1_mask = bool(value & 0x40)
        self.timer2_mask = bool(value & 0x20)
        self.timer2_start = bool(value & 0x02)
        self.timer1_start = bool(value & 0x01)

    def decode_reg05(self, value: int) -> None:
        """Decode register 0x05"""
        self.opl3_mode = bool(value & 0x01)

    def decode_reg08(self, value: int) -> None:
        """Decode register 0x08"""
        self.csw = bool(value & 0x80)
        self.note_sel = bool(value & 0x40)

    def decode_regBD(self, value: int) -> None:
        """Decode register 0xBD (rhythm mode)"""
        self.am_depth = bool(value & 0x80)
        self.vib_depth = bool(value & 0x40)
        self.rhythm_mode = bool(value & 0x20)
        self.bass_drum = bool(value & 0x10)
        self.snare_drum = bool(value & 0x08)
        self.tom_tom = bool(value & 0x04)
        self.cymbal = bool(value & 0x02)
        self.hi_hat = bool(value & 0x01)

    def decode_reg104(self, value: int) -> None:
        """Decode register 0x104 (4-op mask)"""
        self.four_op_mask = value & 0x3F

    def to_dict(self) -> Dict[str, Any]:
        return {
            'mode': {
                'opl3_enabled': self.opl3_mode,
                'waveform_select_enabled': self.waveform_enable,
            },
            'four_op': {
                'mask': f'0x{self.four_op_mask:02X}',
                'channels': self.four_op_channels,
            },
            'modulation_depth': {
                'am_depth': '4.8dB' if self.am_depth else '1dB',
                'vibrato_depth': '14 cents' if self.vib_depth else '7 cents',
            },
            'rhythm': {
                'enabled': self.rhythm_mode,
                'bass_drum': self.bass_drum,
                'snare_drum': self.snare_drum,
                'tom_tom': self.tom_tom,
                'cymbal': self.cymbal,
                'hi_hat': self.hi_hat,
            },
            'timers': {
                'timer1': {'value': self.timer1_value, 'running': self.timer1_start, 'masked': self.timer1_mask},
                'timer2': {'value': self.timer2_value, 'running': self.timer2_start, 'masked': self.timer2_mask},
            },
        }


class OPL4FMDecoder:
    """
    Full OPL4 FM register decoder.

    Maintains state for all 18 channels (36 operators) plus global registers.
    """

    # Operator offset mapping: channel -> (bank, offset for op1, offset for op2)
    # For 2-op instruments
    CHANNEL_OPERATOR_MAP = {
        # Bank 0 (0xC4)
        0: (0, 0x00, 0x03),   # ch0: ops at +0, +3
        1: (0, 0x01, 0x04),   # ch1: ops at +1, +4
        2: (0, 0x02, 0x05),   # ch2: ops at +2, +5
        3: (0, 0x08, 0x0B),   # ch3: ops at +8, +11
        4: (0, 0x09, 0x0C),   # ch4: ops at +9, +12
        5: (0, 0x0A, 0x0D),   # ch5: ops at +10, +13
        6: (0, 0x10, 0x13),   # ch6: ops at +16, +19
        7: (0, 0x11, 0x14),   # ch7: ops at +17, +20
        8: (0, 0x12, 0x15),   # ch8: ops at +18, +21
        # Bank 1 (0xC6)
        9:  (1, 0x00, 0x03),
        10: (1, 0x01, 0x04),
        11: (1, 0x02, 0x05),
        12: (1, 0x08, 0x0B),
        13: (1, 0x09, 0x0C),
        14: (1, 0x0A, 0x0D),
        15: (1, 0x10, 0x13),
        16: (1, 0x11, 0x14),
        17: (1, 0x12, 0x15),
    }

    # 4-op channel pairs (primary, secondary)
    FOUR_OP_PAIRS = [
        (0, 3), (1, 4), (2, 5),      # Bank 0
        (9, 12), (10, 13), (11, 14)  # Bank 1
    ]

    def __init__(self):
        self.global_state = FMGlobalState()
        self.channels: List[FMChannel] = []

        # Initialize 18 channels
        for ch in range(18):
            bank, op1_off, op2_off = self.CHANNEL_OPERATOR_MAP[ch]
            channel = FMChannel(channel=ch, bank=bank)
            self.channels.append(channel)

    def reset(self) -> None:
        """Reset all state to defaults"""
        self.global_state = FMGlobalState()
        for ch in self.channels:
            ch.key_on = False
            ch.block = 0
            ch.f_number = 0
            ch.feedback = 0
            ch.connection = 0
            ch.pan_left = True
            ch.pan_right = True
            for op in ch.operators:
                op.__init__()

    def write_register(self, bank: int, register: int, value: int) -> None:
        """Process a register write"""
        if register == 0x01 and bank == 0:
            self.global_state.decode_reg01(value)
        elif register == 0x02:
            self.global_state.timer1_value = value
        elif register == 0x03:
            self.global_state.timer2_value = value
        elif register == 0x04 and bank == 0:
            self.global_state.decode_reg04(value)
        elif register == 0x05 and bank == 0:
            self.global_state.decode_reg05(value)
        elif register == 0x08 and bank == 0:
            self.global_state.decode_reg08(value)
        elif register == 0xBD and bank == 0:
            self.global_state.decode_regBD(value)
        elif register == 0x04 and bank == 1:  # 0x104
            self.global_state.decode_reg104(value)
        elif 0x20 <= register <= 0x35:
            self._write_operator_reg(bank, register, value, 'reg20')
        elif 0x40 <= register <= 0x55:
            self._write_operator_reg(bank, register, value, 'reg40')
        elif 0x60 <= register <= 0x75:
            self._write_operator_reg(bank, register, value, 'reg60')
        elif 0x80 <= register <= 0x95:
            self._write_operator_reg(bank, register, value, 'reg80')
        elif 0xA0 <= register <= 0xA8:
            ch = self._get_channel(bank, register - 0xA0)
            if ch:
                ch.decode_regA0(value)
        elif 0xB0 <= register <= 0xB8:
            ch = self._get_channel(bank, register - 0xB0)
            if ch:
                ch.decode_regB0(value)
        elif 0xC0 <= register <= 0xC8:
            ch = self._get_channel(bank, register - 0xC0)
            if ch:
                ch.decode_regC0(value)
        elif 0xE0 <= register <= 0xF5:
            self._write_operator_reg(bank, register, value, 'regE0')

    def _get_channel(self, bank: int, ch_offset: int) -> Optional[FMChannel]:
        """Get channel object from bank and offset"""
        if ch_offset > 8:
            return None
        ch_index = ch_offset + (9 if bank else 0)
        if ch_index < 18:
            return self.channels[ch_index]
        return None

    def _write_operator_reg(self, bank: int, register: int, value: int, reg_type: str) -> None:
        """Route operator register write to correct operator"""
        base_regs = {'reg20': 0x20, 'reg40': 0x40, 'reg60': 0x60, 'reg80': 0x80, 'regE0': 0xE0}
        base = base_regs[reg_type]
        offset = register - base

        # Map offset to channel and operator
        for ch_idx, (ch_bank, op1_off, op2_off) in self.CHANNEL_OPERATOR_MAP.items():
            if ch_bank != bank:
                continue
            if offset == op1_off:
                op = self.channels[ch_idx].operators[0]
                getattr(op, f'decode_{reg_type}')(value)
                return
            elif offset == op2_off:
                op = self.channels[ch_idx].operators[1]
                getattr(op, f'decode_{reg_type}')(value)
                return

    def update_4op_state(self) -> None:
        """Update 4-op channel pairing based on global mask"""
        for i, (primary, secondary) in enumerate(self.FOUR_OP_PAIRS):
            enabled = bool(self.global_state.four_op_mask & (1 << i))
            self.channels[primary].is_4op = enabled
            self.channels[primary].is_4op_primary = enabled
            self.channels[primary].paired_channel = secondary if enabled else None
            self.channels[secondary].is_4op = enabled
            self.channels[secondary].is_4op_primary = False
            self.channels[secondary].paired_channel = primary if enabled else None

            # In 4-op mode, primary channel has all 4 operators
            if enabled:
                if len(self.channels[primary].operators) < 4:
                    self.channels[primary].operators.extend([
                        self.channels[secondary].operators[0],
                        self.channels[secondary].operators[1]
                    ])
            else:
                self.channels[primary].operators = self.channels[primary].operators[:2]

    def to_dict(self) -> Dict[str, Any]:
        """Export full state to dictionary"""
        self.update_4op_state()
        return {
            'global': self.global_state.to_dict(),
            'channels': [ch.to_dict() for ch in self.channels],
        }
