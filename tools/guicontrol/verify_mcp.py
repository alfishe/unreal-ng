#!/usr/bin/env python3
"""Live check of `unreal-gui mcp` against a running GUI program: initialize, tools/list, gui_list, gui_call, gui_state
and the refusals, each answer checked on the text an agent reads (content[]), not only structuredContent.

    UNREAL_GUI_CONTROL_DIR=... python3 tools/guicontrol/verify_mcp.py cmake-build-agent-release/bin/unreal-gui

The program needs a debugger with a machine bound (its memory view is driven). Exit code 0 when every check passed.
"""

import json
import subprocess
import sys


class Mcp:
    def __init__(self, binary):
        self.process = subprocess.Popen([binary, 'mcp'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.rid = 0

    def rpc(self, method, params=None):
        self.rid += 1
        message = {'jsonrpc': '2.0', 'id': self.rid, 'method': method}
        if params is not None:
            message['params'] = params
        self.process.stdin.write(json.dumps(message) + '\n')
        self.process.stdin.flush()
        answer = json.loads(self.process.stdout.readline())
        assert answer.get('id') == self.rid, answer
        return answer

    def notify(self, method):
        self.process.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': method}) + '\n')
        self.process.stdin.flush()

    def tool(self, name, arguments):
        result = self.rpc('tools/call', {'name': name, 'arguments': arguments})['result']
        text = ''.join(part['text'] for part in result['content'] if part['type'] == 'text')
        return result, text

    def close(self):
        self.process.stdin.close()
        return self.process.wait(timeout=5)


failures = []


def expect(condition, what, detail=''):
    print(('PASS ' if condition else 'FAIL ') + what + ('' if condition else f'\n     {detail}'))
    if not condition:
        failures.append(what)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    mcp = Mcp(sys.argv[1])

    init = mcp.rpc('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {},
                                  'clientInfo': {'name': 'verify_mcp', 'version': '1'}})['result']
    expect(init['serverInfo']['name'] == 'unreal-gui' and 'tools' in init['capabilities'], 'initialize', init)
    mcp.notify('notifications/initialized')
    expect(mcp.rpc('ping')['result'] == {}, 'ping')

    tools = {tool['name']: tool for tool in mcp.rpc('tools/list')['result']['tools']}
    expect(set(tools) == {'gui_list', 'gui_call', 'gui_state'}, 'three tools', list(tools))
    expect('memory.goto' in tools['gui_call']['description'], 'gui_call lists the commands')

    result, text = mcp.tool('gui_list', {})
    hosts = result['structuredContent']['body']['hosts']
    expect(not result['isError'] and hosts, 'gui_list finds a host', text[:300])
    host = hosts[0]['host']
    expect(f'"host": "{host}"' in text and 'debugger' in text, 'gui_list text names the host and the debugger')

    result, text = mcp.tool('gui_call', {'method': 'debugger.open'})
    expect(not result['isError'], 'gui_call debugger.open', text)
    result, text = mcp.tool('gui_call', {'method': 'memory.goto', 'params': {'addr': '0x6A00'}})
    expect(not result['isError'] and '0x6A00' in text, 'gui_call memory.goto, the text has the cursor', text)
    result, text = mcp.tool('gui_call', {'method': 'memory.select', 'target': 'debugger/memory',
                                         'params': {'from': '0x6A10', 'to': '0x6A1F'}})
    expect(not result['isError'] and '0x6A1F' in text, 'gui_call memory.select by path', text)

    result, text = mcp.tool('gui_state', {'target': 'memory'})
    state = result['structuredContent']['body']
    expect(not result['isError'] and state['state']['selection'] == {'from': '0x6A10', 'to': '0x6A1F'},
           'gui_state memory: the selection', text)
    expect('"selection"' in text and 'instance' in state and state['instance']['id'] in text,
           'gui_state text has the selection and the machine')

    result, text = mcp.tool('gui_call', {'method': 'memory.goto', 'params': {'addr': 'nowhere'}})
    expect(result['isError'] and text.startswith('bad_request:'), 'a bad address is refused in the text', text)
    result, text = mcp.tool('gui_call', {'method': 'no.such'})
    expect(result['isError'] and text.startswith('not_found:'), 'an unknown command is refused', text)
    result, text = mcp.tool('gui_state', {})
    expect(result['isError'] and 'target' in text, 'gui_state needs a target', text)
    expect('error' in mcp.rpc('no/method'), 'an unknown JSON-RPC method is an error')

    expect(mcp.close() == 0, 'the server ends with stdin')
    print(f'{len(failures)} failed' if failures else 'all passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
