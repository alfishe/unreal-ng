#!/usr/bin/env python3
"""Records every machine (and the video modes that can be driven) to real files through the WebAPI and checks them.

For each model: start it, record ~1.5 s of H.264 into <out>/<model>.mp4, stop, and compare the file (ffprobe) with the
picture DisplayGeometry names for the frame the recording started on (core/src/emulator/video/displaygeometry.h,
`Picture`, mirrored in picture() below). One frame of each file is extracted as <out>/<model>.png to look at.
With --switch the machines with drivable modes (check_video_modes.SWITCHES) are recorded across the mode changes: the
file must keep its size and keep growing. Exit code 0 = every file is what it should be.

Needs ffprobe / ffmpeg on PATH and a running unreal-qt (see .recipe/testing/video-modes.md for the ports).
"""
import argparse
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_video_modes import Api, SWITCHES, run_code  # noqa: E402

REFERENCE = (352, 288)


def even(value):
    return (int(value) + 1) & ~1


def picture(width, height, aspect=0.0):
    """DisplayGeometry::Picture"""
    if aspect <= 0:
        aspect = REFERENCE[0] / REFERENCE[1]
    single = width / height
    doubled = single / 2

    def dist(x):
        return x / aspect if x > aspect else aspect / x
    use_double = dist(doubled) < dist(single)
    shape = doubled if use_double else single
    if dist(shape) <= 1.05:
        return even(width), even(height * (2 if use_double else 1))
    if single < aspect:
        return even(height * aspect + 0.999), even(height)
    return even(width), even(width / aspect + 0.999)


def probe(path):
    out = subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-count_frames', '-show_entries',
                                   'stream=width,height,nb_read_frames', '-of', 'json', path])
    stream = json.loads(out)['streams'][0]
    return int(stream['width']), int(stream['height']), int(stream.get('nb_read_frames', 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--url', default='http://127.0.0.1:8090/api/v1')
    parser.add_argument('--out', default='scratch/record-all')
    parser.add_argument('--model', action='append')
    parser.add_argument('--switch', action='store_true')
    parser.add_argument('--scale', type=int, default=1)
    parser.add_argument('--profile', help='an output profile (1080p, 1440p, 4k): the file is that fixed frame, the picture fitted into it')
    args = parser.parse_args()

    api = Api(args.url)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    models = [m['name'] for m in api.call('GET', '/emulator/models').get('models', []) if m.get('creatable')]
    if args.model:
        wanted = {m.lower() for m in args.model}
        models = [m for m in models if m.lower() in wanted]

    problems = []
    for name in models:
        started = api.call('POST', '/emulator/start', {'model': name})
        emulator = started.get('id')
        if not emulator:
            problems.append(f'{name}: could not start: {started}')
            continue
        api.call('PUT', f'/emulator/{emulator}/audio/mixer/master', {'muted': True})
        time.sleep(3 if name.upper().startswith('TSL') else 1.5)

        shot = api.call('GET', f'/emulator/{emulator}/capture/screen?area=full&source=live')
        frame = shot.get('frame')
        if not frame:
            problems.append(f'{name}: no frame: {shot}')
            api.call('DELETE', f'/emulator/{emulator}')
            continue
        expected = picture(frame['width'], frame['height'])
        if frame.get('source') == 'composed':
            expected = (frame['width'], frame['height'])  # a ZX-Poly master records the group's composed frame

        jobs = [(name, None)]
        if args.switch:
            jobs += [(f'{name}-{i}', (what, code)) for i, (what, code) in enumerate(SWITCHES.get(name.upper(), []))]
        # one recording per model across all its switches: the file must keep one size
        path = os.path.join(out, f'{name}.mp4')
        if os.path.exists(path):
            os.remove(path)
        reply = api.call('POST', f'/emulator/{emulator}/video/record',
                         dict({'action': 'start', 'format': 'h264', 'scale': args.scale, 'filename': path},
                              **({'profile': args.profile} if args.profile else {})))
        if not reply.get('recording'):
            problems.append(f'{name}: the recording did not start: {reply}')
            api.call('DELETE', f'/emulator/{emulator}')
            continue
        time.sleep(1.5)
        modes = [frame['mode']]
        if args.switch:
            for what, code in SWITCHES.get(name.upper(), []):
                run_code(api, emulator, code, (0, 1, 2) if name.upper().startswith('TSL') else ())
                modes.append(what)
                time.sleep(0.8)
        stop = api.call('POST', f'/emulator/{emulator}/video/record', {'action': 'stop'})
        time.sleep(0.5)
        api.call('DELETE', f'/emulator/{emulator}')
        try:
            width, height, frames = probe(path)
        except Exception as error:  # noqa: BLE001
            problems.append(f'{name}: the file cannot be read: {error}')
            continue
        want = (expected[0] * args.scale, expected[1] * args.scale)
        if args.profile:
            want = {'1080p': (1920, 1080), '1440p': (2560, 1440), '4k': (3840, 2160)}[args.profile]
        verdict = 'ok'
        if (width, height) != want:
            verdict = f'WRONG SIZE, expected {want[0]}x{want[1]}'
            problems.append(f'{name}: file {width}x{height}, expected {want[0]}x{want[1]} (frame {frame["width"]}x{frame["height"]}, {frame["mode"]})')
        if frames < 20:
            verdict += ' / FEW FRAMES'
            problems.append(f'{name}: only {frames} frames in the file')
        subprocess.call(['ffmpeg', '-v', 'error', '-y', '-sseof', '-0.3', '-i', path, '-frames:v', '1',
                         os.path.join(out, f'{name}.png')])
        print(f'{name:14} {frame["mode"]:13} frame {frame["width"]}x{frame["height"]} -> file {width}x{height}, '
              f'{frames} frames, {len(modes) - 1} switches  {verdict}')

    print()
    if problems:
        print(f'{len(problems)} problem(s):')
        for problem in problems:
            print('  -', problem)
        return 1
    print(f'every file is the picture of its frame; PNGs in {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
