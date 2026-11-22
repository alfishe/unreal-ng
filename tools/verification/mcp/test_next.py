#!/usr/bin/env python3
"""ZX Spectrum Next: the NMI / interrupt / reset report (inspect_state next_nmi), the NMI trigger (invoke_api), and the NextREG, beam and
copper-instruction breakpoints (control_execution bp_add / bp_list), the zxnDDMA part of next_dma. The values are asserted by the core tests; this checks what only the
live MCP shows: the aspect text, the parameters, the type names and the refusals."""

import json
import sys

from mcplib import main, expect


def test_nmi_aspect_and_trigger(s):
    s.fresh_machine('NEXT')
    r = s.call('inspect_state', {'aspects': ['next_nmi']})
    expect('[next_nmi]' in r.text and 'm1_button' in r.text and 'io_trap blocked (the trap is off' in r.text, 'the NMI sources in the text', r.text)
    expect('OFF (POST /next/nmi-journal' in r.text, 'the journal says it is off and how to switch it on', r.text)
    s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/next/nmi-journal', 'body': {'enabled': True, 'clear': True}})
    # the M1 button is off at power on: NR #02 bit 3 is refused with the reason, and the journal says so
    s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/next/nextreg', 'body': {'reg': '02', 'value': '08'}})
    r = s.call('inspect_state', {'aspects': ['next_nmi'], 'nr_nmi_kinds': 'nmi_request', 'nr_nmi_limit': 5})
    expect('nmi_request m1_button REFUSED' in r.text, 'the refusal is in the journal', r.text)
    bad = s.call('inspect_state', {'aspects': ['next_nmi'], 'nr_nmi_kinds': 'jump'}, ok=False)
    expect(bad.is_error or 'kinds' in bad.text, 'a bad kind is refused', bad.text)


def test_magic_port_and_magic_breakpoint(s):
    s.fresh_machine('NEXT')
    r = s.call('inspect_state', {'aspects': ['next_magic_port']})
    expect('[next_magic_port] OFF (POST /next/magic-port' in r.text, 'the console says it is off and how to switch it on', r.text)
    s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/next/magic-port', 'body': {'enabled': True, 'port': 255, 'mode': 'line'}})
    r = s.call('inspect_state', {'aspects': ['next_magic_port']})
    expect('port 0x00FF, mode line' in r.text and 'on,' in r.text, 'the setting in the text', r.text)
    s.call('control_execution', {'action': 'bp_clear'})
    added = s.call('control_execution', {'action': 'bp_add', 'type': 'magic_ed_ff'})
    expect(not added.is_error, 'a magic breakpoint needs no address', added.text)
    listed = s.call('control_execution', {'action': 'bp_list'})
    expect('magic ' in listed.text, 'the listing names the type', listed.text)
    s.call('control_execution', {'action': 'bp_clear'})


def test_uart_aspect_and_the_peers(s):
    s.fresh_machine('NEXT')
    r = s.call('inspect_state', {'aspects': ['next_uart']})
    expect('[next_uart] selected esp' in r.text and 'baud' in r.text, 'the UARTs in the text', r.text)
    expect('esp:' in r.text and 'pi:' in r.text, 'both channels', r.text)
    # UART 1 on a loopback: a byte sent comes back; UART 0 keeps its ESP
    s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/network/config', 'body': {'com_port2': 'loopback'}})
    s.call('control_execution', {'action': 'run_frames', 'frames': 2})   # the machine thread refits the lines at a frame boundary
    r = s.call('inspect_state', {'aspects': ['next_uart']})
    expect('pi:' in r.text and 'loopback' in r.text, 'the Pi line has the loopback', r.text)


def test_next_breakpoint_types(s):
    s.fresh_machine('NEXT')
    s.call('control_execution', {'action': 'bp_clear'})
    added = s.call('control_execution', {'action': 'bp_add', 'type': 'nextreg_write', 'address': 7, 'doors': 'copper,port', 'value': 3,
                                         'value_mask': 3})
    expect('Breakpoint added' in added.text, 'a NextREG breakpoint is added', added.text)
    s.call('control_execution', {'action': 'bp_add', 'type': 'beam', 'address': 99, 'hc': 255})
    s.call('control_execution', {'action': 'bp_add', 'type': 'copper', 'address': 12, 'address_end': 14})
    listing = s.call('control_execution', {'action': 'bp_list'})
    expect('NR 0x0007' in listing.text and 'beam line' in listing.text and 'copper #' in listing.text, 'the three kinds are listed', listing.text)
    for bad in ({'type': 'nextreg_read', 'address': 7, 'doors': 'copper'}, {'type': 'beam', 'address': 600}, {'type': 'copper', 'address': 2000},
                {'type': 'nextreg_write', 'address': 7, 'doors': 'dma'}):
        r = s.call('control_execution', dict(bad, action='bp_add'), ok=False)
        expect(r.is_error, 'refused: ' + json.dumps(bad), r.text)
    s.call('control_execution', {'action': 'bp_clear'})



def test_dma_aspect_shows_the_zxnddma_state(s):
    # the plain NEXT (core 3.02.03) has no zxnDDMA line; NEXT-DDMA on the KS2 board shows the flags, the 22-bit addresses, the 2D block
    # and the SRAM range of Issue 4 (2 MB: bit 21 dropped)
    s.fresh_machine('NEXT')
    r = s.call('inspect_state', {'aspects': ['next_dma']})
    expect('[next_dma] zxn mode' in r.text and 'zxnDDMA' not in r.text, 'core 3.02.03: no zxnDDMA line', r.text)
    s.fresh_machine('NEXT-DDMA')
    # A #0C0000 -> B #064000, physical + 2D + transparent #E3, 16 x 8, strides 16 / 256, loaded (no enable: nothing moves)
    for b in (0x7D, 0x00, 0x00, 0x00, 0x01, 0x14, 0x10, 0xAD, 0x00, 0x40, 0xD7, 0x0C, 0xDB, 0x06, 0xDF, 0x07, 0xE3, 16, 0, 0xE7, 8, 0,
              0xEB, 16, 0, 0xEF, 0x00, 0x01, 0xF3, 0xE3, 0xCF):
        s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/ports/out', 'body': {'port': '0x6B', 'value': '0x%02X' % b}})
    r = s.call('inspect_state', {'aspects': ['next_dma']})
    expect('[next_dma] zxnDDMA physical+two_d+transparent, src 0x0C0000 dst 0x064000, 2D 16x8 at row 0 column 0, strides 16/256, '
           'transparent 0xE3, SRAM 0x000000-0x1FFFFF (issue4)' in r.text, 'the zxnDDMA line', r.text)
    api = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/state/next/dma'})
    ddma = (api.structured or {}).get('body', {}).get('ddma', {})
    expect(ddma.get('ext_a') == '0x0C' and ddma.get('sram', {}).get('range') == '0x000000-0x1FFFFF', 'the REST report has the ddma part',
           json.dumps(api.structured))


if __name__ == '__main__':
    sys.exit(main(globals()))
