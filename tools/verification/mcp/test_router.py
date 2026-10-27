#!/usr/bin/env python3
"""search_api / invoke_api: the WebAPI behind the tools - search by words, call by path, errors as tool errors"""

import sys

from mcplib import expect, main


def test_search(s):
    r = s.call('search_api', {'query': 'registers'})
    expect('/registers' in r.text, 'matching paths in the text', r.text[:300])


def test_invoke_get(s):
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/registers'})
    expect('HTTP 200' in r.text and 'pc' in r.structured['body']['special'], 'the registers', r.structured)


def test_invoke_404_is_an_error(s):
    r = s.call('invoke_api', {'method': 'GET', 'path': '/api/v1/emulator/{id}/no/such/path'}, ok=False)
    expect(r.is_error or '404' in r.text, 'a missing path is reported', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
