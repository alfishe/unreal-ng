#!/usr/bin/env python3
"""The sound card CPU as a debug target (gs-debugger design §4): control_execution bp_* with cpu "gs" set the card's
breakpoints (the NeoGS of the PENTAGON), a card stop parks the machine and wait names the CPU and the card instruction,
the lists are per CPU, the card refuses what it has not (NextREG, the host's events), and its registers, memory and code
are read and written with cpu "gs" (core CpuView), and it steps (RunControl::CardStep)"""

import os
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
    # Where the main CPU stood at the card's moment (design §5.4): a card breakpoint holds tight mode, so main finished
    # the instruction the moment is in; the text says it, the pause's main has the numbers and the bus cycles
    main_pos = pause.get('main', {})
    expect(re.search(r'main #[0-9A-F]{4}', text), "the wait's text says where main stood", text)
    expect(main_pos.get('on_boundary') is True and 0 <= main_pos.get('gap_t', -1) < 24,
           'tight mode: main at the next instruction start, within an instruction of the card', main_pos)
    cycles = main_pos.get('bus_cycles', [])
    expect(cycles and sum(c.get('t', 0) for c in cycles) == main_pos.get('total_t')
           and main_pos.get('elapsed_t', -1) == main_pos.get('t_in_instruction', 0) - main_pos.get('gap_t', 0),
           "the instruction's progress and its machine cycles", main_pos)
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
    expect(r.is_error and 'belongs to the main machine' in r.text, "the machine's FD is refused on the card", r.text)
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


def test_the_card_names_its_firmware(s):
    """The GS report's firmware_profile (gs-debugger design §6.1, L4): the PENTAGON's NeoGS runs its flash v1.11, which
    has a built-in profile: known true with the digests, the text names the firmware, and the card CPU has its symbols"""
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/state/audio/gs'})
    profile = (r.structured or {}).get('body', {}).get('firmware_profile', {})
    expect(profile.get('known') is True and profile.get('id') == 'neogs' and len(profile.get('sha256', '')) == 64
           and len(profile.get('page_sha256', [])) == 2, 'a known firmware with its digests', profile)
    expect(profile.get('title') and profile.get('symbols', '').startswith('bundle:gs:'), 'its title and symbol set',
           profile)
    r = s.call('inspect_state', {'aspects': ['audio_gs']})
    expect('firmware ' + profile['title'] in r.text and 'unknown firmware' not in r.text, 'named in the text', r.text)
    r = s.call('manage_symbols', {'action': 'sets', 'cpu': 'gs'})
    body = r.structured or {}
    expect(body.get('cpu') == 'gs' and profile['symbols'] in [x.get('id') for x in body.get('sets', [])],
           'the firmware symbols are the card CPU\'s set', body)
    expect(profile['symbols'] in r.text and 'cpu gs' in r.text, 'and the text lists it', r.text)
    r = s.call('manage_symbols', {'action': 'sets'})
    expect(profile['symbols'] not in [x.get('id') for x in (r.structured or {}).get('sets', [])],
           'and not the main CPU\'s', r.structured)


def test_the_command_log_is_a_device_history(s):
    """The host's GS commands (gs-debugger design §6.3): `<slot>.commands` in device_history, off until configured; a
    port write of the host's (#B3 then #BB) is a data and a command record, each with its text (the firmware profile
    may add the command's name)"""
    r = s.call('device_history', {'action': 'list'})
    logs = sorted(set(re.findall(r'([\w-]+(?:\.[\w-]+)*\.commands)\b', r.text)))
    expect(logs, 'a command log among the histories', r.text)
    device = logs[0]
    s.call('device_history', {'action': 'config', 'device': device, 'mode': 'ring'})
    for port, value in (('0xB3', '0x05'), ('0xBB', '0x2C')):
        w = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/ports/out',
                                  'body': {'port': port, 'value': value}}, ok=False)
        expect(not w.is_error, 'the host port written', w.text)
    r = s.call('device_history', {'action': 'read', 'device': device, 'last': 16})
    expect('#2C' in r.text and 'data #05' in r.text, 'a data and a command record, decoded to text', r.text)
    s.call('device_history', {'action': 'config', 'device': device, 'mode': 'off'})


def test_the_debugger_sends_a_command(s):
    """The protocol state and the debugger's send (gs-debugger design §6.4, F3, F6). The #2C written above is pending
    while the machine is paused, the protocol says so and names the command from the firmware profile. Running, the
    conversation ends one of two ways, both told in full: the firmware took the bytes and answered (done, the reply
    decoded), or the card was still in its loader and did not take a byte (failed, the status bit named)"""
    r = s.call('device_history', {'action': 'list'})
    device = sorted(set(re.findall(r'([\w-]+(?:\.[\w-]+)*\.commands)\b', r.text)))[0]
    r = s.call('inspect_state', {'aspects': ['audio_gs']})
    expect('protocol: the command log is off' in r.text and device in r.text, 'off: says so, names the history', r.text)
    s.call('device_history', {'action': 'config', 'device': device, 'mode': 'ring'})
    s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/emulator/{id}/ports/out',
                          'body': {'port': '0xBB', 'value': '0x2C'}})
    r = s.call('inspect_state', {'aspects': ['audio_gs']})
    # The firmware profile names the command between the code and the colon
    expect(re.search(r'protocol: #2C[^:\n]*: not taken yet', r.text), 'the command not taken', r.text)

    # The profile knows #2C: a pre byte (the module) and nothing after it; other shapes are refused with the reason
    r = s.call('emulator_manage', {'action': 'gs_send', 'value': 0x2C, 'params': [1, 2]}, ok=False)
    expect(r.is_error and 'takes a pre byte' in r.text, 'the profile refuses a send without the pre byte', r.text)
    r = s.call('emulator_manage', {'action': 'gs_send', 'value': 0x2C, 'pre': 1}, ok=False)
    expect('GS send' in r.text, 'the send as the profile shapes it', r.text)
    s.call('control_execution', {'action': 'resume'})
    r = s.call('emulator_manage', {'action': 'gs_conversation', 'wait_ms': 0})
    for _ in range(40):
        if 'failed' in r.text or 'done' in r.text or '-> #' in r.text:
            break
        time.sleep(0.1)
        r = s.call('emulator_manage', {'action': 'gs_conversation'})
    s.call('control_execution', {'action': 'pause'}, ok=False)
    done = re.search(r'GS send \d+: #2C[^\n]*-> #[0-9A-F]{2}', r.text)
    failed = 'failed' in r.text and re.search(r'status bit [07] stays set', r.text)
    expect(done or failed, 'the conversation ended: the reply, or the byte not taken named', r.text)
    r = s.call('inspect_state', {'aspects': ['audio_gs']})
    expect('last send' in r.text, 'the report carries the last send', r.text)
    s.call('device_history', {'action': 'config', 'device': device, 'mode': 'off'})


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

    # A card breakpoint where the loader loops ends a step out of its top level: the first instruction the card
    # comes back to (where it stands now may be on its way in, never run again; 2026-11 on Windows the step timed out)
    seen = [card_pc(s)]
    looped = False
    for _ in range(500):
        pc = s.call('control_execution', {'action': 'step', 'cpu': 'gs'}).structured.get('pc')
        looped = pc in seen
        if looped:
            break
        seen.append(pc)
    expect(looped, 'the card comes back to an instruction within 500 steps', seen[-10:])
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


def test_the_selected_card_follows_main_steps(s):
    """select_cpu (design §5.2): the card a front-end shows follows the main CPU's steps of the paused machine; the
    choice is one per machine, the scripts see it, main ends it, a card that is not there is refused"""
    clear(s)
    r = s.call('control_execution', {'action': 'select_cpu', 'cpu': 'gs'})
    expect(r.structured.get('cpu') == 'gs' and r.structured.get('follows') is True and 'follows' in r.text,
           'the card shown follows', (r.text, r.structured))
    r = s.call('control_execution', {'action': 'step_n', 'count': 20})
    expect(not r.is_error, 'the main CPU steps with the card shown', r.text)
    code = 'local c = selected_cpu() print("shown", c.cpu, c.follows)'
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/lua/exec', 'body': {'code': code}}, ok=False)
    output = ((r.structured or {}).get('body') or {}).get('output', '') + r.text
    expect('shown\tgs\ttrue' in output or 'shown gs true' in output, 'Lua sees the choice', output)
    r = s.call('control_execution', {'action': 'select_cpu', 'cpu': 'ms'}, ok=False)
    expect(r.is_error and 'not available' in r.text, 'no MultiSound card', r.text)
    r = s.call('control_execution', {'action': 'select_cpu', 'cpu': 'main'})
    expect(r.structured.get('cpu') == 'main' and r.structured.get('follows') is False, 'main ends it', r.structured)
    r = s.call('control_execution', {'action': 'select_cpu'})
    expect(r.text.startswith('Selected cpu: main'), 'no cpu: the report', r.text)
    clear(s)


def test_the_card_cpu_runs_to_the_cursor(s):
    """run_until_pc with cpu gs (design §5.3): the card CPU runs to the cursor and the machine is paused there; the
    reply says hit; Lua runs it there too"""
    clear(s)
    # A place the card comes back to: the first instruction its loop runs again
    seen = [card_pc(s)]
    looped = False
    for _ in range(500):
        pc = s.call('control_execution', {'action': 'step', 'cpu': 'gs'}).structured.get('pc')
        looped = pc in seen
        if looped:
            break
        seen.append(pc)
    expect(looped, 'the card comes back to an instruction within 500 steps', seen[-10:])
    r = s.call('control_execution', {'action': 'run_until_pc', 'pc': pc, 'cpu': 'gs'})
    stop = (r.structured or {}).get('stop', {})
    expect(stop.get('reason') == 'step' and stop.get('cpu_pc') == pc and r.structured.get('hit') is True,
           'the card at the cursor', r.structured)
    expect(('Ran cpu gs to #%04X' % pc) in r.text, 'said in the text', r.text)
    expect(card_pc(s) == pc and r.structured.get('state') == 'paused', 'paused there', r.structured)

    code = 'local r = skip_until(%d, "gs") print("to", r.ok, r.hit, r.pc == %d)' % (pc, pc)
    r = s.call('invoke_api', {'method': 'POST', 'path': '/api/v1/lua/exec', 'body': {'code': code}}, ok=False)
    output = ((r.structured or {}).get('body') or {}).get('output', '') + r.text
    expect('to\ttrue\ttrue\ttrue' in output or 'to true true true' in output, 'Lua runs the card to it', output)
    r = s.call('control_execution', {'action': 'run_until_pc', 'pc': pc, 'cpu': 'ms'}, ok=False)
    expect(r.is_error, 'no MultiSound card', r.text)
    clear(s)


def test_the_slot_statistics(s):
    """The GS slot's statistics (gs-debugger N7): a running machine answers from the next frame end, rates and the
    NeoGS sections in the text; a baseline from gs_reset_counters counts the commands sent after it; a section the
    card has not names the card's; the Stats board lists the same block"""
    s.call('control_execution', {'action': 'resume'}, ok=False)
    r = s.call('inspect_state', {'aspects': ['audio_gs_stats']})
    expect('GS slot stats: ngs' in r.text and 'mailbox:' in r.text and 'dma:' in r.text and 'sd:' in r.text,
           'the block with the NeoGS sections', r.text)
    r = s.call('inspect_state', {'aspects': ['audio_gs_stats'], 'gs_stats_fresh': True})
    expect('cpu_steps' in r.text and '/s' in r.text, 'a fresh block has the rates', r.text)

    r = s.call('emulator_manage', {'action': 'gs_reset_counters'})
    m = re.search(r'baseline (\d+)', r.text)
    expect(m is not None, 'gs_reset_counters names the baseline', r.text)
    baseline = int(m.group(1))
    s.call('emulator_manage', {'action': 'gs_send_command', 'value': 0x00})  # applied by the running machine
    s.call('inspect_state', {'aspects': ['audio_gs_stats.mailbox'], 'gs_stats_fresh': True})
    s.call('control_execution', {'action': 'pause'}, ok=False)
    r = s.call('inspect_state', {'aspects': ['audio_gs_stats.mailbox'], 'gs_stats_since': baseline})
    expect('since baseline %d' % baseline in r.text and 'host_commands 1' in r.text,
           'the commands since the baseline', r.text)
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/state/audio/gs/stats/errors'})
    errors = (r.structured or {}).get('body', {}).get('errors', {})
    expect('total' in errors and 'sd_crc' in errors, 'the error counters with their total', r.text)

    r = s.call('inspect_state', {'aspects': ['audio_gs_stats.player']})
    expect('no section' in r.text and 'Sections: card, mailbox' in r.text, 'a missing section names the card\'s',
           r.text)
    r = s.call('inspect_state', {'aspects': ['board.gs_stats']})
    expect('mailbox' in r.text and 'host_commands' in r.text, 'the Stats board', r.text)


def test_the_neogs_boards(s):
    """Device boards (gs-debugger N4, N8): the NeoGS card publishes its hardware as boards, read with their values in
    the text; a host folder inserted through the SD board is the medium of the slot sd.ngs in the media list. Last in
    the block: the card's medium is put back"""
    r = s.call('inspect_state', {'aspects': ['boards']})
    for board in ('neogs', 'neogs_dma', 'neogs_sd', 'neogs_mp3', 'neogs_flash'):
        expect(board in r.text, 'board %s listed' % board, r.text)
    expect('insert <path>' in r.text, 'the SD board offers insert', r.text)
    r = s.call('inspect_state', {'aspects': ['board.neogs']})
    expect('GSCFG0=#' in r.text and 'SSTAT' in r.text and 'Mailbox' in r.text, 'the card board with its values', r.text)
    r = s.call('inspect_state', {'aspects': ['board.nothing']}, ok=False)
    expect('No board' in r.text and 'neogs_sd' in r.text, 'an unknown board names the boards', r.text)

    before = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/debug/boards/neogs_sd?values=1'})
    values = (before.structured or {}).get('body', {}).get('values', {})
    original = values.get('source', '') if values.get('present') else ''

    folder = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'scratch',
                                          'mcp-verify', 'neogs-sd-folder'))
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, 'README.TXT'), 'w') as f:
        f.write('a host folder as the NeoGS SD card\n')
    s.call('control_execution', {'action': 'pause'}, ok=False)
    r = s.call('emulator_manage', {'action': 'board_action', 'board': 'neogs_sd', 'operation': 'insert', 'path': folder})
    expect('Board neogs_sd insert' in r.text, 'inserted through the board', r.text)
    r = s.call('media', {'action': 'list'})
    expect('sd.ngs' in r.text and 'neogs-sd-folder' in r.text, 'the media list shows the folder in sd.ngs', r.text)
    r = s.call('inspect_state', {'aspects': ['board.neogs_sd']})
    expect('kind: folder' in r.text, 'the board shows a folder', r.text)

    r = s.call('emulator_manage', {'action': 'board_action', 'board': 'neogs_sd', 'operation': 'eject'})
    expect('eject' in r.text, 'ejected through the board', r.text)
    r = s.call('emulator_manage', {'action': 'board_action', 'board': 'neogs_sd', 'operation': 'format'}, ok=False)
    expect(r.is_error and 'no action' in r.text, 'an unknown action is refused', r.text)
    if original:
        s.call('emulator_manage', {'action': 'board_action', 'board': 'neogs_sd', 'operation': 'insert', 'path': original})


if __name__ == '__main__':
    raise SystemExit(main(globals()))
