#!/usr/bin/env python3
"""Regenerates everything of the Stream Deck icon packs, in order, and checks the result.

    python3 tools/icons/regenerate_streamdeck_icons.py              # the glyph list, every pack, the doc, the checks
    python3 tools/icons/regenerate_streamdeck_icons.py magicwb os4  # only these packs (+ the doc and the checks)

1. tools/icons/streamdeck_glyphs.py         -> data/streamdeck/icons/glyphs.json
2. tools/icons/streamdeck_packs/<pack>.py   -> data/streamdeck/icons/packs/<pack>/ (magicwb first: the others
                                               re-skin its drawings)
3. tools/icons/streamdeck_icons_doc.py      -> docs/companions/streamdeck/icons/glyphs.md
4. render_streamdeck_icons.py --validate, then a render with contact sheets (scratch/streamdeck-icons/), then --check
   (the same sources give the same bytes)
Stops at the first step that fails. Requires Pillow, numpy, scipy, cairosvg.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
ORDER = ['magicwb', 'newicons', 'glowicons', 'workbench13', 'duotone', 'banner', 'keycap', 'os4']


def run(*args):
    print('$', ' '.join(args), flush=True)
    r = subprocess.run([sys.executable] + list(args), cwd=ROOT)
    if r.returncode:
        sys.exit('failed: %s' % ' '.join(args))


def main():
    packs = sys.argv[1:] or ORDER
    unknown = [p for p in packs if not os.path.isfile(os.path.join(HERE, 'streamdeck_packs', p + '.py'))]
    if unknown:
        sys.exit('no generator for: %s (tools/icons/streamdeck_packs/<pack>.py)' % ', '.join(unknown))
    run('tools/icons/streamdeck_glyphs.py')
    for p in sorted(packs, key=lambda p: (ORDER.index(p) if p in ORDER else len(ORDER), p)):
        run('tools/icons/streamdeck_packs/%s.py' % p)
    run('tools/icons/streamdeck_icons_doc.py')
    sel = ['--packs', ','.join(packs)] if sys.argv[1:] else []
    run('tools/icons/render_streamdeck_icons.py', '--validate', *sel)
    run('tools/icons/render_streamdeck_icons.py', *sel)
    run('tools/icons/render_streamdeck_icons.py', '--check', *sel)
    print('done: sheets in scratch/streamdeck-icons/sheets/')


if __name__ == '__main__':
    main()
