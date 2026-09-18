"""
MoonSound / OPL4 music format verification tools.

Supports:
- MFM (MoonBlaster FM Music)
- MWM (MoonBlaster Wave Music)

For MSX MoonSound, Wozblaster, FM-Blaster cartridges.

Modules:
- mfm_parser: Parse MFM/MWM files to JSON
- opl4_fm: OPL4 FM synthesis register decoder
- opl4_pcm: OPL4 PCM/Wavetable register decoder
"""

from .mfm_parser import parse_mfm, MFMFile
from .opl4_fm import (
    OPL4FMDecoder,
    FMChannel,
    FMOperator,
    FMGlobalState,
    Waveform,
    WAVEFORM_NAMES,
)
from .opl4_pcm import (
    OPL4PCMDecoder,
    PCMChannel,
    PCMEnvelope,
    PCMLFO,
    PCMGlobalState,
    MFMEventDecoder,
)

__all__ = [
    # MFM Parser
    'parse_mfm',
    'MFMFile',
    # FM Decoder
    'OPL4FMDecoder',
    'FMChannel',
    'FMOperator',
    'FMGlobalState',
    'Waveform',
    'WAVEFORM_NAMES',
    # PCM Decoder
    'OPL4PCMDecoder',
    'PCMChannel',
    'PCMEnvelope',
    'PCMLFO',
    'PCMGlobalState',
    'MFMEventDecoder',
]
