#!/usr/bin/env python3
"""capture_media: screenshots (metadata by default, the picture as an MCP image block on request), digests, status"""

import base64
import json
import sys

from mcplib import expect, main


def test_screenshot_metadata_by_default(s):
    r = s.call('capture_media', {'action': 'screenshot'})
    expect('352x288 png' in r.text and 'include_image' in r.text, 'size, format and how to get the pixels', r.text)
    expect(not r.blocks('image') and 'data' not in r.structured, 'no pixels unless asked')


def test_screenshot_image_block(s):
    r = s.call('capture_media', {'action': 'screenshot', 'include_image': True, 'response_format': 'json'})
    images = r.blocks('image')
    expect(len(images) == 1 and images[0]['mimeType'] == 'image/png', 'one PNG image block', r.content)
    expect(base64.b64decode(images[0]['data'])[:8] == b'\x89PNG\r\n\x1a\n', 'a real PNG')
    echo = json.loads(r.content[-1]['text'])
    expect(echo['data'].startswith('[base64 picture'), 'the JSON echo does not repeat the base64', echo['data'][:60])


def test_gif_screenshot(s):
    r = s.call('capture_media', {'action': 'screenshot', 'format': 'gif', 'include_image': True})
    expect(r.blocks('image')[0]['mimeType'] == 'image/gif', 'GIF on request')


def test_screen_digest_is_stable_while_paused(s):
    s.call('control_execution', {'action': 'pause'})
    first = s.call('capture_media', {'action': 'screen_digest'})
    second = s.call('capture_media', {'action': 'screen_digest'})
    expect(first.structured['combined'] == second.structured['combined'], 'same digest twice',
           (first.structured, second.structured))
    expect('=' in first.text, 'the digest in the text', first.text)


def test_record_and_audio_status(s):
    for action in ('record_status', 'audio_status'):
        r = s.call('capture_media', {'action': action})
        expect('=' in r.text or 'status' in r.text.lower(), f'{action}: fields in the text', r.text)


if __name__ == '__main__':
    sys.exit(main(globals()))
