#!/usr/bin/env python3
"""Drive NextZXOS's menu and Browser on a running NEXT machine through the WebAPI keyboard endpoints.

    drive.py [--port 8090] STEP [STEP ...]

STEP is one of
    boot            wait until the machine idles (the welcome page or the menu waits for a key)
    space           press SPACE and wait (leaves the welcome page / opens the menu's More)
    browser         press B (the menu's Browser) and wait for its listing
    go:<name>       Browser: search for <name> (H, the typed text, ENTER) and open it - a directory or, with a file, run it
    enter           press ENTER (open / run the highlighted entry) without waiting
    root            press EDIT until the Browser is at the card's root
    break           press BREAK (back one level)
    status          print the machine report (CPU clock, DivMMC mapping, MMU slots)

"Idle" is the NextZXOS key-wait loop: the CPU halted at #0C8F. The Browser remembers the directory it was last in (on the card:
the user's NextZXOS state), so `browser` can open anywhere - start with `go:` the names of the path from the folder shown.

Example, run the base Copper real-board test:
    drive.py boot space browser go:tests go:base go:copper enter
"""
import argparse, json, os, re, sys, time, urllib.request


class Machine:
    def __init__(self, port):
        self.base = 'http://localhost:%d/api/v1/emulator' % port
        self.id = self.call('')['emulators'][0]['id']

    def call(self, path, body=None):
        request = urllib.request.Request(self.base + path, data=json.dumps(body).encode() if body is not None else None,
                                         headers={'Content-Type': 'application/json'})
        return json.load(urllib.request.urlopen(request))

    def tap(self, key, frames=6):
        self.call('/%s/keyboard/tap' % self.id, {'key': key, 'frames': frames})

    def idle(self, settle=1.0, timeout=60):
        """Wait until the CPU waits for a key (halted in NextZXOS's loop at #0C8F)"""
        time.sleep(settle)
        end = time.time() + timeout
        while time.time() < end:
            regs = self.call('/%s/registers' % self.id)
            if regs['interrupt']['halted'] and regs['special']['pc'] == 0x0C8F:
                return True
            time.sleep(0.25)
        return False

    def root(self):
        """EDIT goes up a directory: press it until the Browser is at the card's root"""
        for _ in range(8):
            self.tap('edit')
            self.idle(0.6)

    def reach(self, folder, name, runs=False):
        """Put the cursor on `name` of the directory shown (host path `folder`) and press ENTER. The Browser's order is '.' '..' then the
        names case-insensitively; the search (H) takes the shortest unique prefix of letters and digits, any other name is reached by
        counting down from the top. runs=True: the entry is a program, do not wait for the key-wait loop afterwards"""
        entries = ['.', '..'] + sorted(os.listdir(folder), key=lambda n: n.lower())
        lower = [e.lower() for e in entries]
        safe = re.match(r'^[A-Za-z0-9]+', name)
        if safe:
            text = safe.group(0).lower()
            for length in range(1, len(text) + 1):
                if sum(e.startswith(text[:length]) for e in lower) == 1:
                    self.tap('h')
                    time.sleep(0.3)
                    self.call('/%s/keyboard/type' % self.id, {'text': name[:length], 'delay_frames': 5})
                    time.sleep(0.4 + 0.12 * length)
                    self.idle(0.3)
                    self.tap('enter')  # accept the search
                    time.sleep(0.3)
                    break
            else:
                self._count_down(entries.index(name))
        else:
            self._count_down(entries.index(name))
        self.tap('enter')  # open / run the entry
        if not runs:
            self.idle(1.2)

    def _count_down(self, index):
        for _ in range(index):
            self.tap('down', 4)
            time.sleep(0.12)

    def search(self, name, runs=False):
        self.tap('h')
        time.sleep(0.3)
        self.call('/%s/keyboard/type' % self.id, {'text': name, 'delay_frames': 5})
        time.sleep(0.4 + 0.12 * len(name))
        self.idle(0.3)
        self.tap('enter')  # accept the search
        time.sleep(0.3)
        self.tap('enter')  # open the entry found
        if not runs:  # a program that runs never comes back to the key-wait loop
            self.idle(1.2)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', type=int, default=8090)
    parser.add_argument('steps', nargs='+')
    args = parser.parse_args()
    machine = Machine(args.port)
    for step in args.steps:
        if step == 'boot':
            ok = machine.idle(2.5)
        elif step == 'space':
            machine.tap('space')
            ok = machine.idle(2)
        elif step == 'browser':
            machine.tap('b')
            ok = machine.idle(2.5)
        elif step.startswith('go:'):
            machine.search(step[3:])
            ok = True
        elif step == 'enter':
            machine.tap('enter')
            ok = True
        elif step == 'root':
            machine.root()
            ok = True
        elif step == 'break':
            machine.tap('break')
            ok = machine.idle(1)
        elif step == 'status':
            print(json.dumps(machine.call('/%s/state/next' % machine.id)['machine'], indent=1))
            ok = True
        else:
            sys.exit('unknown step ' + step)
        print('%-14s %s' % (step, 'ok' if ok else 'TIMEOUT (the machine is not idle)'))


if __name__ == '__main__':
    main()
