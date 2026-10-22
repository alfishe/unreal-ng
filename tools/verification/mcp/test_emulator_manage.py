#!/usr/bin/env python3
"""emulator_manage: instances (list, create, destroy, status, reset), models, the server, target resolution"""

import re
import sys

from mcplib import expect, main


def test_list_names_the_instance(s):
    r = s.call('emulator_manage', {'action': 'list'})
    emulators = r.structured['emulators']
    expect(len(emulators) == 1, 'the run made one machine', r.structured)
    expect(emulators[0]['id'] in r.text and 'state=' in r.text, 'id and state in the text', r.text)


def test_status_fields_in_the_text(s):
    r = s.call('emulator_manage', {'action': 'status'})
    expect(f"model={s.model}" in r.text and 'ram_kb=' in r.text, 'model and RAM as key=value', r.text)


def test_list_models_has_the_classics(s):
    r = s.call('emulator_manage', {'action': 'list_models'})
    names = {m.get('name', '').upper() for m in r.structured.get('models', [])}
    expect({'PENTAGON', '48K', '128K'} <= names, 'Pentagon, 48K, 128K', sorted(names))


def test_server_fingerprint(s):
    r = s.call('emulator_manage', {'action': 'server'})
    expect('=' in r.text, 'the build fields in the text', r.text)


def test_reset_puts_pc_at_zero(s):
    s.call('emulator_manage', {'action': 'reset'})
    r = s.call('inspect_state', {'aspects': ['registers']})
    expect(re.search(r'PC=0x000[0-9]', r.text), 'PC near 0 after reset', r.text)


def test_switches_say_none_on_a_machine_without_them(s):
    r = s.call('emulator_manage', {'action': 'switches'})
    expect('switches' in r.text, 'the text names the switches', r.text)
    if not r.structured.get('switches'):
        expect('none' in r.text, 'none on this machine', r.text)
        bad = s.call('emulator_manage', {'action': 'switch', 'switch': 'turbo', 'on': True}, ok=False)
        expect(bad.is_error, 'no turbo switch: an error', bad.text)
    missing = s.call('emulator_manage', {'action': 'switch'}, ok=False)
    expect(missing.is_error and 'switch' in missing.text, 'switch needs its args', missing.text)


def test_unknown_action_is_an_error(s):
    r = s.call('emulator_manage', {'action': 'bogus'}, ok=False)
    expect(r.is_error and 'bogus' in r.text, 'names the action', r.text)


def test_auto_target_refuses_two_machines(s):
    second = s.call('emulator_manage', {'action': 'create', 'model': '48K'})
    second_id = (second.structured or {}).get('id') or (second.structured or {}).get('emulator_id')
    try:
        r = s.call('inspect_state', {'aspects': ['registers']}, ok=False)
        expect(r.is_error, 'target auto with two machines is an error, not a guess', r.text)
        listing = s.call('emulator_manage', {'action': 'list'})
        expect(listing.structured['count'] == 2, 'two machines', listing.text)
    finally:
        if second_id:
            s.call('emulator_manage', {'action': 'destroy', 'target': second_id})
    expect(s.call('emulator_manage', {'action': 'list'}).structured['count'] == 1, 'back to one')


if __name__ == '__main__':
    sys.exit(main(globals()))
