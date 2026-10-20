#!/usr/bin/env python3
"""Imports libretro slang CRT presets into data/shaders/crt/<key>/ for unreal-qt's SlangPresetRunner.

usage: import.py --slang <slang-shaders checkout> --cli <librashader-cli> [--out data/shaders/crt] [key ...]

Per preset: preset.json (the passes' metadata, the LUTs, the parameters with their ranges and the preset's values),
per pass GLSL 330 vertex and fragment sources (pass<N>.vert / pass<N>.frag) and the LUT images. librashader-cli
`pack` gives the preprocessed passes (parameters, the framebuffer format of `#pragma format`), `parse` the preset's
metadata, `transpile` the GLSL 330. The uniform and push blocks of the transpiled GLSL become plain uniforms
(`LIBRA_UNIFORM_<member>`), so the runner sets them by name, without uniform buffers.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

PRESETS = [  # (key, preset relative to the slang-shaders checkout)
    ('crt-lottes', 'crt/crt-lottes.slangp'),
    ('crt-lottes-glow', 'crt/crt-lottes-multipass-glow.slangp'),
    ('crt-geom', 'crt/crt-geom.slangp'),
    ('crt-easymode', 'crt/crt-easymode.slangp'),
    ('crt-hyllian', 'crt/crt-hyllian.slangp'),
    ('crt-gdv-mini', 'crt/crt-gdv-mini.slangp'),
    ('guest-advanced', 'crt/crt-guest-advanced.slangp'),
    ('guest-advanced-fast', 'crt/crt-guest-advanced-fast.slangp'),
    ('guest-pal', 'crt/crt-guest-advanced-pal.slangp'),
    ('guest-pal-composite', 'pal/shaders/decoupled-guest/decoupled-guest-advanced-pal 2-Composite.slangp'),
    ('crt-beans-rgb', 'crt/crt-beans-rgb.slangp'),
    ('megatron-v2-sdr', 'hdr/crt-sony-megatron-v2-default-sdr.slangp'),
    ('crt-royale', 'crt/crt-royale.slangp'),
    ('crt-yah', 'crt/crt-yah.slangp'),
    ('crt-yo6-pvm', 'crt/crt-yo6-KV-M1420B.slangp'),
    ('crt-consumer', 'crt/crt-consumer.slangp'),
    ('crt-pi', 'crt/crt-pi.slangp'),
    ('zfast-crt', 'crt/zfast-crt.slangp'),
    ('newpixie', 'crt/newpixie-crt.slangp'),
]

# A block of the transpiled GLSL: `layout(std140) uniform LIBRA_UBO_VERTEX { ... } LIBRA_UBO_VERTEX_INSTANCE;` or
# `struct LIBRA_PUSH_VERTEX { ... };` + `uniform LIBRA_PUSH_VERTEX LIBRA_PUSH_VERTEX_INSTANCE;`
UBO = re.compile(r'layout\(std140\) uniform (LIBRA_UBO_\w+)\s*\{([^}]*)\}\s*\w+;\n')
PUSH = re.compile(r'struct (LIBRA_PUSH_\w+)\s*\{([^}]*)\};\s*uniform \1 \w+;\n')
MEMBER = re.compile(r'^\s*(\w+)\s+(\w+)(\[\d+\])?;\s*$', re.MULTILINE)
ACCESS = re.compile(r'\bLIBRA_(?:UBO|PUSH)_(?:VERTEX|FRAGMENT)_INSTANCE\.(\w+)')


def run(args):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f'{" ".join(args)}: {r.stderr.strip()}')
    return r.stdout


def flatten(glsl):
    """The uniform and push blocks as plain uniforms; returns the source and {member: type}"""
    members = {}

    def declare(match):
        lines = []
        for m in MEMBER.finditer(match.group(2)):
            kind, name, array = m.group(1), m.group(2), m.group(3) or ''
            if name in members:  # in the uniform block and in the push constants: one uniform
                if members[name] != kind + array:
                    raise RuntimeError(f'{name} is {members[name]} and {kind + array}')
                continue
            members[name] = kind + array
            lines.append(f'uniform {kind} LIBRA_UNIFORM_{name}{array};\n')
        return ''.join(lines)

    glsl = UBO.sub(declare, glsl)
    glsl = PUSH.sub(declare, glsl)
    glsl = ACCESS.sub(lambda m: f'LIBRA_UNIFORM_{m.group(1)}', glsl)
    if 'LIBRA_UBO_' in glsl or 'LIBRA_PUSH_' in glsl:
        raise RuntimeError('a uniform block is left after flattening')
    return glsl, members


def closing(text, start):
    """Index of the bracket that closes the one at `start` ( or ["""
    pairs = {'(': ')', '[': ']'}
    stack = []
    for i in range(start, len(text)):
        c = text[i]
        if c in pairs:
            stack.append(pairs[c])
        elif stack and c == stack[-1]:
            stack.pop()
            if not stack:
                return i
    raise RuntimeError('unbalanced brackets')


def split_top(text):
    """Comma-separated items at the top level"""
    items, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c in '([':
            depth += 1
        elif c in ')]':
            depth -= 1
        elif c == ',' and depth == 0:
            items.append(text[start:i].strip())
            start = i + 1
    items.append(text[start:].strip())
    return items


def leaves(expression, kind):
    """The elements of a nested array constructor `T[][](T[](a, b), ...)`, row major"""
    match = re.match(re.escape(kind) + r'(?:\[\])+\(', expression)
    if not match:
        return [expression]
    end = closing(expression, match.end() - 1)
    return [leaf for item in split_top(expression[match.end():end]) for leaf in leaves(item, kind)]


ARRAY_OF_ARRAYS = re.compile(r'\b(\w+)\s+(\w+)((?:\[\d+\]){2,})\s*=\s*')


def flatten_arrays_of_arrays(glsl):
    """Arrays of arrays (GL 4.3, not on macOS's 4.1) as one-dimensional arrays with the index computed"""
    if '][' not in glsl:
        return glsl
    while True:
        match = ARRAY_OF_ARRAYS.search(glsl)
        if not match:
            break
        kind, name = match.group(1), match.group(2)
        dims = [int(d) for d in re.findall(r'\d+', match.group(3))]
        start = match.end()
        end = closing(glsl, glsl.index('(', start))
        elements = leaves(glsl[start:end + 1], kind)
        total = 1
        for d in dims:
            total *= d
        if len(elements) != total:
            raise RuntimeError(f'{name}: {len(elements)} elements for {dims}')
        glsl = glsl[:match.start()] + f'{kind} {name}[{total}] = {kind}[]({", ".join(elements)})' + glsl[end + 1:]
        # name[i][j][k] -> name[(int(i) * dj + int(j)) * dk + int(k)]
        access = re.compile(r'\b' + re.escape(name) + r'\[')
        position = 0
        while True:
            found = access.search(glsl, position)
            if not found:
                break
            indices, cursor = [], found.end() - 1
            for _ in dims:
                if cursor >= len(glsl) or glsl[cursor] != '[':
                    break
                close = closing(glsl, cursor)
                indices.append(glsl[cursor + 1:close].strip())
                cursor = close + 1
            if len(indices) != len(dims):
                position = found.end()
                continue
            index = f'int({indices[0]})'
            for d, i in zip(dims[1:], indices[1:]):
                index = f'({index}) * {d} + int({i})'
            replacement = f'{name}[{index}]'
            glsl = glsl[:found.start()] + replacement + glsl[cursor:]
            position = found.start() + len(replacement)
    return glsl.replace('#extension GL_ARB_arrays_of_arrays : require\n', '')


def alias(name):
    """A pass alias, None for none (the presets also write `alias = ""`)"""
    name = (name or '').strip('"')
    return name if name and name != 'None' else None


def tidy(glsl):
    """No trailing whitespace, one newline at the end"""
    return '\n'.join(line.rstrip() for line in glsl.rstrip().split('\n')) + '\n'


def scale(axis):
    factor = axis['factor']
    return {'type': axis['scale_type'], 'factor': factor.get('Float', factor.get('Absolute'))}


def import_preset(key, preset, args):
    out = os.path.join(args.out, key)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    parsed = json.loads(run([args.cli, 'parse', '-p', preset]))
    with tempfile.TemporaryDirectory() as tmp:
        pack_path = os.path.join(tmp, 'pack.json')
        run([args.cli, 'pack', '-p', preset, '-o', pack_path, '-f', 'json'])
        with open(pack_path) as f:
            pack = json.load(f)

    passes, parameters, textures = [], {}, []
    for n, (meta_pass, packed) in enumerate(zip(parsed['passes'], pack['passes'])):
        meta = meta_pass['meta']
        uniforms = {}
        for stage in ('vertex', 'fragment'):
            glsl = run([args.cli, 'transpile', '-s', meta_pass['path'], '-o', stage, '-f', 'glsl', '-v', '330'])
            glsl, members = flatten(glsl)
            glsl = flatten_arrays_of_arrays(glsl)
            uniforms.update(members)
            with open(os.path.join(out, f'pass{n}.{"vert" if stage == "vertex" else "frag"}'), 'w') as f:
                f.write(tidy(glsl))
        for name, p in packed['data']['parameters'].items():
            parameters.setdefault(name, {'description': p['description'], 'initial': p['initial'],
                                         'minimum': p['minimum'], 'maximum': p['maximum'], 'step': p['step']})
        passes.append({
            'source': os.path.relpath(meta_pass['path'], args.slang),
            # The preset's alias, else the shader's own `#pragma name`
            'alias': alias(meta['alias']) or alias(packed['data']['name']),
            'filter': meta['filter'],
            'wrap': meta['wrap_mode'],
            'frame_count_mod': meta['frame_count_mod'],
            'srgb_framebuffer': meta['srgb_framebuffer'],
            'float_framebuffer': meta['float_framebuffer'],
            'format': packed['data']['format'],
            'mipmap_input': meta['mipmap_input'],
            'scale': {'valid': meta['scaling']['valid'], 'x': scale(meta['scaling']['x']),
                      'y': scale(meta['scaling']['y'])},
            'uniforms': uniforms,
        })

    for texture in parsed['textures']:
        meta = texture['meta']
        name = meta['name'] + os.path.splitext(texture['path'])[1].lower()
        shutil.copyfile(texture['path'], os.path.join(out, name))
        textures.append({'name': meta['name'], 'file': name, 'filter': meta['filter_mode'], 'wrap': meta['wrap_mode'],
                         'mipmap': meta['mipmap']})

    # The preset's own values for the parameters (`name = value` lines of the .slangp and the presets it references)
    for p in parsed['parameters']:
        name, value = (p['name'], p['value']) if isinstance(p, dict) else p
        if name in parameters:
            parameters[name]['initial'] = value

    manifest = {'key': key, 'preset': os.path.relpath(preset, args.slang), 'passes': passes, 'textures': textures,
                'parameters': parameters}
    with open(os.path.join(out, 'preset.json'), 'w') as f:
        json.dump(manifest, f, indent=1)
        f.write('\n')
    return len(passes)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--slang', required=True, help='libretro slang-shaders checkout')
    parser.add_argument('--cli', required=True, help='librashader-cli binary')
    parser.add_argument('--out', default=os.path.join(os.path.dirname(__file__), '..', '..', 'data', 'shaders', 'crt'))
    parser.add_argument('keys', nargs='*', help='only these presets')
    args = parser.parse_args()
    args.slang = os.path.abspath(args.slang)
    revision = run(['git', '-C', args.slang, 'rev-parse', 'HEAD']).strip()
    failed = 0
    for key, preset in PRESETS:
        if args.keys and key not in args.keys:
            continue
        try:
            print(f'{key}: {import_preset(key, os.path.join(args.slang, preset), args)} passes')
        except (RuntimeError, KeyError, OSError) as error:
            print(f'{key}: FAILED {error}')
            failed += 1
    with open(os.path.join(args.out, 'SOURCE'), 'w') as f:
        f.write(f'libretro/slang-shaders {revision}, imported by tools/crt-shaders/import.py\n')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
