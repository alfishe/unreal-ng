#!/usr/bin/env python3
"""Run every ZXSpectrumNextTests program on our NEXT machine, judge the ones that carry their own verdict, and lay the rest next to
the photograph of the real board.

    UNREAL_NEXT_TESTS=<clone of MrKWatkins/ZXSpectrumNextTests> run-all.py [--out DIR] [--only NAME ...]

Needs a built core-tests (CORE_TESTS overrides the path) and PIL. Writes DIR/<name>.png (our picture), DIR/sheet-<area>.png
(ours | board photo, side by side) and DIR/report.md. The exit status is non-zero when a program with an automatic verdict fails.

A verdict is automatic only where the program paints its own pass / fail (a red error cell, a green / red border). Everything else
is "look": the photograph is the reference, not another emulator.
"""
import argparse, os, struct, subprocess, sys
from PIL import Image, ImageDraw

RED = {(255, 0, 0), (182, 0, 0)}   # error red: ULA colour 2 normal / bright
GREEN = {(0, 182, 0), (0, 255, 0)}

# program -> (frames, keys "key@frame:count,...", rule)  (default: 500 frames, no keys, look)
PROGRAMS = {
    '!NextReg.snx': (400, '', 'nextreg-cells'),
    'NReg0x69.snx': (300, '', 'border-green'),
    '!Z80N.snx': (1200, '2@10:3,5@40:3', 'z80n-rows'),
    '!Z80Nc2.snx': (1500, '2@10:3,5@40:3', 'look'),
    'z80bltst.sna': (500, '', 'no-red'),
}


def frame(path):
    b = open(path, 'rb').read()
    w, h = struct.unpack('<II', b[:8])
    return Image.frombytes('RGBA', (w, h), b[8:8 + w * h * 4]).convert('RGB')


def rule_result(rule, im):
    px = im.load()
    if rule == 'nextreg-cells':   # 16 x 16 cells of the register grid; red = R/W/default ERROR
        bad = [r * 16 + c for r in range(16) for c in range(16) if px[66 + c * 16 + 8, 64 + r * 16 + 8] in RED]
        return not bad, 'error cells: ' + (' '.join('%02X' % n for n in bad) or 'none')
    if rule == 'border-green':
        return px[4, 4] in GREEN, 'border %s' % (px[4, 4],)
    if rule == 'no-red':
        red = sum(1 for y in range(64, 400) for x in range(64, 576) if px[x, y] in RED)
        green = sum(1 for y in range(64, 400) for x in range(64, 576) if px[x, y] in GREEN)
        return red == 0 and green > 100, 'red %d green %d' % (red, green)
    if rule == 'z80n-rows':       # the result column: ERR rows paper red, OK rows paper green
        err = ok = 0
        for row in range(24):
            for col in range(28, 32):
                p = px[64 + col * 16 + 2, 64 + row * 16 + 2]
                err += p in RED
                ok += p in GREEN
        return err == 0 and ok > 0, 'err %d ok %d' % (err, ok)
    return None, 'look'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='scratch/realboard')
    ap.add_argument('--only', nargs='*')
    args = ap.parse_args()
    clone = os.environ.get('UNREAL_NEXT_TESTS')
    if not clone:
        sys.exit('UNREAL_NEXT_TESTS is not set')
    binary = os.environ.get('CORE_TESTS', 'cmake-build-agent-release/bin/core-tests')
    tests = os.path.join(clone, 'Tests')
    os.makedirs(args.out, exist_ok=True)
    rows, sheets = [], {}
    for area in sorted(os.listdir(tests)):
        ap_ = os.path.join(tests, area)
        if not os.path.isdir(ap_):
            continue
        for test in sorted(os.listdir(ap_)):
            tp = os.path.join(ap_, test)
            if not os.path.isdir(tp):
                continue
            for prog in sorted(f for f in os.listdir(tp) if f.lower().endswith(('.snx', '.sna'))):
                if args.only and prog not in args.only:
                    continue
                frames, keys, rule = PROGRAMS.get(prog, (500, '', 'look'))
                env = dict(os.environ, UNREAL_NEX=os.path.join(tp, prog), UNREAL_NEX_FRAMES=str(frames),
                           UNREAL_NEX_KEYS=keys, UNREAL_NEX_OUT=args.out)
                subprocess.run([binary, '--gtest_filter=LoaderNexRun*'], env=env, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
                raw = os.path.join(args.out, 'frame.rgba')
                if not os.path.exists(raw):
                    rows.append((area, test, prog, 'NO PICTURE', ''))
                    continue
                im = frame(raw)
                os.remove(raw)
                name = prog.replace('!', '')
                im.save(os.path.join(args.out, name + '.png'))
                ok, detail = rule_result(rule, im)
                rows.append((area, test, prog, 'look' if ok is None else ('PASS' if ok else 'FAIL'), detail))
                photo = next((os.path.join(tp, f) for f in sorted(os.listdir(tp)) if f.lower().endswith(('.jpg', '.png'))), None)
                cell = Image.new('RGB', (652, 256), (40, 40, 40))
                cell.paste(im.resize((320, 256)), (0, 0))
                if photo:
                    cell.paste(Image.open(photo).convert('RGB').resize((320, 256)), (332, 0))
                ImageDraw.Draw(cell).text((4, 4), '%s ours | board photo' % prog, fill=(255, 255, 0))
                sheets.setdefault(area, []).append(cell)
                print('%-14s %-34s %-5s %s' % (area, prog, rows[-1][3], detail), flush=True)
    for area, cells in sheets.items():
        sheet = Image.new('RGB', (652, 260 * len(cells)))
        for i, c in enumerate(cells):
            sheet.paste(c, (0, i * 260))
        sheet.save(os.path.join(args.out, 'sheet-%s.png' % area))
    with open(os.path.join(args.out, 'report.md'), 'w') as f:
        f.write('| area | test | program | verdict | detail |\n|:--|:--|:--|:--|:--|\n')
        for r in rows:
            f.write('| %s | %s | %s | %s | %s |\n' % r)
    failed = [r for r in rows if r[3] in ('FAIL', 'NO PICTURE')]
    print('\n%d programs, %d automatic PASS, %d FAIL, %d look' % (len(rows), sum(r[3] == 'PASS' for r in rows),
          len(failed), sum(r[3] == 'look' for r in rows)))
    sys.exit(1 if failed else 0)


main()
