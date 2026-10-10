#!/usr/bin/env python3
"""Run the titles of a games / demos collection on the card through NextZXOS's Browser and photograph each.

    collection.py --card <card> [--index <card>/collection/index.tsv] [--out DIR] [--wait 12] [--port 8090]
                  [--only-category games/platform ...] [--start N] [--limit N] [--match TEXT]

The collection folder (<card>/collection/{games,demos}/<genre>/<Title>/, built by the collection builder with an index.tsv whose
`main_file` column is `games/<genre>/<Title>/<file>`) is walked the way a person walks it: reset, boot, Browser, directory by
directory (only the part of the path that differs from the previous title's folder: EDIT up, then down; from the card root for the
first title and after any title that did not show a picture), ENTER on the main file. A directory or file name is found by the shortest unique prefix typed into the Browser's
search (H); names that begin with a character the search cannot type are reached by counting down from the top of the listing.
For each title the report keeps: the picture after `--wait` seconds, whether the machine is still in NextZXOS (idle at the key-wait
loop = nothing started), the CPU clock and the DivMMC mapping. Writes <out>/<NNN>-<title>.png, <out>/sheet-<category>.png and
<out>/report.md (a title that did not start is listed first).
"""
import argparse, base64, csv, io, json, os, re, sys, time, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from drive import Machine  # noqa: E402

SAFE = re.compile(r'^[A-Za-z0-9]+')


def sorted_entries(folder):
    """The Browser's order: '.' and '..' on top, then names case-insensitively"""
    names = sorted(os.listdir(folder), key=lambda n: n.lower())
    return ['.', '..'] + names


def reach(machine, folder, name):
    """Put the Browser's cursor on `name` of the directory shown (host path `folder`)"""
    entries = sorted_entries(folder)
    lower = [e.lower() for e in entries]
    match = SAFE.match(name)
    if match:
        text = match.group(0).lower()
        for length in range(1, len(text) + 1):
            prefix = text[:length]
            hits = [e for e in lower if e.startswith(prefix)]
            if len(hits) == 1:
                machine.tap('h')
                time.sleep(0.3)
                machine.call('/%s/keyboard/type' % machine.id, {'text': name[:length], 'delay_frames': 5})
                time.sleep(0.4 + 0.12 * length)
                machine.idle(0.3)
                machine.tap('enter')
                time.sleep(0.3)
                return True
        # ambiguous: fall through to counting
    index = entries.index(name)
    for _ in range(index):
        machine.tap('down', 4)
        time.sleep(0.12)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--card', required=True)
    parser.add_argument('--index')
    parser.add_argument('--out', default='scratch/collection-run')
    parser.add_argument('--wait', type=float, default=12.0)
    parser.add_argument('--port', type=int, default=8090)
    parser.add_argument('--only-category', nargs='*')
    parser.add_argument('--start', type=int, default=0)
    parser.add_argument('--limit', type=int, default=10 ** 9)
    parser.add_argument('--match')
    parser.add_argument('--keys', default='enter,space,1', help='keys tried one at a time after the first picture until the picture changes (a title screen waits for one); empty = none')
    args = parser.parse_args()
    from PIL import Image, ImageDraw
    index = args.index or os.path.join(args.card, 'collection', 'index.tsv')
    root = os.path.join(args.card, 'collection')
    with open(index, newline='') as f:
        titles = list(csv.DictReader(f, delimiter='\t'))
    machine = Machine(args.port)
    os.makedirs(args.out, exist_ok=True)
    rows, sheets = [], {}
    done = 0
    position = None  # the Browser's directory, as names from the card root; None = unknown (resync from the root)
    for n, t in enumerate(titles):
        if n < args.start or done >= args.limit:
            continue
        category = t['category'] + '/' + t['genre_or_kind']
        if args.only_category and category not in args.only_category:
            continue
        if args.match and args.match.lower() not in t['title'].lower():
            continue
        parts = t['main_file'].split('/')
        host = os.path.join(root, *parts)
        if not os.path.exists(host):
            rows.append((n, category, t['title'], 'MISSING', '', ''))
            continue
        done += 1
        machine.call('/%s/reset' % machine.id, {})
        machine.idle(2.5)
        machine.tap('space')
        machine.idle(2)
        machine.tap('b')
        machine.idle(2.5)
        # The Browser reopens in the directory it was last in (the title's own folder), so walk only the difference: EDIT up to the
        # common ancestor, then down. The card root is the starting point only for the first title and after anything went wrong
        target = ['collection'] + parts[:-1]  # the directory of the main file, from the card root
        if position is None:
            machine.root()
            position = []
        common = 0
        while common < min(len(position), len(target)) and position[common] == target[common]:
            common += 1
        for _ in range(len(position) - common):
            machine.tap('edit')
            machine.idle(0.6)
        folder = os.path.join(args.card, *target[:common])
        for name in target[common:]:
            reach(machine, folder, name)
            machine.tap('enter')
            machine.idle(1.2)
            folder = os.path.join(folder, name)
        position = target  # (until the verdict says otherwise)
        reach(machine, folder, parts[-1])
        machine.tap('enter')  # run the main file
        time.sleep(args.wait)
        state = machine.call('/%s/state/next' % machine.id)
        regs = machine.call('/%s/registers' % machine.id)
        # A BASIC program waiting for a key sits in the same key-wait loop (#0C8F) as the Browser does, so for .bas the loop says nothing:
        # those titles are judged by what is on the screen (NextZXOS grey paper) after the start keys
        is_basic = parts[-1].lower().endswith('.bas')
        idle = (not is_basic) and regs['interrupt']['halted'] and regs['special']['pc'] == 0x0C8F
        png = os.path.join(args.out, '%03d-%s.png' % (n, re.sub(r'[^A-Za-z0-9]+', '_', t['title'])[:40]))
        data = json.load(urllib.request.urlopen('%s/%s/capture/screen' % (machine.base, machine.id)))
        with open(png, 'wb') as f:
            f.write(base64.b64decode(data['data']))
        im = Image.open(png).convert('RGB')
        flat = len(set(im.getdata())) <= 2
        verdict = 'IN NEXTZXOS' if idle else ('flat' if flat else 'picture')
        # Loading is not enough: a program that runs sits on its title screen until a key. Watch it move on its own, then press the
        # start keys and see whether the picture changes, whether it stays alive, and what the CPU does
        life = ''
        if not idle:
            def grab():
                d = json.load(urllib.request.urlopen('%s/%s/capture/screen' % (machine.base, machine.id)))
                return Image.open(io.BytesIO(base64.b64decode(d['data']))).convert('RGB')

            def changed(a, b):
                pa, pb = a.getdata(), b.getdata()
                return sum(1 for x, y in zip(pa, pb) if x != y) / max(1, len(pa))
            time.sleep(1.0)
            moves = changed(im, grab()) > 0.001  # animates by itself
            after = im
            if args.keys:
                # one key at a time, the next only if the picture did not change: a title screen takes any key, and a key that is not
                # needed can end a BASIC program (SPACE + something is BREAK), so the keys are not stacked
                for key in args.keys.split(','):
                    machine.tap(key)
                    time.sleep(2.5)
                    after = grab()
                    if changed(im, after) > 0.001:
                        break
                time.sleep(3.0)
                after = grab()
            reacts = changed(im, after) > 0.001
            regs2 = machine.call('/%s/registers' % machine.id)
            if is_basic:
                counts = {}
                for px in after.getdata():
                    counts[px] = counts.get(px, 0) + 1
                top, n_top = max(counts.items(), key=lambda kv: kv[1])
                back = top == (182, 182, 182) and n_top > 0.6 * (after.size[0] * after.size[1])  # NextZXOS's own screen
            else:
                back = regs2['interrupt']['halted'] and regs2['special']['pc'] == 0x0C8F
            life = ('moves ' if moves else 'static ') + ('reacts ' if reacts else 'no-reaction ') + ('BACK-IN-OS' if back else 'running')
            after.save(png.replace('.png', '-keys.png'))
            if back:
                verdict = 'IN NEXTZXOS'
        if verdict != 'picture':
            position = None  # something may have gone wrong: the next title starts from the root again
        rows.append((n, category, t['title'], verdict, '%g MHz' % (state['machine']['cpu_clock_hz'] / 1e6),
                     'DivMMC in' if state['divmmc']['mapped'] else 'DivMMC out'))
        print('%3d %-28s %-34s %-12s %s  %s' % (n, category, t['title'][:34], verdict, rows[-1][4], life), flush=True)
        cell = Image.new('RGB', (326, 262), (40, 40, 40))
        cell.paste(im.resize((320, 256)), (3, 3))
        ImageDraw.Draw(cell).text((6, 6), '%d %s' % (n, t['title'][:40]), fill=(255, 255, 0))
        sheets.setdefault(category.replace('/', '_'), []).append(cell)
    for cat, cells in sheets.items():
        for page in range(0, len(cells), 12):
            chunk = cells[page:page + 12]
            sheet = Image.new('RGB', (326 * 4, 262 * ((len(chunk) + 3) // 4)))
            for i, c in enumerate(chunk):
                sheet.paste(c, ((i % 4) * 326, (i // 4) * 262))
            sheet.save(os.path.join(args.out, 'sheet-%s-%d.png' % (cat, page // 12)))
    with open(os.path.join(args.out, 'report.md'), 'w') as f:
        f.write('| # | category | title | result | CPU | DivMMC |\n|--:|:--|:--|:--|:--|:--|\n')
        for r in sorted(rows, key=lambda r: (r[3] == 'picture', r[0])):
            f.write('| %d | %s | %s | %s | %s | %s |\n' % r)
    print('%d titles, %d in NextZXOS (not started), %d flat' % (len(rows), sum(r[3] == 'IN NEXTZXOS' for r in rows),
                                                              sum(r[3] == 'flat' for r in rows)))


main()
