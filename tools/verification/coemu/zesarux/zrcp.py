#!/usr/bin/env python3
"""A small client for ZRCP, the ZEsarUX remote command protocol (a line-based text protocol over TCP).

  zrcp.py freeports BASE COUNT       print COUNT free TCP ports at or above BASE (never 8090)
  zrcp.py cmd PORT CMD [CMD...]      run each command, print its reply
  zrcp.py run PORT ...               drive one co-emulation run (see ../README.md and `zrcp.py run -h`)
"""
import argparse
import re
import socket
import sys
import time

PROMPT = b"command> "

# Key codes of ZRCP's send-keys-event (ZEsarUX utils.h, enum util_teclas)
KEY_ENTER = 129
KEY_DOWN = 144


def log(msg):
    print(f"zrcp: {msg}", flush=True)


class Zrcp:
    def __init__(self, port, host="127.0.0.1", timeout=30.0):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self._read_to_prompt()

    def _read_to_prompt(self):
        buf = b""
        while not buf.endswith(PROMPT):
            chunk = self.s.recv(65536)
            if not chunk:
                raise ConnectionError("ZRCP connection closed")
            buf += chunk
        return buf[: -len(PROMPT)]

    def cmd(self, line):
        self.s.sendall(line.encode() + b"\n")
        return self._read_to_prompt().decode("latin-1").strip("\r\n")

    def key(self, code, hold=0.15, gap=0.25):
        """Press and release one key (wall-clock hold: the emulator runs in real time)"""
        self.cmd(f"send-keys-event {code} 1")
        time.sleep(hold)
        self.cmd(f"send-keys-event {code} 0")
        time.sleep(gap)

    def screen(self):
        """The screen as text (ZEsarUX's own OCR of the ROM font), blank lines at the end dropped"""
        lines = [line.rstrip() for line in self.cmd("get-ocr").splitlines()]
        while lines and not lines[-1]:
            lines.pop()
        return "\n".join(lines)

    def byte(self, addr):
        return int(self.cmd(f"read-memory {addr} 1")[:2], 16)

    def close(self):
        try:
            self.s.sendall(b"quit\n")
        except OSError:
            pass
        self.s.close()


def connect(port, timeout):
    """Connect once the emulator has opened its ZRCP port"""
    end = time.time() + timeout
    while True:
        try:
            return Zrcp(port)
        except OSError:
            if time.time() > end:
                raise
            time.sleep(0.2)


def free_ports(base, count):
    ports = []
    p = base
    while len(ports) < count and p < 65536:
        if p != 8090:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                s.bind(("127.0.0.1", p))
                ports.append(p)
            except OSError:
                pass
            finally:
                s.close()
        p += 1
    return ports


def wait_screen(z, text, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if text in z.screen():
            return True
        time.sleep(0.25)
    return False


def trdos_run(z):
    """From the Pentagon's 128 menu: 128 BASIC, RANDOMIZE USR 15616 (TR-DOS), then RUN (the disk's boot)"""
    if not wait_screen(z, "128 BASIC", 20):
        raise RuntimeError("no 128 menu on screen:\n" + z.screen())
    z.key(KEY_DOWN)  # the menu starts on "Tape Loader"; one down is "128 BASIC"
    z.key(KEY_ENTER)
    time.sleep(1.0)
    z.cmd("send-keys-string 100 randomize usr 15616")  # 128 BASIC takes keywords letter by letter
    time.sleep(0.5)
    z.key(KEY_ENTER)
    if not wait_screen(z, "A>", 10):
        raise RuntimeError("TR-DOS did not start:\n" + z.screen())
    z.key(ord("r"))  # TR-DOS takes keywords like 48 BASIC: R in K mode is RUN
    z.key(KEY_ENTER)
    log("TR-DOS: RUN")


def baseconf_boot(z, seconds):
    """ZX-Evo BaseConf: its boot menu (EVO Reset Service) should show words within seconds of power-on"""
    end = time.time() + seconds
    while time.time() < end:
        text = z.screen()
        if re.search(r"[A-Za-z]{3}", text):
            # It booted: the menu steps to TR-DOS are not written yet (ZEsarUX 13.0 never got this far)
            raise RuntimeError("BaseConf booted, but this runner has no steps for its menu yet:\n" + text)
        time.sleep(1.0)
    raise RuntimeError(f"BaseConf did not boot: no words on the screen {seconds} s after power-on "
                       f"(PC {z.cmd('get-registers').split()[0][3:]})")


def run(a):
    z = connect(a.port, a.connect_timeout)
    log(f"connected to ZEsarUX {z.cmd('get-version')}, machine {z.cmd('get-current-machine')}")
    if a.baseconf:
        baseconf_boot(z, 30)
    if a.trdos:
        trdos_run(z)
    # Frames are counted from ZEsarUX's partial T-state counter (it overflows at 10^9: read and reset it)
    z.cmd("reset-tstates-partial")
    tstates = 0
    done = False
    next_note = 0
    while True:
        time.sleep(a.poll)
        v = z.cmd("get-tstates-partial")
        z.cmd("reset-tstates-partial")
        tstates += int(v) if v.isdigit() else 10**9
        frames = tstates // a.frame_tstates
        if z.byte(a.done) == 1:
            done = True
            break
        if frames >= a.max_frames:
            break
        if frames >= next_note:
            log(f"frame {frames}")
            next_note += 1000
    log(f"{'DONE' if done else 'timeout'} after {frames} frames")
    with open(a.out + ".screen.txt", "w") as f:
        f.write(z.screen() + "\n")
    if done:
        hexdump = "".join(z.cmd(f"read-memory {a.start} {a.end - a.start}").split())
        data = bytes.fromhex(hexdump)
        if len(data) != a.end - a.start:
            raise RuntimeError(f"read {len(data)} bytes, expected {a.end - a.start}")
        with open(a.out + ".bin", "wb") as f:
            f.write(data)
        log(f"dumped #{a.start:04X}..#{a.end - 1:04X} to {a.out}.bin")
    try:
        z.cmd("exit-emulator")
    except (OSError, ConnectionError):
        pass
    return 0 if done else 1


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "freeports":
        print(" ".join(map(str, free_ports(int(sys.argv[2]), int(sys.argv[3])))))
        return 0
    if len(sys.argv) > 1 and sys.argv[1] == "cmd":
        z = Zrcp(int(sys.argv[2]))
        try:
            for c in sys.argv[3:]:
                print(z.cmd(c))
        except ConnectionError:  # exit-emulator closes the connection without a prompt
            return 0
        z.close()
        return 0
    p = argparse.ArgumentParser(prog="zrcp.py run")
    p.add_argument("mode", choices=["run"])
    p.add_argument("port", type=int)
    p.add_argument("--done", type=int, required=True, help="address of the DONE byte")
    p.add_argument("--start", type=int, required=True, help="first address to dump")
    p.add_argument("--end", type=int, required=True, help="address after the last one to dump")
    p.add_argument("--frame-tstates", type=int, required=True)
    p.add_argument("--max-frames", type=int, required=True)
    p.add_argument("--out", required=True, help="output prefix: <out>.bin, <out>.screen.txt")
    p.add_argument("--trdos", action="store_true", help="start the disk from TR-DOS (Pentagon)")
    p.add_argument("--baseconf", action="store_true", help="ZX-Evo BaseConf: boot through its menu first")
    p.add_argument("--poll", type=float, default=1.0, help="seconds between DONE checks")
    p.add_argument("--connect-timeout", type=float, default=60.0)
    a = p.parse_args()
    try:
        return run(a)
    except Exception as e:  # noqa: BLE001 - one line in the log is what the runner needs
        log(f"error: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
