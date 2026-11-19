#!/usr/bin/env python3
"""POC 050: start unreal-qt offscreen on ports of its own (WebAPI BASE, CLI +100, MCP +200, ...), wait for the
WebAPI, run the given command, stop the emulator."""

import os
import socket
import subprocess
import sys
import time
import urllib.request


def main():
    binary, base = sys.argv[1], int(sys.argv[2])
    command = sys.argv[3:]
    for offset in (0, 100, 200, 300, 400, 500):
        with socket.socket() as probe:
            if probe.connect_ex(("127.0.0.1", base + offset)) == 0:
                raise SystemExit("port {} is taken".format(base + offset))
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", UNREAL_STREAMDECK_SUPERVISOR="off",
               UNREAL_QT_VIEW_SETTINGS="off", UNREAL_RECENT_FILES="off")
    for name, offset in (("UNREAL_WEBAPI_PORT", 0), ("UNREAL_CLI_PORT", 100), ("UNREAL_MCP_PORT", 200),
                         ("UNREAL_GDB_PORT", 300), ("UNREAL_DEZOG_PORT", 400), ("UNREAL_ZRCP_PORT", 500)):
        env[name] = str(base + offset)
    process = subprocess.Popen([binary, "-platform", "offscreen"], env=env, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    try:
        url = "http://127.0.0.1:{}/api/v1/emulator".format(base)
        for _ in range(120):
            try:
                urllib.request.urlopen(url, timeout=2).read()
                break
            except OSError:
                if process.poll() is not None:
                    raise SystemExit("unreal-qt exited with {}".format(process.returncode))
                time.sleep(0.5)
        # mute every instance the emulator starts with (run_taps.py mutes the ones it creates)
        import json
        for emu in json.load(urllib.request.urlopen(url, timeout=5)).get("emulators", []):
            req = urllib.request.Request("http://127.0.0.1:{}/api/v1/emulator/{}/audio/mixer/master".format(
                base, emu["id"]), data=b'{"muted":true}', method="PUT", headers={"Content-Type": "application/json"})
            urllib.request.urlopen(req, timeout=5).read()
        sys.exit(subprocess.call(command))
    finally:
        process.terminate()
        try:
            process.wait(10)
        except subprocess.TimeoutExpired:
            process.kill()


if __name__ == "__main__":
    main()
