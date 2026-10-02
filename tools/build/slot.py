#!/usr/bin/env python3
"""slot.py - run a command while holding one slot of a named, machine-wide pool.

    slot.py [-t SECONDS] [-q] POOL -- COMMAND [ARGS...]
    slot.py --status

Pools and sizes (override with UNREAL_SLOTS_<POOL>=N, POOL upper-cased, '-' -> '_'):
    build = 2    concurrent compiles / links, across all agents and worktrees
    test  = 1    concurrent test runs
    other = 1

A slot is a file <slots dir>/<pool>/<N>.lock held with flock(2). The KERNEL releases the
lock when the holding process dies for any reason (exit, crash, SIGKILL, closed terminal),
so there is no stale-owner detection, no pid/start-time bookkeeping and no timing rule.

A caller that vanishes must not leave its build running, so the command runs under a small
watchdog process that shares the lock and a pipe with this wrapper. When the pipe closes
without the "finished" byte (wrapper killed, however), the watchdog terminates the
command's process group (TERM, then KILL after 2 s) and only then exits, which frees the
slot - normally within a second or two, never more than ~3 s.

The command runs at lowered priority (nice 10; UNREAL_NICE=0 disables) in pools build/test.

Slots dir: $UNREAL_SLOTS_DIR, else ${XDG_CACHE_HOME:-~/.cache}/unreal-ng/slots - one per
user, so it is shared by every checkout and worktree on the machine.

Exit status: the command's, 128+N if it died from signal N, 75 on -t timeout, 64 usage,
70 environment problem. POSIX only (macOS, Linux); on Windows the command is run directly,
without a limit.
"""

import os
import random
import signal
import subprocess
import sys
import time

DEFAULT_SIZES = {"build": 2, "test": 1}
POLL = 0.25
TERM_GRACE = 2.0


def die(code, msg):
    sys.stderr.write("slot.py: %s\n" % msg)
    sys.exit(code)


def slots_dir():
    d = os.environ.get("UNREAL_SLOTS_DIR")
    if not d:
        cache = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
        d = os.path.join(cache, "unreal-ng", "slots")
    return d


def pool_size(pool):
    var = "UNREAL_SLOTS_" + pool.upper().replace("-", "_")
    raw = os.environ.get(var)
    if raw is None:
        return DEFAULT_SIZES.get(pool, 1)
    if not raw.isdigit() or int(raw) < 1:
        die(64, "%s must be a positive integer" % var)
    return int(raw)


def parse(argv):
    timeout, quiet, status = 0, False, False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "-t":
            if i + 1 >= len(argv) or not argv[i + 1].isdigit():
                die(64, "-t needs seconds")
            timeout = int(argv[i + 1])
            i += 2
        elif a == "-q":
            quiet = True
            i += 1
        elif a == "--status":
            status = True
            i += 1
        elif a == "--":
            i += 1
            break
        elif a.startswith("-"):
            die(64, "unknown option %s" % a)
        else:
            break
    if status:
        return timeout, quiet, True, None, []
    if i >= len(argv):
        die(64, "usage: slot.py [-t SECONDS] [-q] POOL -- COMMAND [ARGS...]")
    pool = argv[i]
    i += 1
    if i < len(argv) and argv[i] == "--":
        i += 1
    cmd = argv[i:]
    if not cmd:
        die(64, "no command given")
    if not pool.replace("-", "").isalnum():
        die(64, "bad pool name %r" % pool)
    return timeout, quiet, False, pool, cmd


def try_lock(path):
    """Return an open fd holding the exclusive lock on path, or None when it is busy."""
    import fcntl
    fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o644)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        os.close(fd)
        return None
    return fd


def describe(fd, cmd):
    """Leave a human-readable note about the holder in the lock file (for --status)."""
    try:
        os.ftruncate(fd, 0)
        os.pwrite(fd, ("%d %d %s\n" % (os.getpid(), int(time.time()), " ".join(cmd))).encode("utf-8", "replace"), 0)
    except OSError:
        pass


def status():
    root = slots_dir()
    if not os.path.isdir(root):
        print("no slots yet (%s)" % root)
        return 0
    for pool in sorted(os.listdir(root)):
        pdir = os.path.join(root, pool)
        if not os.path.isdir(pdir):
            continue
        size = pool_size(pool)
        busy = []
        for n in range(size):
            path = os.path.join(pdir, "%d.lock" % n)
            if not os.path.exists(path):
                continue
            fd = try_lock(path)
            if fd is None:
                with open(path, "r", errors="replace") as f:
                    busy.append("  slot %d: %s" % (n, f.read().strip() or "(held)"))
            else:
                os.close(fd)
        print("%s: %d/%d busy" % (pool, len(busy), size))
        for line in busy:
            print(line)
    return 0


def watchdog(read_fd, pgid):
    """Runs in a forked process that holds the lock fd too. Never returns."""
    try:
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        finished = False
        while True:
            data = os.read(read_fd, 16)
            if not data:
                break                      # every write end closed: wrapper is gone
            if b"X" in data:
                finished = True
                break
        if not finished:
            try:
                os.killpg(pgid, signal.SIGTERM)
            except OSError:
                pass
            deadline = time.time() + TERM_GRACE
            while time.time() < deadline:
                try:
                    os.killpg(pgid, 0)
                except OSError:
                    break
                time.sleep(0.05)
            try:
                os.killpg(pgid, signal.SIGKILL)
            except OSError:
                pass
    finally:
        os._exit(0)


def run_with_slot(pool, cmd, timeout, quiet):
    import fcntl  # noqa: F401  (fail early where unavailable)
    size = pool_size(pool)
    pdir = os.path.join(slots_dir(), pool)
    try:
        os.makedirs(pdir, exist_ok=True)
    except OSError as e:
        die(70, "cannot create %s: %s (set UNREAL_SLOTS_DIR)" % (pdir, e))

    t0 = time.time()
    last_note = 0.0
    first = True
    fd = None
    while fd is None:
        order = list(range(size))
        random.shuffle(order)               # spread the herd, harmless for correctness
        for n in order:
            fd = try_lock(os.path.join(pdir, "%d.lock" % n))
            if fd is not None:
                break
        if fd is not None:
            break
        waited = time.time() - t0
        if timeout and waited >= timeout:
            die(75, "timed out after %ds waiting for a '%s' slot (all %d busy)" % (timeout, pool, size))
        if not quiet and (first or waited - last_note >= 15):
            sys.stderr.write("slot.py: waiting for a '%s' slot (all %d busy), queued %ds...\n" % (pool, size, waited))
            first = False
            last_note = waited
        time.sleep(POLL)
    waited = time.time() - t0
    if not quiet and waited >= 2:
        sys.stderr.write("slot.py: got '%s' slot after %ds\n" % (pool, waited))
    describe(fd, cmd)

    argv = list(cmd)
    preexec = None
    if pool in ("build", "test") and os.environ.get("UNREAL_NICE", "1") != "0":
        preexec = lambda: os.nice(10)       # noqa: E731

    r, w = os.pipe()                        # non-inheritable by default (PEP 446)
    try:
        child = subprocess.Popen(argv, stdin=subprocess.DEVNULL, start_new_session=True,
                                 preexec_fn=preexec)
    except OSError as e:
        sys.stderr.write("slot.py: cannot run %s: %s\n" % (argv[0], e))
        return 127 if isinstance(e, FileNotFoundError) else 126
    pgid = child.pid

    wd = os.fork()
    if wd == 0:
        os.close(w)
        watchdog(r, pgid)                   # holds a copy of fd (the lock) until done
    os.close(r)

    state = {"sig": None}

    def forward(signum, _frame):
        state["sig"] = signum
        try:
            os.killpg(pgid, signal.SIGTERM if signum != signal.SIGKILL else signal.SIGKILL)
        except OSError:
            pass
        # escalate if it ignores TERM
        signal.signal(signum, signal.SIG_DFL)
        signal.setitimer(signal.ITIMER_REAL, TERM_GRACE + 1)

    def escalate(_signum, _frame):
        try:
            os.killpg(pgid, signal.SIGKILL)
        except OSError:
            pass

    signal.signal(signal.SIGALRM, escalate)
    for s in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(s, forward)

    while True:
        try:
            rc = child.wait()
            break
        except InterruptedError:
            continue

    # Tell the watchdog it is a normal finish, then wait for it so the slot is really
    # free by the time we return (the next waiter must not need another poll cycle).
    signal.setitimer(signal.ITIMER_REAL, 0)
    try:
        os.write(w, b"X")
    except OSError:
        pass
    os.close(w)
    try:
        os.waitpid(wd, 0)
    except OSError:
        pass
    os.close(fd)

    if state["sig"] is not None:
        return 128 + state["sig"]
    return 128 - rc if rc < 0 else rc


def main():
    timeout, quiet, want_status, pool, cmd = parse(sys.argv[1:])
    if want_status:
        return status()
    if os.name != "posix":
        sys.stderr.write("slot.py: no slot limiter on this platform, running the command directly\n")
        return subprocess.call(cmd)
    return run_with_slot(pool, cmd, timeout, quiet)


if __name__ == "__main__":
    sys.exit(main())
