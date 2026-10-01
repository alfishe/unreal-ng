"""Hand-written names and comments for the PLD configuration loader (ROM page #C)."""

LISTING = 'loader/bios304-pc-loader.asm'
SYMFILE = 'bios304-pc-loader.map'
SYMBANK = 'ROM12'
SYMTITLE = 'Sprinter BIOS 3.04, ROM page #C: PLD configuration loader at #0000'

HEADER = [
    ';' + '=' * 75,
    '; Peters Plus Sprinter Sp2000 - BIOS 3.04: the PLD configuration loader',
    '; ROM page #C (file offset #30000), the first 256 bytes; the CPU runs it',
    '; at #0000 after power-on, when the ACEX PLD is still empty.',
    ';' + '=' * 75,
    ';',
    '; Before the PLD is configured only the small EPM7064 CPLD works: it shows',
    '; the ROM to the CPU and turns every CPU memory write in the configuration',
    '; window into one configuration clock (DCLK) with data bit D0 (BIOS-TT',
    '; 0271ac3 src/altera/max/SP2_MAX.TDF). The loader therefore writes each',
    '; bitstream byte 8 times, rotating it right between the writes (bit 0',
    '; first). The bitstream is ROM #0100-#E84E (59 215 bytes, identical to',
    '; BIOS-PP ALTERA/SP2K_304.BIN); the CPU sees ROM pages #C-#F at',
    '; #0000-#FFFF here.',
    ';',
    '; Exact count (tdd-ports-memory.md section 6): 59 215 bytes x 8 writes =',
    '; 473 720 memory writes, no stack or other memory writes before them (no',
    '; CALL / PUSH). The loop at #0088 never ends by itself: it keeps writing',
    '; (from #E84F on: #FF filler bytes) until the configured PLD resets the',
    '; CPU. To be confirmed by a runtime trace (MAME or S1).',
    ';',
    '; Compare: BIOS-TT bios/loader/loader.asm (same 8-write loop; it adds a',
    '; packed-bitstream header check that 3.04 does not have).',
    ';' + '=' * 75,
]

SYMBOLS = [
    (0x0000, 'LoaderStart'),
    (0x0038, 'IntRestart'),
    (0x0066, 'NmiRestart'),
    (0x003B, 'CheckReloadRequest'),
    (0x0069, 'StreamFromRom'),
    (0x006C, 'PickDestination'),
    (0x0088, 'StreamLoop'),
    (0x009C, 'Halt'),
    (0x009E, 'ReloadString'),
]

DROP = []

COMMENTS = {
    0x0000: [
        'Z84C15 system control registers #EE/#EF (wait states, chip-select',
        'boundary #FE: #FE00-#FFFF is the fast RAM), SIO A (#19 <- #05, #62)',
        'and PIO (#1D <- #CF, #00; port A #1C <- #EA).',
    ],
    0x003B: [
        'Reload request: if fast RAM #FEF0-#FEFF holds "ACEX_30K_LOADING" (the',
        'BIOS puts it there with a new bitstream at #1000 before a reset), the',
        'chip-select boundary becomes #F0 and the stream starts at RAM #1000',
        '(HL = #1000). Otherwise the stream is the ROM bitstream at #0100.',
    ],
    0x006C: [
        'Destination of the writes: DE = #FE00 (#FD00 when #FEE0 holds "IM").',
        'Only E is incremented, so the writes cycle through #FE00-#FEFF.',
        'IY = #0107 and IX = #FFFD are handed to the BIOS (page 8 #02B3 checks',
        'IY = #0107 and stores IX at #C13E).',
    ],
    0x0088: [
        'StreamLoop: per bitstream byte 8 writes of A to (DE), RRCA between them,',
        'so bit 0 goes first. Worked example: byte #A5 = 1010 0101 -> D0 of the',
        'eight writes = 1, 0, 1, 0, 0, 1, 0, 1. The loop has no exit: the',
        'configured PLD (CONF_DONE) resets the CPU after the last bit.',
    ],
}
