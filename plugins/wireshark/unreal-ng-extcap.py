#!/usr/bin/env python3
"""Wireshark extcap for unreal-ng (network traffic task #91, phase T3).

Lists every running emulator of an unreal-ng instance as a capture interface in Wireshark and captures its network
traffic: everything the machine's adapters sent and received (frame cards' Ethernet frames, socket adapters'
operations as packets). The script asks the emulator's WebAPI to start its live pcapng stream (a TCP port), connects
to it and copies the stream into Wireshark's fifo. Python 3 standard library only; macOS, Linux and Windows.

Install: copy this file (and on Windows also unreal-ng-extcap.bat) into Wireshark's personal extcap folder
(Wireshark: Help > About Wireshark > Folders > Personal Extcap path), make it executable on macOS / Linux
(chmod +x), restart Wireshark. The WebAPI address defaults to http://127.0.0.1:8090 (the interface's options, or the
UNREAL_WEBAPI_URL environment variable, change it).

Without Wireshark's interface list the stream can be read directly: wireshark -k -i TCP@127.0.0.1:<port>
"""

import argparse
import json
import os
import socket
import sys
import urllib.parse
import urllib.request

PREFIX = "unrealng-"
DEFAULT_URL = os.environ.get("UNREAL_WEBAPI_URL", "http://127.0.0.1:8090")


def api(url, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    request = urllib.request.Request(url.rstrip("/") + path, data=data,
                                     headers={"Content-Type": "application/json"} if data else {})
    with urllib.request.urlopen(request, timeout=5) as reply:
        return json.loads(reply.read().decode() or "{}")


def emulators(url):
    try:
        listing = api(url, "/api/v1/emulator")
    except OSError:
        return []
    return listing.get("emulators", []) if isinstance(listing, dict) else []


def interfaces(url):
    print("extcap {version=1.0}{help=https://github.com/alfishe/unreal-ng}")
    for e in emulators(url):
        eid = e.get("id", "")
        name = e.get("symbolic_id") or e.get("model") or eid[:8]
        print("interface {value=%s%s}{display=unreal-ng %s (%s)}" % (PREFIX, eid, name, e.get("model", "")))


def dlts():
    print("dlt {number=1}{name=EN10MB}{display=Ethernet}")


def config():
    print("arg {number=0}{call=--webapi}{display=unreal-ng WebAPI}{type=string}{default=%s}"
          "{tooltip=The emulator's WebAPI address}" % DEFAULT_URL)


def capture(url, interface, fifo):
    emulator = interface[len(PREFIX):]
    base = "/api/v1/emulator/" + emulator + "/network/traffic"
    state = api(url, base + "?last=1").get("stream", {})
    if not state.get("running"):
        state = api(url, base, {"action": "stream", "port": 0}).get("stream", {})
    port = int(state.get("port", 0))
    if not port:
        sys.stderr.write("the emulator did not start its traffic stream\n")
        return 1
    host = urllib.parse.urlparse(url).hostname or "127.0.0.1"
    with socket.create_connection((host, port)) as stream, open(fifo, "wb", buffering=0) as out:
        while True:
            chunk = stream.recv(65536)
            if not chunk:
                return 0
            out.write(chunk)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--extcap-interfaces", action="store_true")
    p.add_argument("--extcap-interface")
    p.add_argument("--extcap-dlts", action="store_true")
    p.add_argument("--extcap-config", action="store_true")
    p.add_argument("--extcap-version")
    p.add_argument("--capture", action="store_true")
    p.add_argument("--fifo")
    p.add_argument("--webapi", default=DEFAULT_URL)
    args, _ = p.parse_known_args()
    if args.extcap_interfaces:
        interfaces(args.webapi)
    elif args.extcap_dlts:
        dlts()
    elif args.extcap_config:
        config()
    elif args.capture and args.extcap_interface and args.fifo:
        try:
            return capture(args.webapi, args.extcap_interface, args.fifo)
        except (OSError, BrokenPipeError, KeyboardInterrupt):
            return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
