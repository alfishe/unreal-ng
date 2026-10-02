"""Hand-written names and comments for ROM page 8 ("EXP", the BIOS proper).

Names carried automatically from BIOS-TT 0271ac3 (transfer.py) appear in the
listing as "; = NAME (src: FILE:LINE ...)" notes; the names below win.
"""
import fntables

LISTING = 'exp/bios304-p8-exp.asm'
SYMFILE = 'bios304-p8-exp.map'
SYMBANK = 'ROM8'
SYMTITLE = 'Sprinter BIOS 3.04, ROM page 8 (BIOS proper, "EXP"), mapped at #0000'

HEADER = [
    ';' + '=' * 75,
    '; Peters Plus Sprinter Sp2000 - BIOS 3.04 (build 253, 17.06.2003)',
    '; ROM page 8: the BIOS proper ("EXP"), seen by the CPU at #0000-#3FFF',
    '; Image: data/rom/sprinter/sp2k-3.04.rom (CRC32 1729cb5c), file offset #20000',
    ';' + '=' * 75,
    ';',
    '; What this page does (details: ../README.md and exp/README.md):',
    ';   * cold start after the PLD loader: power-on self test (POST) with',
    ';     progress codes on the PIO, the port table (DCP) written into RAM',
    ';     page #40, ports and devices set up, then SETUP copied to #8000 and',
    ';     started;',
    ';   * RST #18 (and RST #08 from RAM): the BIOS function call, function',
    ';     number in C. #80-#FF through the table at #3000, #40-#48 (old HDD',
    ';     API) here, #50-#5F (disk API) in ROM page 0 via the stub at #3FE8;',
    ';   * the screen/text/window functions, the memory manager, CMOS, the',
    ';     8x8 font (#2800) and the packed port table (#1400).',
    ';',
    '; Window 0 shows ROM page 8 while OUT (#7C) bit 0 = 0 and ROM page 0',
    '; when it is 1. Both pages carry stubs at the same addresses (#3FD0-#3FFF)',
    '; so that code switching the page continues in the other page.',
    ';',
    '; No public source of 3.04 exists. Labels come from three places:',
    ';   * hand names (this file, dict_exp.py),',
    ';   * "FnNN_NAME": the handler of BIOS function NN, read from the',
    ';     dispatch table; NAME from Shared_Includes BIOS_equ.inc,',
    ';   * names carried by byte pattern from the closest source, Tolik-Trek',
    ';     Sprinter-BIOS 0271ac3 (2023) bios/exp/*.asm. Each is shown as',
    ';     "; = NAME (src: FILE:LINE)"; "~" = matched by a short unique window',
    ';     only (less certain). Unnamed lXXXXh code has no counterpart there.',
    ';',
    '; Regenerate and verify: see scripts/README.md (gen.py, checkcov.py).',
    ';' + '=' * 75,
]

_table = fntables.wordTable('bios304-p8.bin', 0, 0x3000, 0x80, 128)
_hdd = fntables.chain('bios304-p8.bin', 0, 0x057B, 0x40)
NOT_IMPLEMENTED = 0x315D

SYMBOLS = [
    (0x0000, 'Reset'),
    (0x0003, 'RomNumber'),
    (0x0005, 'BoardId'),
    (0x0008, 'Rst08FromRam'),
    (0x0018, 'Rst18'),
    (0x001B, 'Rst18Return'),
    (0x0038, 'IntVector'),
    (0x0066, 'NmiVector'),
    (0x00A0, 'PostCodeTable'),
    (0x00C0, 'BiosVersionText'),
    (0x0100, 'ColdStart'),
    (0x0116, 'RestartSignature'),
    (0x0126, 'StartSetupInRam'),
    (0x0151, 'InitCpuPorts'),
    (0x024D, 'PostDcpInit'),
    (0x0253, 'PostDcpOpened'),
    (0x02AC, 'PostLoaderHandOver'),
    (0x0310, 'CheckSystemPage'),
    (0x035D, 'RunSetup'),
    (0x036E, 'PortsInit'),
    (0x0424, 'SpectrumMode'),
    (0x045A, 'StartSpectrumRom'),
    (0x046C, 'SpectrumRomStub5B00'),
    (0x04C1, 'MsgNoSpectrumRom'),
    (0x0571, 'HddFnDispatch'),
    (0x0CA1, 'DcpInit'),
    (0x1400, 'DcpTablePacked'),
    (0x2800, 'Font8x8'),
    (0x3000, 'FnTable80'),
    (0x3100, 'BiosCallFromPage0'),
    (0x310A, 'BiosDispatch'),
    (0x314F, 'CallFnTable'),
    (NOT_IMPLEMENTED, 'FnNotImplemented'),
    (0x3288, 'CmosEmulatedWrite'),
    (0x329E, 'CmosWriteRegister'),
    (0x32A9, 'CmosEmulatedRead'),
    (0x32BF, 'CmosReadRegister'),
    (0x3302, 'FddSet720'),
    (0x3308, 'FddSet1440'),
    (0x3D13, 'ToBios3D13'),
    (0x3FD0, 'BackFromPage0'),
    (0x3FE8, 'ToPage0'),
    (0x3FF8, 'Page0CallEntry'),
] + fntables.symbols(_hdd) + fntables.symbols(_table, exclude=(NOT_IMPLEMENTED,),
                                            fallback=fntables.carried('exp'))

# Carried names known to be wrong here (a repeated idiom matched the wrong copy)
DROP = []

_fnList = ', '.join(f'#{n:02X}' for a, n in _table if a == NOT_IMPLEMENTED)

COMMENTS = {
    0x0000: [
        'Reset vector. The PLD loader (ROM page #C) ends with a CPU reset once the',
        'PLD is configured; the CPU then starts here with ROM page 8 in window 0.',
    ],
    0x0003: [
        'RomNumber: 3 bytes read by programs that identify the board. Byte #0005',
        '(BoardId) is #07 in this image and #24 in the ZXMAK2 copy SP_304.BIN -',
        'the only difference in this page between the two known 3.04 dumps.',
    ],
    0x0008: [
        'RST #08 from RAM, ROM half. A program running with RAM in window 0',
        '(DSS) has its own stub at #0008: PUSH AF / LD A,0 / OUT (#7C),A. That OUT',
        'maps this ROM page into window 0, so the CPU fetches #000D from here:',
        'POP AF, JR Rst18Return -> CALL BiosDispatch -> JR #0008. Now this',
        'copy runs: OUT (#3C),0 switches window 0 back to RAM and the program\'s',
        'own #000D (POP AF / RET) returns to the caller. Same page-switch trick',
        'as the stubs at #3FD0.',
    ],
    0x0018: [
        'RST #18: the BIOS call from ROM code (SETUP, page 0). C = function number.',
        'Example: LD D,#10 : LD C,#F6 : RST #18 reads CMOS register #10 into A.',
    ],
    0x0100: [
        'Cold start. If the 12 bytes at #FFE0 equal RestartSignature, a program',
        'asked for a "restart through RAM": jump there (it wipes itself and falls',
        'through to a reset). Otherwise continue at InitCpuPorts.',
    ],
    0x0126: [
        'Copied to #C000 and called (RunSetup). OUT (#7C),5 = ROM page 0 in window 0',
        '(bit 0) with port map 0 enabled (bit 2); LDIR copies page 0 #1000-#3FFF to',
        'RAM #8000; OUT (#7C),4 = back to page 8; JP #8005 enters the SETUP stub',
        '(the "SETUP (C) 2001 PETERS PLUS" header, then the Hrust depacker that',
        'unpacks SETUP to #8000). A\' = 1 when SPACE was held (port #7FFE bit 0).',
    ],
    0x0151: [
        'Z84C15 on-chip devices: SIO A (#19) and PIO (#1C/#1D) set up, system',
        'control registers #EE/#EF (wait states, chip selects), border black.',
        'From here on the PIO port A (#1C) shows POST progress codes from',
        'PostCodeTable (#00A0: a 7-segment-style pattern per step).',
    ],
    0x024D: [
        'POST step 3: write the port table (DCP) into RAM page #40 (DcpInit), then',
        'IN A,(#E2) - the first port READ after reset "opens" the decoder: until',
        'then the PLD ignores port writes (hardware-reference section 4.2).',
    ],
    0x02AC: [
        'The PLD loader leaves IY = #0107 and IX = #FFFD (loader 3.04 #0000-#0099).',
        'If IY is #0107 the IX value is kept, otherwise IX = #FFFD; it is stored',
        'at #C13E of the system page (#FE). Then RAM pages for windows 0-3 and',
        'PortsInit.',
    ],
    0x035D: [
        'RunSetup: copy StartSetupInRam (43 bytes) to #C000 and call it; SETUP',
        'normally never returns. If it does, enter Spectrum mode.',
    ],
    0x036E: [
        'PortsInit: ISA reset pulse (#9FBD), SIO A and B (keyboard and mouse,',
        'MAN 9.1/9.4), CTC channel 0, PIO, Covox-Blaster muted (#4F x 256),',
        'port map 3 with the Beta system register (#FF), then:',
        '  OUT (#BC),#21  -> address #21BC, code #2B: primary IDE channel',
        '  OUT (#7FFD),#10 and OUT (#1FFD),#01: Spectrum paging registers',
    ],
    0x0571: [
        'Functions #40-#4F (C already had bit 6 cleared, so C = 0..#0F here) are',
        'the old HDD API and are served in this page; #50-#7F go to ROM page 0',
        'through ToPage0 (#3FE8).',
    ],
    0x0CA1: [
        'DcpInit: unpack the port table into RAM page #40 (seen at #C000-#FFFF).',
        'Stream at #1400: a flag byte, then for each of its 8 bits (MSB first)',
        'either a literal byte (bit = 1) or a zero (bit = 0). 16 384 bytes out.',
        'Worked example: flags #60 = 0110 0000 -> 0, literal, literal, 0, 0, 0,',
        '0, 0 (two literal bytes follow the flag byte).',
        'Then map 3 (#F000-#FFFF) = 4 copies of map 0\'s first 1 KB, which is the',
        '"write/read, DOS on, PN5 = 0" quarter: map 3 decodes the floppy ports',
        'whatever the DOS signal says. Returns with JP (HL\') after IN A,(#E2).',
        'tools/machines/sprinter/dcp-table/dcp-table.py does the same unpacking and prints the table.',
    ],
    0x1400: [
        'DcpTablePacked: the port table in the format DcpInit reads (#1400-#27F3).',
        'Unpacked it is byte for byte the page dump old_files/DCP_PAGE.bin of',
        'BIOS-TT 0271ac3 (see hardware-reference.md 4.4 for the decoded table).',
    ],
    0x2800: ['Font8x8: 256 characters x 8 bytes (same as BIOS-TT 0271ac3 FONT).'],
    0x3000: [
        'FnTable80: handler addresses of BIOS functions #80-#FF, entry at',
        '#3000 + 2 * (C - #80). Functions served by FnNotImplemented (#315D,',
        'SCF : RET) in 3.04: ' + _fnList[:60],
        '  ' + _fnList[60:130],
        '  ' + _fnList[130:],
    ],
    0x310A: [
        'BiosDispatch, C = function number:',
        '  #80-#FF  -> CallFnTable (table at #3000)',
        '  #40-#7F  -> bit 6 cleared, HddFnDispatch (#0571)',
        '  #00-#3F  -> Carry set: not a BIOS function',
    ],
    0x315D: ['FnNotImplemented: returns Carry = 1 (error).'],
    0x3299: [
        'CMOS write (function #F7): D = register, A = value. If no DS12887 answers',
        '(CmosTest fails) the value goes to an emulated CMOS in the system page',
        '(#FE, at #FF00 + D in window 3). Real chip: address to port #DFBD,',
        'data to #BFBD (codes #1D / #1E). Read (#F6): data from #FFBD (code #1C).',
    ],
    0x32CA: [
        'CMOS presence test (function #F5): invert register #3F, read it back,',
        'restore it. Carry = 1 when no chip answers.',
    ],
    0x32F0: [
        'Function #8F (FN_TURBO): A = 2 / 3 turbo off / on (through #7C), A = #12 /',
        '#13 floppy density: OUT (#BD) with A = #01 (address #01BD, code #16,',
        '720 KB, 250 kbit/s) or #21 (#21BD, code #17, 1.44 MB, 500 kbit/s).',
    ],
    0x3FD0: [
        'Page stubs. ROM pages 0 and 8 have code at #3FD0-#3FFF that is meant to',
        'run across a page switch: OUT (#7C),A changes the page under the running',
        'code, and the next instruction is fetched from the other page at the',
        'next address. Worked example, ToPage0 (#3FE8): PUSH AF / LD A,1 /',
        'OUT (#7C),A switches to page 0; the CPU continues at page 0 #3FED, which',
        'holds JP #0100 (page 0 dispatcher). Page 0 returns through its own',
        '#3FE8 (LD A,0 / OUT (#7C),A), landing here at #3FED: POP AF / RET.',
    ],
}
