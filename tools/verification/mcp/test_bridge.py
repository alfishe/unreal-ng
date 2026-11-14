#!/usr/bin/env python3
"""unreal-mcp-bridge: a call the emulator cannot answer ends at once under the request's own id - nothing listens
(data.bridge unreachable: not started, exited, crashed) or the connection breaks mid-answer (connection_lost: the
process went away during the call); a notification gets no answer; a live server is piped through"""

import json
import os
import socket
import subprocess
import sys
import threading

from mcplib import expect, main

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))


def bridge_binary():
    for folder in ('cmake-build-agent-release', 'cmake-build-release', 'cmake-build-debug'):
        for name in ('unreal-mcp-bridge', 'unreal-mcp-bridge.exe'):
            path = os.path.join(ROOT, folder, 'bin', name)
            if os.path.exists(path):
                return path
    raise AssertionError('no unreal-mcp-bridge binary (build it with tools/build/build.sh)')


def through_bridge(url, lines, timeout=20):
    """Feeds lines to a bridge on url; returns its stdout lines"""
    done = subprocess.run([bridge_binary(), '--url', url], input='\n'.join(lines) + '\n', capture_output=True,
                          text=True, timeout=timeout)
    return [json.loads(line) for line in done.stdout.splitlines() if line.strip()]


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def test_nothing_listens(s):
    url = f'http://127.0.0.1:{free_port()}/mcp'
    answers = through_bridge(url, [
        json.dumps({'jsonrpc': '2.0', 'id': 7, 'method': 'tools/call', 'params': {'name': 'inspect_state'}}),
        json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}),
        json.dumps({'jsonrpc': '2.0', 'params': {'id': 99, 'text': '"id": 5'}, 'id': "req-\"8\"", 'method': 'ping'}),
    ])
    expect(len(answers) == 2, 'one answer per request, none for the notification', answers)
    expect(answers[0]['id'] == 7, 'the error carries the request id', answers[0])
    expect(answers[0]['error']['data']['bridge'] == 'unreachable', 'unreachable', answers[0])
    expect('not running, or it has exited or crashed' in answers[0]['error']['message'], 'says why', answers[0])
    expect(answers[1]['id'] == 'req-"8"', 'a string id, nested "id" keys ignored', answers[1])


def test_connection_breaks_mid_answer(s):
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    port = listener.getsockname()[1]

    def serve():
        connection, _ = listener.accept()
        connection.recv(65536)
        connection.sendall(b'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 100\r\n\r\n{"jsonr')
        connection.close()  # the process went away while answering
        listener.close()

    server = threading.Thread(target=serve, daemon=True)
    server.start()
    answers = through_bridge(f'http://127.0.0.1:{port}/mcp', [json.dumps({'jsonrpc': '2.0', 'id': 3, 'method': 'ping'})])
    server.join(5)
    expect(len(answers) == 1 and answers[0]['id'] == 3, 'answered under the request id', answers)
    expect(answers[0]['error']['data']['bridge'] == 'connection_lost', 'connection_lost', answers[0])
    expect('may have exited or crashed' in answers[0]['error']['message'], 'says why', answers[0])


def test_live_server_is_piped(s):
    answers = through_bridge(s.url, [json.dumps({'jsonrpc': '2.0', 'id': 11, 'method': 'ping'})])
    expect(answers == [{'id': 11, 'jsonrpc': '2.0', 'result': {}}], 'ping through the bridge', answers)


if __name__ == '__main__':
    sys.exit(main(globals()))
