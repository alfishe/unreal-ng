#!/usr/bin/env python3
"""Switch the video wall's single sync mode through the GUI control plane (`tilegrid.single_sync`,
docs/emulator/design/gui-control), and optionally load a snapshot on the machine every tile then shows.

    videowall_singlesync.py --on [--snapshot game.z80]
    videowall_singlesync.py --off
    videowall_singlesync.py game.z80          # on, then the snapshot

The video wall is found by its discovery file (program unreal-videowall); --host picks one by id, --tcp connects to
a TCP listener (UNREAL_GUI_CONTROL_PORT). The snapshot goes through the WebAPI the video wall announces.
"""
import argparse
import os
import sys

import requests

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..', 'guicontrol'))
from guicontrol import Gui, GuiError, hosts  # noqa: E402


def connect(args):
    if args.tcp:
        return Gui.connect(tcp=args.tcp)
    walls = [e for e in hosts() if e.get('program') == 'unreal-videowall' and (not args.host or e['id'] == args.host)]
    if not walls:
        raise GuiError('not_found', 'no unreal-videowall is running' + (f' with host id {args.host}' if args.host else ''))
    return Gui.connect(host=walls[0]['id'])


def load_snapshot(endpoint, emulator_id, snapshot_path):
    url = f"{endpoint}/api/v1/emulator/{emulator_id}/snapshot/load"
    print(f"Loading snapshot via {url}")
    try:
        response = requests.post(url, json={"path": os.path.abspath(snapshot_path)}, timeout=5)
        response.raise_for_status()
        print("Snapshot loaded")
        return True
    except requests.exceptions.RequestException as e:
        print(f"Error loading snapshot: {e}")
        if getattr(e, 'response', None) is not None:
            print(f"Server response: {e.response.text}")
        return False


def main():
    parser = argparse.ArgumentParser(description="Video wall single sync mode (GUI control plane)")
    parser.add_argument("snapshot_pos", nargs="?", help="Snapshot to load on the synced machine (SNA/Z80)")
    parser.add_argument("--on", action="store_true", help="Enable single sync mode")
    parser.add_argument("--off", action="store_true", help="Disable single sync mode")
    parser.add_argument("--snapshot", help="Snapshot to load on the synced machine (SNA/Z80)")
    parser.add_argument("--host", help="The video wall's host id (its discovery file); default: the oldest one")
    parser.add_argument("--tcp", help="ADDRESS:PORT of the video wall's TCP listener instead of the local socket")
    args = parser.parse_args()

    snapshot_path = args.snapshot or args.snapshot_pos
    if args.off:
        enable = False
    elif args.on or snapshot_path:
        enable = True
    else:
        print("Please specify --on, --off, or pass a snapshot file path.")
        parser.print_help()
        sys.exit(1)

    try:
        with connect(args) as gui:
            body = gui.call('tilegrid.single_sync', on=enable)
            print(f"Single sync: {'on' if body.get('single_sync') else 'off'}")
            if not (enable and snapshot_path):
                return
            tiles = gui.state('main/tilegrid')['state'].get('tiles') or []
            endpoints = gui.list().get('endpoints') or []
    except (OSError, GuiError) as e:
        print(f"GUI control: {e}")
        sys.exit(1)
    if not tiles or not endpoints:
        print("No machine on the tiles or no WebAPI announced: the snapshot is not loaded")
        sys.exit(1)
    if not load_snapshot(endpoints[0], tiles[0]['instance'], snapshot_path):
        sys.exit(1)


if __name__ == "__main__":
    main()
