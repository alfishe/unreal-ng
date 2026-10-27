"""Hand names and comments for the NextZXOS DivMMC ROM (enNxtmmc.rom, 8K)."""

LISTING = 'nxtmmc/next-nxtmmc.asm'
SYMFILE = 'next-nxtmmc.map'
TITLE = 'NextZXOS DivMMC ROM (enNxtmmc.rom), mapped at #0000-#1FFF by the DivMMC automap'

HEADER = [
    ';' + '=' * 75,
    '; ZX Spectrum Next - NextZXOS DivMMC ROM',
    '; Image: machines/next/enNxtmmc.rom (8192 bytes), loaded by TBBLUE.FW into SRAM (DivMMC ROM page)',
    ';',
    '; The ROM the DivMMC shows at #0000-#1FFF when an entry point is fetched (RST $08 hooks, NMI, tape',
    '; traps; the table is NR #B8-#BB). It uses Z80N instructions (NEXTREG, PUSH nnnn): the Z80N',
    '; must work before any card access does. The SD driver at #1EC6-#1FDD speaks the same SPI',
    '; conversation as NextZXOS ROM 2 (zx2.asm #18D6-#1A82); card initialization is NOT here, it is in ROM 2.',
    '; Status of this listing: code found by reachability from the vectors plus tentative entries',
    '; (T-labels: word / jump tables and gap sweeps, not hand verified); the rest is data.',
    ';' + '=' * 75,
]

NAMES = {
    0x0000: 'Reset',
    0x0008: 'Rst08',
    0x0010: 'Rst10',
    0x0018: 'Rst18',
    0x0020: 'Rst20',
    0x0028: 'Rst28',
    0x0030: 'Rst30',
    0x0038: 'Rst38',
    0x0045: 'CallInOsRam',
    0x0066: 'Nmi',
    0x006A: 'ColdStart',
    0x0079: 'ReadNextReg',
    0x0082: 'MapBank7ToMmu67',
    0x0087: 'MapBankCToMmu67',
    0x00CE: 'SelectConMem',
    0x00E5: 'Rst38Handler',
    0x1ED2: 'SectorToCardAddress',
    0x1F0F: 'SdCommandZeroArg',
    0x1F15: 'SdCommand',
    0x1F3D: 'SdWaitNotFF',
    0x1F4B: 'SdWaitDataToken',
    0x1F5C: 'SdReadBlock',
    0x1F79: 'SdDeselect',
    0x1F85: 'SdStopTransmission',
    0x1F92: 'SdWriteBlock',
    0x1FDF: 'Inc32BitPosition',
    0x1FEF: 'GetDriveFlag',
    0x1FF3: 'SetConMem',
}

COMMENTS = {
    0x0000: 'DI, then the cold start. The DivMMC shows this ROM at #0000 after a reset.',
    0x0008: 'RST $08 : DEFB hook - the esxDOS-compatible API gateway (jumps to #0512).',
    0x0045: 'Read #E3, force CONMEM-less map bits, call the OS code in DivMMC RAM at #2009 on a private stack\n'
            '(#26ED) and restore #E3 and the caller stack afterwards.',
    0x0066: 'NMI entry: the ROM does nothing here (PUSH AF / POP AF / RETN); the NMI work is done by the\n'
            'OS code the automap maps for the NMI (NR #BB bits 1:0 choose instant or delayed).',
    0x0079: 'BC = #243B, OUT (C),A selects the NextREG, IN (C) reads it (port #253B): A = NextREG A.',
    0x0087: 'C = 16K bank number: NEXTREG #56 = 2C, NEXTREG #57 = 2C + 1 (MMU slots 6 and 7 = #C000-#FFFF).',
    0x00E5: 'RST $38 / maskable interrupt while mapped.',
    0x1F15: 'A = command byte, HL:DE = argument, B = CRC. Selects card 0 (OUT #E7,#FE) or card 1 (#FD) by Z,\n'
            'one dummy read, then six OUT (#EB) with 4-T spacing (a transfer takes 16 CPU clocks).',
    0x1F3D: 'Poll IN (#EB) until the byte is not #FF (about #32 * 256 tries).',
    0x1F4B: 'Wait for the data token #FE (up to 10 polls).',
    0x1F5C: 'CMD17: read one 512-byte block into (IX) with two INIR, two CRC reads, deselect.',
    0x1F79: 'Common exit: OUT (#E7),#FF with dummy reads.',
    0x1F85: 'CMD12 and wait for the busy byte to clear.',
    0x1F92: 'CMD24: write one block (token #FE, two OTIR, two #FF CRC bytes), data response and CMD13 status.',
    0x1FF3: 'OUT (#E3),#80: CONMEM on (DivMMC ROM and RAM mapped).',
}
