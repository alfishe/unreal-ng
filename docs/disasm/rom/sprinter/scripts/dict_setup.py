"""Hand-written names and comments for SETUP (unpacked from ROM page 0).

Names carried from BIOS-PP 1273243 SETUP/DSETUP.ASM (Setup 2.41 of BIOS
2.17, with KEY.ASM, VIDEO_IO.ASM, AUTOIDE.ASM) appear as notes; the names
below win.
"""

LISTING = 'rom/bios304-setup.asm'
SYMFILE = 'bios304-setup.map'
SYMBANK = ''
SYMTITLE = 'Sprinter BIOS 3.04, SETUP unpacked to RAM at #8000 (from ROM page 0 #115F)'

HEADER = [
    ';' + '=' * 75,
    '; Peters Plus Sprinter Sp2000 - BIOS 3.04 (build 253, 17.06.2003)',
    '; SETUP: the boot program and the setup menu, 13 893 bytes, run at #8000',
    '; Source bytes: ROM page 0 #115F-#3209, packed with Hrust 1.x;',
    '; unpacked by scripts/extract.py (scripts/hrust.py) exactly as the',
    '; SETUP stub at page 0 #1000 does it.',
    ';' + '=' * 75,
    ';',
    '; What SETUP does (details and the step-by-step boot: rom/README.md):',
    ';   keyboard and interrupts on, CMOS settings read (defaults when the',
    ';   checksum is bad), memory cleared, the boot screen (logo, version,',
    ';   memory sizes, CMOS clock), floppy and IDE detection, DEL = setup menu,',
    ';   then the boot from the CMOS boot device: LBA 1 of that device must',
    ';   start with "Starting..." + #00; it is copied to #8000 and run at #800C.',
    ';',
    '; Addresses #8000-#B644 are RAM (window 2). JP #800C and the use of',
    '; #7C00/#7E00 refer to the loaded boot sector, not to this image.',
    ';',
    '; Labels: hand names (dict_setup.py) and names carried by byte pattern',
    '; from BIOS-PP 1273243 DSETUP.ASM and its includes, shown as',
    '; "; = NAME (src: FILE:LINE)", "~" = short-window match (less certain).',
    ';' + '=' * 75,
]

SYMBOLS = [
    (0x8000, 'SetupEntry'),
    (0x8101, 'SetupIntHandler'),
    (0x81BE, 'SetupStart'),
    (0x8372, 'ApplyScreenPosition'),
    (0x83C4, 'BootAltDevice'),
    (0x83C9, 'BootSysDevice'),
    (0x83CC, 'BootFromCmosNibble'),
    (0x83EF, 'BootFloppy'),
    (0x8401, 'BootCdRom'),
    (0x840C, 'BootRamDisk'),
    (0x8416, 'BootIde'),
    (0x8451, 'LoadBootSector'),
    (0x8486, 'BootSignature'),
    (0x8492, 'RunBootSector'),
    (0x84A3, 'BootMover7C00'),
    (0x9EF0, 'KeyboardInterrupt'),
    (0xA373, 'KeyboardInit'),
]

DROP = []

COMMENTS = {
    0x8000: [
        'Entry from the depacker (it RETs to #8000): the stub pushed AF and the',
        'return address; SP = #80F0 (stack in #8040-#80EF), then SetupStart.',
    ],
    0x8101: [
        'IM 2 interrupt handler (I = #80, vector at #80FF): saves all registers',
        'and runs KeyboardInterrupt, which drains the keyboard bytes from the',
        'Z84C15 SIO channel A into the key buffer (#9E00).',
    ],
    0x81BE: [
        'SetupStart, the boot sequence:',
        '  1. KeyboardInit (SIO A), clear the text screen, read the CMOS',
        '     settings (READING); a bad checksum loads the defaults (SETDEFX);',
        '  2. clear the user interrupt hook (#C124-#C127, system page #FE);',
        '  3. TR-DOS quick-start check, floppy drive table (FINSTAL), clear',
        '     memory (CLEARM);',
        '  4. screen position from CMOS #1F (ApplyScreenPosition);',
        '  5. warm start? (signature RSTID at #F000 of the system page): skip',
        '     the screen, else draw the boot screen: logo, BIOS id, memory',
        '     size (function #C0), CMOS clock or "no CMOS";',
        '  6. TSETUP: DEL enters the setup menu, ESC leaves to Spectrum mode;',
        '  7. IDE auto-detect, then BootSysDevice; on failure BootAltDevice;',
        '     if both fail, "press a key" and start over.',
    ],
    0x8372: [
        'ApplyScreenPosition: a trick to reach a PLD register that no port maps:',
        'temporarily store code #CB (HOLD, the screen position counters) at',
        'page #40 offset #0400 (map 0, write, DOS off, all address bits 0), do',
        'OUT (C),A with BC = #0000 (A = CMOS #1F), then restore the table byte.',
    ],
    0x83C4: [
        'Boot device from CMOS register #10: low nibble = system disk, high',
        'nibble = alternative disk (G_VALUE with B = mask, C = register).',
        'Value -> device code in B (the BIOS disk API number):',
        '  0 -> #00 floppy A, 1 -> #01 floppy B, 2 -> #80 IDE master,',
        '  3 -> #81 IDE slave, 4 -> #6E RAM disk; anything else fails.',
        'A CD-ROM unit found at the IDE position prints a message and fails',
        '(no CD boot in 3.04).',
    ],
    0x83EF: [
        'Floppy: function #51 (DRV_RESET) with A = drive first; it runs the',
        'density probe (ROM page 0 FddProbeDensity), so a 1.44 MB disk is read',
        'at 500 kbit/s from here on.',
    ],
    0x8451: [
        'LoadBootSector: function #55 (DRV_READ), A = device, HL:IX = #0000:#0001',
        '(LBA 1), B = 1 sector, DE = #7E00. The 512 bytes must start with',
        'BootSignature, "Starting..." + #00 (12 bytes); otherwise Carry = 1 and',
        'the next device is tried.',
        'Worked example (DSS 1.62 floppy, testdata/machines/sprinter/): LBA 1 =',
        'cylinder 0, side 0, sector 2; its first 12 bytes are "Starting...",0.',
    ],
    0x8492: [
        'RunBootSector: copy BootMover7C00 (26 bytes) to #7C00 and jump there. It',
        'sets SP = #7FFF, clears #8000, copies the sector from #7E00 to #8000 and',
        'jumps to #800C (the code after the 12-byte signature) with A = the',
        'device code (from AF\'). The DSS loader then loads LBA 2-3 and',
        'SYSTEM.DOS (hardware-reference.md 14).',
    ],
    0x9EF0: [
        'KeyboardInterrupt: while SIO A (#19) reports a received byte, read it',
        'from #18 (AT scan code set 2 from the keyboard controller in the PLD /',
        'keyboard), handle the #E0 / #F0 prefixes and put the key into the',
        'ring buffer (HEAD/HOST at #9E40/#9E41).',
    ],
    0xA373: [
        'KeyboardInit: program SIO channel A (#19) for the keyboard (register',
        'sequence of MAN 9.1) and reset the key buffer.',
    ],
}
