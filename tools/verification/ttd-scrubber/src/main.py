#!/usr/bin/env python3
"""
TTD Scrubber — entry point.

Connects to the emulator WebAPI, waits for an active instance, and lets
the user drive the Time-Travel Debug engine (record, stop, seek, scrub,
resume) via a PySide6 GUI.

Usage:
    python main.py [--url http://localhost:8090]

The script lives in tools/verification/ttd-scrubber/. It expects the
shared api_client.py from tools/verification/webapi/src/ to be on the
Python path. The repo's run.sh wrapper sets this up automatically; if
you run main.py directly, do so from the src/ directory and add the
webapi/src path:

    PYTHONPATH=../../../webapi/src python main.py

Ctrl+C (SIGINT) in the console quits the application cleanly: the
worker thread is told to shut down via the MainWindow close path and
the process exits with status 0.
"""

import argparse
import logging
import os
import signal
import sys


def _bootstrap_path():
    """Add ../webapi/src to sys.path so api_client.py is importable.

    Resolves relative to this file, not the current working directory,
    so the launcher can be invoked from anywhere.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    # ../webapi/src relative to ttd-scrubber/src/main.py.
    webapi_src = os.path.normpath(os.path.join(here, "..", "..", "webapi", "src"))
    if os.path.isdir(webapi_src) and webapi_src not in sys.path:
        sys.path.insert(0, webapi_src)
    # Also add this directory so poll_worker can find ttd_client.
    if here not in sys.path:
        sys.path.insert(0, here)


def _parse_args(argv):
    p = argparse.ArgumentParser(description="TTD Scrubber — WebAPI client")
    p.add_argument(
        "--url", default="http://localhost:8090",
        help="WebAPI base URL (default: http://localhost:8090)",
    )
    p.add_argument(
        "--verbose", "-v", action="store_true",
        help="Enable debug logging",
    )
    return p.parse_args(argv)


def _install_sigint_handler(app, window):
    """Make Ctrl+C in the console quit the Qt app cleanly.

    Two problems have to be solved together:
      1. Qt installs its own SIGINT handler that terminates the process
         abruptly — no Python `finally` blocks run, the worker thread
         is never told to shut down, sockets can leak.
      2. Even with a Python handler installed, Python defers signal
         delivery until the interpreter regains control. The Qt event
         loop runs in C++, so without a periodic Python-scheduled
         wakeup the handler never runs.

    Solution: install a Python SIGINT handler that triggers the normal
    window-close path (which drains the worker thread), and run a
    ~50ms QTimer so Python's signal-check point fires often enough to
    catch the SIGINT before the user gives up and sends SIGKILL.
    """
    from PySide6.QtCore import QTimer

    def _on_sigint(signum, frame):
        logging.getLogger("main").info(
            "SIGINT received — closing window and shutting down")
        # Trigger the normal close path so the worker thread drains.
        # closeEvent is invoked synchronously by Qt when close() is
        # called on a top-level window.
        window.close()
        # Belt-and-braces: quit the app if close() was vetoed.
        app.quit()

    signal.signal(signal.SIGINT, _on_sigint)

    # 50ms wakeup timer. The callback does nothing; its only job is to
    # yield back to Python so the signal handler can fire. Without this,
    # Ctrl+C in a terminal would have no visible effect until the user
    # interacts with the window.
    wakeup = QTimer()
    wakeup.setInterval(50)
    wakeup.timeout.connect(lambda: None)
    wakeup.start()
    # Guard against GC — keep a reference on the app.
    app._sigint_wakeup = wakeup


def main(argv=None):
    args = _parse_args(argv if argv is not None else sys.argv[1:])
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(name)s %(levelname)s: %(message)s",
    )

    _bootstrap_path()

    # Import after sys.path is configured.
    from PySide6.QtWidgets import QApplication
    from main_window import MainWindow

    app = QApplication(sys.argv)
    app.setApplicationName("TTD Scrubber")

    window = MainWindow(base_url=args.url)
    window.show()

    _install_sigint_handler(app, window)

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
