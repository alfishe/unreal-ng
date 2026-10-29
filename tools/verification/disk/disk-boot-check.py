#!/usr/bin/env python3
"""Boot disk images in unreal-ng and keep a screenshot of each: does the image insert, and where does the program get to
after N frames. One fresh machine per image; a results table, the screenshots and a contact sheet go to --out.

    python3 tools/verification/disk/disk-boot-check.py --launch --model auto --frames 6000 disks/*.dsk
    python3 tools/verification/disk/disk-boot-check.py --port 8090 --key none game.trd

The +3 loader menu takes ENTER (the default --key); TR-DOS images autostart on insert, --key none there. A black
screenshot with the floppy controller still in its execution phase usually means a protection check that never ends:
look at it with fdc-probe.py.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import disklib  # noqa: E402


def blank(png):
    """True when the screenshot has a single colour (None without Pillow)"""
    try:
        from PIL import Image
    except ImportError:
        return None
    with Image.open(png) as image:
        return len(image.convert('RGB').getcolors(1 << 24) or []) == 1


def contact_sheet(pngs, path, columns=3):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return False
    images = [Image.open(p).convert('RGB') for p in pngs]
    if not images:
        return False
    w, h = images[0].size
    label = 14
    rows = (len(images) + columns - 1) // columns
    sheet = Image.new('RGB', (w * columns, (h + label) * rows))
    draw = ImageDraw.Draw(sheet)
    for i, (image, png) in enumerate(zip(images, pngs)):
        x, y = (i % columns) * w, (i // columns) * (h + label)
        sheet.paste(image.resize((w, h)), (x, y + label))
        draw.text((x + 4, y + 1), os.path.splitext(os.path.basename(png))[0], fill=(255, 255, 255))
    sheet.save(path)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('images', nargs='+')
    disklib.add_instance_args(parser)
    parser.add_argument('--model', default='auto', help='machine model, or auto (DSK: PLUS3, else PENTAGON)')
    parser.add_argument('--frames', type=int, default=6000, help='frames to run after the key (default 6000)')
    parser.add_argument('--settle', type=int, default=150, help='frames before the key (default 150)')
    parser.add_argument('--key', default='enter', help="key tapped after --settle frames, or 'none' (default enter)")
    parser.add_argument('--out', default=os.path.join(disklib.REPO, 'scratch', 'disk-boot-check'))
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    api, process = disklib.open_instance(args)
    rows, pngs = [], []
    try:
        for path in args.images:
            name = os.path.splitext(os.path.basename(path))[0]
            model = disklib.model_for(path, args.model)
            machine = disklib.Machine(api, model)
            try:
                ok, message = machine.insert(path)
                if not ok:
                    rows.append((name, model, 'insert failed', message, '', '', ''))
                    print(f'{name}\t{model}\tinsert failed\t{message}', flush=True)
                    continue
                machine.resume()
                machine.run_frames(args.settle)
                if args.key != 'none':
                    machine.tap(args.key)
                machine.run_frames(args.frames)
                machine.pause()
                png = os.path.join(args.out, name + '.png')
                shot = machine.screenshot(png)
                pc = '%04X' % machine.registers().get('special', {}).get('pc', 0)
                fdc = machine.fdc()
                fdc_state = f"{fdc.get('command', {}).get('name', '')}/{fdc.get('phase', '')}" if fdc else ''
                empty = blank(png) if shot else None
                status = 'no screenshot' if not shot else ('blank screen' if empty else 'picture')
                if shot:
                    pngs.append(png)
                rows.append((name, model, status, message, pc, fdc_state, png if shot else ''))
                print(f'{name}\t{model}\t{status}\tpc {pc}\tfdc {fdc_state}', flush=True)
            finally:
                machine.close()
    finally:
        disklib.stop(process)

    with open(os.path.join(args.out, 'results.tsv'), 'w') as out:
        out.write('# image\tmodel\tstatus\tinsert message\tpc\tfdc command/phase\tscreenshot\n')
        for row in rows:
            out.write('\t'.join(str(v) for v in row) + '\n')
    if contact_sheet(pngs, os.path.join(args.out, 'sheet.png')):
        print(f"sheet: {os.path.join(args.out, 'sheet.png')}")
    failed = sum(1 for r in rows if r[2] != 'picture')
    print(f'{len(rows) - failed} of {len(rows)} images show a picture; results: {os.path.join(args.out, "results.tsv")}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
