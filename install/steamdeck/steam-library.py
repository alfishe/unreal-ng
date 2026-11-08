#!/usr/bin/env python3
"""The emulator's entry in the Steam library on a Steam Deck: a non-Steam game with its artwork (runs on the Deck).

Steam keeps non-Steam games in userdata/<account>/config/shortcuts.vdf (binary KeyValues) and their pictures in
userdata/<account>/config/grid/, named after the entry's app id: <appid>p.png (poster), <appid>.png (wide capsule),
<appid>_hero.png, <appid>_logo.png, <appid>_icon.png. Steam reads both when it starts, so `add` writes them together
while Steam is stopped and the entry shows with its pictures at once. The app id is the one Steam ROM Manager and
steamtinkerlaunch use, crc32 of the quoted executable and the name with the top bit set: the same on every install, so
an update finds its entry (https://developer.valvesoftware.com/wiki/Steam_Library_Shortcuts).

    ./steam-library.py add --name "Unreal NG" --exe ~/Applications/unreal-ng/unreal-ng.sh --icon unreal-ng --art art
    ./steam-library.py check --name "Unreal NG" --exe ~/Applications/unreal-ng/unreal-ng.sh --art art   # 0: up to date
    ./steam-library.py remove --exe ~/Applications/unreal-ng/unreal-ng.sh
    ./steam-library.py art --match p01-sdl-gpu-latency --art art   # pictures for entries Steam added itself
    ./steam-library.py list

`add` and `remove` refuse while Steam runs (it writes the file back on exit); install.sh stops and restarts it.
"""

import argparse
import filecmp
import glob
import os
import shutil
import struct
import subprocess
import sys
import zlib

STEAM = os.path.expanduser('~/.local/share/Steam/userdata')
HERE = os.path.dirname(os.path.abspath(__file__))
# art/ name -> the grid name's suffix after the app id
PICTURES = {
    'poster.png': 'p.png',
    'capsule.png': '.png',
    'hero.png': '_hero.png',
    'logo.png': '_logo.png',
    'icon.png': '_icon.png',
}


def read_vdf(data):
    """Binary KeyValues: 0x00 name {...} 0x08, 0x01 name string, 0x02 name int32, 0x07 name uint64."""
    pos = 0

    def cstring():
        nonlocal pos
        end = data.index(b'\0', pos)
        text = data[pos:end].decode('utf-8', 'replace')
        pos = end + 1
        return text

    def node():
        nonlocal pos
        out = {}
        while pos < len(data):
            kind = data[pos]
            pos += 1
            if kind == 0x08:
                return out
            key = cstring()
            if kind == 0x00:
                out[key] = node()
            elif kind == 0x01:
                out[key] = cstring()
            elif kind == 0x02:
                out[key] = struct.unpack_from('<i', data, pos)[0]
                pos += 4
            elif kind == 0x07:
                out[key] = ('u64', struct.unpack_from('<Q', data, pos)[0])
                pos += 8
            else:
                raise ValueError(f'unknown KeyValues type {kind:#x} at {pos - 1}')
        return out

    return node()


def write_vdf(root):
    out = bytearray()

    def node(d):
        for key, value in d.items():
            k = key.encode('utf-8') + b'\0'
            if isinstance(value, dict):
                out.extend(b'\x00' + k)
                node(value)
                out.append(0x08)
            elif isinstance(value, tuple) and value[0] == 'u64':
                out.extend(b'\x07' + k + struct.pack('<Q', value[1]))
            elif isinstance(value, int):
                out.extend(b'\x02' + k + struct.pack('<i', value))
            else:
                out.extend(b'\x01' + k + str(value).encode('utf-8') + b'\0')

    node(root)
    out.append(0x08)
    return bytes(out)


def accounts():
    """The config folders of the Deck's Steam accounts (userdata/<id>/config), 0 (no account) left out."""
    return [os.path.join(d, 'config') for d in sorted(glob.glob(os.path.join(STEAM, '*')))
            if os.path.basename(d).isdigit() and os.path.basename(d) != '0']


def load(config):
    path = os.path.join(config, 'shortcuts.vdf')
    if not os.path.exists(path):
        return {'shortcuts': {}}
    with open(path, 'rb') as f:
        root = read_vdf(f.read())
    return root if root else {'shortcuts': {}}


def save(config, root):
    path = os.path.join(config, 'shortcuts.vdf')
    os.makedirs(config, exist_ok=True)
    if os.path.exists(path):
        shutil.copyfile(path, path + '.bak')
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(write_vdf(root))
    os.replace(tmp, path)


def entries_of(root):
    shortcuts = next(iter(root.values()), {}) if root else {}
    return shortcuts if isinstance(shortcuts, dict) else {}


def field(item, name, default=None):
    for k, v in item.items():
        if k.lower() == name.lower():
            return v
    return default


def shortcut_appid(exe_quoted, name):
    """Steam ROM Manager's id: crc32 of the quoted executable and the name, the top bit set (unsigned)."""
    return zlib.crc32((exe_quoted + name).encode('utf-8')) | 0x80000000


def steam_running():
    return subprocess.run(['pgrep', '-x', 'steam'], stdout=subprocess.DEVNULL).returncode == 0


def put_art(config, appid, art_dir):
    grid = os.path.join(config, 'grid')
    os.makedirs(grid, exist_ok=True)
    for source, suffix in PICTURES.items():
        src = os.path.join(art_dir, source)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(grid, f'{appid}{suffix}'))


def drop_art(config, appid):
    for suffix in PICTURES.values():
        path = os.path.join(config, 'grid', f'{appid}{suffix}')
        if os.path.exists(path):
            os.remove(path)


def cmd_add(args):
    exe = os.path.abspath(os.path.expanduser(args.exe))
    exe_quoted = f'"{exe}"'
    start_dir = os.path.abspath(os.path.expanduser(args.start_dir or os.path.dirname(exe))) + '/'
    appid = shortcut_appid(exe_quoted, args.name)
    signed = struct.unpack('<i', struct.pack('<I', appid))[0]
    configs = accounts()
    if not configs:
        print('no Steam account on this machine (start Steam and sign in once)', file=sys.stderr)
        return 1
    for config in configs:
        root = load(config)
        shortcuts = entries_of(root)
        kept = []
        for item in shortcuts.values():
            if field(item, 'exe', '') == exe_quoted:
                old = field(item, 'appid', 0) & 0xFFFFFFFF
                if old != appid:
                    drop_art(config, old)
            else:
                kept.append(item)
        kept.append({
            'appid': signed, 'AppName': args.name, 'Exe': exe_quoted, 'StartDir': start_dir,
            'icon': args.icon or '', 'ShortcutPath': args.shortcut_path or '', 'LaunchOptions': args.launch_options or '',
            'IsHidden': 0, 'AllowDesktopConfig': 1, 'AllowOverlay': 1, 'OpenVR': 0, 'Devkit': 0,
            'DevkitGameID': '', 'DevkitOverrideAppID': 0, 'LastPlayTime': 0, 'FlatpakAppID': '', 'sortas': '',
            'tags': {},
        })
        key = next(iter(root.keys()), 'shortcuts') if root else 'shortcuts'
        save(config, {key: {str(i): item for i, item in enumerate(kept)}})
        put_art(config, appid, args.art)
        print(f'Steam library: {args.name} ({appid}) with its artwork in {os.path.dirname(config)}')
    return 0


def cmd_check(args):
    """0 when every account has the entry as `add` would write it and its artwork as in --art: nothing to change"""
    exe = os.path.abspath(os.path.expanduser(args.exe))
    exe_quoted = f'"{exe}"'
    appid = shortcut_appid(exe_quoted, args.name)
    configs = accounts()
    if not configs:
        return 1
    for config in configs:
        ours = [item for item in entries_of(load(config)).values() if field(item, 'exe', '') == exe_quoted]
        if len(ours) != 1 or field(ours[0], 'appid', 0) & 0xFFFFFFFF != appid or field(ours[0], 'appname') != args.name:
            return 1
        for source, suffix in PICTURES.items():
            src = os.path.join(args.art, source)
            dst = os.path.join(config, 'grid', f'{appid}{suffix}')
            if os.path.exists(src) and not (os.path.exists(dst) and filecmp.cmp(src, dst, shallow=False)):
                return 1
    return 0


def cmd_remove(args):
    exe_quoted = f'"{os.path.abspath(os.path.expanduser(args.exe))}"'
    for config in accounts():
        root = load(config)
        shortcuts = entries_of(root)
        kept = [item for item in shortcuts.values() if field(item, 'exe', '') != exe_quoted]
        if len(kept) == len(shortcuts):
            continue
        for item in shortcuts.values():
            if field(item, 'exe', '') == exe_quoted:
                drop_art(config, field(item, 'appid', 0) & 0xFFFFFFFF)
        key = next(iter(root.keys()), 'shortcuts')
        save(config, {key: {str(i): item for i, item in enumerate(kept)}})
        print(f'Steam library: removed {exe_quoted} from {os.path.dirname(config)}')
    return 0


def cmd_art(args):
    done = 0
    for config in accounts():
        for item in entries_of(load(config)).values():
            appid = field(item, 'appid', 0) & 0xFFFFFFFF
            exe = field(item, 'exe', '')
            if appid and any(m in exe for m in args.match):
                put_art(config, appid, args.art)
                print(f'artwork: {field(item, "appname", "")} ({appid})')
                done += 1
    if not done:
        print(f'no non-Steam game runs {" or ".join(args.match)}', file=sys.stderr)
    return 0 if done else 1


def cmd_list(_args):
    for config in accounts():
        for item in entries_of(load(config)).values():
            appid = field(item, 'appid', 0) & 0xFFFFFFFF
            print(f'{appid:>10}  {field(item, "appname", "")}  {field(item, "exe", "")}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = parser.add_subparsers(dest='command', required=True)
    add = sub.add_parser('add', help='add or update a non-Steam game with its artwork (Steam stopped)')
    add.add_argument('--name', required=True)
    add.add_argument('--exe', required=True)
    add.add_argument('--start-dir')
    add.add_argument('--icon', help='an icon name or path')
    add.add_argument('--shortcut-path', help='the .desktop file it came from')
    add.add_argument('--launch-options')
    add.add_argument('--art', default=os.path.join(HERE, 'art'))
    check = sub.add_parser('check', help='exit 0 when the entry and its artwork are in place (nothing to change)')
    check.add_argument('--name', required=True)
    check.add_argument('--exe', required=True)
    check.add_argument('--art', default=os.path.join(HERE, 'art'))
    rm = sub.add_parser('remove', help='remove the entries of an executable and their artwork (Steam stopped)')
    rm.add_argument('--exe', required=True)
    art = sub.add_parser('art', help='artwork for entries whose executable contains a string')
    art.add_argument('--match', action='append', required=True)
    art.add_argument('--art', default=os.path.join(HERE, 'art'))
    sub.add_parser('list', help='the non-Steam games and their app ids')
    args = parser.parse_args()
    if args.command in ('add', 'remove') and steam_running():
        print('Steam is running: it would write shortcuts.vdf back on exit (stop it first)', file=sys.stderr)
        return 2
    return {'add': cmd_add, 'check': cmd_check, 'remove': cmd_remove, 'art': cmd_art,
            'list': cmd_list}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
