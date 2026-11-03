#!/usr/bin/env python3
"""The sound card CPU as a debug target (gs-debugger design §4): control_execution bp_* with cpu "gs" set the card's
breakpoints (the NeoGS of the PENTAGON), a card stop parks the machine and wait names the CPU and the card instruction,
the lists are per CPU, the card refuses what it has not (NextREG, the host's events), and its registers, memory and code
are read and written with cpu "gs" (core CpuView), and it steps (RunControl::CardStep)"""

import re
import time

from mcplib import expect, main


def setup(s):
    s.fresh_machine()
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/state/audio/gs'}, ok=False)
    expect(not r.is_error, 'the PENTAGON fits a General Sound card (NeoGS)', r.text)


def clear(s):
    s.call('control_execution', {'action': 'pause'}, ok=False)
    s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'})
    s.call('control_execution', {'action': 'bp_clear'})


def stop_of(s):
    """Resume and wait for the stop: (the wait's text, its pause object)"""
    s.call('control_execution', {'action': 'resume'})
    r = s.call('control_execution', {'action': 'wait', 'timeout_ms': 5000})
    return r.text, (r.structured or {}).get('pause', {})


def card_pc(s):
    """The instruction the card CPU is at (the machine paused)"""
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/state/audio/gs'})
    pc = (r.structured or {}).get('body', {}).get('cpu', {}).get('pc')
    expect(isinstance(pc, int), 'the card CPU state has its pc', r.text)
    return pc


def test_card_breakpoint_parks_the_machine(s):
    clear(s)
    # Without an SD card the NeoGS loader polls it with interrupts off: the instruction it is at comes round at once
    pc = card_pc(s)
    r = s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs'})
    expect(re.search(r'#\d+', r.text), 'the id in the text', r.text)
    text, pause = stop_of(s)
    expect('at breakpoint' in text and 'of the gs CPU' in text, 'the wait names the card CPU', text)
    expect(pause.get('cpu') == 'gs' and pause.get('address') == pc and pause.get('cpu_pc') == pc,
           'the pause carries cpu and the card instruction', pause)
    expect(s.call('inspect_state', {'aspects': ['registers']}).text, 'the machine answers while parked')
    hits = s.call('control_execution', {'action': 'bp_list', 'cpu': 'gs'}).text
    expect(re.search(r'hits [1-9]', hits), 'counted on the card', hits)
    clear(s)


def test_lists_are_per_cpu(s):
    clear(s)
    s.call('control_execution', {'action': 'bp_add', 'type': 'event', 'event': 'card_command', 'cpu': 'gs',
                                 'note': 'gs-check'})
    s.call('control_execution', {'action': 'bp_add', 'address': '0x0038'})
    gs = s.call('control_execution', {'action': 'bp_list', 'cpu': 'gs'}).text
    main_list = s.call('control_execution', {'action': 'bp_list'}).text
    expect('(gs CPU)' in gs and 'event card_command' in gs and '0x0038' not in gs, 'the card list', gs)
    expect('gs-check' not in main_list and '0x0038' in main_list, 'the main list', main_list)
    groups = s.call('control_execution', {'action': 'bp_groups', 'cpu': 'gs'})
    expect('(gs CPU): 1' in groups.text and groups.structured['groups'][0]['total'] == 1, "the card's groups",
           groups.text)
    clear(s)


def test_the_card_refuses_what_it_has_not(s):
    clear(s)
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'nextreg', 'address': 7, 'cpu': 'gs'}, ok=False)
    expect(r.is_error, 'no NextREG on the card', r.text)
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'event', 'event': 'host_command', 'cpu': 'gs'},
               ok=False)
    expect(r.is_error and 'main CPU' in r.text, "the host's event is the main CPU's", r.text)
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'event', 'event': 'card_reply'}, ok=False)
    expect(r.is_error and 'gs CPU' in r.text, "the card's event is the card CPU's", r.text)
    r = s.call('control_execution', {'action': 'bp_list', 'cpu': 'z81'}, ok=False)
    expect(r.is_error and 'cpu' in r.text, 'an unknown CPU is refused', r.text)
    r = s.call('control_execution', {'action': 'bp_add', 'type': 'event', 'event': 'host_command'})
    expect(not r.is_error, "the host's event on the main CPU", r.text)
    clear(s)


def test_a_card_condition(s):
    """A condition on a card breakpoint reads the card's registers; bp_condition with cpu gs changes it, and the
    machine's own state (FD) is refused there"""
    clear(s)
    pc = card_pc(s)
    r = s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs', 'condition': 'PC != #%04X' % pc})
    expect(' if ' in r.text, 'the text names the condition', r.text)
    never = int(re.search(r'#(\d+)', r.text).group(1))
    s.call('control_execution', {'action': 'resume'})
    deadline = time.time() + 5
    listing = ''
    while time.time() < deadline:
        listing = s.call('control_execution', {'action': 'bp_list', 'cpu': 'gs'}).text
        if re.search(r'hits 0 matched [1-9]', listing):
            break
        time.sleep(0.05)
    s.call('control_execution', {'action': 'pause'})
    expect(re.search(r'hits 0 matched [1-9]', listing), 'a false condition matches without a hit', listing)

    r = s.call('control_execution', {'action': 'bp_condition', 'bp_id': never, 'cpu': 'gs', 'condition': 'FD == 10'},
               ok=False)
    expect(r.is_error and "the machine's" in r.text, "the machine's FD is refused on the card", r.text)
    pc = card_pc(s)
    r = s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs',
                                     'condition': 'PC == #%04X && M(#%04X) == M(#%04X)' % (pc, pc, pc)})
    held = int(re.search(r'#(\d+)', r.text).group(1))
    expect(held != never, 'a conditional breakpoint is never shared', r.text)
    text, pause = stop_of(s)
    expect(pause.get('cpu') == 'gs' and pause.get('cpu_pc') == pc, 'parked by the condition that holds', pause)
    r = s.call('control_execution', {'action': 'bp_condition', 'bp_id': never, 'cpu': 'gs', 'condition': ''})
    expect(not r.is_error, 'cleared on the card', r.text)
    clear(s)


def test_a_python_tap_on_the_card(s):
    """A Python callback on the card's port reads (cpu gs): MCP lists it with the CPU, and its hits count"""
    clear(s)
    code = ('import unreal_emulator as ue\n'
            'emu = ue.emu_get_selected()\n'
            'gs_seen = []\n'
            'gs_cb = emu.on_tap(lambda hit: gs_seen.append(hit["cpu"]), "port_in", 0, mask=0, cpu="gs", '
            'label="gs-check")\n'
            'print(gs_cb)\n')
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/python/exec', 'body': {'code': code}}, ok=False)
    if 'Python automation is not available' in r.text:
        print('        no Python in this build (ENABLE_PYTHON_AUTOMATION=OFF): not checked')
        return
    expect(not r.is_error, 'registered', r.text)
    s.call('control_execution', {'action': 'resume'})
    deadline = time.time() + 5
    while time.time() < deadline:
        r = s.call('control_execution', {'action': 'callbacks'})
        if re.search(r'\(gs CPU\) \(python "gs-check"\) active, policy suspend, hits [1-9]', r.text):
            break
        time.sleep(0.1)
    s.call('control_execution', {'action': 'pause'})
    expect(re.search(r'#(\d+) tap in #0000 \(gs CPU\) \(python "gs-check"\) active, policy suspend, hits [1-9]',
                     r.text), 'the card tap on one line, its hits counted', r.text)
    entry = next(e for e in r.structured['callbacks'] if e['label'] == 'gs-check')
    expect(entry['cpu'] == 'gs', 'cpu in structuredContent', entry)
    s.call('control_execution', {'action': 'callback_remove', 'callback_id': entry['id']})
    clear(s)


def test_the_card_cpus_registers_memory_and_code(s):
    """inspect_state cpu gs: the card's registers, windows and code; memory_access space gs writes and reads its RAM
    window; a card CPU that is not there (ms on the PENTAGON) is said in the text"""
    clear(s)
    pc = card_pc(s)
    r = s.call('inspect_state', {'aspects': ['registers', 'memory_map', 'disasm'], 'cpu': 'gs'})
    expect('[registers] cpu gs:' in r.text and 'PC=0x%04X' % pc in r.text, "the card's PC in the text", r.text)
    windows = (r.structured or {}).get('memory_map', {}).get('windows', [])
    expect(len(windows) == 4, 'four windows', windows)
    expect(any(w.get('kind') == 'ram' for w in windows) and 'the CPU\'s windows' in r.text,
           'RAM windows, listed in the text', r.text)
    code = (r.structured or {}).get('disasm', {}).get('instructions', [])
    expect(code and code[0].get('address') == pc, "the code from the card's PC", code[:1])

    # The NeoGS firmware runs from RAM with RAMRO: its pages 0 / 1 keep their bytes as the card CPU's writes would
    landed = None
    for window in (w for w in windows if w.get('kind') == 'ram'):
        address = '0x%04X' % (window['start'] + 0x100)
        w = s.call('memory_access', {'action': 'write', 'space': 'gs', 'offset': address, 'hex': 'A55A'})
        rd = s.call('memory_access', {'action': 'read', 'space': 'gs', 'offset': address, 'length': 2})
        took = bool(re.search(r'A5\s*5A', rd.text.upper()))
        expect("into the gs CPU's view" in w.text and took == ('kept by a read-only window' not in w.text),
               'the write says what landed', (w.text, rd.text))
        if took and landed is None:
            landed = address
    expect(landed is not None, 'a writable RAM window of the card', windows)
    address = landed
    rd = s.call('memory_access', {'action': 'read', 'space': 'gs', 'offset': address, 'length': 2})
    expect("the gs CPU's view" in rd.text, 'read back from the card', rd.text)
    main_rd = s.call('memory_access', {'action': 'read', 'offset': address, 'length': 2})
    expect(main_rd.text != rd.text, "the machine's view is another memory", main_rd.text)

    r = s.call('inspect_state', {'aspects': ['registers'], 'cpu': 'ms'})
    expect('[registers] unavailable:' in r.text and 'ms' in r.text, 'no MultiSound card: said in the text', r.text)
    r = s.call('control_execution', {'action': 'set_register', 'name': 'a', 'value': 16, 'cpu': 'gs'})
    expect(not r.is_error, "the card's A set", r.text)
    clear(s)


def test_the_card_cpu_from_python(s):
    """Python's cpu= on the register, memory and code functions; memory_map('gs') the windows; ms is a RuntimeError"""
    clear(s)
    code = ('import unreal_emulator as ue\n'
            'emu = ue.emu_get_selected()\n'
            'r = emu.get_registers(cpu="gs")\n'
            'assert r["cpu"] == "gs" and "card_time" in r and "t" not in r\n'
            'assert emu.get_register("pc", cpu="gs") == r["pc"]\n'
            'assert emu.set_register("b", 0x24, cpu="gs") and emu.get_register("b", cpu="gs") == 0x24\n'
            'rams = [w["start"] + 0x100 for w in emu.memory_map("gs")["windows"] if w["kind"] == "ram"]\n'
            'for a in rams: emu.mem_write(a, 0x5A, cpu="gs")\n'
            'assert any(emu.mem_read(a, cpu="gs") == 0x5A for a in rams)\n'
            'assert emu.disasm(-1, 2, cpu="gs")[0]["address"] == r["pc"]\n'
            'try:\n'
            '    emu.get_registers(cpu="ms")\n'
            '    print("ms answered")\n'
            'except RuntimeError as e:\n'
            '    print("card-ok", e)\n')
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/python/exec', 'body': {'code': code}}, ok=False)
    if 'Python automation is not available' in r.text:
        print('        no Python in this build (ENABLE_PYTHON_AUTOMATION=OFF): not checked')
        return
    output = ((r.structured or {}).get('body') or {}).get('output', '') + r.text
    expect(not r.is_error and 'card-ok' in output and 'not available' in output, 'the card CPU from Python',
           r.structured or r.text)
    clear(s)


def test_the_card_cpu_steps(s):
    """control_execution step / step_n / step_over / step_out with cpu gs: the machine runs until the card's step ends
    and is paused again; the text names where the card and the main CPU stand; a card breakpoint ends a step; Lua and
    Python step it too"""
    clear(s)
    before = card_pc(s)
    r = s.call('control_execution', {'action': 'step', 'cpu': 'gs'})
    stop = (r.structured or {}).get('stop', {})
    expect(stop.get('reason') == 'step' and stop.get('cpu') == 'gs', 'a step of the card CPU', r.structured)
    expect(stop.get('cpu_pc') == r.structured.get('pc') == card_pc(s), 'the reply has the card PC', r.structured)
    expect(re.search(r'Card CPU gs at \$[0-9A-F]{4}; main CPU at \$[0-9A-F]{4}', r.text), 'said in the text', r.text)
    expect(r.structured.get('state') == 'paused', 'paused again', r.structured)
    expect(card_pc(s) != before or r.structured.get('sp') is not None, 'the card moved on', (before, r.structured))

    r = s.call('control_execution', {'action': 'step_n', 'count': 3, 'cpu': 'gs'})
    expect((r.structured or {}).get('stop', {}).get('reason') == 'step', 'three card instructions', r.structured)
    r = s.call('control_execution', {'action': 'step_over', 'cpu': 'gs'})
    expect((r.structured or {}).get('stop', {}).get('reason') == 'step', 'a step over on the card', r.structured)

    # A card breakpoint where the loader loops ends a step out of its top level
    pc = card_pc(s)
    s.call('control_execution', {'action': 'step', 'cpu': 'gs'})
    s.call('control_execution', {'action': 'bp_add', 'address': pc, 'cpu': 'gs'})
    r = s.call('control_execution', {'action': 'step_out', 'cpu': 'gs'})
    stop = (r.structured or {}).get('stop', {})
    expect(stop.get('reason') in ('breakpoint', 'step'), 'the step out ended', r.structured)
    if stop.get('reason') == 'breakpoint':
        expect(stop.get('cpu') == 'gs' and stop.get('cpu_pc') == pc and 'of cpu gs' in r.text,
               "the card breakpoint's stop", (r.text, stop))
    s.call('control_execution', {'action': 'bp_clear', 'cpu': 'gs'})

    r = s.call('control_execution', {'action': 'run_frame', 'cpu': 'gs'}, ok=False)
    expect(r.is_error and 'a card CPU takes step' in r.text, 'a frame run stays the machine\'s', r.text)
    r = s.call('control_execution', {'action': 'step', 'cpu': 'ms'}, ok=False)
    expect(r.is_error, 'no MultiSound card', r.text)

    # A script MCP sends acts in MCP's session: it steps the machine MCP holds, release_control leaves that session
    code = ('local r = step("gs") release_control() '
            'if r.ok then print(r.ok, r.stop.reason, r.stop.cpu) else print("refused", r.error) end')
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/lua/exec', 'body': {'code': code}}, ok=False)
    output = ((r.structured or {}).get('body') or {}).get('output', '') + r.text
    expect('true\tstep\tgs' in output or 'true step gs' in output, 'Lua steps the card', output)

    code = ('import unreal_emulator as ue\n'
            'emu = ue.emu_get_selected()\n'
            'emu.take_control("gs-step")\n'
            'r = emu.steps(2, cpu="gs")\n'
            'print("card-step", r["ok"], r["stop"]["reason"], r["cpu"])\n')
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/python/exec', 'body': {'code': code}}, ok=False)
    if 'Python automation is not available' not in r.text:
        output = ((r.structured or {}).get('body') or {}).get('output', '') + r.text
        expect('card-step True step gs' in output, 'Python steps the card', output)
    r = s.call('control_execution', {'action': 'step', 'cpu': 'gs'})
    expect((r.structured or {}).get('stop', {}).get('reason') == 'step', 'MCP still holds the machine', r.text)
    clear(s)


if __name__ == '__main__':
    raise SystemExit(main(globals()))
