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
"""

import argparse
import logging
import os
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

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
