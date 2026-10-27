#!/usr/bin/env python3
"""The quality of the emulator's Z80 disassembler as the agents see it: over the WebAPI (GET /disasm) and over MCP
(debug_code disassemble, inspect_state disasm).

    disasmcheck.py [--port 8090] [--mcp-port 8092] [--target ID] [--model PENTAGON]
                   [--z88dk-dis PATH] [--sjasmplus PATH] [--out DIR] [--verbose]

Every opcode of the Z80 (unprefixed, CB, ED, DD, FD, DD CB, FD CB: 1792 encodings, undocumented ones included) is
written into RAM at #8000, one per 8-byte slot, followed by fixed operand bytes (#34 #12: n = #34, nn = #1234,
d = +#34, e = +#34). Each slot is disassembled through the WebAPI and checked:

  length   the size equals z88dk-dis' (an independent disassembler)                              hard
  text     the mnemonic equals z88dk-dis' after spelling is normalized (#34 / $34 / 34h, spaces,
           case); known spelling synonyms are listed in SYNONYMS                                  hard
  target   jr / djnz / jp / call / rst carry the 'target' field with the right address           hard
  syntax   the mnemonic is plain assembler text (no '??', no runtime notes)                       hard
  reasm    sjasmplus assembles the mnemonic back to the same bytes                               hard for documented
           opcodes; undocumented forms sjasmplus does not take are counted and listed
  rom      a linear sweep of the 48K ROM with its symbols matches z88dk-dis (boundaries, text with
           'LABEL (#addr)' read as the address, bytes = memory)                                  hard
  wire     the WebAPI rejects a bad address / count with 400 (it used to fall back to PC);
           '$', '#', 'h' address spellings work; count 256 gives 256 lines                       hard
  mcp      debug_code disassemble carries the listing in content[0].text (one line per
           instruction), inspect_state disasm too; response_format "json" echoes the structured
           result in content[] for clients that pass only the text to the model                  hard when --mcp-port
                                                                                                  answers

The emulator's own instance (it creates one unless --target): run the build under test on ports of its own when other
sessions use the defaults, e.g.
    PORT=8095
    UNREAL_WEBAPI_PORT=$PORT UNREAL_CLI_PORT=$((PORT+100)) UNREAL_MCP_PORT=$((PORT+200)) UNREAL_GDB_PORT=$((PORT+300)) \\
    UNREAL_DEZOG_PORT=$((PORT+400)) UNREAL_ZRCP_PORT=$((PORT+500)) <build>/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
    tools/verification/disasm/disasmcheck.py --port $PORT --mcp-port $((PORT+200))

z88dk-dis: --z88dk-dis or UNREAL_Z88DK_DIS; sjasmplus: --sjasmplus or UNREAL_ASM_SJASMPLUS (the reasm check is skipped
without it). Exit code 0 = every hard check passed.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

BASE = 0x8000
SLOT = 8
OPERANDS = [0x34, 0x12, 0x34, 0x12]   # n = #34, nn = #1234, d = e = +#34; DDCB d op: d = #34, op from the table

# Spelling differences that are not errors: (emulator regex, z88dk-dis regex) on the canonical text. Both sides are
# first brought to one spelling by canonical() (hx = ixh, the accumulator of add / adc / sbc, the DD CB register copy)
SYNONYMS = [
    (r'^reti$', r'^retn$'),          # ED 5D / 6D / 7D: undocumented mirrors; tables name them either (both copy IFF2)
    (r'^im 0$', r'^im 0/1$'),        # ED 4E / 6E: the undefined mode, IM 0 in practice
    (r'^nop$', r'^(ld i,i|ld r,r|trap)$'),   # ED 77 / 7F / FE: eZ80 / z88dk's own names; a Z80 executes NOP
]


def canonical(text):
    """One spelling for the forms disassemblers write differently"""
    text = re.sub(r'\s*\*$', '', text)   # the emulator's undocumented-opcode mark
    text = re.sub(r'\b([hl])([xy])\b', r'i\2\1', text)   # hx -> ixh, ly -> iyl
    text = re.sub(r'^(add|adc|sbc) ([^,]+)$', r'\1 a,\2', text)   # z88dk drops the accumulator
    text = re.sub(r'^ld ([abcdehl]),((?:rlc|rrc|rl|rr|sla|sra|sll|srl|res \d|set \d)[ ,]\(i[xy][+-]\d+\))$', r'\2,\1',
                  text)   # DD CB d op with a register copy: "ld b,rlc (ix+5)" = "rlc (ix+5),b"
    return text


def request(method, url, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json'}, method=method)
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return resp.status, json.load(resp)
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.load(e)
        except ValueError:
            return e.code, None


def encodings():
    """(bytes, documented) for every Z80 opcode, operands appended"""
    out = []
    for op in range(256):
        if op in (0xCB, 0xDD, 0xED, 0xFD):
            continue
        out.append((bytes([op] + OPERANDS[:3]), True))
    for op in range(256):
        documented = not 0x30 <= op <= 0x37   # SLL
        out.append((bytes([0xCB, op] + OPERANDS[:2]), documented))
    for op in range(256):
        documented = op in ED_DOCUMENTED
        out.append((bytes([0xED, op] + OPERANDS[:2]), documented))
    for prefix in (0xDD, 0xFD):
        for op in range(256):
            if op in (0xCB, 0xDD, 0xED, 0xFD):
                continue
            documented = op in INDEX_DOCUMENTED
            out.append((bytes([prefix, op] + OPERANDS[:3]), documented))
        for op in range(256):
            # DD CB d op: only op & 7 == 6 (and BIT's x6) are documented
            out.append((bytes([prefix, 0xCB, 0x34, op]), (op & 7) == 6))
    return out


ED_DOCUMENTED = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4D, 0x4F, 0x50, 0x51, 0x52,
                 0x53, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x67, 0x68, 0x69, 0x6A, 0x6F,
                 0x72, 0x73, 0x78, 0x79, 0x7A, 0x7B, 0xA0, 0xA1, 0xA2, 0xA3, 0xA8, 0xA9, 0xAA, 0xAB, 0xB0, 0xB1, 0xB2,
                 0xB3, 0xB8, 0xB9, 0xBA, 0xBB}
INDEX_DOCUMENTED = {0x09, 0x19, 0x21, 0x22, 0x23, 0x29, 0x2A, 0x2B, 0x34, 0x35, 0x36, 0x39, 0x46, 0x4E, 0x56, 0x5E, 0x66,
                    0x6E, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x77, 0x7E, 0x86, 0x8E, 0x96, 0x9E, 0xA6, 0xAE, 0xB6, 0xBE,
                    0xE1, 0xE3, 0xE5, 0xE9, 0xF9}


def normalize(text):
    """Spelling-neutral form: lower case, single spaces, no space after commas, every number as decimal"""
    text = text.strip().lower()
    text = re.sub(r'\s*;.*$', '', text)
    text = re.sub(r'\s+', ' ', text)
    text = re.sub(r'\s*,\s*', ',', text)
    text = re.sub(r'\s*([+-])\s*', r'\1', text)
    text = re.sub(r'(?:#|\$|0x)([0-9a-f]+)\b', lambda m: str(int(m.group(1), 16)), text)
    text = re.sub(r'\b([0-9][0-9a-f]*)h\b', lambda m: str(int(m.group(1), 16)), text)
    return text


def synonym(ours, theirs):
    """True when the two spellings are a SYNONYMS pair (b may refer to a's groups as \\1)"""
    for a, b in SYNONYMS:
        m = re.match(a, ours)
        if m and re.match(m.expand(b), theirs):
            return True
    return False


def z88dk_listing(z88dk_dis, image, slots):
    """{address: (size, text)} z88dk-dis gives for each slot start"""
    out = {}
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, 'image.bin')
        open(path, 'wb').write(image)
        for address in slots:
            run = subprocess.run([z88dk_dis, '-mz80', '-o', str(BASE), '-s', str(address), '-e', str(address + SLOT), path],
                                 capture_output=True, text=True)
            for line in run.stdout.splitlines():
                m = re.match(r'\s*(.*?)\s*;\[([0-9a-f]{4})\]\s+((?:[0-9a-f]{2}\s*)+)$', line)
                if m and int(m.group(2), 16) == address:
                    out[address] = (len(m.group(3).split()), m.group(1))
                    break
    return out


def sjasmplus_bytes(sjasmplus, items):
    """{address: bytes or None} sjasmplus builds for (address, mnemonic) pairs; None = it rejected the line"""
    out = {}
    with tempfile.TemporaryDirectory() as tmp:
        source = os.path.join(tmp, 'reasm.asm')
        binary = os.path.join(tmp, 'reasm.bin')
        lines = ['        DEVICE ZXSPECTRUM48']
        line_of = {}
        for address, mnemonic in items:
            lines.append(f'        ORG #{address:04X}')
            line_of[len(lines) + 1] = address
            lines.append(f'        {mnemonic}')
        end = max(a for a, _ in items) + SLOT
        lines.append(f'        SAVEBIN "{binary}",#{BASE:04X},#{end - BASE:04X}')
        open(source, 'w').write('\n'.join(lines) + '\n')
        run = subprocess.run([sjasmplus, '--nologo', '--msg=err', source], capture_output=True, text=True, cwd=tmp)
        rejected = set()
        for line in (run.stdout + run.stderr).splitlines():
            m = re.search(r'reasm\.asm\((\d+)\)', line)
            if m and int(m.group(1)) in line_of:
                rejected.add(line_of[int(m.group(1))])
        image = open(binary, 'rb').read() if os.path.exists(binary) else b''
        for address, _ in items:
            if address in rejected or not image:
                out[address] = None
            else:
                out[address] = image[address - BASE:address - BASE + SLOT]
    return out


def expected_target(code, address):
    """The branch target of an encoding, or None when it has no fixed one"""
    op = code[0]
    nn = code[1] | (code[2] << 8) if len(code) > 2 else None
    if op in (0x10, 0x18, 0x20, 0x28, 0x30, 0x38):
        e = code[1] - 256 if code[1] >= 128 else code[1]
        return (address + 2 + e) & 0xFFFF
    if op in (0xC3, 0xCD) or (op & 0xC7) in (0xC2, 0xC4):
        return nn
    if (op & 0xC7) == 0xC7:
        return op & 0x38
    return None


class Report:
    def __init__(self, verbose):
        self.verbose = verbose
        self.failures = {}
        self.notes = {}
        self.passed = {}

    def ok(self, check):
        self.passed[check] = self.passed.get(check, 0) + 1

    def fail(self, check, message):
        self.failures.setdefault(check, []).append(message)

    def note(self, check, message):
        self.notes.setdefault(check, []).append(message)

    def print(self):
        for check in sorted(set(self.passed) | set(self.failures) | set(self.notes)):
            failed = self.failures.get(check, [])
            print(f'{check:7} {self.passed.get(check, 0):5} ok, {len(failed):4} failed'
                  + (f', {len(self.notes[check])} note(s)' if check in self.notes else ''))
            for message in failed[:None if self.verbose else 40]:
                print(f'          FAIL {message}')
            if len(failed) > 40 and not self.verbose:
                print(f'          ... {len(failed) - 40} more (--verbose)')
            notes = self.notes.get(check, [])
            for message in notes[:None if self.verbose else 10]:
                print(f'          note {message}')
            if len(notes) > 10 and not self.verbose:
                print(f'          ... {len(notes) - 10} more notes (--verbose)')
        return not self.failures


def hexbytes(code):
    return ' '.join(f'{b:02X}' for b in code)


def check_opcodes(api, args, report):
    table = encodings()
    image = bytearray(len(table) * SLOT)
    slots = []
    for i, (code, _) in enumerate(table):
        image[i * SLOT:i * SLOT + len(code)] = code
        slots.append(BASE + i * SLOT)
    status, body = request('POST', api + '/pause')
    status, body = request('POST', api + '/memory/write', {'address': str(BASE), 'data': list(image)})
    if status != 200 or body.get('bytes_written') != len(image):
        sys.exit(f'memory write failed: {status} {body}')

    ours = {}
    for address in slots:
        status, body = request('GET', f'{api}/disasm?address={address}&count=1')
        if status != 200 or not body.get('instructions'):
            report.fail('length', f'#{address:04X}: GET /disasm answered {status} {body}')
            continue
        ours[address] = body['instructions'][0]

    theirs = z88dk_listing(args.z88dk_dis, bytes(image), slots)
    reasm_items = []
    for (code, documented), address in zip(table, slots):
        line = ours.get(address)
        if line is None:
            continue
        size, mnemonic = line['size'], line['mnemonic']
        where = f'#{address:04X} {hexbytes(code[:size])}'
        if re.search(r'[?;]', mnemonic):
            report.fail('syntax', f'{where}: "{mnemonic}"')
        else:
            report.ok('syntax')
        if address not in theirs:
            report.note('length', f'{where}: z88dk-dis gave no line')
            continue
        their_size, their_text = theirs[address]
        if size != their_size:
            report.fail('length', f'{hexbytes(code)}: {size} byte(s) "{mnemonic}", z88dk-dis {their_size} "{their_text}"')
            continue
        report.ok('length')
        a, b = canonical(normalize(mnemonic)), canonical(normalize(their_text))
        if a == b or synonym(a, b):
            report.ok('text')
        else:
            report.fail('text', f'{where}: "{mnemonic}" vs z88dk-dis "{their_text}"')
        target = expected_target(code, address) if code[0] not in (0xCB, 0xDD, 0xED, 0xFD) else None
        if code[0] in (0xDD, 0xFD) and code[1] == 0xE9:
            target = None
        if target is not None:
            if line.get('target') == target:
                report.ok('target')
            else:
                report.fail('target', f'{where} "{mnemonic}": target {line.get("target")}, expected {target}')
        reasm_items.append((address, mnemonic, code[:size], documented))

    if args.sjasmplus:
        built = sjasmplus_bytes(args.sjasmplus, [(a, m) for a, m, _, _ in reasm_items])
        for address, mnemonic, code, documented in reasm_items:
            got = built.get(address)
            where = f'#{address:04X} {hexbytes(code)} "{mnemonic}"'
            if got is not None and got[:len(code)] == code:
                report.ok('reasm')
            elif documented:
                report.fail('reasm', f'{where}: sjasmplus ' + ('rejects it' if got is None else f'builds {hexbytes(got[:len(code)])}'))
            else:
                report.note('reasm', f'{where} (undocumented): sjasmplus ' +
                            ('rejects it' if got is None else f'builds {hexbytes(got[:len(code)])}'))
    return slots


def read_memory(api, address, length):
    """Bytes of the CPU view (GET /memory/read hexdump)"""
    status, body = request('GET', f'{api}/memory/read/{address}?length={length}')
    if status != 200:
        sys.exit(f'memory read failed: {status} {body}')
    out = bytearray()
    for line in body['hexdump'].splitlines():
        out += bytes(int(h, 16) for h in line.split(':', 1)[1].split('|')[0].split())
    return bytes(out)


def unlabel(text):
    """Each 'LABEL (#addr)' as '#addr'. A ROM name may hold '+', '/' and parens ("(stk-zero)", "SA/LD-RET"): the name
    runs back from ' (#addr)' to a space or a comma outside its own parens, or to the '(' it sits in. The mnemonic
    itself is never a name ("ld (#5C5F),hl")"""
    pos = 0
    while True:
        m = re.compile(r' \((#[0-9A-F]{4})\)').search(text, pos)
        if not m:
            return text
        start, depth = m.start(), 0
        while start > 0:
            c = text[start - 1]
            if c == ')':
                depth += 1
            elif c == '(':
                if depth == 0:
                    break
                depth -= 1
            elif c in ' ,' and depth == 0:
                break
            start -= 1
        if start == 0 or start == m.start():
            pos = m.end()
            continue
        text = text[:start] + m.group(1) + text[m.end():]
        pos = start + len(m.group(1))


def check_rom(root, args, report):
    """A linear sweep of the 48K ROM (with its symbols: 'jp START-NEW (#11CB)') against z88dk-dis over the same bytes:
    the same instruction boundaries, the same text with each 'LABEL (#addr)' read as its address, the bytes field
    equal to memory, one instruction after the other"""
    status, body = request('POST', root + '/start', {'model': '48K'})
    if status not in (200, 201) or 'id' not in body:
        report.fail('rom', f'cannot create a 48K emulator: {status} {body}')
        return
    api = f'{root}/{body["id"]}'
    try:
        request('POST', api + '/pause')
        rom = read_memory(api, 0, 0x4000)
        ours = []
        address = 0
        while address < 0x4000:
            status, body = request('GET', f'{api}/disasm?address={address}&count=256')
            for line in body.get('instructions', []):
                if line['address'] >= 0x4000:
                    break
                ours.append(line)
            address = ours[-1]['address'] + ours[-1]['size']
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, 'rom.bin')
            open(path, 'wb').write(rom)
            run = subprocess.run([args.z88dk_dis, '-mz80', '-o', '0', path], capture_output=True, text=True)
        theirs = {}
        for line in run.stdout.splitlines():
            m = re.match(r'\s*(.*?)\s*;\[([0-9a-f]{4})\]\s+((?:[0-9a-f]{2}\s*)+)$', line)
            if m:
                theirs[int(m.group(2), 16)] = (len(m.group(3).split()), m.group(1))
        expected = 0
        labelled = 0
        for line in ours:
            where = f'#{line["address"]:04X} {line["bytes"]} "{line["mnemonic"]}"'
            if line['address'] != expected:
                report.fail('rom', f'{where}: expected an instruction at #{expected:04X}')
            expected = line['address'] + line['size']
            if bytes.fromhex(line['bytes']) != rom[line['address']:line['address'] + line['size']]:
                report.fail('rom', f'{where}: bytes differ from memory')
                continue
            their = theirs.get(line['address'])
            if their is None or their[0] != line['size']:
                report.fail('rom', f'{where}: z88dk-dis {their}')
                continue
            text = line['mnemonic']
            if 'targetLabel' in line:   # ROM names hold commas and parens: "call DE,(DE+1) (#2AEE)"
                text = text.replace(f'{line["targetLabel"]} (#{line["target"]:04X})', f'#{line["target"]:04X}')
            text = unlabel(text)
            labelled += text != line['mnemonic']
            a, b = canonical(normalize(text)), canonical(normalize(their[1]))
            if a == b or synonym(a, b):
                report.ok('rom')
            else:
                report.fail('rom', f'{where}: z88dk-dis "{their[1]}"')
        if labelled < 100:
            report.fail('rom', f'only {labelled} instruction(s) show a ROM symbol: are the ROM symbols loaded?')
    finally:
        request('DELETE', api)


def check_wire(api, report):
    """Address / count parsing of GET /disasm"""
    request('POST', api + '/memory/write', {'address': str(BASE), 'data': [0x00, 0xC9]})
    for spelling in ('32768', '0x8000', '%248000', '%238000', '8000h'):
        status, body = request('GET', f'{api}/disasm?address={spelling}&count=1')
        if status == 200 and body.get('address') == BASE:
            report.ok('wire')
        else:
            report.fail('wire', f'address={urllib.parse.unquote(spelling)}: {status} {body}')
    for query in ('address=zz', 'address=70000', 'count=0', 'count=257', 'count=abc'):
        status, body = request('GET', f'{api}/disasm?{query}')
        if status == 400:
            report.ok('wire')
        else:
            report.fail('wire', f'{query}: expected 400, got {status} {body}')
    status, body = request('GET', f'{api}/disasm?address=0&count=256')
    if status == 200 and len(body.get('instructions', [])) == 256:
        report.ok('wire')
    else:
        report.fail('wire', f'count=256: {status}, {len((body or {}).get("instructions", []))} line(s)')


def mcp_call(url, name, arguments):
    status, body = request('POST', url, {'jsonrpc': '2.0', 'id': 1, 'method': 'tools/call',
                                         'params': {'name': name, 'arguments': arguments}})
    return status, body


def check_mcp(mcp_url, target, report):
    status, body = mcp_call(mcp_url, 'debug_code', {'action': 'disassemble', 'target': target,
                                                    'address': f'0x{BASE:04X}', 'count': 40})
    if status != 200 or 'result' not in (body or {}):
        report.fail('mcp', f'debug_code disassemble: {status} {body}')
        return
    result = body['result']
    text = result['content'][0]['text'] if result.get('content') else ''
    rows = [line for line in text.splitlines() if re.match(r'[0-9A-F]{4}  ', line)]
    structured = result.get('structuredContent', {}).get('instructions', [])
    if len(rows) == 40 and len(structured) == 40:
        report.ok('mcp')
    else:
        report.fail('mcp', f'debug_code disassemble: {len(rows)} listing line(s) in the text, {len(structured)} in '
                           f'structuredContent, 40 asked:\n{text[:400]}')
    for row, line in zip(rows, structured):
        if not (row.startswith(f'{line["address"]:04X}') and line['mnemonic'] in row):
            report.fail('mcp', f'listing line "{row}" does not match {line}')
            break
    else:
        report.ok('mcp')

    # The address as a hex string, as agents write it (it threw "Value is not convertible to UInt")
    status, body = mcp_call(mcp_url, 'inspect_state', {'aspects': ['registers', 'disasm'], 'target': target,
                                                       'address': f'0x{BASE:04X}', 'count': 8})
    text = '\n'.join(c.get('text', '') for c in body.get('result', {}).get('content', [])) if body else ''
    if len([line for line in text.splitlines() if re.match(r'[0-9A-F]{4}  ', line)]) >= 8:
        report.ok('mcp')
    else:
        report.fail('mcp', f'inspect_state disasm: no listing in content[]:\n{text[:400]}')

    # Any structured result reaches a text-only client on request: the emulator list as the sample
    status, body = mcp_call(mcp_url, 'emulator_manage', {'action': 'list', 'response_format': 'json'})
    content = body.get('result', {}).get('content', []) if body else []
    echoed = None
    for item in content[1:]:
        try:
            echoed = json.loads(item.get('text', ''))
        except ValueError:
            pass
    if echoed is not None and echoed == body['result'].get('structuredContent'):
        report.ok('mcp')
    else:
        report.fail('mcp', 'emulator_manage list, response_format json: structuredContent is not echoed in content[]')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--port', type=int, default=8090)
    parser.add_argument('--host', default='localhost')
    parser.add_argument('--mcp-port', type=int, default=8092, help='0 = skip the MCP checks')
    parser.add_argument('--target', help='an existing emulator id (default: create one, removed at the end)')
    parser.add_argument('--model', default='PENTAGON')
    parser.add_argument('--z88dk-dis', default=os.environ.get('UNREAL_Z88DK_DIS') or shutil.which('z88dk-dis'))
    parser.add_argument('--sjasmplus', default=os.environ.get('UNREAL_ASM_SJASMPLUS') or shutil.which('sjasmplus'))
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args()
    if not args.z88dk_dis or not os.path.exists(args.z88dk_dis):
        sys.exit('z88dk-dis not found: --z88dk-dis or UNREAL_Z88DK_DIS')

    root = f'http://{args.host}:{args.port}/api/v1/emulator'
    target = args.target
    created = False
    if not target:
        status, body = request('POST', root + '/start', {'model': args.model})
        if status not in (200, 201) or 'id' not in body:
            sys.exit(f'cannot create an emulator: {status} {body}')
        target, created = body['id'], True
    api = f'{root}/{target}'
    report = Report(args.verbose)
    try:
        check_opcodes(api, args, report)
        check_wire(api, report)
        check_rom(root, args, report)
        if args.mcp_port:
            mcp_url = f'http://{args.host}:{args.mcp_port}/mcp'
            try:
                urllib.request.urlopen(urllib.request.Request(mcp_url, data=b'{"jsonrpc":"2.0","id":0,"method":"ping"}',
                                                              headers={'Content-Type': 'application/json'}), timeout=5)
            except (urllib.error.URLError, OSError) as e:
                report.note('mcp', f'MCP at {mcp_url} does not answer ({e}): skipped')
            else:
                check_mcp(mcp_url, target, report)
        if not args.sjasmplus:
            report.note('reasm', 'sjasmplus not found (--sjasmplus or UNREAL_ASM_SJASMPLUS): skipped')
    finally:
        if created:
            request('DELETE', f'{root}/{target}')
    ok = report.print()
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
