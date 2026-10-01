"""The four listings: binary, CPU origin, entry points, hand-verified data.

entries     CPU addresses where code starts (vectors, documented entry points,
            targets reached only through computed jumps)
wordTables  (address, count) tables of 16-bit code addresses (jump tables)
data        address -> name for data blocks (split points inside unreached runs)
forceData   (start, end) ranges that are data: the walk never decodes them
"""

TARGETS = {
    # ROM page 8: the BIOS proper ("EXP"), mapped at #0000 (window 0)
    'exp': {
        'bin': 'bios304-p8.bin',
        'org': 0x0000,
        'entries': [0x0000, 0x0008, 0x0018, 0x001B, 0x0038, 0x0066, 0x0100,
                    0x0116,     # restart signature, also executed in place
                    0x045A,     # copies #046C-#04C0 to #5B00 and runs it there
                    0x046C,     # the #5B00 code (addresses inside are #5Bxx)
                    0x0523,     # after the "Spectrum ROM not installed" text
                    0x0CA1,     # DCP table writer, entered by JP with return in HL'
                    0x312F, 0x3142,     # save / restore the interrupt state (LD A,R)
                    0x3D13,     # ToBIOS_3D13 entry (BIOS_equ.inc)
                    0x3E22, 0x3F2F, 0x3F69],
        'wordTables': [(0x3000, 128),       # RST #18 functions #80-#FF
                       (0x37CB, 10)],
        'data': {0x3000: 'FnTable80', 0x37CB: 'Table37CB', 0x1400: 'DcpTablePacked',
                 0x27F4: 'Data27F4', 0x2800: 'Font8x8'},
    },
    # ROM page 0: disk drivers ("EXTENDED") at #0000, SETUP stub, packed SETUP
    'rom': {
        'bin': 'bios304-p0.bin',
        'org': 0x0000,
        'entries': [0x0000, 0x0010, 0x0038, 0x0066, 0x0100, 0x0107,
                    0x3FD0, 0x3FD8, 0x3FE0, 0x3FE8, 0x3FF0, 0x3FF8],
        'data': {0x0EBC: 'Fill0EBC', 0x1000: 'SetupStub', 0x115F: 'SetupPacked', 0x320A: 'Fill320A'},
        'forceData': [(0x1000, 0x3FD0)],
    },
    # SETUP, unpacked by the stub to #8000
    'setup': {
        'bin': 'bios304-setup.bin',
        'org': 0x8000,
        'entries': [0x8000],
        'data': {0x8009: 'CopyrightText', 0x8040: 'StackArea'},
        'forceData': [(0x8009, 0x8022), (0x8040, 0x80F0)],
    },
    # The SETUP stub (page 0 #1000-#115E) where it runs: RAM #8000; the
    # depacker part (#803E-) is copied to #D000 and runs there
    'stub': {
        'bin': 'bios304-stub.bin',
        'org': 0x8000,
        'entries': [0x8005, 0x803E],
        'data': {0x8000: 'SetupName', 0x8007: 'SetupCopyright'},
        'forceData': [(0x8000, 0x8005), (0x8007, 0x8020)],
    },
    # The PLD configuration loader, ROM page #C, run at #0000 at power-on
    'loader': {
        'bin': 'bios304-pc.bin',
        'org': 0x0000,
        'entries': [0x0000, 0x0038, 0x0066],
        'data': {0x009C: 'Halt', 0x009E: 'ReloadString', 0x00AE: 'PostCodeTable', 0x00BE: 'Fill00BE'},
        'forceData': [(0x009E, 0x00AE)],
    },
}
