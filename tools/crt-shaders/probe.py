#!/usr/bin/env python3
"""Every pass of every preset transpiled to GLSL 1.20 (vertex and fragment) by librashader-cli: which fail, and the
features each preset needs (float / sRGB framebuffers, mipmaps, frame_count_mod, integer and derivative functions).
The reason unreal-qt's window runs a 3.3 core context.

usage: probe.py [--slang DIR] [--cli PATH]"""
import json
import os
import subprocess

from common import parser
from presets import PRESETS


def main():
    args = parser(__doc__, cli=True).parse_args()
    if not args.slang:
        raise SystemExit('--slang (or $SLANG_SHADERS) is needed: a libretro slang-shaders checkout')
    for key, preset in PRESETS:
        p = json.loads(subprocess.run([args.cli, 'parse', '-p', os.path.join(args.slang, preset)],
                                      capture_output=True, text=True).stdout)
        fails = []
        feats = set()
        for i, ps in enumerate(p['passes']):
            m = ps['meta']
            if m['float_framebuffer']: feats.add('float-fb')
            if m['srgb_framebuffer']: feats.add('srgb-fb')
            if m['mipmap_input']: feats.add('mipmap')
            if m['frame_count_mod']: feats.add('fcmod')
            for stage in ('vertex', 'fragment'):
                r = subprocess.run([args.cli, 'transpile', '-s', ps['path'], '-o', stage, '-f', 'glsl', '-v', '120'],
                                   capture_output=True, text=True)
                if r.returncode:
                    fails.append(f'{i}{stage[0]}: {r.stderr.strip()[-120:]}')
                    continue
                for word in ('textureLod', 'texelFetch', 'textureSize', 'uint', '<<', 'bitfield', 'dFdx', 'fwidth',
                             'inverse('):
                    if word in r.stdout:
                        feats.add(word)
        print(f'{key:22} passes {p["pass_count"]:2} luts {len(p["textures"])} feats {sorted(feats)}')
        for f in fails:
            print('   FAIL', f)


if __name__ == '__main__':
    main()
