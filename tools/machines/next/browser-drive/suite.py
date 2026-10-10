#!/usr/bin/env python3
"""Run every program of the card's tests/ tree the way a user does: through NextZXOS's Browser, one fresh boot each, and photograph the result.

    suite.py --card scratch/nextcard-show [--out scratch/browser-suite] [--wait 12] [--port 8090] [--only NAME ...]

The card is the folder the running NEXT machine uses as its SD card (its tests/<area>/<test>/ folders hold the ZXSpectrumNextTests
programs and board photographs; see tools/machines/next/realboard). For each program: reset, boot, Browser, tests/<area>/<test>/,
run the program, wait, capture the screen. Writes <out>/<program>.png, <out>/sheet-<area>.png (ours | board photo) and report.md.
This tests NextZXOS's own loading of snapshots (the Multiface NMI, esxDOS, the 48K mode switch) - the path a person takes - where
tools/machines/next/realboard tests the snapshot loader.
"""
import argparse, base64, json, os, sys, time, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from drive import Machine  # noqa: E402


# keys a program waits for after it starts ("2" sets 28 MHz, "5" runs every test of the Z80N programs)
KEYS = {'!Z80N.snx': ['2', '5'], '!Z80Nc2.snx': ['2', '5'], 'UlaScrol.snx': ['r']}  # R: skip UlaScroll's 10 s animation


def capture(machine, path):
    data = machine.call('/%s/capture/screen' % machine.id) if False else json.load(urllib.request.urlopen(
        '%s/%s/capture/screen' % (machine.base, machine.id)))
    with open(path, 'wb') as f:
        f.write(base64.b64decode(data['data']))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--card', required=True)
    parser.add_argument('--out', default='scratch/browser-suite')
    parser.add_argument('--wait', type=float, default=12.0)
    parser.add_argument('--port', type=int, default=8090)
    parser.add_argument('--only', nargs='*')
    args = parser.parse_args()
    from PIL import Image, ImageDraw
    machine = Machine(args.port)
    os.makedirs(args.out, exist_ok=True)
    root = os.path.join(args.card, 'tests')
    rows, sheets = [], {}
    for area in sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d))):
        for test in sorted(os.listdir(os.path.join(root, area))):
            folder = os.path.join(root, area, test)
            if not os.path.isdir(folder):
                continue
            for program in sorted(f for f in os.listdir(folder) if f.lower().endswith(('.snx', '.sna'))):
                if args.only and program not in args.only:
                    continue
                machine.call('/%s/reset' % machine.id, {})
                machine.idle(2.5)  # the welcome page waits for a key
                machine.tap('space')
                machine.idle(2)
                machine.tap('b')
                machine.idle(2.5)
                machine.root()
                folder = args.card
                for part in ('tests', area, test):
                    machine.reach(folder, part)
                    folder = os.path.join(folder, part)
                machine.reach(folder, program, runs=True)
                if program.lower().endswith('.sna'):  # NextZXOS's own snapshot loader page: ENTER starts the program
                    time.sleep(4)
                    machine.tap('enter')
                for key in KEYS.get(program, []):
                    time.sleep(2)
                    machine.tap(key)
                time.sleep(args.wait)
                state = machine.call('/%s/state/next' % machine.id)
                png = os.path.join(args.out, program.replace('!', '') + '.png')
                capture(machine, png)
                clock = state['machine']['cpu_clock_hz'] / 1e6
                black = len(set(Image.open(png).convert('RGB').getdata())) <= 2
                rows.append((area, program, 'BLACK' if black else 'picture', '%g MHz' % clock,
                             'DivMMC in' if state['divmmc']['mapped'] else 'DivMMC out'))
                print('%-12s %-16s %-8s %s' % rows[-1][:4], flush=True)
                photo = next((os.path.join(folder, f) for f in sorted(os.listdir(folder)) if f.lower().endswith(('.jpg', '.png'))), None)
                cell = Image.new('RGB', (652, 256), (40, 40, 40))
                cell.paste(Image.open(png).convert('RGB').resize((320, 256)), (0, 0))
                if photo:
                    cell.paste(Image.open(photo).convert('RGB').resize((320, 256)), (332, 0))
                ImageDraw.Draw(cell).text((4, 4), '%s via the Browser | board photo' % program, fill=(255, 255, 0))
                sheets.setdefault(area, []).append(cell)
    for area, cells in sheets.items():
        sheet = Image.new('RGB', (652, 260 * len(cells)))
        for i, c in enumerate(cells):
            sheet.paste(c, (0, i * 260))
        sheet.save(os.path.join(args.out, 'sheet-%s.png' % area))
    with open(os.path.join(args.out, 'report.md'), 'w') as f:
        f.write('| area | program | screen | CPU | DivMMC |\n|:--|:--|:--|:--|:--|\n')
        for r in rows:
            f.write('| %s | %s | %s | %s | %s |\n' % r)
    print('%d programs, %d black' % (len(rows), sum(r[2] == 'BLACK' for r in rows)))


main()
