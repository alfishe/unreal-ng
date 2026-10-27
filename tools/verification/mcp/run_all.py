#!/usr/bin/env python3
"""Runs every test_*.py of the MCP verification against one server: a running one (--mcp-url) or one it launches
(--launch <unreal-qt>, ports of its own from --port-base). Exit code 0 = every script passed.

    tools/verification/mcp/run_all.py --launch cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt
    tools/verification/mcp/run_all.py --mcp-url http://127.0.0.1:8295/mcp test_inspect_state test_symbols
"""

import glob
import os
import subprocess
import sys
import time

import mcplib


def main():
    names = [a for a in sys.argv[1:] if not a.startswith('-') and a.startswith('test_')]
    argv = [a for a in sys.argv[1:] if a not in names]
    args = mcplib.parse_args(argv)
    here = os.path.dirname(os.path.abspath(__file__))
    scripts = sorted(glob.glob(os.path.join(here, 'test_*.py')))
    if names:
        scripts = [p for p in scripts if os.path.basename(p)[:-3] in names or os.path.basename(p) in names]
    process = None
    url = args.mcp_url
    if args.launch:
        process = mcplib.launch(args.launch, args.port_base)
        url = f'http://127.0.0.1:{args.port_base + 200}/mcp'
    failed = []
    started = time.time()
    try:
        for script in scripts:
            command = [sys.executable, script, '--mcp-url', url, '--model', args.model]
            if args.only:
                command += ['-k', args.only]
            if subprocess.call(command) != 0:
                failed.append(os.path.basename(script))
    finally:
        if process:
            process.terminate()
            process.wait(timeout=20)
    print(f'\n{len(scripts) - len(failed)} of {len(scripts)} scripts passed in {time.time() - started:.0f} s'
          + (f'; failed: {", ".join(failed)}' if failed else ''))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
