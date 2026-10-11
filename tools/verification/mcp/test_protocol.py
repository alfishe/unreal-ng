#!/usr/bin/env python3
"""MCP protocol: handshake, tool list, response_format (text by default, json echoes structuredContent), errors,
resources"""

import json
import sys

from mcplib import expect, main


def test_initialize_and_ping(s):
    def handshake(version):
        return s.rpc('initialize', {'protocolVersion': version, 'capabilities': {},
                                    'clientInfo': {'name': 'mcp-verification', 'version': '1'}})['result']
    result = handshake('2025-03-26')
    expect(result['protocolVersion'] == '2025-03-26', 'a supported version is echoed', result['protocolVersion'])
    expect('tools' in result['capabilities'] and 'resources' in result['capabilities'], 'tools and resources', result)
    expect(result.get('instructions'), 'server instructions for the model', result)
    fallback = handshake('2099-01-01')['protocolVersion']
    expect(fallback in ('2024-11-05', '2025-03-26', '2025-06-18'), 'an unknown version gets one the server speaks',
           fallback)
    expect(s.rpc('ping')['result'] == {}, 'ping answers {}')


def test_every_tool_has_a_schema_and_response_format(s):
    tools = s.tools()
    expect(len(tools) >= 15, f'the tool set ({len(tools)})', sorted(tools))
    for name, tool in tools.items():
        expect(len(tool['description']) > 40, f'{name}: a description the model can use', tool['description'])
        properties = tool['inputSchema'].get('properties', {})
        expect(properties.get('response_format', {}).get('enum') == ['text', 'json'], f'{name}: response_format',
               properties.get('response_format'))


def test_default_reply_is_text_only(s):
    r = s.call('emulator_manage', {'action': 'list'})
    expect(len(r.content) == 1 and r.content[0]['type'] == 'text', 'one text block', r.content)
    expect(r.structured is not None, 'structuredContent for programs')


def test_json_reply_echoes_structured_content(s):
    r = s.call('emulator_manage', {'action': 'list', 'response_format': 'json'})
    expect(len(r.content) == 2, 'text + JSON', r.content)
    expect(json.loads(r.content[1]['text']) == r.structured, 'the echo is the structuredContent')


def test_bad_response_format_is_a_tool_error(s):
    r = s.call('emulator_manage', {'action': 'list', 'response_format': 'xml'}, ok=False)
    expect(r.is_error and 'response_format' in r.text, 'a tool error naming the argument', r.text)


def test_unknown_tool_is_a_jsonrpc_error(s):
    body = s.rpc('tools/call', {'name': 'no_such_tool', 'arguments': {}})
    expect('error' in body, 'JSON-RPC error', body)


def test_tool_errors_are_results_not_crashes(s):
    r = s.call('inspect_state', {'aspects': ['memory'], 'address': {'not': 'a number'}}, ok=False)
    expect(r.is_error, 'a malformed argument is a tool error', r.text)
    expect(s.rpc('ping')['result'] == {}, 'the server still answers')


def test_resources_list_and_read(s):
    resources = s.rpc('resources/list')['result']['resources']
    uris = {r['uri'] for r in resources}
    expect('unreal://z80-isa' in uris, 'the Z80 reference', sorted(uris))
    contents = s.rpc('resources/read', {'uri': 'unreal://z80-isa'})['result']['contents']
    expect(contents and len(contents[0].get('text', '')) > 500, 'a resource with text', contents[:1])


if __name__ == '__main__':
    sys.exit(main(globals()))
