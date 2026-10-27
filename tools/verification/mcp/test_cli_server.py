#!/usr/bin/env python3
"""The CLI server: connections opened right after one another each keep their own socket. The accept loop used to close
the sockets of clients it thought gone while their threads closed them too; the next connection, given the same
number, was closed under it (it saw EOF before the greeting). The CLI is found from the MCP port (CLI = MCP - 100)."""

import re
import socket
import sys
import time
import urllib.parse

from mcplib import expect, main


def cli_port(s):
    return urllib.parse.urlparse(s.url).port - 100


def converse(port, command):
    """The greeting and the answer to `command` on a fresh connection; '' parts when the server closed it"""
    with socket.create_connection(('127.0.0.1', port), timeout=10) as sock:
        def read_until_prompt():
            data = b''
            deadline = time.time() + 10
            while not re.search(rb'\n> (\x1b\[K)?$', data) and time.time() < deadline:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                data += chunk
            return data.decode(errors='replace')
        greeting = read_until_prompt()
        sock.sendall((command + '\n').encode())
        return greeting, read_until_prompt()


def setup(s):
    pass  # no machine needed


def test_back_to_back_connections_each_get_their_answer(s):
    port = cli_port(s)
    for i in range(100):
        greeting, answer = converse(port, 'companion help')
        expect('Welcome' in greeting, f'connection {i}: the greeting', repr(greeting))
        expect('Companion clients' in answer, f'connection {i}: the answer', repr(answer))


def test_overlapping_connections_stay_apart(s):
    port = cli_port(s)
    held = [socket.create_connection(('127.0.0.1', port), timeout=10) for _ in range(5)]
    try:
        for i in range(20):
            greeting, answer = converse(port, 'companion help')
            expect('Welcome' in greeting and 'Companion clients' in answer, f'connection {i} beside five open ones',
                   repr(greeting + answer))
    finally:
        for sock in held:
            sock.close()


if __name__ == '__main__':
    sys.exit(main(globals()))
