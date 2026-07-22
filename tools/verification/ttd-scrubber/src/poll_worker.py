"""
PollWorker — the background HTTP thread for the TTD Scrubber.

Runs in its own QThread so the UI never blocks on a network call. A QTimer
on the thread drives adaptive polling (2s when no instance, 1s idle, 500ms
when recording or detached). UI clicks are forwarded as slot calls; the
worker performs the HTTP request and emits a result signal back to the UI
thread.

Signals (UI consumes these):
    connected(bool, str)              — server reachable, message
    instances(list[dict])             — list of emulator instances
    selected(str, dict)               — current instance id + info
    ttd_status(dict)                  — /ttd/status payload
    ttd_position(dict)                — /ttd/position payload
    ttd_markers(list[dict])           — /ttd/markers payload
    action_result(str, dict, str)     — (verb, response, error_or_empty)
    error(str)                        — unrecoverable error message

Slots (UI emits these):
    connect_to(url)
    select_instance(str)
    start_recording()
    stop_recording()
    invalidate(str)
    seek(int, int)
    step_back()
    step_forward()
    resume(int, int)
    shutdown()
"""

import logging
import time
from typing import Optional

from PySide6.QtCore import QThread, QTimer, Signal, Slot, QMutex


logger = logging.getLogger("PollWorker")


class PollWorker(QThread):
    """Background HTTP worker. Owns the TTDApiClient; never touches widgets."""

    # ------------------------------------------------------------------
    # Signals (emitted on the worker thread; UI must use Qt.QueuedConnection
    # for any slot that touches widgets, which is the default for cross-
    # thread signal/slot connections).
    # ------------------------------------------------------------------
    connected = Signal(bool, str)
    instances = Signal(list)
    selected = Signal(str, dict)
    ttd_status = Signal(dict)
    ttd_position = Signal(dict)
    ttd_markers = Signal(list)
    action_result = Signal(str, dict, str)  # (verb, response, error_msg)
    error = Signal(str)

    # ------------------------------------------------------------------
    # ctor
    # ------------------------------------------------------------------
    def __init__(self, parent=None):
        super().__init__(parent)
        # The TTDApiClient lives on the worker thread. Created in run().
        self._client = None
        # Base URL set by on_connect_to.
        self._base_url: Optional[str] = None
        # The currently selected instance id (string) and its info dict.
        self._instance_id: Optional[str] = None
        self._instance_info: dict = {}
        # Last observed TTD state ("idle" / "recording" / "detached").
        # Drives the adaptive poll cadence.
        self._last_state: str = "idle"
        # Mutex guards _instance_id and _pending_actions.
        self._mutex = QMutex()
        # Action queue: list of (verb, args) tuples. The tick drains one
        # entry per cycle so HTTP calls stay serialized and predictable.
        self._pending_actions: list = []
        # Set true by shutdown(). Stops the timer and exits run().
        self._stopping = False

    # ------------------------------------------------------------------
    # Slot: connect to a WebAPI base URL.
    # ------------------------------------------------------------------
    @Slot(str)
    def on_connect_to(self, url: str):
        url = (url or "").strip().rstrip("/")
        if not url:
            url = "http://localhost:8090"
        self._base_url = url
        # Force the client to be rebuilt on the next tick.
        self._client = None
        # Drop any in-flight actions and clear selection.
        self._mutex.lock()
        self._instance_id = None
        self._instance_info = {}
        self._pending_actions.clear()
        self._mutex.unlock()
        logger.info("connect_to: %s", url)

    # ------------------------------------------------------------------
    # Slot: select a specific instance by id.
    # ------------------------------------------------------------------
    @Slot(str)
    def on_select_instance(self, instance_id: str):
        self._mutex.lock()
        self._instance_id = instance_id or None
        self._instance_info = {}
        self._pending_actions.clear()
        self._mutex.unlock()
        # Emit the new selection immediately (info may be empty until the
        # next instances tick refreshes it).
        self.selected.emit(self._instance_id or "", self._instance_info)
        logger.info("select_instance: %s", self._instance_id)

    # ------------------------------------------------------------------
    # Slots: queue UI-driven TTD actions. The tick loop drains them one
    # per cycle so HTTP stays serialized and the UI sees a clean
    # action_result signal for each.
    # ------------------------------------------------------------------
    @Slot()
    def on_start_recording(self):
        self._enqueue("start", {})

    @Slot()
    def on_stop_recording(self):
        self._enqueue("stop", {})

    @Slot(str)
    def on_invalidate(self, reason: str):
        self._enqueue("invalidate", {"reason": reason or "User requested"})

    @Slot(int, int)
    def on_seek(self, frame: int, tinframe: int):
        self._enqueue("seek", {"frame": int(frame), "tinframe": int(tinframe)})

    @Slot()
    def on_step_back(self):
        self._enqueue("step_back", {})

    @Slot()
    def on_step_forward(self):
        self._enqueue("step_forward", {})

    @Slot(int, int)
    def on_resume(self, frame: int, tinframe: int):
        # frame == -1 means "resume from current position"
        args = {} if frame < 0 else {"frame": int(frame), "tinframe": int(tinframe)}
        self._enqueue("resume", args)

    @Slot()
    def on_shutdown(self):
        self._stopping = True
        # Quit the Qt event loop inside run().
        self.quit()
        logger.info("shutdown requested")

    def _enqueue(self, verb: str, args: dict):
        self._mutex.lock()
        self._pending_actions.append((verb, args))
        self._mutex.unlock()
        logger.debug("queued action: %s %s", verb, args)

    # ------------------------------------------------------------------
    # Thread entry point.
    # ------------------------------------------------------------------
    def run(self):
        # Lazy import so the module loads even without PySide6 in the
        # environment (e.g. unit tests of the API client alone).
        # (TTDApiClient is created in _ensure_client.)

        # The QTimer lives on this thread. Start at 1s; _tick adjusts it.
        self._timer = QTimer()
        self._timer.timeout.connect(self._tick)
        self._timer.setInterval(1000)
        self._timer.start()

        logger.info("PollWorker started")

        # Run the thread's Qt event loop. This blocks until quit() is
        # called from shutdown(). The QTimer fires _tick from within
        # this loop, so polling runs without a manual while loop.
        super().run()

        self._timer.stop()
        logger.info("PollWorker stopped")

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------
    def _ensure_client(self):
        """Recreate the TTDApiClient if the base URL changed or it's gone."""
        if not self._base_url:
            return False
        if self._client is not None:
            return True
        try:
            from ttd_client import TTDApiClient
            self._client = TTDApiClient(self._base_url)
            logger.info("client rebuilt for %s", self._base_url)
            return True
        except Exception as exc:
            self.error.emit(f"failed to create API client: {exc}")
            self._client = None
            return False

    # ------------------------------------------------------------------
    # Main tick — fires on every timer interval.
    # ------------------------------------------------------------------
    def _tick(self):
        if self._stopping:
            return
        if not self._ensure_client():
            # No base URL configured yet. Nothing to poll.
            return

        # Drain one queued action per tick.
        if self._drain_action():
            # An action was dispatched; skip polling this cycle to keep
            # responses ordered. The next tick will poll.
            return

        # Poll sequence. Order matters: instance list first, then TTD
        # state. If the selected instance disappeared we recover.
        if not self._poll_instances():
            return
        if self._instance_id is None:
            # Auto-select when none chosen.
            self._auto_select()
            # Slow down when no instance is selected.
            if self._timer.interval() != 2000:
                self._timer.setInterval(2000)
                logger.debug("poll cadence -> 2000 ms (no instance)")
            return
        self._poll_ttd_state()

    # ------------------------------------------------------------------
    # Drain a single queued action and dispatch its HTTP call.
    # Returns True if an action was dispatched (caller skips poll).
    # ------------------------------------------------------------------
    def _drain_action(self) -> bool:
        self._mutex.lock()
        if not self._pending_actions:
            self._mutex.unlock()
            return False
        verb, args = self._pending_actions.pop(0)
        instance_id = self._instance_id
        self._mutex.unlock()

        if not instance_id:
            self.action_result.emit(verb, {}, "no emulator instance selected")
            return True
        if not self._client:
            self.action_result.emit(verb, {}, "not connected")
            return True

        try:
            response = self._dispatch(verb, instance_id, args)
            self.action_result.emit(verb, response or {}, "")
        except Exception as exc:
            logger.exception("action %s failed", verb)
            self.action_result.emit(verb, {}, str(exc))
        return True

    def _dispatch(self, verb: str, instance_id: str, args: dict) -> dict:
        """Single dispatch. One verb -> one HTTP call."""
        if verb == "start":
            return self._client.ttd_start(instance_id)
        if verb == "stop":
            return self._client.ttd_stop(instance_id)
        if verb == "invalidate":
            return self._client.ttd_invalidate(instance_id, args.get("reason", ""))
        if verb == "seek":
            return self._client.ttd_seek(instance_id, args["frame"], args.get("tinframe", 0))
        if verb == "step_back":
            return self._client.ttd_step_back(instance_id)
        if verb == "step_forward":
            return self._client.ttd_step_forward(instance_id)
        if verb == "resume":
            frame = args.get("frame")
            tin = args.get("tinframe")
            return self._client.ttd_resume(instance_id, frame, tin)
        raise ValueError(f"unknown action verb: {verb}")

    # ------------------------------------------------------------------
    # Poll the instance list. Returns False if the call failed (caller
    # skips TTD polling).
    # ------------------------------------------------------------------
    def _poll_instances(self) -> bool:
        try:
            data = self._client.list_instances()
        except Exception as exc:
            self.connected.emit(False, f"connection error: {exc}")
            return False

        if not isinstance(data, list):
            # Older or unexpected payload shape — treat as empty.
            data = []

        self.connected.emit(True, f"connected — {len(data)} instance(s)")
        self.instances.emit(data)

        # Validate the current selection still exists.
        if self._instance_id is not None and data:
            ids = set()
            for i, info in enumerate(data):
                for key in ("id", "emulator_id", "instance_id", "uuid"):
                    if key in info and info[key]:
                        ids.add(str(info[key]))
                ids.add(str(i))
            if self._instance_id not in ids:
                logger.info("selected instance %s disappeared", self._instance_id)
                self._mutex.lock()
                self._instance_id = None
                self._instance_info = {}
                self._mutex.unlock()
                self.selected.emit("", {})
        return True

    # ------------------------------------------------------------------
    # Auto-pick the first active instance when none is selected.
    # ------------------------------------------------------------------
    def _auto_select(self):
        try:
            instance_id, info, idx = self._client.find_first_active()
        except IndexError:
            return
        self._mutex.lock()
        self._instance_id = instance_id
        self._instance_info = info
        self._mutex.unlock()
        self.selected.emit(instance_id, info)
        logger.info("auto-selected instance %s", instance_id)

    # ------------------------------------------------------------------
    # Poll TTD state, position, and markers. Adjusts the timer interval
    # based on the observed state.
    # ------------------------------------------------------------------
    def _poll_ttd_state(self):
        instance_id = self._instance_id
        if not instance_id or not self._client:
            return

        # Status
        try:
            status = self._client.ttd_status(instance_id)
            self.ttd_status.emit(status or {})
            state = (status or {}).get("state", "idle")
        except Exception as exc:
            logger.warning("ttd_status failed: %s", exc)
            state = "idle"

        # Position
        try:
            pos = self._client.ttd_position(instance_id)
            self.ttd_position.emit(pos or {})
        except Exception as exc:
            logger.warning("ttd_position failed: %s", exc)

        # Markers (lower priority — every other tick is fine, but we
        # fetch every tick for simplicity; the volume is tiny).
        try:
            markers_resp = self._client.ttd_markers(instance_id)
            markers_list = (markers_resp or {}).get("markers", [])
            self.ttd_markers.emit(markers_list if isinstance(markers_list, list) else [])
        except Exception as exc:
            logger.warning("ttd_markers failed: %s", exc)

        # Adapt cadence. Recording/detached = 500 ms; idle = 1 s;
        # if no instance was selected earlier in this tick, the timer
        # stays slow (set in _poll_instances path implicitly).
        if state != self._last_state:
            logger.info("state transition %s -> %s", self._last_state, state)
            self._last_state = state

        target_interval = 500 if state in ("recording", "detached") else 1000
        if self._timer.interval() != target_interval:
            self._timer.setInterval(target_interval)
            logger.debug("poll cadence -> %d ms", target_interval)
