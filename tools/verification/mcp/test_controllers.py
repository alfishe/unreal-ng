#!/usr/bin/env python3
"""controllers: the host-side verbs of the controller manager as an agent sees them - devices, probe, the device
database and an import (save=false: the user's devices.user.json stays as it is), role / pin / identify refusals for
an unknown device, learn refused with the planes that offer it, and the session haptics trial (events laid over the
loaded game in memory, seen by game.events, cleared)"""

import sys
import time

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


def test_haptics_trial_fires_and_clears(s):
    r = s.call('controllers', {'action': 'haptics_trial'})
    expect('no haptics trial' in r.text, 'no trial at the start', r.text)
    # The IM 1 handler runs once a frame on a running Pentagon: an event that fires in every frame
    trial = 'events:\n  irq: {probe: exec, address: "0x0038"}\nhaptics:\n  rules:\n    - {on: irq, play: tick}\n'
    r = s.call('controllers', {'action': 'haptics_trial', 'text': trial})
    expect('haptics trial over' in r.text and '1 sources' in r.text, 'the trial in the text', r.text)
    r = s.call('controllers', {'action': 'context'})
    expect('session trial' in r.text, 'the haptics reason names the trial', r.text)
    # game.events counts live frames (run_frames steps a paused machine): run it until the event has fired
    s.call('control_execution', {'action': 'resume'})
    fired = None
    for _ in range(40):
        r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/monitor/game.events'})
        fired = (((r.structured or {}).get('body') or {}).get('events') or {}).get('irq', {}).get('fires', 0)
        if fired:
            break
        time.sleep(0.05)
    s.call('control_execution', {'action': 'pause'})
    expect(fired, 'game.events counts the trial event', r.text)
    r = s.call('controllers', {'action': 'haptics_trial', 'text': 'haptics: {rules: [{on: nobody, play: hit}]}'},
               ok=False)
    expect(r.is_error and 'nobody' in r.text, 'a rule on an unknown source refused with the reason', r.text)
    r = s.call('controllers', {'action': 'haptics_trial', 'clear': True})
    expect('no haptics trial' in r.text, 'clear drops it', r.text)
    r = s.call('controllers', {'action': 'context'})
    expect('session trial' not in r.text, 'the haptics reason without the trial', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
