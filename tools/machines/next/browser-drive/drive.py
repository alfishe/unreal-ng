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
The keys, hold frames and waits come from testdata/machines/zxnext/browser-navigation.json (navigation.py), shared with
regress.py and NexBrowserRun_Test.

Example, run the base Copper real-board test:
    drive.py boot space browser go:tests go:base go:copper enter
"""
import argparse, base64, hashlib, io, json, os, sys, time, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import navigation as nav  # noqa: E402


def paper(im, left, top, right, bottom):
    """A box given in paper pixels (the 256 x 192 paper at 2x: 512 x 384) as a crop box of the captured frame, whatever its border
    (640 x 480 with a 64-pixel border, 720 x 576 with the full one)"""
    x0, y0 = (im.size[0] - 512) // 2, (im.size[1] - 384) // 2
    return (x0 + left, y0 + top, x0 + right, y0 + bottom)


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

    def combo(self, keys, frames=6):
        self.call('/%s/keyboard/combo' % self.id, {'keys': keys, 'frames': frames})

    def idle(self, settle=1.0, timeout=60):
        """Wait until the CPU waits for a key (halted in NextZXOS's loop at #0C8F)"""
        time.sleep(settle)
        end = time.time() + timeout
        while time.time() < end:
            regs = self.call('/%s/registers' % self.id)
            if regs['interrupt']['halted'] and regs['special']['pc'] == nav.IDLE_PC:
                return True
            time.sleep(0.25)
        return False

    def step(self, step, chars=0):
        """One step of browser-navigation.json (a name or a step object)"""
        return nav.realtime_step(self, step, chars)

    def root(self):
        """EDIT goes up a directory: press it until the Browser is at the card's root"""
        for _ in range(8):
            self.step('up')

    def screen(self):
        from PIL import Image
        data = json.load(urllib.request.urlopen('%s/%s/capture/screen' % (self.base, self.id)))
        return Image.open(io.BytesIO(base64.b64decode(data['data']))).convert('RGB')

    def search_line_sig(self):
        """The Browser's bottom prompt line (Search: <text>) as a hash: it changes when typed text arrives"""
        im = self.screen()
        return hashlib.md5(im.crop(paper(im, 0, 354, 512, 370)).tobytes()).hexdigest()

    def type_search(self, text):
        """H, the text, ENTER. A typed text can fail to arrive (the Search prompt stayed empty and the next ENTER opened the wrong
        entry). The prompt line has a blinking cursor, so "did it change" compares against BOTH blink states taken before typing; the
        text is typed again only when the prompt still equals one of them after a settle time (typing twice leaves doubled letters:
        never retype on a doubt)"""
        self.step('search')
        self.idle(0.3)
        before = set()
        for _ in range(6):  # a few frames apart: both blink phases
            before.add(self.search_line_sig())
            time.sleep(0.15)
        for attempt in range(2):
            self.call('/%s/keyboard/type' % self.id, {'text': text, 'delay_frames': nav.STEPS['type']['delay']})
            self.step('type', len(text))
            shots = {self.search_line_sig() for _ in range(4) if time.sleep(0.12) is None}
            if not (shots & before):
                break
        self.step('accept')  # the cursor stays on the match; the Browser has taken the search before the next ENTER

    def reach(self, folder, name, root=False):
        """Put the Browser's cursor on `name` of the directory shown (host path `folder`; root: the card root, no '.' / '..') by the
        search with the shortest unique prefix, or by counting down from the top (browser-navigation.json, search)"""
        listing = nav.entries(folder, root)
        prefix = nav.search_prefix(listing, name)
        if prefix:
            self.type_search(prefix)
            return 'search:' + prefix
        for _ in range(listing.index(name)):
            self.step('down')
        return 'down:%d' % listing.index(name)

    def run_file(self, name, between=None):
        """Run the file `name` under the cursor (its extension's run steps); between(step) is called after each step"""
        for step in nav.run_steps(name):
            self.step(step)
            if between:
                between(step)

    def search(self, name, runs=False):
        """go:<name>: search for the typed text, then open the entry found (a directory) or run it (runs=True: no idle wait)"""
        self.type_search(name)
        if runs:
            self.tap('enter')
        else:
            self.step('open')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', type=int, default=8090)
    parser.add_argument('steps', nargs='+')
    args = parser.parse_args()
    machine = Machine(args.port)
    for step in args.steps:
        if step == 'boot':
            ok = machine.step('boot')
        elif step == 'space':
            ok = machine.step('menu')
        elif step == 'browser':
            ok = machine.step('browser')
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
            ok = machine.step('back')
        elif step == 'status':
            print(json.dumps(machine.call('/%s/state/next' % machine.id)['machine'], indent=1))
            ok = True
        else:
            sys.exit('unknown step ' + step)
        print('%-14s %s' % (step, 'ok' if ok else 'TIMEOUT (the machine is not idle)'))


if __name__ == '__main__':
    main()
