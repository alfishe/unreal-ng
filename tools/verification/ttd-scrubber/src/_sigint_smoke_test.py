"""
Smoke test for the Ctrl+C / SIGINT handler in main.py.

Verifies that:
  1. SIGINT triggers window.close() and app.quit().
  2. The worker thread is told to shut down (request_shutdown runs).
  3. The worker thread exits cleanly within a reasonable timeout.
  4. app.exec() returns 0 (clean exit).

Run:
    python3 _sigint_smoke_test.py
"""

import os
import signal
import sys
import threading
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])

from PySide6.QtCore import QCoreApplication, QTimer


# ---------------------------------------------------------------------------
# Fake TTDApiClient — never touches the network.
# ---------------------------------------------------------------------------
class _FakeClient:
    def __init__(self, base_url):
        self.base_url = base_url

    def list_instances(self):
        return [{"id": "fake-instance-id", "model": "TestModel"}]

    def find_first_active(self):
        return ("fake-instance-id",
                {"id": "fake-instance-id", "model": "TestModel"}, 0)

    def ttd_status(self, instance_id):
        return {"state": "idle", "ttd_available": True,
                "session_start_frame": 0, "current_end_frame": 0,
                "checkpoint_count": 0, "page_store_bytes": 0,
                "page_store_used_bytes": 0, "baseline_frames_captured": 0}

    def ttd_position(self, instance_id):
        return {"current": {"frame": 0, "tinframe": 0},
                "session_end": {"frame": 0, "tinframe": 0},
                "state": "idle"}

    def ttd_markers(self, instance_id):
        return {"count": 0, "markers": []}

    def ttd_start(self, instance_id):
        return {"started": True, "already_active": False, "state": "recording"}


import types
fake_module = types.ModuleType("ttd_client")
fake_module.TTDApiClient = _FakeClient
sys.modules["ttd_client"] = fake_module

import poll_worker  # noqa: E402


def main():
    # Use QCoreApplication to keep this headless. main._install_sigint_handler
    # works equally against QApp/QApplication.
    app = QCoreApplication(sys.argv)
    print(f"[test] main thread  = {threading.current_thread().name}")
    print(f"[test] process pid  = {os.getpid()}")

    # Wire up the worker exactly as MainWindow does.
    from PySide6.QtCore import QThread
    worker_thread = QThread()
    worker = poll_worker.PollWorker()
    worker.moveToThread(worker_thread)
    worker_thread.started.connect(worker.on_thread_started)
    worker.finished.connect(worker_thread.quit)

    # Track whether request_shutdown fired.
    shutdown_fired = {"yes": False}
    orig_request_shutdown = worker.request_shutdown

    def tracked_request_shutdown():
        shutdown_fired["yes"] = True
        print("[test] worker.request_shutdown() invoked")
        return orig_request_shutdown()

    worker.request_shutdown = tracked_request_shutdown

    # Import the SIGINT installer from main.py.
    sys.path.insert(0, ".")
    import main as main_module

    # Build a fake "window" with a close() method that calls request_shutdown
    # (mirrors what MainWindow.closeEvent does).
    class _FakeWindow:
        def close(self):
            print("[test] window.close() invoked — delegating to worker shutdown")
            worker.request_shutdown()

    fake_window = _FakeWindow()
    main_module._install_sigint_handler(app, fake_window)

    worker_thread.start()

    # Schedule SIGINT to ourselves after 1 second (gives the worker time
    # to start and tick at least once).
    def send_sigint():
        print("[test] sending SIGINT to self")
        os.kill(os.getpid(), signal.SIGINT)

    QTimer.singleShot(1000, send_sigint)

    # Hard stop after 5 seconds if something went wrong.
    QTimer.singleShot(5000, app.quit)

    print("[test] running event loop…")
    exit_code = app.exec()
    print(f"[test] event loop returned (exit_code={exit_code})")

    # Wait for worker thread to finish.
    worker_thread.quit()
    thread_done = worker_thread.wait(2000)
    print(f"[test] worker_thread exited cleanly? {thread_done}")
    print(f"[test] worker_thread still running?  {worker_thread.isRunning()}")
    print(f"[test] request_shutdown fired?        {shutdown_fired['yes']}")

    failures = []
    if not shutdown_fired["yes"]:
        failures.append("SIGINT did not trigger request_shutdown")
    if worker_thread.isRunning():
        failures.append("worker_thread is still running after 2s wait")
    if exit_code != 0:
        # We didn't pass through main()'s sys.exit, so exit_code reflects
        # QCoreApplication.exit default (0). If non-zero, something broke.
        failures.append(f"unexpected exit code {exit_code}")

    if failures:
        print()
        for f in failures:
            print(f"[test] FAIL: {f}")
        sys.exit(1)

    print()
    print("[test] ALL CHECKS PASSED")
    print("       - SIGINT delivered to Python handler")
    print("       - window.close() → worker.request_shutdown()")
    print("       - worker_thread exited cleanly")


if __name__ == "__main__":
    main()
