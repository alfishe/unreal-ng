"""Hand-written names and comments for the SETUP stub (ROM page 0 #1000-#115E, run at #8000)."""

LISTING = 'rom/bios304-p0-setupstub.asm'
SYMFILE = 'bios304-p0-setupstub.map'
SYMBANK = ''
SYMTITLE = 'Sprinter BIOS 3.04, SETUP stub and Hrust depacker (ROM page 0 #1000-#115E) as run at RAM #8000'

HEADER = [
    ';' + '=' * 75,
    '; Peters Plus Sprinter Sp2000 - BIOS 3.04: the SETUP stub',
    '; ROM page 0 #1000-#115E, shown at the address it runs from: RAM #8000.',
    '; ROM page 8 (StartSetupInRam, page 8 #0126) copies page 0 #1000-#3FFF to',
    '; RAM #8000-#AFFF and jumps to #8005.',
    ';' + '=' * 75,
    ';',
    '; #8000  "SETUP" and a JR over the copyright text',
    '; #8020  copy the depacker to #D000, push #8000 as the return address,',
    ';        JP #D000 with HL = #815F (packed SETUP), DE = #8000',
    '; #803E  Hrust 1.x depacker (assembled for #D000: its own jumps and data',
    ';        pointers are #D0xx). The unpacked SETUP overwrites #8000 on, so',
    ';        the depacker must run from its copy at #D000.',
    '; Same code as BIOS-PP 1273243 SETUP/BSETUP.ASM + DEPACK.ASM.',
    ';' + '=' * 75,
]

SYMBOLS = [
    (0x8000, 'SetupName'),
    (0x8005, 'SetupStubEntry'),
    (0x8007, 'SetupCopyright'),
    (0x8020, 'SetupStubStart'),
    (0x803E, 'HrustDepacker'),
]

DROP = []

COMMENTS = {
    0x8020: [
        'SP = #7FFF; the caller\'s return address and AF are kept on the new',
        'stack. Push #8000 (where the depacker returns to) and #D000 (where the',
        'RET below goes), LDIR #21CC bytes from #803E to #D000 (the depacker; the',
        'count also covers the packed data), HL = #815F = the packed data in the',
        '#8000 copy, DE = #8000, RET = JP #D000. The depacker first moves the',
        'packed block up with LDDR so the output growing from #8000 never',
        'overtakes the input.',
    ],
}
