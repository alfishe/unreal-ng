#!/usr/bin/env python3
"""The MGT +D as an agent drives it: an .mgt on a machine without the card is refused with the models that have it;
on 48K-PLUSD / 128K-PLUSD load_software autostart answers the plan and its BASIC in the text (the steps go on after
the answer), `basic` picks the BASIC of the reset (128 refused on a 48K), the fdc report carries the card and the
autostart's progress, and REST and the WebSocket call take `basic` the same way. The plans themselves are asserted by
the core tests (plusdautostart_test.cpp); this checks the live surface."""

import atexit
import json
import os
import shutil
import sys
import tempfile

from mcplib import expect, main
from test_actions import release_mcp_session, ws_calls
from test_surfaces import end_session, rest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
SYSTEM_DISK = os.path.join(ROOT, 'data', 'boot', 'plusd', 'gdos-2a-system.mgt')


def emulator_id(s):
    return s.call('emulator_manage', {'action': 'list'}).structured['emulators'][0]['id']


_FOLDER = tempfile.mkdtemp(prefix='plusd-mcp-', dir=os.path.join(ROOT, 'scratch') if os.path.isdir(
    os.path.join(ROOT, 'scratch')) else None)
atexit.register(shutil.rmtree, _FOLDER, True)
_COPIES = []


def work_disk():
    """A fresh copy of the system disk (+SYS, CONFIG, no AUTO: the run+load plan); the autostart may write to it"""
    _COPIES.append(os.path.join(_FOLDER, f'work{len(_COPIES)}.mgt'))
    shutil.copyfile(SYSTEM_DISK, _COPIES[-1])
    return _COPIES[-1]


def test_an_mgt_without_the_card_is_refused(s):
    s.fresh_machine('PENTAGON')
    r = s.call('load_software', {'path': work_disk(), 'autostart': True}, ok=False)
    expect(r.is_error, 'an .mgt on a Pentagon is an error', r.text)
    expect('48K-PLUSD' in r.text, 'the error names the models with the card', r.text)


def test_the_plan_and_its_basic_are_in_the_text(s):
    s.fresh_machine('128K-PLUSD')
    r = s.call('load_software', {'path': work_disk(), 'autostart': True, 'basic': '48'})
    expect('autostarted' in r.text, 'the text says the autostart ran', r.text)
    expect('plan run+load, 48 BASIC' in r.text, 'the plan and the chosen BASIC in the text', r.text)
    expect('inspect_state fdc' in r.text, 'the text says where the progress is', r.text)
    st = r.structured or {}
    expect(st.get('plan') == 'run+load' and st.get('basic') == '48' and st.get('dos') == 'disk',
           'structuredContent has plan, basic, dos', st)

    fdc = s.call('inspect_state', {'aspects': ['fdc']})
    expect('WD1772 (MGT +D)' in fdc.text, 'the fdc report is the card', fdc.text[:2000])
    expect('autostart run+load, 48 BASIC' in fdc.text, 'the report carries the autostart with its BASIC',
           fdc.text[:3000])
    # The steps run with the machine: the reset is the first one
    s.call('control_execution', {'action': 'run_frames', 'frames': 50})
    fdc = s.call('inspect_state', {'aspects': ['fdc']})
    expect('reset' in fdc.text, 'the reset step is logged once the machine runs', fdc.text[:3000])
    s.call('control_execution', {'action': 'pause'}, ok=False)


def test_128_basic_on_a_48k_is_refused(s):
    s.fresh_machine('48K-PLUSD')
    r = s.call('load_software', {'path': work_disk(), 'autostart': True, 'basic': '128'}, ok=False)
    expect(r.is_error, '128 BASIC on a 48K is an error', r.text)
    expect('128K-PLUSD' in r.text, 'the error names the model to use', r.text)
    r = s.call('load_software', {'path': work_disk(), 'autostart': True, 'basic': '64'}, ok=False)
    expect(r.is_error and '48 or 128' in r.text, 'an unknown BASIC is refused by name', r.text)


def test_rest_and_the_socket_take_the_basic(s):
    s.fresh_machine('48K-PLUSD')
    eid = emulator_id(s)
    release_mcp_session(s)  # the MCP calls above hold an implicit session: REST would be refused with 409
    status, body, session = rest(s, 'POST', f'/api/v1/emulator/{eid}/disk/A/insert',
                                 {'path': work_disk(), 'autostart': True, 'basic': 48})
    try:
        expect(status == 200 and body.get('plan') == 'run+load' and body.get('basic') == '48',
               'REST: a number is taken as the text', (status, body))
        reply = ws_calls(s, [('POST', f'/api/v1/emulator/{eid}/disk/A/insert',
                              {'path': work_disk(), 'autostart': True, 'basic': '128'}, session)])[1]
        expect(reply['status'] == 400 and '128K-PLUSD' in json.dumps(reply['body']),
               'the socket call refuses 128 BASIC on a 48K as REST does', reply)
    finally:
        end_session(s, eid, session)


if __name__ == '__main__':
    sys.exit(main(globals()))
