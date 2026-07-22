"""
PollWorker — the background HTTP worker for the TTD Scrubber.

Implemented using the recommended Qt worker-object pattern:
PollWorker is a QObject that lives on a dedicated QThread (created and
owned by MainWindow). Because the worker object is moved to that thread
via moveToThread(), every QTimer it creates and every slot that runs on
it executes on the worker thread — no threading ambiguity.

Adaptive polling cadence:
  - No instance selected          → 2000 ms
  - Instance selected, idle       → 1000 ms
  - Recording or detached         → 500 ms

UI clicks are delivered via the request_* slots below; with the worker
moved to its thread, cross-thread signal/slot invocations are queued
automatically. The worker never touches widgets.

Signals (UI consumes these):
    connected(bool, str)              — server reachable, message
    instances(list[dict])             — list of emulator instances
    selected(str, dict)               — current instance id + info
    ttd_status(dict)                  — /ttd/status payload
    ttd_position(dict)                — /ttd/position payload
    ttd_markers(list[dict])           — /ttd/markers payload
    action_result(str, dict, str)     — (verb, response, error_or_empty)
    error(str)                        — unrecoverable error message
    finished()                        — emitted when the worker exits

Slots (UI emits these — connected with Qt.QueuedConnection):
    on_thread_started()               — starts the QTimer (auto-connected)
    request_connect_to(str)
    request_select_instance(str)
    request_start_recording()
    request_stop_recording()
    request_invalidate(str)
    request_seek(int, int)
    request_step_back()
    request_step_forward()
    request_resume(int, int)
    request_shutdown()
"""

import logging
from typing import Optional

from PySide6.QtCore import QObject, QTimer, Signal, Slot, QMutex


logger = logging.getLogger("PollWorker")


class PollWorker(QObject):
    """Background HTTP worker. Owns the TTDApiClient; never touches widgets.

    Thread-affinity contract: the caller constructs this object on the UI
    thread, then immediately calls moveToThread(worker_thread) before the
    thread is started. After that, all slots execute on the worker thread.
    """

    # ------------------------------------------------------------------
    # Signals (emitted on the worker thread; UI consumes via Qt's
    # automatic cross-thread queueing).
    # ------------------------------------------------------------------
    connected = Signal(bool, str)
    instances = Signal(list)
    selected = Signal(str, dict)
    ttd_status = Signal(dict)
    ttd_position = Signal(dict)
    ttd_markers = Signal(list)
    action_result = Signal(str, dict, str)  # (verb, response, error_msg)
    error = Signal(str)
    finished = Signal()

    # ------------------------------------------------------------------
    # ctor — runs on whatever thread constructs the object (UI thread).
    # After moveToThread(), all subsequent slot calls run on the worker.
    # ------------------------------------------------------------------
    def __init__(self, parent=None):
        super().__init__(parent)
        # The TTDApiClient lives on the worker thread. Created in
        # _ensure_client() the first time _tick runs.
        self._client = None
        # Base URL set by request_connect_to.
        self._base_url: Optional[str] = None
        # Currently selected instance id and info dict.
        self._instance_id: Optional[str] = None
        self._instance_info: dict = {}
        # Last observed TTD state — drives the adaptive poll cadence.
        self._last_state: str = "idle"
        # Mutex guards _instance_id and _pending_actions.
        self._mutex = QMutex()
        # Action queue: list of (verb, args) tuples. The tick drains one
        # entry per cycle so HTTP calls stay serialized and predictable.
        self._pending_actions: list = []
        # Set true by request_shutdown(). Stops the timer and exits.
        self._stopping = False
        # The QTimer itself — created in on_thread_started() so it has
        # worker-thread affinity.
        self._timer: Optional[QTimer] = None

    # ------------------------------------------------------------------
    # Lifecycle: starts the QTimer when the owning QThread starts.
    # Auto-connected by MainWindow to QThread.started.
    # ------------------------------------------------------------------
    @Slot()
    def on_thread_started(self):
        # QTimer created here — affinity is the worker thread (because
        # this slot runs on the worker thread after moveToThread).
        self._timer = QTimer()
        self._timer.timeout.connect(self._tick)  # DirectConnection implied:
        # both sender (QTimer) and receiver (self QObject) live on the
        # worker thread, so Qt picks DirectConnection by default.
        self._timer.setInterval(1000)
        self._timer.start()
        logger.info("PollWorker started on its thread")

    # ------------------------------------------------------------------
    # Request slots — UI emits these; they execute on the worker thread.
    # ------------------------------------------------------------------
    @Slot(str)
    def request_connect_to(self, url: str):
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

    @Slot(str)
    def request_select_instance(self, instance_id: str):
        self._mutex.lock()
        self._instance_id = instance_id or None
        self._instance_info = {}
        self._pending_actions.clear()
        self._mutex.unlock()
        self.selected.emit(self._instance_id or "", self._instance_info)
        logger.info("select_instance: %s", self._instance_id)

    @Slot()
    def request_start_recording(self):
        self._enqueue("start", {})

    @Slot()
    def request_stop_recording(self):
        self._enqueue("stop", {})

    @Slot(str)
    def request_invalidate(self, reason: str):
        self._enqueue("invalidate", {"reason": reason or "User requested"})

    @Slot(int, int)
    def request_seek(self, frame: int, tinframe: int):
        self._enqueue("seek", {"frame": int(frame), "tinframe": int(tinframe)})

    @Slot()
    def request_step_back(self):
        self._enqueue("step_back", {})

    @Slot()
    def request_step_forward(self):
        self._enqueue("step_forward", {})

    @Slot(int, int)
    def request_resume(self, frame: int, tinframe: int):
        # frame == -1 means "resume from current position"
        args = {} if frame < 0 else {"frame": int(frame), "tinframe": int(tinframe)}
        self._enqueue("resume", args)

    @Slot()
    def request_shutdown(self):
        # Just flip the flag. We deliberately do NOT call
        # self._timer.stop() here because this slot may be invoked
        # directly from the UI thread (e.g. closeEvent) — touching the
        # QTimer from a non-worker thread triggers Qt warnings and is
        # silently ignored anyway. Setting _stopping makes _tick a
        # no-op, so the timer is effectively inert; it will be cleaned
        # up when the worker QThread exits and the worker QObject is
        # eventually garbage-collected.
        self._stopping = True
        logger.info("shutdown requested")
        self.finished.emit()

    def _enqueue(self, verb: str, args: dict):
        self._mutex.lock()
        self._pending_actions.append((verb, args))
        self._mutex.unlock()
        logger.debug("queued action: %s %s", verb, args)

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
    # Main tick — fires on every timer interval (worker thread).
    # ------------------------------------------------------------------
    def _tick(self):
        if self._stopping or self._timer is None:
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
            # Auto-select when none chosen. May succeed or not.
            self._auto_select()
        # Re-check selection after auto-select attempt.
        if self._instance_id is None:
            # Still no instance — slow down the poll.
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
    # Poll the instance list. Returns False if the call failed.
    # ------------------------------------------------------------------
    def _poll_instances(self) -> bool:
        try:
            data = self._client.list_instances()
        except Exception as exc:
            self.connected.emit(False, f"connection error: {exc}")
            return False

        if not isinstance(data, list):
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
        if not instance_id or not self._client or self._timer is None:
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

        # Markers
        try:
            markers_resp = self._client.ttd_markers(instance_id)
            markers_list = (markers_resp or {}).get("markers", [])
            self.ttd_markers.emit(markers_list if isinstance(markers_list, list) else [])
        except Exception as exc:
            logger.warning("ttd_markers failed: %s", exc)

        # Adapt cadence: recording/detached = 500 ms; otherwise 1 s.
        if state != self._last_state:
            logger.info("state transition %s -> %s", self._last_state, state)
            self._last_state = state

        target_interval = 500 if state in ("recording", "detached") else 1000
        if self._timer.interval() != target_interval:
            self._timer.setInterval(target_interval)
            logger.debug("poll cadence -> %d ms", target_interval)
            self._last_state = state

        target_interval = 500 if state in ("recording", "detached") else 1000
        if self._timer.interval() != target_interval:
            self._timer.setInterval(target_interval)
            logger.debug("poll cadence -> %d ms", target_interval)
            self._last_state = state

        target_interval = 500 if state in ("recording", "detached") else 1000
        if self._timer.interval() != target_interval:
            self._timer.setInterval(target_interval)
            logger.debug("poll cadence -> %d ms", target_interval)
