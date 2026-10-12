#!/usr/bin/env python3
"""bus_analyzer: the bus list, enable with checks, a TS-Conf program that reads #57 while its own SPI DMA runs (a dma_collision
finding with the program's PC, the DMA's bytes as the DMA's), the SD card timing profile, the VCD export and the refusals. The
values are asserted by the core tests (busanalyzer_test.cpp); this checks what only the live MCP shows: the text an agent reads."""

import os

from mcplib import main, expect

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))

# DI; DMA destination #50000, length #FF words, one burst; select the SD card; start the SPI -> RAM DMA; IN A,(#57) at once; JR $
COLLIDE = ('F3 01 AF 1D 3E 00 ED 79 06 1E ED 79 06 1F 3E 14 ED 79 06 26 3E FF ED 79 06 28 AF ED 79 '
           '3E 00 D3 77 06 27 3E 02 ED 79 DB 57 18 FE')
IN_PC = 0x8000 + 39  # the IN A,(#57)


def fresh_tsconf(s):
    s.fresh_machine('TSL')
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/audio/mixer/master', 'body': {'muted': True}})


def test_list_enable_and_the_dma_collision(s):
    fresh_tsconf(s)
    r = s.call('bus_analyzer', {'action': 'list'})
    expect('zc.spi (spi): Z-Controller SPI' in r.text and 'trace off' in r.text, 'the bus and its state in the text', r.text)
    expect('slot 0: sd sd present=' in r.text and 'timing=ideal' in r.text, 'the SD socket and its timing', r.text)

    r = s.call('bus_analyzer', {'action': 'enable', 'bus': 'zc.spi'})
    expect('trace on' in r.text and 'checks on' in r.text, 'tracing and checks are on', r.text)

    s.call('memory_access', {'action': 'write', 'address': '0x8000', 'hex': COLLIDE})
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/registers/pc', 'body': {'value': 0x8000}})
    s.call('control_execution', {'action': 'run_frames', 'frames': 2})
    s.call('control_execution', {'action': 'pause'})

    r = s.call('bus_analyzer', {'action': 'findings', 'bus': 'zc.spi'})
    expect('error dma_collision' in r.text, 'the collision is found', r.text)
    expect('pc=0x%04X' % IN_PC in r.text, 'with the PC of the IN', r.text)
    expect('interleave on the real board' in r.text, 'and what it means on the hardware', r.text)

    r = s.call('bus_analyzer', {'action': 'events', 'bus': 'zc.spi', 'kinds': 'byte', 'limit': 5})
    expect(' dma slot=0 byte mosi=0xFF' in r.text, "the DMA's bytes are the DMA's", r.text)
    # no select check: a select event is a change of the chip selects, and the BIOS may have left the card selected


def test_sd_timing_export_and_refusals(s):
    fresh_tsconf(s)
    s.call('bus_analyzer', {'action': 'enable', 'bus': 'all'})
    r = s.call('bus_analyzer', {'action': 'sd_timing', 'bus': 'zc.spi', 'profile': 'slow'})
    expect('timing=slow' in r.text, 'the profile in the text', r.text)
    s.call('bus_analyzer', {'action': 'sd_timing', 'bus': 'zc.spi', 'profile': 'ideal'})

    s.call('control_execution', {'action': 'run_frames', 'frames': 5})
    s.call('control_execution', {'action': 'pause'})
    path = os.path.join(REPO_ROOT, 'scratch', 'mcp-bus-test.vcd')
    r = s.call('bus_analyzer', {'action': 'export', 'bus': 'zc.spi', 'path': path})
    expect('exported zc.spi as vcd to ' + path in r.text, 'the export says where', r.text)
    with open(path) as vcd:
        head = vcd.read(400)
    expect('$timescale 1ns $end' in head and 'sck' in head, 'a VCD with the SPI wires', head)
    os.remove(path)

    bad = s.call('bus_analyzer', {'action': 'events', 'bus': 'next.spi'}, ok=False)
    expect(bad.is_error and 'zc.spi' in bad.text, 'an unknown bus is refused with the ids the machine has', bad.text)
    bad = s.call('bus_analyzer', {'action': 'sd_timing', 'bus': 'zc.spi', 'profile': 'fast'}, ok=False)
    expect(bad.is_error, 'an unknown profile is refused', bad.text)
    bad = s.call('bus_analyzer', {'action': 'events', 'bus': 'zc.spi', 'kinds': 'jump'}, ok=False)
    expect(bad.is_error and 'kinds' in bad.text, 'a bad kind is refused, naming the argument', bad.text)
    r = s.call('bus_analyzer', {'action': 'disable'})
    expect('trace off' in r.text, 'every bus off again', r.text)


if __name__ == '__main__':
    main(globals())
