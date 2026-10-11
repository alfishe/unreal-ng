"""A small terminal UI for the examples, standard library only.

Works in Windows Terminal, PowerShell and cmd (Windows 10+: ANSI is switched on for the console), macOS
Terminal, iTerm2 and Linux terminals. Without a terminal (a pipe, a CI log) it prints plain lines and never waits
for a key.

    choose("Pick an emulator", ["48K running", "PENTAGON paused"])  -> 0 / 1, None for q / Esc
    wait_until(find_something, "Waiting for unreal-qt")            -> what find_something returned
    progress("frame 12 / 200")                                     -> one status line, rewritten in place
"""

import os
import sys
import time

INTERACTIVE = sys.stdin.isatty() and sys.stdout.isatty()


def _enable_ansi():
    """True when the terminal understands ANSI escapes; on Windows switch them on for this console."""
    if not INTERACTIVE or os.environ.get("TERM") == "dumb":
        return False
    if os.name != "nt":
        return True
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.GetStdHandle(-11)                 # STD_OUTPUT_HANDLE
        mode = ctypes.c_uint32()
        if not kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            return False
        return bool(kernel32.SetConsoleMode(handle, mode.value | 0x0004))   # ENABLE_VIRTUAL_TERMINAL_PROCESSING
    except Exception:
        return False


ANSI = _enable_ansi()


def _style(text, code):
    return "\x1b[{}m{}\x1b[0m".format(code, text) if ANSI else text


def bold(text):
    return _style(text, "1")


def dim(text):
    return _style(text, "2")


# --- keys ---

def read_key():
    """One key press: 'up', 'down', 'enter', 'esc', or the character. Ctrl+C raises KeyboardInterrupt."""
    if os.name == "nt":
        import msvcrt
        ch = msvcrt.getwch()
        if ch in ("\x00", "\xe0"):                          # arrows and other special keys come in two parts
            return {"H": "up", "P": "down"}.get(msvcrt.getwch(), "")
        if ch == "\x03":
            raise KeyboardInterrupt
        return {"\r": "enter", "\n": "enter", "\x1b": "esc"}.get(ch, ch)

    import select
    import termios
    import tty
    fd = sys.stdin.fileno()
    saved = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)                                   # keys one by one, Ctrl+C still interrupts
        ch = os.read(fd, 1)
        if ch == b"\x1b":
            if not select.select([fd], [], [], 0.05)[0]:
                return "esc"
            seq = os.read(fd, 2)                            # ESC [ A / ESC O A (application cursor mode)
            return {b"[A": "up", b"OA": "up", b"[B": "down", b"OB": "down"}.get(seq, "")
        if ch in (b"\r", b"\n"):
            return "enter"
        return ch.decode(errors="replace")
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, saved)


# --- the menu ---

def choose(title, items, refresh=None):
    """Let the user pick one of `items` (strings). Returns its index, or None when the user quits.
    `refresh`: a function giving the items anew (key r). Without a terminal: None when there is a choice to make."""
    if not items:
        return None
    if not INTERACTIVE:
        print(title)
        for i, item in enumerate(items, 1):
            print("  {}. {}".format(i, item))
        return None
    if not ANSI:
        return _choose_by_number(title, items)

    selected = 0
    drawn = 0
    hint = "Up/Down or 1-9: select   Enter: choose   " + ("r: refresh   " if refresh else "") + "q: quit"
    sys.stdout.write("\x1b[?25l")                           # hide the cursor while the menu is up
    try:
        while True:
            lines = [bold(title)]
            for i, item in enumerate(items):
                marker = "> " if i == selected else "  "
                text = "{}{}. {}".format(marker, i + 1, item)
                lines.append(_style(text, "7") if i == selected else text)
            lines.append(dim(hint))
            if drawn:
                sys.stdout.write("\x1b[{}F".format(drawn))  # back to the menu's first line
            for line in lines:
                sys.stdout.write("\x1b[2K" + line + "\n")
            for _ in range(drawn - len(lines)):             # the list got shorter: clear what is left below
                sys.stdout.write("\x1b[2K\n")
            if drawn > len(lines):
                sys.stdout.write("\x1b[{}F".format(drawn - len(lines)))
            sys.stdout.flush()
            drawn = len(lines)

            key = read_key()
            if key == "up":
                selected = (selected - 1) % len(items)
            elif key == "down":
                selected = (selected + 1) % len(items)
            elif key.isdigit() and 1 <= int(key) <= len(items):
                selected = int(key) - 1
            elif key == "enter":
                return selected
            elif key in ("q", "Q", "esc"):
                return None
            elif key in ("r", "R") and refresh:
                items = refresh() or items
                selected = min(selected, len(items) - 1)
    finally:
        sys.stdout.write("\x1b[?25h")
        sys.stdout.flush()


def _choose_by_number(title, items):
    print(title)
    for i, item in enumerate(items, 1):
        print("  {}. {}".format(i, item))
    while True:
        answer = input("Number (Enter = 1, q = quit): ").strip()
        if answer in ("q", "Q"):
            return None
        if not answer:
            return 0
        if answer.isdigit() and 1 <= int(answer) <= len(items):
            return int(answer) - 1


# --- waiting and status ---

_status_shown = False


def progress(text):
    """A status line rewritten in place (a terminal); without one, nothing."""
    global _status_shown
    if INTERACTIVE:
        sys.stdout.write("\r" + text[:120].ljust(min(120, max(len(text), 60))))
        sys.stdout.flush()
        _status_shown = True


def end_progress():
    """Leave the status line before printing anything else."""
    global _status_shown
    if _status_shown:
        sys.stdout.write("\r" + " " * 120 + "\r")
        sys.stdout.flush()
        _status_shown = False


def wait_until(probe, message, interval=0.5):
    """Call probe() until it returns something true and return that. A spinner and the time waited show meanwhile;
    Ctrl+C gives up (KeyboardInterrupt)."""
    found = probe()
    if found:
        return found
    if not INTERACTIVE:
        print(message)
    started = time.time()
    spinner = "|/-\\"
    i = 0
    while True:
        if INTERACTIVE:
            progress("{} {}  ({} s, Ctrl+C to quit)".format(spinner[i % 4], message, int(time.time() - started)))
            i += 1
        time.sleep(interval)
        found = probe()
        if found:
            end_progress()
            return found
