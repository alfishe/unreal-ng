#!/usr/bin/env python3
"""Video mode consistency check, through the WebAPI of a running unreal-qt (or any server with the WebAPI).

For every machine (and, with --switch, the video modes a machine can be driven into from outside) it compares what
the surfaces say about the picture and reports every disagreement:

  * `state/screen/mode`  - the mode the machine reports: `video_mode`, `resolution` (the working picture), `framebuffer`
  * `capture/screen`     - the presented frame: `frame.mode`, `frame.width/height`, `screen_window`; the image in the
                           CRT's shape (DisplayGeometry::Picture: a 736x288 frame is a 736x576 image)
  * the raster table     - the frame size each mode name must have (RASTER below, from screen.h rasterDescriptors)
  * the display shape    - DisplayGeometry::DisplayFrame: what window / tile / recording is sized for
  * the window (macOS, --window-port): the real unreal-qt window against that display shape

Standard library only (Quartz is used for --window-port when importable). Exit code 0 = no disagreement.
Usage: see .recipe/testing/video-modes.md
"""
import argparse
import json
import subprocess
import sys
import time
import urllib.request

# Frame size of every video mode (core/src/emulator/video/screen.h, rasterDescriptors; names: Screen::GetVideoModeName)
RASTER = {
    'ZX': (352, 288), 'ZX128': (352, 288), 'Pentagon128K': (352, 288), 'PMC': (352, 288), 'P16': (352, 288),
    'P384': (384, 304), 'PHR': (352, 288), 'Timex': (352, 288),
    'TS16': (720, 288), 'TS256': (720, 288), 'TSTX': (720, 288), 'TSZX': (720, 288),
    'ATM16': (320, 288), 'ATMHR': (640, 288), 'ATMTX': (640, 288), 'ATMTL': (640, 288),
    'PROFI': (352, 288), 'GMX': (352, 288), 'BRD': (352, 288), 'Scorpion': (352, 288), 'PROFIHR': (608, 288),
    'Sprinter': (736, 288), 'Next': (640, 512),
}
REFERENCE = (352, 288)  # the CRT frame every picture is shown in (core/src/emulator/video/displaygeometry.h)

# TS-Conf: the graphics modes show video RAM (V_PAGE 0 after boot: pages 0-2, 192 lines of 256 bytes), where the BIOS keeps text data: noise as a bitmap.
# The script blanks those pages before it switches, so a picture is the mode's own and not the machine's leftovers
# Modes a machine can be put into from outside: model -> [(what, Z80 code run at #8000)]. Verified by hand 2026-10-11.
# A video mode is a machine state, not an API setting: the code writes the machine's own port
SWITCHES = {
    'ATM450': [
        ('ATM 640x200 (OUT #BE)', [0xF3, 0xD3, 0xBE, 0x18, 0xFE]),
        ('ATM EGA 320x200 (OUT #9E)', [0xF3, 0xD3, 0x9E, 0x18, 0xFE]),
        ('ATM ZX (OUT #FE)', [0xF3, 0xD3, 0xFE, 0x18, 0xFE]),
    ],
    'ATM710': [('ATM 640x200 (OUT #BE)', [0xF3, 0xD3, 0xBE, 0x18, 0xFE])],
    'ATM3': [
        ('ATM EGA 320x200 (#FF77 = 0)', [0xF3, 0x3E, 0x01, 0xD3, 0xBF, 0x01, 0x77, 0xFF, 0x3E, 0, 0xED, 0x79, 0x18, 0xFE]),
        ('ATM HWMC 640x200 (#FF77 = 2)', [0xF3, 0x3E, 0x01, 0xD3, 0xBF, 0x01, 0x77, 0xFF, 0x3E, 2, 0xED, 0x79, 0x18, 0xFE]),
        ('ATM ZX (#FF77 = 3)', [0xF3, 0x3E, 0x01, 0xD3, 0xBF, 0x01, 0x77, 0xFF, 0x3E, 3, 0xED, 0x79, 0x18, 0xFE]),
        ('ATM text 80x25 (#FF77 = 6)', [0xF3, 0x3E, 0x01, 0xD3, 0xBF, 0x01, 0x77, 0xFF, 0x3E, 6, 0xED, 0x79, 0x18, 0xFE]),
        ('ATM linear text (#FF77 = 7)', [0xF3, 0x3E, 0x01, 0xD3, 0xBF, 0x01, 0x77, 0xFF, 0x3E, 7, 0xED, 0x79, 0x18, 0xFE]),
    ],
    'TSL': [
        ('TS-Conf ZX (V_CONFIG = #00)', [0xF3, 0x01, 0xAF, 0x00, 0x3E, 0x00, 0xED, 0x79, 0x18, 0xFE]),
        ('TS-Conf 16C 256x192 (V_CONFIG = #01)', [0xF3, 0x01, 0xAF, 0x00, 0x3E, 0x01, 0xED, 0x79, 0x18, 0xFE]),
        ('TS-Conf 256C 256x192 (V_CONFIG = #02)', [0xF3, 0x01, 0xAF, 0x00, 0x3E, 0x02, 0xED, 0x79, 0x18, 0xFE]),
        ('TS-Conf TXT 256x192 (V_CONFIG = #03)', [0xF3, 0x01, 0xAF, 0x00, 0x3E, 0x03, 0xED, 0x79, 0x18, 0xFE]),
        ('TS-Conf 256C 320x200 (V_CONFIG = #42)', [0xF3, 0x01, 0xAF, 0x00, 0x3E, 0x42, 0xED, 0x79, 0x18, 0xFE]),
        ('TS-Conf 256C 320x240 (V_CONFIG = #82)', [0xF3, 0x01, 0xAF, 0x00, 0x3E, 0x82, 0xED, 0x79, 0x18, 0xFE]),
        ('TS-Conf 16C 360x288 (V_CONFIG = #C1)', [0xF3, 0x01, 0xAF, 0x00, 0x3E, 0xC1, 0xED, 0x79, 0x18, 0xFE]),
        ('TS-Conf TXT 360x288 (V_CONFIG = #C3)', [0xF3, 0x01, 0xAF, 0x00, 0x3E, 0xC3, 0xED, 0x79, 0x18, 0xFE]),
    ],
    # Pentagon-1024 (started with ram_size 1024): the #EFF7 feature register; the AlCo bits are exclusive
    'PENTAGON-1024': [
        ('Pentagon 16c (#EFF7 = #01)', [0xF3, 0x01, 0xF7, 0xEF, 0x3E, 0x01, 0xED, 0x79, 0x18, 0xFE]),
        ('Pentagon HiRes 512 (#EFF7 = #02)', [0xF3, 0x01, 0xF7, 0xEF, 0x3E, 0x02, 0xED, 0x79, 0x18, 0xFE]),
        ('Pentagon multicolor (#EFF7 = #20)', [0xF3, 0x01, 0xF7, 0xEF, 0x3E, 0x20, 0xED, 0x79, 0x18, 0xFE]),
        ('Pentagon 384x304 (#EFF7 = #40)', [0xF3, 0x01, 0xF7, 0xEF, 0x3E, 0x40, 0xED, 0x79, 0x18, 0xFE]),
        ('Pentagon ZX (#EFF7 = #00)', [0xF3, 0x01, 0xF7, 0xEF, 0x3E, 0x00, 0xED, 0x79, 0x18, 0xFE]),
    ],
    'PROFI': [('Profi 512x240 (#DFFD bit 7)', [0xF3, 0x01, 0xFD, 0xDF, 0x3E, 0x80, 0xED, 0x79, 0x18, 0xFE])],
}


def display_frame(width, height):
    """DisplayGeometry::DisplayFrame: the smallest 352:288 frame that holds the picture, nothing squeezed"""
    rw, rh = REFERENCE
    if width * rh < height * rw:
        return -(-height * rw // rh), height
    return width, -(-width * rh // rw)


def crt_picture(width, height):
    """DisplayGeometry::Picture of a whole frame: each line once or twice when that is within 5% of 352:288, else the
    picture grown to that shape (even sizes) - the size of a screenshot or a recording of the frame"""
    aspect = REFERENCE[0] / REFERENCE[1]
    even = lambda value: (int(value) + 1) & ~1
    distance = lambda shape: shape / aspect if shape > aspect else aspect / shape
    single = width / height
    repeat = 2 if distance(single / 2) < distance(single) else 1
    if distance(single / repeat) <= 1.05:
        return even(width), even(height * repeat)
    if single < aspect:
        return even(height * aspect + 0.999), even(height)
    return even(width), even(width / aspect + 0.999)


class Api:
    def __init__(self, base):
        self.base = base.rstrip('/')

    def call(self, method, path, body=None):
        request = urllib.request.Request(self.base + path, method=method,
                                         data=json.dumps(body).encode() if body is not None else None,
                                         headers={'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(request, timeout=30) as reply:
                text = reply.read().decode()
                return json.loads(text) if text else {}
        except Exception as error:  # noqa: BLE001 - reported to the caller as a disagreement
            return {'error': str(error)}


_PIDS = {}


def window_size(port):
    """Size of the biggest on-screen window of the process listening on `port` (macOS), or None"""
    try:
        import Quartz
        if port not in _PIDS:  # lsof is slow: ask once
            _PIDS[port] = int(subprocess.check_output(['lsof', '-t', '-i', f':{port}', '-sTCP:LISTEN']).split()[0])
        pid = _PIDS[port]
    except Exception:  # noqa: BLE001
        return None
    best = None
    for window in Quartz.CGWindowListCopyWindowInfo(Quartz.kCGWindowListOptionAll, Quartz.kCGNullWindowID):
        if window.get('kCGWindowOwnerPID') == pid and window.get('kCGWindowLayer') == 0:
            bounds = window['kCGWindowBounds']
            if best is None or bounds['Width'] * bounds['Height'] > best[0] * best[1]:
                best = (int(bounds['Width']), int(bounds['Height']))
    return best


def settled_window(port):
    last, same = None, 0
    for _ in range(40):
        time.sleep(0.15)
        current = window_size(port)
        same = same + 1 if current == last else 0
        last = current
        if same >= 4:
            break
    return last


def check(api, emulator, what, problems, window_port, chrome):
    """Compare every surface for the machine as it is now; appends disagreements to `problems`"""
    mode = api.call('GET', f'/emulator/{emulator}/state/screen/mode')
    shot = api.call('GET', f'/emulator/{emulator}/capture/screen?area=full&source=live')
    if 'error' in mode or 'error' in shot or shot.get('status') != 'success':
        problems.append(f'{what}: could not read the picture: {mode.get("error") or shot.get("error") or shot}')
        return None
    frame = shot['frame']
    size = (frame['width'], frame['height'])
    name = frame['mode']
    window = shot['screen_window']
    line = f'{what}: mode {name}, frame {size[0]}x{size[1]}, working picture {window["width"]}x{window["height"]} ' \
           f'at ({window["x"]},{window["y"]})'

    def mismatch(text):
        problems.append(f'{what}: {text}')
        return '  <-- ' + text

    notes = []
    if name not in RASTER:
        notes.append(mismatch(f'video mode "{name}" is not in the raster table of this script (a new mode?)'))
    elif RASTER[name] != size and frame.get('source') == 'native':
        notes.append(mismatch(f'the raster table says {name} is {RASTER[name][0]}x{RASTER[name][1]}, the frame is {size[0]}x{size[1]}'))
    shown = size if frame.get('source') == 'external' else crt_picture(*size)
    if (shot['width'], shot['height']) != shown:
        notes.append(mismatch(f'the screenshot is {shot["width"]}x{shot["height"]}, the {size[0]}x{size[1]} frame '
                              f'on the CRT {shown[0]}x{shown[1]}'))
    if window['x'] + window['width'] > size[0] or window['y'] + window['height'] > size[1] or window['width'] == 0:
        notes.append(mismatch('the working picture does not lie inside the frame'))

    reported = mode.get('framebuffer')
    if reported and reported != f'{size[0]}x{size[1]}':
        notes.append(mismatch(f'state/screen/mode says framebuffer {reported}, the frame is {size[0]}x{size[1]}'))
    resolution = mode.get('resolution')
    # A ZX-Poly master shows the group's composed frame (2x, four modules): the module's own resolution is not its size
    # TS-Conf reports its resolution in dots; a dot is two stored pixels wide in the 16C / 256C modes
    dots = None
    if name.startswith('TS') and name != 'TSTX' and window['width'] % 2 == 0:
        dots = f'{window["width"] // 2}x{window["height"]}'
    if resolution and frame.get('source') == 'native' and resolution not in (f'{window["width"]}x{window["height"]}', dots):
        notes.append(mismatch(f'state/screen/mode says resolution {resolution}, the working picture is {window["width"]}x{window["height"]}'))
    if not reported and frame.get('source') == 'native':
        notes.append('  (state/screen/mode has no "framebuffer": the frame size cannot be cross-checked there)')

    # A video card's picture (the VDAC2's FT812) has square pixels: the window takes its own shape
    frame_shape = tuple(size) if frame.get('source') == 'external' else display_frame(*size)
    line += f', display frame {frame_shape[0]}x{frame_shape[1]}'
    if window_port:
        win = settled_window(window_port)
        if win and chrome is not None:
            picture = (win[0] - chrome[0], win[1] - chrome[1])
            zoom = picture[0] / frame_shape[0]
            line += f', window {win[0]}x{win[1]} = picture {picture[0]}x{picture[1]} (zoom {zoom:.2f})'
            if abs(picture[0] * frame_shape[1] - picture[1] * frame_shape[0]) > 4 * frame_shape[1]:
                if 780 <= win[0] <= 800:
                    # An automation session shows the remote-control bar: its toolbar cannot be narrower than ~790 px
                    notes.append('  (window is at its 790 px minimum width, which the remote-control bar of an automation '
                                 'session imposes: not a mismatch)')
                else:
                    notes.append(mismatch(f'the window picture {picture[0]}x{picture[1]} is not the display shape {frame_shape[0]}x{frame_shape[1]}'))
    print(line)
    for note in notes:
        print(note)
    return frame_shape


def run_code(api, emulator, code, blank_pages=()):
    api.call('POST', f'/emulator/{emulator}/pause')
    for page in blank_pages:
        api.call('PUT', f'/emulator/{emulator}/memory/ram/{page}/0', {'offset': 0, 'data': [0] * 16384})
    api.call('PUT', f'/emulator/{emulator}/memory/ram/2/0', {'offset': 0, 'data': code})
    api.call('PUT', f'/emulator/{emulator}/registers/pc', {'value': 0x8000})
    api.call('POST', f'/emulator/{emulator}/resume')
    time.sleep(0.8)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--url', default='http://127.0.0.1:8090/api/v1', help='WebAPI base of a running instance')
    parser.add_argument('--model', action='append', help='a model to check (repeatable); default: every creatable one')
    parser.add_argument('--switch', action='store_true', help='also drive the machines that allow it into their other video modes')
    parser.add_argument('--window-port', type=int, help='macOS: WebAPI port of the unreal-qt whose window is measured too')
    args = parser.parse_args()

    api = Api(args.url)
    models = api.call('GET', '/emulator/models').get('models')
    if not models:
        sys.exit(f'no answer from {args.url}: is the emulator running?')
    names = [m['name'] for m in models if m.get('creatable')]
    if 'PENTAGON' in names and args.switch:
        names.append('PENTAGON-1024')
    if args.model:
        names = [n for n in names if n.lower() in {m.lower() for m in args.model}]

    problems = []
    chrome = None
    for name in names:
        # "PENTAGON-1024" is PENTAGON with 1024 KB of RAM: the model whose port decoder owns #EFF7
        body = {'model': 'PENTAGON', 'ram_size': 1024} if name == 'PENTAGON-1024' else {'model': name}
        started = api.call('POST', '/emulator/start', body)
        emulator = started.get('id')
        if not emulator:
            problems.append(f'{name}: could not start: {started}')
            continue
        api.call('PUT', f'/emulator/{emulator}/audio/mixer/master', {'muted': True})
        time.sleep(1.5)

        if args.window_port and chrome is None and name.upper() in ('PENTAGON', '48K'):
            win = settled_window(args.window_port)
            if win:  # the window around a picture at zoom 2 of a 352x288 frame: what is not picture is the chrome
                chrome = (win[0] - 2 * REFERENCE[0], win[1] - 2 * REFERENCE[1])
        check(api, emulator, name, problems, args.window_port, chrome)

        if args.switch:
            if name.upper().startswith('TSL'):
                time.sleep(4)  # the TS-BIOS writes V_CONFIG while it boots: let it finish
                run_code(api, emulator, [0xF3, 0x01, 0xAF, 0x00, 0x3E, 0x03, 0xED, 0x79, 0x18, 0xFE])  # the first write after boot is lost to the BIOS; this one is the primer
                time.sleep(1.0)
            if name.upper() == 'PENTAGON':  # the one mode with an API: overscan (384x304) and its viewports
                for viewport in ('symmetric', 'full'):
                    api.call('PUT', f'/emulator/{emulator}/state/screen/overscan', {'enabled': True, 'viewport': viewport})
                    time.sleep(1.0)
                    check(api, emulator, f'{name} / overscan, viewport {viewport}', problems, args.window_port, chrome)
                api.call('PUT', f'/emulator/{emulator}/state/screen/overscan', {'enabled': False})
                time.sleep(1.0)
                check(api, emulator, f'{name} / overscan off', problems, args.window_port, chrome)
            for what, code in SWITCHES.get(name.upper(), []):
                run_code(api, emulator, code, (0, 1, 2) if name.upper().startswith('TSL') else ())
                check(api, emulator, f'{name} / {what}', problems, args.window_port, chrome)
        api.call('DELETE', f'/emulator/{emulator}')
        time.sleep(0.3)

    print()
    if problems:
        print(f'{len(problems)} disagreement(s):')
        for problem in problems:
            print('  -', problem)
        return 1
    print('every surface agrees')
    return 0


if __name__ == '__main__':
    sys.exit(main())
