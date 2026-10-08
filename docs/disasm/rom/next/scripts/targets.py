"""The images of the Next ROM listings.

bin       binary in this folder (extract.py makes them)
org       CPU address the listing is written for
entries   hand-verified code entry points (the vectors are added by analyze.py)
tables    (start, count, step) of 3-byte `JP` entries to seed (API jump tables)
"""
VECTORS = [0x0000, 0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030, 0x0038, 0x0066]

TARGETS = {
    # the FPGA boot ROM (cores/zxnext/src/rom/bootrom.vhd), mapped at #0000 while bootrom_en
    'boot': dict(listing='boot/next-boot.asm', symfile='next-boot.map', title='ZX Spectrum Next FPGA boot ROM', bin='next-boot.bin', org=0x0000, entries=[0x0080], tables=[], extra=[]),
    # the 8K DivMMC ROM of NextZXOS (/machines/next/enNxtmmc.rom)
    'nxtmmc': dict(listing='nxtmmc/next-nxtmmc.asm', symfile='next-nxtmmc.map', title='NextZXOS DivMMC ROM', bin='next-nxtmmc.bin', org=0x0000, entries=[], tables=[], extra=[]),
    # the four 16K ROMs of enNextZX.rom, each shown at #0000
    'zx0': dict(listing='nextzx/next-rom0.asm', symfile='next-rom0.map', title='NextZXOS ROM 0', bin='next-rom0.bin', org=0x0000, entries=[], tables=[], extra=[]),
    'zx1': dict(listing='nextzx/next-rom1.asm', symfile='next-rom1.map', title='NextZXOS ROM 1', bin='next-rom1.bin', org=0x0000, entries=[], tables=[], extra=[]),
    'zx2': dict(listing='nextzx/next-rom2.asm', symfile='next-rom2.map', title='NextZXOS ROM 2', bin='next-rom2.bin', org=0x0000, entries=[], tables=[(0x0100, 0x4A, 3)], extra=[]),
    'zx3': dict(listing='nextzx/next-rom3.asm', symfile='next-rom3.map', title='NextZXOS ROM 3', bin='next-rom3.bin', org=0x0000, entries=[], tables=[], extra=[], carry=[('48.rom', '48k_rom.map')]),
}
