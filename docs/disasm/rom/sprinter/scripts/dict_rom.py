"""Hand-written names and comments for ROM page 0 (disk drivers, SETUP stub).

Names carried automatically from BIOS-PP 1273243 SETUP/EXTENDED.ASM (the
Peters Plus disk drivers, 2.17/Setup 2.41) and from BIOS-TT 0271ac3 (the
same drivers as rewritten in bios/exp/EXTENDED/*.asm, and the CD driver)
appear as "; = NAME (src: ...)" notes; the names below win.
"""
import fntables

LISTING = 'rom/bios304-p0-drivers.asm'
SYMFILE = 'bios304-p0-drivers.map'
SYMBANK = 'ROM0'
SYMTITLE = 'Sprinter BIOS 3.04, ROM page 0 (disk drivers, SETUP stub, packed SETUP), mapped at #0000'

HEADER = [
    ';' + '=' * 75,
    '; Peters Plus Sprinter Sp2000 - BIOS 3.04 (build 253, 17.06.2003)',
    '; ROM page 0: disk drivers ("EXTENDED"), the SETUP stub and packed SETUP',
    '; Image: data/rom/sprinter/sp2k-3.04.rom (CRC32 1729cb5c), file offset #00000',
    ';' + '=' * 75,
    ';',
    '; Layout of the page (details: ../README.md and rom/README.md):',
    ';   #0000-#0EBB  disk drivers, entered from ROM page 8 through the page',
    ';                stubs (BIOS functions #50-#5F, C = number - #40 here):',
    ';                floppy (WD1793), IDE hard disk, ATAPI CD-ROM, RAM disk',
    ';   #1000-#115E  SETUP stub: "SETUP (C) 2001 PETERS PLUS LTD", then code',
    ';                that copies the Hrust depacker to #D000 and unpacks SETUP',
    ';                to #8000. This block RUNS AT #8000: ROM page 8 copies',
    ';                #1000-#3FFF of this page to RAM #8000 first, so its jump',
    ';                and load addresses are #80xx/#81xx (listing addresses',
    ';                here are the ROM offsets #10xx/#11xx)',
    ';   #115F-#3209  SETUP packed with Hrust 1.x (13 893 bytes unpacked;',
    ';                listing: rom/bios304-setup.asm)',
    ';   #3FD0-#3FFF  page stubs matching those of ROM page 8',
    ';   bytes 4-7    the ROM checksum (#3B #0D #05 #80; ZXMAK2 copy: #58 #7E',
    ';                #83 #D4)',
    ';',
    '; Labels: hand names (dict_rom.py); "FnNN_NAME" handlers of the disk',
    '; function dispatch; names carried by byte pattern from BIOS-PP 1273243',
    '; (EXTENDED.ASM, FDRIVER2.ASM, HDRIVER6.ASM) and BIOS-TT 0271ac3, shown as',
    '; "; = NAME (src: FILE:LINE)", "~" = short-window match (less certain).',
    ';' + '=' * 75,
]

_disk = fntables.chain('bios304-p0.bin', 0, 0x0433, 0x50)
_oldHdd = fntables.chain('bios304-p0.bin', 0, 0x010F, 0x40)

SYMBOLS = [
    (0x0000, 'RomStart'),
    (0x0004, 'RomChecksum'),
    (0x0038, 'IntHandler'),
    (0x0068, 'CallUserInt'),
    (0x0100, 'FromPage8Call'),
    (0x0107, 'FromRamCall'),
    (0x010E, 'OldHddDispatch'),
    (0x0420, 'DiskFnDispatch'),
    (0x0638, 'FddFlipDensity'),
    (0x0653, 'FddApplyDensity'),
    (0x0669, 'FddProbeDensity'),
    (0x0977, 'FddSetDensityDD'),
    (0x097C, 'FddSetDensityHD'),
    (0x1000, 'SetupStub'),
    (0x115F, 'SetupPacked'),
    (0x3FD0, 'ToPage8Call'),
    (0x3FE8, 'BackToPage8'),
] + fntables.symbols(_disk) + [(a, n + '_old') for a, n in fntables.symbols(_oldHdd)]

DROP = ['IDE.Write.Command', 'IDE.Write.DriveCtrl', 'IDE.Read.Status',
        'IDE.HDD_INIT_TABLE.Chanel', 'IDE.HDD_INIT_TABLE.SectorsPerTrack',
        'IDE.HDD_INIT_TABLE.HeadsNumber', 'IDE.HDD_INIT_TABLE.CylinderNumberLow',
        'IDE.HDD_INIT_TABLE.CylinderNumberHigh', 'IDE.HDD_INIT_TABLE.SectorsPerCylinderLow',
        'IDE.HDD_INIT_TABLE.SectorsPerCylinderHigh', 'HDD_5x.RESET',
        # the four "CP #10 / JP C,..." device switches look alike once their jump
        # targets are masked; the run match is shifted by one for these
        'CLREAD', 'CLWRITE', 'CREAD', 'CWRITE', 'SET144', 'RES144']

COMMENTS = {
    0x0000: [
        'DI : HALT - this page is never entered at #0000 (the CPU starts in the',
        'PLD loader, ROM page #C, and then in ROM page 8). Bytes 4-7 hold the ROM',
        'checksum; they and page 8 byte 5 are the 5 bytes that differ from the',
        'ZXMAK2 copy of 3.04.',
    ],
    0x0038: [
        'Interrupt handler while this page is in window 0: if the system page',
        '(#FE, mapped at #C000 here) holds #AA at #C127, call the user handler',
        'whose address is at #C124 and RAM page at #C126 (CallUserInt maps the',
        'page into the window the address points to).',
    ],
    0x0100: [
        'Entry from ROM page 8 (its ToPage0 stub at #3FE8 lands at #3FED = JP #0100',
        'in this page). AF was pushed by the stub. DiskFnDispatch serves the',
        'call; BackToPage8 (#3FE8) switches back and page 8 returns to the caller.',
    ],
    0x0420: [
        'DiskFnDispatch. C = function number - #40 (page 8 cleared bit 6):',
        '  bit 7, 6 or 5 set -> Carry (not a disk function)',
        '  bit 4 = 0 -> OldHddDispatch (#40-#47, the 2.x HDD API kept for old',
        '               programs; page 8 serves #40-#48 itself in 3.04)',
        '  bit 4 = 1 -> #50-#5F, the disk API (A = device: #00-#0F floppy,',
        '               #80-#8F IDE, #C0-#CF CD-ROM; HL:IX = sector number):',
        '     #50 -, #51 reset, #52 long read, #53 long write, #54 verify,',
        '     #55 read, #56 write, #57 detect, #58 / #59 get / set media',
        '     parameters, #5A version (DE = #0235: disk subsystem version 2,',
        '     modification #35 = 53, i.e. 2.53 - the "253" of the build name;',
        '     the 2.17 sources return 2.41), #5F device list (DRV_LIST)',
        '3.04 adds a CD-ROM (ATAPI) branch to each device switch',
        '(cp #C0 / cp #D0); the 2.17 sources (BIOS-PP) have it commented out.',
    ],
    0x0481: [
        'Function #5F (device list): counts the floppy drives (descriptors at',
        '#C1E0 and #C1E8 of the system page) and the IDE units of ONE channel',
        '(descriptors #C1C0 = master, #C1C8 = slave; type byte +7: #01 hard disk,',
        '#02 CD-ROM, #FF none). Result: 4 bytes at (IX): #04, floppies, hard',
        'disks, CD-ROMs.',
    ],
    0x0638: [
        'Floppy density. The current density is bit 7 of #C1E0 in the system',
        'page (#FE): 1 = HD (1.44 MB, 500 kbit/s), 0 = DD (720 KB, 250 kbit/s).',
        'FddFlipDensity toggles it, FddApplyDensity writes it to the hardware',
        '(FddSetDensityDD / FddSetDensityHD below).',
    ],
    0x0669: [
        'FddProbeDensity ("DISK_ID" in the 2.17 sources): find the density of the',
        'disk in the drive.',
        '  1. apply the current density, seek (command #18);',
        '  2. READ ADDRESS (#C0) to port #0F (= WD1793 command register through',
        '     the port table; the code runs from ROM so the #1F operand rewrite',
        '     does not apply) and poll port #FF (bit 7 INTRQ, bit 6 DRQ) for at',
        '     most #F000 loops;',
        '  3. on a time-out flip the density and try again: 4 tries in all, so',
        '     each density gets two chances.',
        'Worked example: a 1.44 MB disk while the latch says DD: try 1 times out',
        '(no ID field is readable at 250 kbit/s), flip to HD, try 2 reads an ID.',
        'Returns A = #80 (HD) or #00 (DD), Carry = 1 when all 4 tries failed.',
    ],
    0x0977: [
        'The density latch: OUT (#BD),A puts A on address lines A15-A8, so',
        'A = #01 -> port #01BD -> code #16 (720 KB) and A = #21 -> #21BD ->',
        'code #17 (1.44 MB) in the port table (hardware-reference.md 4.4).',
    ],
    0x1000: [
        'SetupStub, assembled for #8000 (it runs from the RAM copy that page 8',
        'makes): "SETUP (C) 2001 PETERS PLUS LTD", JR over the text, then: SP =',
        '#7FFF, push the return address #8000, copy the depacker to #D000 and',
        'jump to it with HL = packed data (#815F in RAM = #115F here), DE =',
        '#8000. The depacker returns to #8000, the start of unpacked SETUP.',
    ],
    0x115F: [
        'SetupPacked: Hrust 1.x stream with a ZX header ("HR", unpacked length',
        '#3645 = 13 893, packed length, the last 6 bytes raw). scripts/hrust.py',
        'unpacks it; result: rom/bios304-setup.asm.',
    ],
    0x3FD0: [
        'Page stubs (see the same addresses in ROM page 8). OUT (#7C),0 selects',
        'ROM page 8 (bit 0 = 0), OUT (#7C),1 page 0; OUT (#3C) maps RAM instead',
        'of ROM into window 0. The instruction after each OUT is fetched from',
        'whatever the window shows next.',
    ],
}
