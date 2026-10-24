#!/usr/bin/env python3
"""signature_info / controllers match: the signatures of testdata files in the text, a disk loaded into a drive, the
comparison, the game-profile match of a signature and of a path"""

import os
import sys

from mcplib import expect, main

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
TAP = os.path.join(ROOT, 'testdata', 'loaders', 'tap', 'aydetect.tap')
SCL = os.path.join(ROOT, 'testdata', 'loaders', 'scl', 'eyeache2.scl')


def test_file_signature_in_the_text(s):
    r = s.call('signature_info', {'action': 'file', 'path': TAP})
    sig = r.structured.get('signature', {})
    expect(sig.get('kind') == 'tape' and sig.get('content', '').startswith('tape:sha256:'), 'a tape content signature',
           r.structured)
    expect(sig['sha256'] in r.text and sig['content'] in r.text, 'the hashes in the text', r.text)


def test_disk_file_set(s):
    r = s.call('signature_info', {'action': 'file', 'path': SCL, 'details': True})
    sig = r.structured['signature']
    expect(sig['content'].startswith('trdset:sha256:') and sig['file_set'], 'a TR-DOS file set', sig)
    expect(sig['file_set'][0] in r.text, 'the file set tokens in the text', r.text)


def test_compare_same_file_is_exact(s):
    r = s.call('signature_info', {'action': 'compare', 'path': TAP, 'with': TAP})
    expect(r.structured['match']['level'] == 'exact' and 'exact' in r.text, 'exact', r.text)
    r = s.call('signature_info', {'action': 'compare', 'path': TAP, 'with': SCL})
    expect(r.structured['match']['level'] == 'none', 'a tape and a disk do not match', r.text)


def test_missing_file_is_an_error(s):
    r = s.call('signature_info', {'action': 'file', 'path': '/nonexistent/x.tap'}, ok=False)
    expect(r.is_error and 'x.tap' in r.text, 'names the file', r.text)


def test_loaded_media(s):
    s.call('media', {'action': 'insert', 'slot': 'fdd.a', 'path': SCL})
    r = s.call('signature_info', {'action': 'media'})
    expect('eyeache2.scl' in r.text and 'trdset:sha256:' in r.text, 'the disk and its signature', r.text)
    s.call('media', {'action': 'eject', 'slot': 'fdd.a'}, ok=False)


def test_controllers_match_by_signature(s):
    sig = s.call('signature_info', {'action': 'file', 'path': TAP}).structured['signature']
    r = s.call('controllers', {'action': 'match', 'signatures': ','.join(sig['keys'])})
    expect('matches' in r.structured and 'next' in r.structured, 'matches and next', r.structured)
    expect(sig['content'] in r.text and r.structured['next'] in r.text, 'the signatures and next in the text', r.text)
    r = s.call('controllers', {'action': 'match', 'path': TAP})
    expect(sig['content'] in r.structured.get('signatures', []), 'the Qt client computed the path\'s signatures',
           r.structured)
    r = s.call('controllers', {'action': 'context'})
    expect('haptics' in r.text and 'game matches' in r.text, 'context names the matches and the haptics', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
