#!/usr/bin/env python3
"""controllers: the host-side verbs of the controller manager as an agent sees them - devices, probe, the device
database and an import (save=false: the user's devices.user.json stays as it is), role / pin / identify refusals for
an unknown device, and learn refused with the planes that offer it"""

import sys

from mcplib import expect, main

RETROARCH = '\n'.join([
    'input_device = "Verify Pad"',
    'input_vendor_id = "4660"',
    'input_product_id = "22136"',
    'input_b_btn = "1"',
    'input_a_btn = "2"',
    'input_up_btn = "h0up"',
    'input_l_x_plus_axis = "+0"',
])


def test_devices_in_text(s):
    r = s.call('controllers', {'action': 'devices'})
    expect(r.text.strip() != '', 'the devices (or "none connected") in the text', r.text)


def test_probe_names_backends(s):
    r = s.call('controllers', {'action': 'probe'})
    expect('user devices:' in r.text and 'devices.user.json' in r.text, 'the user devices path in the text', r.text)
    expect(('running' in r.text) or ('no backends' in r.text), 'each backend running or why not', r.text)


def test_database_known_and_unknown(s):
    r = s.call('controllers', {'action': 'database', 'vidpid': '045e:0b00'})
    expect('045e:0b00 is ' in r.text and 'score' in r.text and 'layout' in r.text, 'the Elite 2 match in the text',
           r.text)
    r = s.call('controllers', {'action': 'database', 'vidpid': '1234:5678'})
    expect('no family for 1234:5678' in r.text or 'unknown' in r.text, 'an unknown VID:PID said so', r.text)
    r = s.call('controllers', {'action': 'database', 'vidpid': 'zz'}, ok=False)
    expect(r.is_error, 'a malformed VID:PID is an error', r.text)


def test_database_import_not_saved(s):
    r = s.call('controllers', {'action': 'database_import', 'text': RETROARCH, 'save': False})
    expect('retroarch' in r.text, 'the source in the text', r.text)
    expect('south' in r.text and 'button.1' in r.text, 'the map in the text (RetroPad B = south)', r.text)
    expect('gamecontrollerdb: ' in r.text and '03000000341200007856' in r.text, 'the SDL line in the text', r.text)
    expect(not (r.structured or {}).get('saved', False), 'save=false writes nothing', r.structured)


def test_device_verbs_on_unknown_device(s):
    for action, extra in (('role', {'role': 'disabled'}), ('pin', {'pin': 'all'}), ('identify', {})):
        args = {'action': action, 'device': 'no:such:device'}
        args.update(extra)
        r = s.call('controllers', args, ok=False)
        expect(r.is_error and 'no:such:device' in r.text, f'{action} names the unknown device', r.text)


def test_learn_points_elsewhere(s):
    r = s.call('controllers', {'action': 'learn', 'device': 'kbd'}, ok=False)
    expect(r.is_error and 'controllers_learn' in r.text and '/api/v1/controllers/learn' in r.text,
           'learn refused with the planes that run it', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
