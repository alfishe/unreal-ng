"""
MoonSound / OPL4 music format verification tools.

Supports:
- MFM (MoonBlaster FM Music)
- MWM (MoonBlaster Wave Music)

For MSX MoonSound, Wozblaster, FM-Blaster cartridges.
"""

from .mfm_parser import parse_mfm, MFMFile

__all__ = ['parse_mfm', 'MFMFile']
