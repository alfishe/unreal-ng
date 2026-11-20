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


def set_feature(api, emulator, name, enabled):
    """Switch a feature; False when this build does not have it (esp_jit comes with the ESP32 JIT)"""
    try:
        api.call('PUT', f'{emulator}/feature/{name}', {'enabled': enabled})
        return True
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return False
        raise


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
    # The window's own machine would run beside the workload the whole time: pause it
    for instance in api.call('GET', '/emulator').get('emulators', []):
        pause(api, f"/emulator/{instance['id']}")
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

    jit = {'present': False}

    def doom(emulator):
        jit['present'] = set_feature(api, emulator, 'esp_jit', True)
        load(api, emulator, sd + '/_DOOM.SPG', sd)
        pause(api, emulator)
        frames = max(100, int(DOOM_FRAMES * scale))
        start = time.time()
        api.call('POST', emulator + '/run_frames', {'frames': frames})
        label = 'JIT' if jit['present'] else 'no JIT in this build'
        print(f'  DOOM ({label}) {1000.0 * (time.time() - start) / frames:7.2f} ms/frame', flush=True)
        if jit['present']:
            set_feature(api, emulator, 'esp_jit', False)

    ms = run_machine(api, 'TSL-VDAC4', max(10, int(DOOM_INTERPRETER_FRAMES * scale)), doom)
    print(f'  DOOM ({"interpreter" if jit["present"] else "continued"}) {ms:7.2f} ms/frame', flush=True)


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


def print_log_tail(path, lines=60):
    """The end of the application's log (on the CI the file stays on the runner)"""
    try:
        with open(path, errors='replace') as f:
            tail = f.read().splitlines()[-lines:]
    except OSError:
        return
    print(f'--- the last {len(tail)} lines of {path}', file=sys.stderr)
    for line in tail:
        print(line, file=sys.stderr)


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
    # MSVC: the instrumented binary writes its .pgc files where VCPROFILE_PATH says (else beside the executable);
    # the USE link reads them beside the .pgd, which is in the profile directory
    env['VCPROFILE_PATH'] = profile_dir
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
                crashed = process.poll()   # None: still running (the script failed), else how it ended
                process.kill()
                process.wait()
                state = 'still running' if crashed is None else f'exit code {crashed:#x}'
                print(f'training failed, unreal-qt {state}', file=sys.stderr)
                print_log_tail(log_path)
                raise
            process.stdin.close()   # the script returns, the application quits and writes the profile
            code = process.wait(timeout=300)
        print(f'unreal-qt exited with {code} after {time.time() - started:.0f} s')
        if code != 0:
            print_log_tail(log_path)
            return 1

    # .pgc files left beside the executable (an MSVC runtime that ignores VCPROFILE_PATH) go to the .pgd
    for stray in glob.glob(os.path.join(os.path.dirname(os.path.abspath(args.binary)), '*.pgc')):
        shutil.move(stray, os.path.join(profile_dir, os.path.basename(stray)))
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
