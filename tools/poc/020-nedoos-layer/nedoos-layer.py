#!/usr/bin/env python3
"""NedoOS layer POC: see and drive a running NedoOS in unreal-ng over the WebAPI.

  nedoos-layer.py [--url URL] [--id ID] <command> [args]

Read-only (the machine is paused for a consistent view, then resumed):
  detect      is this the kernel build the symbols describe
  tasks       task table: states, parents, windows, command lines, where each stopped
  pages       physical page owners
  pipes       kernel pipes (console plumbing)
  sockets     Wiznet driver socket table
  files       open FatFS files
  kernel      is the CPU in the kernel, for which task, in which call (busy report)
  all         everything above

Changing the machine (debug mode is switched on for the duration):
  trace [N]                 sample N kernel calls
  call TASK CMD [DE] [HL]   run one kernel call on behalf of TASK (CMD: name or number)
  unstick                   end a stuck Wiznet call with ERR_NOTCONN
"""

import argparse
import json
import sys

from nedooslayer.control import Control
from nedooslayer.layer import NedoOs
from nedooslayer.webapi import WebApi


def hexpages(ports):
    return "[" + " ".join(f"{p ^ 0xFF:02X}" for p in ports) + "]"


def show_detect(os_view):
    ok, checks = os_view.detect()
    print(f"NedoOS kernel (symbols evo-inetdrv1-44049473): {'yes' if ok else 'NO'}")
    for name, passed in checks.items():
        print(f"  {'ok ' if passed else 'BAD'} {name}")
    return ok


def show_tasks(os_view):
    current, focus = os_view.current_app(), os_view.focus_app()
    print("id par state                  pages(0/4/8/C)  pc    sp    cmdline")
    for t in os_view.tasks():
        if t.app == current:
            where = {"pc": None, "sp": None}
            mark = " CURRENT"
        else:
            where = os_view.stopped_at(t)
            mark = ""
        if t.app == focus:
            mark += " FOCUS"
        pc = f"{where['pc']:04X}" if where["pc"] is not None else "cpu "
        sp = f"{where['sp']:04X}" if where["sp"] is not None else "cpu "
        print(f"{t.id:2} {t.parent:3} {','.join(t.states):22} {hexpages(t.windows):15} {pc}  {sp}  "
              f"{t.command_line!r}{mark}")


def show_pages(os_view):
    owners = os_view.page_owners()
    by_owner = {}
    for page, owner in enumerate(owners):
        by_owner.setdefault(owner, []).append(page)
    for owner in sorted(by_owner):
        label = {0: "free", 0xFF: "system"}.get(owner, f"task {owner}")
        pages = by_owner[owner]
        shown = pages if len(pages) <= 24 else pages[:24] + ["..."]
        print(f"{label:8} {len(pages):3} pages  {shown}")


def show_pipes(os_view):
    for p in os_view.pipes():
        print(f"pipe #{p['handle']:02X}: open sides {p['open_sides']}, reader task {p['reader']}, "
              f"{p['lines']} lines, queued {len(p['queued'])} bytes {p['queued'][:40]!r}")


def show_sockets(os_view):
    sockets, local_port = os_view.sockets()
    print(f"next local port #{local_port:04X}")
    for s in sockets:
        if s["owner"]:
            print(f"socket {s['handle']:2}: owner task {s['owner']}, chip socket {s['chip_socket']}, "
                  f"rx {s['rx_count']}")
    print("(protocol, state and addresses live in the chip only)")


def show_files(os_view):
    for f in os_view.open_files():
        print(f"file {f['index']:2}: owner task {f['owner']}, {f['position']}/{f['size']} bytes, "
              f"dir entry in sector {f['dir_sector']} at +#{f['dir_entry_offset']:X} (name needs a sector read)")


def show_kernel(os_view):
    s = os_view.kernel_state()
    if not s["in_kernel"]:
        print(f"CPU in user mode at #{s['pc']:04X}, banks {s['bank_pages']}")
        return s
    print(f"CPU IN KERNEL for task {s['task']} ({s['task_name']}): {', '.join(s['command'])}")
    print(f"  handler {s['handler_label']}, called from #{s['caller_pc']:04X}, user sp #{s['user_sp']:04X}")
    print(f"  cpu pc #{s['pc']:04X} = {s['cpu_label']}, bc=#{s['bc']:04X}, a=#{s['a']:02X}")
    return s


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--url", default="http://localhost:8090")
    parser.add_argument("--id", default=None)
    parser.add_argument("--json", action="store_true", help="kernel/trace/call results as JSON")
    parser.add_argument("command")
    parser.add_argument("args", nargs="*")
    a = parser.parse_args()

    api = WebApi(a.url, a.id)
    os_view = NedoOs(api)
    control = Control(os_view)
    views = {"detect": show_detect, "tasks": show_tasks, "pages": show_pages, "pipes": show_pipes,
             "sockets": show_sockets, "files": show_files, "kernel": show_kernel}

    if a.command in views or a.command == "all":
        was_paused = api.is_paused()
        api.pause()
        try:
            if not show_detect(os_view) and a.command != "detect":
                return 1
            for name in (views if a.command == "all" else [a.command]):
                if name == "detect":
                    continue
                print(f"\n== {name}")
                views[name](os_view)
        finally:
            if not was_paused:
                api.resume()
        return 0

    if a.command == "trace":
        count = int(a.args[0]) if a.args else 60
        tasks = {t.id: t.name for t in os_view.tasks()}
        for (task, cmd), n in control.trace(count).most_common():
            print(f"task {task} ({tasks.get(task, '?')}): CMD_{cmd} x{n}")
        return 0

    if a.command == "call":
        task_id = int(a.args[0])
        cmd = a.args[1]
        number = int(cmd, 0) if cmd[0].isdigit() else os_view.sym["CMD_" + cmd.upper().removeprefix("CMD_")]
        de = int(a.args[2], 0) if len(a.args) > 2 else None
        hl = int(a.args[3], 0) if len(a.args) > 3 else None
        r = control.call(task_id, number, de=de, hl=hl)
        m = r["main"]
        out = {"a": m["af"] >> 8, "f": m["af"] & 0xFF, "bc": m["bc"], "de": m["de"], "hl": m["hl"],
               "ix": r["index"]["ix"], "iy": r["index"]["iy"]}
        print(json.dumps(out) if a.json else " ".join(f"{k}=#{v:0{2 if k in 'af' else 4}X}" for k, v in out.items()))
        return 0

    if a.command == "unstick":
        api.pause()
        state = control.end_stuck_network_call()
        api.resume()
        print(f"ended {', '.join(state['command'])} of task {state['task']} with ERR_NOTCONN")
        return 0

    parser.error(f"unknown command {a.command}")


if __name__ == "__main__":
    sys.exit(main())
