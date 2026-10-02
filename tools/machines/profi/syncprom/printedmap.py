def printed_v5_map():
    """Kondor 'PROFI 3+' K573RF2 map printed in the v5.0 album (profi50.pdf p.9), transcribed."""
    rom = bytearray([0x39] * 2048)
    def row(addr, first16, last16):
        rom[addr:addr + 16] = bytes(first16); rom[addr + 16:addr + 32] = bytes(last16)
    T = lambda s: [int(x, 16) for x in s.split()]
    a = T('4B 4B 49 49 48 C8 49 49 49 49 4B 0B 0B 0B 0B 4F')
    for r in range(12): row(r * 32, [0x4F] * 15 + [0x4B], a)
    b = T('4B 4B 49 49 48 C8 49 49 49 49 4B 0B 0B 0B 0B 4B')
    for r in (12, 13, 18, 19): row(r * 32, [0x4B] * 16, b)
    row(14 * 32, [0x49] * 16, T('49 49 49 49 48 C8 49 49 49 49 49 09 09 09 09 49'))
    row(15 * 32, [0x59] * 16, T('59 59 59 59 58 D8 59 59 59 59 59 19 19 19 19 59'))
    row(16 * 32, [0x41] * 7 + [0xC1] + [0x41] * 8, T('41 41 41 41 40 C0 41 41 41 41 41 01 01 01 01 41'))
    row(17 * 32, [0x49] * 16, T('49 49 49 49 48 C8 49 49 49 49 49 09 09 09 09 49'))
    row(20 * 32, [0x4F] * 15 + [0x4B], T('4B 49 49 49 48 C8 49 49 49 69 69 2B 2B 2B 2B 6F'))
    h = 0x400
    c = T('4B 49 48 C8 49 49 49 0B 0B 0B 0B 0B 0B 0B 0B 4F')
    for r in range(15): row(h + r * 32, [0x4F] * 15 + [0x4B], c)
    row(h + 15 * 32, [0x4B] * 16, T('4B 49 48 C8 49 49 49 0B 0B 0B 0B 0B 0B 0B 0B 4B'))
    row(h + 16 * 32, [0x41] * 6 + [0xC1] + [0x41] * 9, T('41 41 40 C0 41 41 41 01 01 01 01 01 01 01 01 41'))
    row(h + 17 * 32, [0x59] * 16, T('59 59 58 D8 59 59 59 19 19 19 19 19 19 19 19 59'))
    row(h + 18 * 32, [0x49] * 16, T('49 49 48 C8 49 49 49 09 09 09 09 09 09 09 09 49'))
    row(h + 19 * 32, [0x4B] * 16, T('4B 49 48 C8 49 49 49 0B 0B 0B 0B 0B 0B 0B 0B 4B'))
    row(h + 20 * 32, [0x29] * 16, T('29 29 08 C8 49 49 69 2F 29 29 29 29 29 29 29 29'))
    return bytes(rom)

