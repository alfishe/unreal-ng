#!/usr/bin/env python3
"""The headless test runner: unreal-qt --headless --script <file.lua> --exit-on-script ends with the script's exit code.

Each case starts its own unreal-qt on ports of its own (--port-base) and waits for it to exit (~2 s a case).
Usage: test_headless_runner.py <unreal-qt binary> [--port-base 18695]. Exit code 0 = every case passed.
"""
import argparse
import os
import subprocess
import sys
import tempfile

# (name, script, expected exit code, text expected in stdout or stderr)
CASES = [
    ('an integer result is the exit code', 'return 7', 7, 'script exit code 7'),
    ('true is 0', 'return true', 0, 'script exit code 0'),
    ('false is 1', 'return false', 1, 'script exit code 1'),
    ('an error is 1 with its message', "error('boom')", 1, 'boom'),
    ('print reaches stdout', "print('hello from lua')", 0, 'hello from lua'),
    ('the emulator is up', 'return emu_count() >= 1', 0, 'script exit code 0'),
    ('the emulator is muted', "local m = audio_mixer_get('master'); print('muted', m and m.muted); "
                              "return m ~= nil and m.muted == true", 0, 'muted\ttrue'),
    ('the script runs frames', 'run_frames(2); return 0', 0, 'script exit code 0'),
]


def run_case(binary, port_base, script, scratch):
    path = os.path.join(scratch, 'case.lua')
    with open(path, 'w') as f:
        f.write(script)
    env = dict(os.environ)
    for name, offset in (('UNREAL_WEBAPI_PORT', 0), ('UNREAL_CLI_PORT', 100), ('UNREAL_MCP_PORT', 200),
                         ('UNREAL_GDB_PORT', 300), ('UNREAL_DEZOG_PORT', 400), ('UNREAL_ZRCP_PORT', 500)):
        env[name] = str(port_base + offset)
    env.setdefault('UNREAL_STREAMDECK_SUPERVISOR', 'off')
    env.setdefault('UNREAL_QT_VIEW_SETTINGS', 'off')
    env.setdefault('UNREAL_RECENT_FILES', 'off')
    process = subprocess.run([binary, '--headless', '--script', path, '--exit-on-script'], env=env,
                             capture_output=True, text=True, timeout=60)
    return process.returncode, process.stdout + process.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary')
    parser.add_argument('--port-base', type=int, default=18695)
    args = parser.parse_args()
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    scratch_root = os.path.join(root, 'scratch', 'headless-runner')
    os.makedirs(scratch_root, exist_ok=True)
    failed = 0
    with tempfile.TemporaryDirectory(dir=scratch_root) as scratch:
        for name, script, code, text in CASES:
            got, output = run_case(args.binary, args.port_base, script, scratch)
            ok = got == code and text in output
            failed += 0 if ok else 1
            print(f"  {'ok  ' if ok else 'FAIL'}  {name} (exit {got})")
            if not ok:
                print('        expected exit', code, 'and', repr(text))
                print('        ' + '\n        '.join(output.strip().splitlines()[-8:]))
    print(f'{len(CASES) - failed} of {len(CASES)} cases passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
