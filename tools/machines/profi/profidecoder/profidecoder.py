#!/usr/bin/env python3
"""Profi port decoder PROM (K556RT4, 256 x 4) -> port map per mode, and a check against unreal-ng's decoder.

PROM address inputs (both boards, chip pins A0..A7 = 5, 6, 7, 4, 3, 2, 1, 15):
  A0 = ADR5   A1 = ADR6   A2 = 0 only while TR-DOS is paged in AND CP/M is off   A3 = ROM14 (v4/v5) or ADR15 (v3.2)
  A4 = ADR7   A5 = ADR1   A6 = ADR0   A7 = /CPM (0 = CP/M mode, #DFFD bit 5 set)   /CS = /IORQ
  v5: the Profi 5.x schematic (PROF5-10.TXT, D10 "556 PT4"); v3.2: the MDESK re-trace (interface list 1, U5).
  A2 on v5 is D32 = NAND(DISK, /CP-M); on v3.2 it is BAS, the other half of the DISK / BAS latch U30, whose gate
  U30:B also takes CP/M (pin 12), so CP/M holds BAS high. On both boards CP/M therefore makes A2 = 1 whatever the
  TR-DOS latch says, and the table rows with CP/M on and A2 = 0 are never addressed.

Outputs (active low) and what they select, from the consumers on the same schematics:
  v5 (printed table, legend "Q1(12)--D3 ... Q4(9)--D0"):
     bit3 F2 = FDC system register     bit2 F1 = VG93 /CS
     bit1 F6 = extended group: enables D11 (ИД4), which splits it by ADR4..ADR2 into P0..P7
               (P0 VG93, P1 8255, P2 IDE, P3 COM, P4 split by ADR6 ADR5 into the timer and the control register, P7 RTC with ADR5 = AS / DS; the schematic text labels the ИД4's low select input ADR7, but only ADR2 gives the manuals' port list #E3 #E7 #EB #EF #F3)
     bit0 F5 = 8255 /CS
  v3.2 (MDESK dump, its schematic labels pin 12 as D0):
     bit0 F2 = FDC system register     bit1 F1 = VG93 /CS
     bit2 F4 = only enables the controller's data bus buffer (U8 -> U31); decodes #7FFD
     bit3 F5 = 8255 /CS
Each bit order is the only one under which the table makes sense: the other one puts the system register on
#1F..#7F.

usage: profidecoder.py <v3.2 PROM .bin> <v4/v5 PROM .bin>
"""
import sys

V5_BITS = {3: 'SYS', 2: 'FDC', 1: 'EXT', 0: '8255'}
V3_BITS = {0: 'SYS', 1: 'FDC', 2: 'BUF7FFD', 3: '8255'}
EXT_P = {0: 'FDC', 1: '8255', 2: 'IDE', 3: 'COM', 4: 'P4', 5: 'P5', 6: 'P6', 7: 'RTC'}


def prom_index(port, dos, cpm, a3):
    a5, a6, a7, a1, a0 = (port >> 5) & 1, (port >> 6) & 1, (port >> 7) & 1, (port >> 1) & 1, port & 1
    a2 = 0 if (dos and not cpm) else 1
    return a5 | a6 << 1 | a2 << 2 | a3 << 3 | a7 << 4 | a1 << 5 | a0 << 6 | (0 if cpm else 1) << 7


def prom_devices(prom, bits, port, dos, cpm, a3):
    d = prom[prom_index(port, dos, cpm, a3)] & 0x0F
    devs = [name for bit, name in bits.items() if not (d >> bit) & 1]
    if 'EXT' in devs:
        devs.remove('EXT')
        devs.append(EXT_P[(port >> 2) & 7])
    return sorted(devs)


def unrealng_devices(port, dos, cpm, rom14):
    """unreal-ng today (PortDecoder_Profi, one board): FDC / system port / 8255-Covox / extended devices."""
    p1 = port & 0xFF
    devs = []
    dosports = dos or cpm                      # CF_DOSPORTS = DOS latch or CP/M
    ext = cpm and rom14                        # IsExtMode()
    if ext and (p1 & 0x9F) == 0x9F:
        devs.append('RTC')
    elif dosports:
        if ext:
            if (p1 & 0x9F) == 0x83:
                devs.append('FDC')
            elif (p1 & 0xE3) == 0x23:
                devs.append('SYS')
            elif (p1 & 0x9F) == 0x87:
                devs.append('8255')            # the Covox aliases #A7 / #C7
        else:
            if (p1 & 0x83) == 0x03:
                devs.append('FDC')
            elif (p1 & 0xE3) == (0xA3 if cpm else 0xE3):
                devs.append('SYS')
    elif (p1 & 0x83) == 0x03:
        devs.append('8255')                    # Covox #3F / #5F, joystick #1F
    if ext and (p1 & 0x1F) == 0x0B and (p1 & 0x80):
        devs.append('IDE')                     # #xxCB / #xxEB (IdeAdapter, Profi scheme)
    return sorted(devs)


def modes(board):
    for cpm in (0, 1):
        for dos in (0, 1):
            for a3 in (0, 1):
                yield cpm, dos, a3


def port_map(prom, bits, board):
    out = []
    a3name = 'A15' if board == 'v3' else 'ROM14'
    for cpm, dos, a3 in modes(board):
        rows = {}
        for p in range(256):
            if (p & 3) != 3 and not (board == 'v3' and (p & 3) == 1):
                continue
            for dev in prom_devices(prom, bits, p, dos, cpm, a3):
                rows.setdefault(dev, []).append(p)
        desc = ', '.join(f'{d} ' + ' '.join(f'#{p:02X}' for p in ps) for d, ps in sorted(rows.items())) or 'nothing'
        out.append(f'  CP/M={cpm} DOS={dos} {a3name}={a3}: {desc}')
    return '\n'.join(out)


def compare(prom):
    diffs = []
    for cpm, dos, rom14 in modes('v5'):
        for p in range(256):
            a = prom_devices(prom, V5_BITS, p, dos, cpm, rom14)
            a = [x for x in a if x in ('FDC', 'SYS', '8255', 'RTC', 'IDE')]
            b = unrealng_devices(p, dos, cpm, rom14)
            if (p & 3) == 3 and a != b:
                diffs.append((cpm, dos, rom14, p, a, b))
    return diffs


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    v3 = open(sys.argv[1], 'rb').read()
    v5 = open(sys.argv[2], 'rb').read()
    print('=== v3.2 port decoder PROM (ports with A1 A0 = 11, and A1 A0 = 01 for the #7FFD buffer enable)')
    print(port_map(v3, V3_BITS, 'v3'))
    print('=== v4 / v5 port decoder PROM (ports with A1 A0 = 11)')
    print(port_map(v5, V5_BITS, 'v5'))
    print('=== v5 PROM vs unreal-ng today (low byte, A1 A0 = 11; COM and the control register are not modeled)')
    groups = {}
    for cpm, dos, rom14, p, a, b in compare(v5):
        groups.setdefault((cpm, dos, rom14, tuple(a), tuple(b)), []).append(p)
    if not groups:
        print('  no differences')
    for (cpm, dos, rom14, a, b), ps in sorted(groups.items()):
        print(f'  CP/M={cpm} DOS={dos} ROM14={rom14}: PROM {list(a) or "nothing"} vs unreal-ng {list(b) or "nothing"} at '
              + ' '.join(f'#{p:02X}' for p in ps))


if __name__ == '__main__':
    main()
