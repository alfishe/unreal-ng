#!/usr/bin/env python3
"""PGO training run: drive an instrumented unreal-qt (-DUNREAL_PGO=GENERATE) through a fixed workload, let it exit
cleanly so the profile is written, and leave the profile ready for -DUNREAL_PGO=USE.

The workload (all from testdata, deterministic):
  - every creatable model boots for a while (the CPU cores, the ULAs and video modes, the sound chips, the ROMs);
  - programs: the TS-Conf sprite demo, a TurboSound FM tune on the Pentagon;
  - DOOM on TS-Conf + VDAC4 into its demo, with the ESP32 JIT, then a stretch in the ESP32 interpreter.

unreal-qt runs headless with a Lua script that waits for a line on stdin (--script, --exit-on-script): closing stdin
ends the script and the application quits through its own shutdown, the only way every toolchain writes the profile
(MSVC .pgc beside the .pgd, GCC .gcda, Clang .profraw). Clang's raw files are then merged into default.profdata.

Usage: train.py <unreal-qt binary> --profile-dir <UNREAL_PGO_DIR> [--port-base 18900] [--quick]
Exit code 0 = the workload ran and a profile is there. Guide: tools/pgo/README.md
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TESTDATA = os.path.join(REPO, 'testdata')

BOOT_FRAMES = 300          # each model from power on
PROGRAM_FRAMES = 600       # each program
DOOM_FRAMES = 6600         # loading (~4500), the title, then the demo game (from ~6000)
DOOM_INTERPRETER_FRAMES = 200

PROGRAMS = [
    # (model, testdata path)
    ('TSL', 'machines/tsconf/spg/sprites.spg'),
    ('PENTAGON', 'sound/tsfm/tech_support.sna'),
]
DOOM_FILES = ['_DOOM.SPG', 'DOOM.ELF', 'DOOM1.WAD']   # testdata/vdac4: the SD card's root

# The waiting script: one line on stdin (or its end) and it returns 0
WAIT_SCRIPT = 'io.read("*l")\nreturn 0\n'


class Api:
    def __init__(self, port):
        self.base = f'http://127.0.0.1:{port}/api/v1'

    def call(self, method, path, body=None, timeout=3600):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.base + path, data=data, method=method,
                                         headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=timeout) as reply:
            text = reply.read()
        return json.loads(text) if text else {}

    def wait_up(self, process, seconds=120):
        deadline = time.time() + seconds
        while time.time() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f'unreal-qt exited before its WebAPI came up (exit {process.returncode})')
            try:
                self.call('GET', '/emulator', timeout=2)
                return
            except (urllib.error.URLError, ConnectionError, OSError):
                time.sleep(0.5)
        raise RuntimeError(f'the WebAPI did not come up within {seconds} s')


def run_machine(api, model, frames, setup=None):
    """Create a paused machine of @p model, let @p setup load it, run @p frames, remove it. Returns ms per frame."""
    reply = api.call('POST', '/emulator/create', {'model': model})
    if 'id' not in reply:
        raise RuntimeError(f'create {model}: {reply}')
    emulator = f"/emulator/{reply['id']}"
    try:
        # Mute every instance (nothing to hear on a build machine)
        api.call('PUT', emulator + '/audio/mixer/master', {'muted': True})
        if setup:
            setup(emulator)
        pause(api, emulator)
        start = time.time()
        reply = api.call('POST', emulator + '/run_frames', {'frames': frames})
        if reply.get('status') != 'success':
            raise RuntimeError(f'{model}: run_frames: {reply}')
        return 1000.0 * (time.time() - start) / frames
    finally:
        api.call('DELETE', emulator)


def pause(api, emulator):
    try:
        api.call('POST', emulator + '/pause')
    except urllib.error.HTTPError:
        pass   # not running (created or loaded paused): nothing to pause


def load(api, emulator, path, sd=None):
    if sd:
        api.call('POST', emulator + '/media/sd.zc/insert', {'path': sd})
    reply = api.call('POST', emulator + '/snapshot/load', {'path': path, 'switch_model': False})
    if reply.get('status') != 'success':
        raise RuntimeError(f'load {path}: {reply}')


def workload(api, scratch, quick):
    scale = 0.1 if quick else 1.0
    models = [m['name'] for m in api.call('GET', '/emulator/models')['models'] if m.get('creatable', True)]
    for model in models:
        # A model that cannot start here (a ROM the build machine lacks) only narrows the profile
        try:
            ms = run_machine(api, model, max(10, int(BOOT_FRAMES * scale)))
            print(f'  boot {model:12} {ms:7.2f} ms/frame', flush=True)
        except (RuntimeError, urllib.error.HTTPError) as error:
            print(f'  boot {model:12} skipped: {error}', flush=True)
    for model, relative in PROGRAMS:
        path = os.path.join(TESTDATA, relative).replace('\\', '/')
        ms = run_machine(api, model, max(10, int(PROGRAM_FRAMES * scale)), lambda e, p=path: load(api, e, p))
        print(f'  {relative:40} {ms:7.2f} ms/frame', flush=True)

    # DOOM: a copy of the SD card (the game may write to it), the JIT on, then the interpreter for a stretch
    sd = os.path.join(scratch, 'sd')
    os.makedirs(sd)
    for name in DOOM_FILES:
        shutil.copy(os.path.join(TESTDATA, 'vdac4', name), sd)
    sd = sd.replace('\\', '/')

    def doom(emulator):
        api.call('PUT', emulator + '/feature/esp_jit', {'enabled': True})
        load(api, emulator, sd + '/_DOOM.SPG', sd)
        pause(api, emulator)
        start = time.time()
        api.call('POST', emulator + '/run_frames', {'frames': max(100, int(DOOM_FRAMES * scale))})
        print(f'  DOOM (JIT) {1000.0 * (time.time() - start) / max(100, int(DOOM_FRAMES * scale)):7.2f} ms/frame',
              flush=True)
        api.call('PUT', emulator + '/feature/esp_jit', {'enabled': False})

    ms = run_machine(api, 'TSL-VDAC4', max(10, int(DOOM_INTERPRETER_FRAMES * scale)), doom)
    print(f'  DOOM (interpreter) {ms:7.2f} ms/frame', flush=True)


def merge_clang_profile(profile_dir, llvm_profdata):
    raw = glob.glob(os.path.join(profile_dir, '*.profraw'))
    if not raw:
        return
    if llvm_profdata:
        tool = [llvm_profdata]
    elif sys.platform == 'darwin':
        tool = ['xcrun', 'llvm-profdata']
    else:
        tool = [shutil.which('llvm-profdata') or 'llvm-profdata']
    output = os.path.join(profile_dir, 'default.profdata')
    subprocess.run(tool + ['merge', '-o', output] + raw, check=True)
    print(f'merged {len(raw)} .profraw into {output}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('binary', help='the instrumented unreal-qt executable')
    parser.add_argument('--profile-dir', required=True, help='UNREAL_PGO_DIR of the build')
    parser.add_argument('--port-base', type=int, default=18900)
    parser.add_argument('--llvm-profdata', help='llvm-profdata of the compiler (default: xcrun on macOS, else PATH)')
    parser.add_argument('--quick', action='store_true', help='a tenth of the frames (checking the script itself)')
    args = parser.parse_args()

    profile_dir = os.path.abspath(args.profile_dir)
    os.makedirs(profile_dir, exist_ok=True)
    env = dict(os.environ)
    for name, offset in (('UNREAL_WEBAPI_PORT', 0), ('UNREAL_CLI_PORT', 1), ('UNREAL_MCP_PORT', 2),
                         ('UNREAL_GDB_PORT', 3), ('UNREAL_DEZOG_PORT', 4), ('UNREAL_ZRCP_PORT', 5)):
        env[name] = str(args.port_base + offset)
    # No companion daemon, no saved window state or recent files from or into the user's settings
    env.update(UNREAL_STREAMDECK_SUPERVISOR='off', UNREAL_QT_VIEW_SETTINGS='off', UNREAL_RECENT_FILES='off')

    with tempfile.TemporaryDirectory(prefix='pgo-train-', dir=profile_dir) as scratch:
        script = os.path.join(scratch, 'wait.lua')
        with open(script, 'w') as f:
            f.write(WAIT_SCRIPT)
        log_path = os.path.join(profile_dir, 'train-unreal-qt.log')
        started = time.time()
        with open(log_path, 'w') as log:
            process = subprocess.Popen([args.binary, '--headless', '--script', script, '--exit-on-script'], env=env,
                                       stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
            try:
                api = Api(args.port_base)
                api.wait_up(process)
                workload(api, scratch, args.quick)
            except BaseException:
                process.kill()
                process.wait()
                print(f'training failed; the application log: {log_path}', file=sys.stderr)
                raise
            process.stdin.close()   # the script returns, the application quits and writes the profile
            code = process.wait(timeout=300)
        print(f'unreal-qt exited with {code} after {time.time() - started:.0f} s')
        if code != 0:
            print(f'the application log: {log_path}', file=sys.stderr)
            return 1

    merge_clang_profile(profile_dir, args.llvm_profdata)
    found = {pattern: len(glob.glob(os.path.join(profile_dir, '**', pattern), recursive=True))
             for pattern in ('*.pgc', '*.gcda', 'default.profdata')}
    print('profile files: ' + ', '.join(f'{n} {p}' for p, n in found.items()))
    if not any(found.values()):
        print(f'no profile in {profile_dir}: was the binary built with -DUNREAL_PGO=GENERATE?', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
