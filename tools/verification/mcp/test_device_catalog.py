#!/usr/bin/env python3
"""The device catalog (GET /state/devices) over the machines and their cards: for every model of the build and every
card that fits it alone (the slot catalog says which), the catalog lists what the machine has, every listed report
answers on its route, every absent one says why, and every plane answers the same - REST, a WebSocket call, CLI
`state devices --json`, MCP `inspect_state devices`, Lua and Python `devices_state()`.

The expectation comes from outside the catalog: the slot report names the cards, the model names the platform.
Quick tier: a model of each family with its cards; UNREAL_LONG_TESTS=1: every model with every card."""

import json
import os
import re
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from mcplib import WebSocket, expect, main

# A model of each family: the quick tier's machines
QUICK_MODELS = ('48K', '128K', 'PENTAGON', 'PLUS3', 'SCORPION', 'TSL-VDAC2', 'SPRINTER', 'NEXT', 'PROFI')

# The platform reports and the models that have them (by name)
PLATFORM = {
    'tsconf': lambda m: m.startswith('TS'),
    'sprinter': lambda m: m == 'SPRINTER',
    'isa': lambda m: m == 'SPRINTER',
    'next': lambda m: m.startswith('NEXT'),
    'next-audio': lambda m: m.startswith('NEXT'),
    'next-dma': lambda m: m.startswith('NEXT'),
    'next-uart': lambda m: m.startswith('NEXT'),
    'next-divmmc': lambda m: m.startswith('NEXT'),
    'next-sd': lambda m: m.startswith('NEXT'),
    'profi': lambda m: m.startswith('PROFI'),
}

# The cards whose report the catalog names
GS_CARDS = {'gs', 'gs-lw', 'neogs'}

LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'scratch', 'device-catalog')


def long_tier():
    return os.environ.get('UNREAL_LONG_TESTS') == '1'


def webapi(s):
    port = urllib.parse.urlparse(s.url).port - 200
    return f'http://127.0.0.1:{port}'


def http(s, method, path, body=None):
    """(status, JSON body) of a WebAPI request"""
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(webapi(s) + path, data=data, method=method,
                                     headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return response.status, json.loads(response.read().decode() or 'null')
    except urllib.error.HTTPError as error:
        raw = error.read().decode()
        try:
            return error.code, json.loads(raw)
        except ValueError:
            return error.code, {'raw': raw}


def destroy_all(s):
    status, listing = http(s, 'GET', '/api/v1/emulator')
    for emulator in (listing or {}).get('emulators', []):
        http(s, 'DELETE', f"/api/v1/emulator/{emulator['id']}")


def machine(s, model, slots=None):
    """The id of the only machine: @p model with @p slots (the [SLOTS] form, replacing the configuration's set);
    None when the machine is refused"""
    destroy_all(s)
    body = {'model': model}
    if slots is not None:
        body['slots'] = slots
    status, reply = http(s, 'POST', '/api/v1/emulator/create', body)
    if status not in (200, 201) or 'id' not in (reply or {}):
        return None
    emu = reply['id']
    http(s, 'PUT', f'/api/v1/emulator/{emu}/audio/mixer/master', {'muted': True})
    return emu


def cli(s, emu, command):
    """The CLI's answer to @p command on the machine @p emu"""
    port = urllib.parse.urlparse(s.url).port - 100
    with socket.create_connection(('127.0.0.1', port), timeout=30) as sock:
        def read_until_prompt():
            data = b''
            deadline = time.time() + 30
            while not re.search(rb'\n> (\x1b\[K)?$', data) and time.time() < deadline:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                data += chunk
            return data.decode(errors='replace')
        read_until_prompt()
        sock.sendall(f'select {emu}\n'.encode())
        read_until_prompt()
        sock.sendall((command + '\n').encode())
        answer = re.sub(r'\n> (\x1b\[K)?$', '', read_until_prompt()).replace('\r', '')
        return answer.split('\n', 1)[1] if answer.startswith(command) else answer  # the echoed command


def ws_call(s, path):
    port = urllib.parse.urlparse(s.url).port - 200
    ws = WebSocket(port)
    try:
        ws.send({'op': 'call', 'rid': 1, 'method': 'GET', 'path': path})
        message = ws.until(lambda m: m.get('op') == 'result' and m.get('rid') == 1, f'the call of {path}', timeout=30)
        return message.get('body')
    finally:
        ws.close()


def interpreter(s, language, code):
    """The printed output of @p code, None when this build has no such interpreter"""
    status, reply = http(s, 'POST', f'/api/v1/{language}/exec', {'code': code})
    if status != 200 or not isinstance(reply, dict):
        return None
    return reply.get('output', '')


def cards_of(slots_report):
    """slot -> card of the machine's slot report: the working ones (a card that would need an adapter is listed in its
    slot, disabled)"""
    return {slot['slot']: slot['card'] for slot in slots_report.get('slots', [])
            if slot.get('card') and slot.get('state') != 'disabled'}


def expected(model, cards):
    """(must be listed, must be absent) by the model and the cards, independent of the catalog"""
    present, absent = {'execution', 'screen', 'slots'}, set()
    for report, has in PLATFORM.items():
        (present if has(model.upper()) else absent).add(report)
    fitted = set(cards.values())
    if fitted & GS_CARDS:
        present.add('gs')
    elif 'multisound' not in fitted:
        absent.add('gs')
    if cards.get('ay-socket') == 'tsfm':
        present |= {'ay', 'fm'}
    elif 'multisound' not in fitted:
        absent.add('fm')
    if cards.get('ay-socket') in ('ts', 'ay'):
        present.add('ay')
    (present if 'moonsound' in fitted else absent).add('moonsound')
    (present if 'multisound' in fitted else absent).add('multisound')
    return present, absent


def check_machine(s, model, slots, label, log):
    emu = machine(s, model, slots)
    if emu is None:
        log.append(f'{label}: refused')
        return 0
    base = f'/api/v1/emulator/{emu}'
    status, catalog = http(s, 'GET', base + '/state/devices')
    expect(status == 200 and catalog.get('available'), f'{label}: the catalog answers', catalog)
    listed = [r['id'] for r in catalog['reports']]
    status, everything = http(s, 'GET', base + '/state/devices?all=1')
    expect(status == 200, f'{label}: the all form answers', everything)
    present_all = [r['id'] for r in everything['reports'] if r['available']]
    expect(present_all == listed, f'{label}: the all form marks the listed ones present', (listed, present_all))
    log.append(f'{label}: ' + ', '.join(f"{r['id']} ({r['summary']})" if r.get('summary') else r['id']
                                        for r in catalog['reports']))

    # Every listed report answers on its route; every absent one says why and its route says so too
    for r in everything['reports']:
        status, report = http(s, 'GET', f"{base}/{r['route']}")
        if r['available']:
            expect(status == 200 and report.get('available', True), f"{label}: {r['id']} answers on {r['route']}",
                   (status, str(report)[:300]))
            for key in ('title', 'route', 'cli', 'script', 'mcp'):
                expect(r.get(key), f"{label}: {r['id']} has {key}", r)
        else:
            expect(r.get('description'), f"{label}: absent {r['id']} says why", r)
            # Absent by the report (404 / available false) or by the catalog's own test of the report (an AY report
            # without a chip, a network report without a device: "not on this machine")
            gone = status == 404 or not report.get('available', True) or r['description'] == 'not on this machine'
            expect(gone, f"{label}: absent {r['id']} is absent on {r['route']} too", (status, str(report)[:300]))

    # The expectation by the slot report and the model
    status, slots_report = http(s, 'GET', base + '/slots')
    cards = cards_of(slots_report)
    must, must_not = expected(model, cards)
    expect(must <= set(listed), f'{label}: lists {sorted(must - set(listed))} (cards {cards})', listed)
    expect(not (must_not & set(listed)), f'{label}: does not list {sorted(must_not & set(listed))} (cards {cards})',
           listed)

    # Every plane: the same catalog
    expect(ws_call(s, base + '/state/devices') == catalog, f'{label}: a WebSocket call answers as REST')
    text = cli(s, emu, 'state devices --json')
    expect(json.loads(text) == catalog, f'{label}: CLI --json answers as REST', text[:400])
    text = cli(s, emu, 'state devices')
    for r in catalog['reports']:
        expect(re.search(rf"^{r['id']}\s+{re.escape(r['title'])}", text, re.M), f"{label}: CLI names {r['id']}", text)
    r = s.call('inspect_state', {'aspects': ['devices'], 'target': emu})
    expect(r.structured['devices'] == catalog, f'{label}: MCP structured answers as REST')
    expect(f'[devices] {len(listed)} reports' in r.text, f'{label}: MCP text counts', r.text)
    for report in catalog['reports']:
        expect(f"{report['id']}: {report['title']}" in r.text, f"{label}: MCP names {report['id']}", r.text)
    out = interpreter(s, 'python', 'import json, unreal_emulator as ue\n'
                                   'print(json.dumps(ue.emu_get_selected().devices_state()))\n')
    if out is not None:
        expect(json.loads(out.strip().splitlines()[-1]) == catalog, f'{label}: Python answers as REST', out[:400])
    out = interpreter(s, 'lua', 'for _, r in ipairs(devices_state().reports) do print(r.id) end')
    if out is not None:
        expect(out.split() == listed, f'{label}: Lua lists the same ids', (out, listed))
    return 1


def models(s):
    status, reply = http(s, 'GET', '/api/v1/emulator/models')
    expect(status == 200, 'the models', reply)
    # A model without a configuration of its own (creatable false) cannot be made: none to check
    names = [m['name'] for m in reply.get('models', []) if m.get('creatable', True)]
    return names if long_tier() else [m for m in names if m.upper() in QUICK_MODELS]


def fitting_cards(s, model):
    """(slot, card) of every card the slot catalog fits on @p model alone"""
    emu = machine(s, model)
    if emu is None:
        return []
    status, catalog = http(s, 'GET', f'/api/v1/emulator/{emu}/slots/catalog')
    fits = []
    for card in (catalog or {}).get('cards', []):
        here = card.get('thisMachine', {})
        if here.get('outcome') in ('fits', 'needs-replace') and here.get('slot'):
            fits.append((here['slot'], card['id']))
    return fits


def setup(s):
    pass  # every test makes its machines


def test_every_model_and_card(s):
    os.makedirs(LOG, exist_ok=True)
    log, checked = [], 0
    chosen = models(s)
    expect(chosen, 'models to check', chosen)
    for model in chosen:
        checked += check_machine(s, model, None, f'{model} (its configuration)', log)
        checked += check_machine(s, model, {}, f'{model} (no cards)', log)
        for slot, card in fitting_cards(s, model):
            checked += check_machine(s, model, {slot: card}, f'{model} + {slot} = {card}', log)
    with open(os.path.join(LOG, 'catalogs.txt'), 'w') as f:
        f.write('\n'.join(log) + '\n')
    print(f'        {checked} machines checked (scratch/device-catalog/catalogs.txt)')
    expect(checked >= 20, 'a matrix, not a sample', checked)
    destroy_all(s)


if __name__ == '__main__':
    sys.exit(main(globals()))
