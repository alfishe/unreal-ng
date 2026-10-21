#!/usr/bin/env python3
"""capture_media: screenshots (metadata by default, the picture as an MCP image block on request), digests, status,
the GUI window's display settings, its CRT parameters, the overscan viewport"""

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


def test_window_display_changes_and_restores(s):
    before = s.call('capture_media', {'action': 'window_display'})
    expect(before.text.startswith('window gpu '), 'a one-line summary', before.text)
    expect('basic' in before.structured['crt_profiles'], 'the profiles the window offers', before.structured)

    changed = s.call('capture_media', {'action': 'window_display_set',
                                       'display': {'crt_profile': 'basic', 'blending_frames': 4}})
    expect(changed.structured['crt'] and changed.structured['crt_profile'] == 'basic', 'the profile switches CRT on',
           changed.structured)
    expect('crt basic' in changed.text and '4 frames' in changed.text, 'the change in the text', changed.text)

    bad = s.call('capture_media', {'action': 'window_display_set', 'display': {'crt_profile': 'nope'}}, ok=False)
    expect('HTTP 400' in bad.text and 'basic' in bad.text, 'an unknown profile lists the offered ones', bad.text)

    restore = {k: before.structured[k] for k in ('crt_profile', 'crt', 'blending_frames')}
    after = s.call('capture_media', {'action': 'window_display_set', 'display': restore})
    expect(after.structured['crt_profile'] == before.structured['crt_profile'], 'restored', after.structured)


def test_window_display_hud_keyboard_and_blending_decay(s):
    before = s.call('capture_media', {'action': 'window_display'}).structured
    expect('retro_zx' in before['hud_themes'] and 'ram-page' in before['hud_notices'], 'themes and notices offered',
           before)
    changed = s.call('capture_media', {'action': 'window_display_set', 'display': {
        'hud': True, 'hud_theme': 'retro_zx', 'hud_notices': {'ram-page': False},
        'blending_weights': 'exponential', 'blending_decay': 0.3}})
    st = changed.structured
    expect(st['hud'] and st['hud_theme'] == 'retro_zx' and not st['hud_notices']['ram-page'], 'the HUD follows', st)
    expect(abs(st['blending_decay'] - 0.3) < 1e-6 and 'decay 0.3' in changed.text, 'the decay set and shown',
           changed.text)
    expect('pentagon' in st['osd_keyboard_pictures'], 'the Pentagon keyboard pictures', st)
    bad = s.call('capture_media', {'action': 'window_display_set', 'display': {'hud_theme': 'neon'}}, ok=False)
    expect('HTTP 400' in bad.text and 'retro_zx' in bad.text, 'an unknown theme lists the offered ones', bad.text)
    restore = {k: before[k] for k in ('hud', 'hud_theme', 'blending_weights', 'blending_decay')}
    restore['hud_notices'] = {'ram-page': before['hud_notices']['ram-page']}
    s.call('capture_media', {'action': 'window_display_set', 'display': restore})


def test_crt_params_of_basic(s):
    before = s.call('capture_media', {'action': 'window_display'}).structured
    s.call('capture_media', {'action': 'window_display_set', 'display': {'crt_profile': 'basic'}})
    listed = s.call('capture_media', {'action': 'window_crt_params'})
    names = [p['name'] for p in listed.structured['parameters']]
    expect(listed.structured['kind'] == 'profile' and 'curvature' in names, 'basic has the built-in parameters',
           listed.structured)
    expect('curvature = ' in listed.text, 'each parameter in the text', listed.text)
    moved = s.call('capture_media', {'action': 'window_crt_params_set', 'crt_params': {'steps': {'curvature': 3}}})
    curvature = next(p for p in moved.structured['parameters'] if p['name'] == 'curvature')
    expect(abs(curvature['value'] - (curvature['initial'] + 3 * curvature['step'])) < 1e-4, 'moved by 3 steps',
           curvature)
    bad = s.call('capture_media', {'action': 'window_crt_params_set', 'crt_params': {'values': {'nope': 1}}}, ok=False)
    expect('HTTP 400' in bad.text and 'curvature' in bad.text, 'an unknown parameter lists them', bad.text)
    reset = s.call('capture_media', {'action': 'window_crt_params_set', 'crt_params': {'reset': True}})
    curvature = next(p for p in reset.structured['parameters'] if p['name'] == 'curvature')
    expect(abs(curvature['value'] - curvature['initial']) < 1e-6, 'reset', curvature)
    s.call('capture_media', {'action': 'window_display_set',
                             'display': {k: before[k] for k in ('crt_profile', 'crt')}})


def test_viewport_crops_the_overscan_frame(s):
    off = s.call('capture_media', {'action': 'viewport'})
    expect(off.structured['overscan_available'] and not off.structured['overscan'], 'a Pentagon, no overscan yet',
           off.structured)
    refused = s.call('capture_media', {'action': 'viewport_set', 'viewport': 'standard'}, ok=False)
    expect('HTTP 409' in refused.text, 'a crop needs overscan', refused.text)
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/state/screen/overscan',
                          'body': {'enabled': True}})
    cropped = s.call('capture_media', {'action': 'viewport_set', 'viewport': 'standard'})
    expect(cropped.structured['viewport'] == 'standard' and cropped.structured['display'] == '352x288',
           'the standard crop', cropped.text)
    s.call('invoke_api', {'method': 'PUT', 'path': '/api/v1/emulator/{id}/state/screen/overscan',
                          'body': {'enabled': False}})


if __name__ == '__main__':
    sys.exit(main(globals()))
