"""Hand names and comments for the FPGA boot ROM (cores/zxnext/src/rom/bootrom.vhd, 8K)."""

LISTING = 'boot/next-boot.asm'
SYMFILE = 'next-boot.map'
TITLE = 'ZX Spectrum Next FPGA boot ROM (the loader that reads TBBLUE.FW), shown at #0000-#1FFF while bootrom_en'

HEADER = [
    ';' + '=' * 75,
    '; ZX Spectrum Next - FPGA boot ROM (the loader of TBBLUE.FW)',
    '; Image: the byte array of cores/zxnext/src/rom/bootrom.vhd (8192 bytes)',
    ';',
    '; Mapped at #0000-#1FFF while bootrom_en = 1: set by a reset while in configuration mode, cleared by',
    '; any write to NextREG #03. Source of the same program: tbblue/src/firmware/loader (C, FAT code in',
    '; fat.c, SD in mmc.s). Flow: DI, IM 1, JP #0080; #0080: SP = #FFFF, CALL #1CC7, CALL #0145, JP #0100.',
    '; The loader reads TBBLUE.FW from the card and starts the chosen module with JP #6000.',
    '; Status: code found by reachability plus tentative entries (T-labels); not hand verified.',
    ';' + '=' * 75,
]

NAMES = {
    0x0000: 'Reset',
    0x0080: 'Start',
    0x0100: 'LoaderMain',
    0x0145: 'InitVideo',
    0x1CC7: 'InitHardware',
}
COMMENTS = {
    0x0000: 'DI, IM 1, JP #0080. The RST / NMI stubs at #0008-#0066 are RETI / RETN.',
}
