#!/usr/bin/env python3
"""Print a macOS crash report (.ips) as text: process, pid, exception, whether the binary had a sanitizer runtime, the
crashed thread's stack, and the stacks of every other thread that passes through project code.

Usage:
    tools/sanitize/ips_stacks.py ~/Library/Logs/DiagnosticReports/unreal-qt-2026-10-10-001426.ips [--all] [--frames N]
    tools/sanitize/ips_stacks.py --list            # unreal-* reports, newest first, with pid and sanitizer flag

Guide: docs/testing/sanitizers.md
"""
import argparse
import glob
import json
import os
import sys

REPORTS = os.path.expanduser('~/Library/Logs/DiagnosticReports')


def is_project(image):
    """The emulator's own binaries: unreal-qt, unreal-screen-viewer, core-tests, unreal-asm-tests ..."""
    return image.startswith('unreal') or image.endswith('-tests')


def load(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        header, _, body = f.read().partition('\n')
    return json.loads(header), json.loads(body)


def frame_text(frame, images):
    image = images[frame['imageIndex']].get('name', '?') if 'imageIndex' in frame else '?'
    where = f" {frame['sourceFile']}:{frame.get('sourceLine', '')}" if 'sourceFile' in frame else ''
    return image, f"{image:<28} {frame.get('symbol', '?')[:140]}{where}"


def sanitizer_of(body):
    names = [i.get('name') or '' for i in body.get('usedImages', [])]
    for kind in ('asan', 'tsan', 'ubsan'):
        if any(f'clang_rt.{kind}' in n for n in names):
            return kind
    return ''


def print_report(path, show_all, depth):
    header, body = load(path)
    images = body.get('usedImages', [])
    exc = body.get('exception', {})
    print(f"{os.path.basename(path)}: {header.get('app_name')} pid {body.get('pid')} at {body.get('captureTime')}")
    print(f"  {exc.get('type')} {exc.get('signal')} {exc.get('subtype', '')} {sanitizer_of(body) or 'no sanitizer'}")
    asi = body.get('asi')
    if asi:
        print(f"  {asi}")
    crashed = body.get('faultingThread', 0)
    for index, thread in enumerate(body.get('threads', [])):
        lines = [frame_text(f, images) for f in thread.get('frames', [])]
        project = any(is_project(image) for image, _ in lines)
        if index != crashed and not show_all and not project:
            continue
        name = thread.get('name') or thread.get('queue') or ''
        print(f"\n== thread {index} {name}{'  <- CRASHED' if index == crashed else ''}")
        for _, text in lines[:depth]:
            print('   ' + text)


def list_reports():
    paths = sorted(glob.glob(os.path.join(REPORTS, 'unreal-*.ips')), key=os.path.getmtime, reverse=True)
    for path in paths:
        try:
            _, body = load(path)
        except (OSError, ValueError):
            continue
        exc = body.get('exception', {})
        print(f"{os.path.basename(path)}  pid {body.get('pid')}  {exc.get('type')}/{exc.get('signal')}  "
              f"{sanitizer_of(body) or '-'}  {body.get('captureTime')}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('report', nargs='?')
    parser.add_argument('--list', action='store_true', help='list unreal-* reports in ' + REPORTS)
    parser.add_argument('--all', action='store_true', help='every thread, not only those in project code')
    parser.add_argument('--frames', type=int, default=16, help='frames per thread (16)')
    args = parser.parse_args()
    if args.list:
        list_reports()
    elif args.report:
        print_report(args.report, args.all, args.frames)
    else:
        parser.print_help()
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
