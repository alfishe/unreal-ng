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
    emulator_run_state(bool, bool)    — (is_running, is_paused) on transition
    action_result(str, dict, str)     — (verb, response, error_or_empty)
    error(str)                        — unrecoverable error message
    finished()                        — emitted when the worker exits

The emulator_run_state signal fires whenever the selected emulator's
run/pause state changes — including changes driven by OTHER clients
(the debugger, CLI, GDB, another UI). This lets the TTD Scrubber UI
follow the real emulator state instead of assuming it owns the only
control surface. The engine exposes is_running/is_paused on
GET /api/v1/emulator (per-instance) so we don't need a separate poll; we
piggyback on the existing instance-list refresh.

Slots (UI emits these — connected with Qt.QueuedConnection):
    on_thread_started()               — starts the QTimer (auto-connected)
    request_connect_to(str)
    request_select_instance(str)
    request_start_recording()         — RECORDING flow
    request_stop_recording()          — RECORDING flow
    request_invalidate(str)           — RECORDING flow (drops history)
    request_seek(int, int)            — PLAYBACK/SCRUB flow
    request_step_back()               — PLAYBACK/SCRUB flow
    request_step_forward()            — PLAYBACK/SCRUB flow
    request_emulator_pause()          — PLAYBACK transport (keeps TTD state)
    request_emulator_resume()         — PLAYBACK transport (keeps TTD state)
    request_shutdown()

NOTE: there is deliberately NO slot here for /ttd/resume
(ResumeRecordingFrom). That endpoint is a recording-flow operation
(truncates the future timeline, flips state Detached→Recording) and
has no place in a tool whose playback surface must be fully
disconnected from recording. If a future need arises, add a separate
verb explicitly named 'request_branch_from_here' so callers can't
mistake it for playback.
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
    # Emulator run/pause state — emitted only on transitions. Drives the
    # transport button enablement in MainWindow so external pauses (from
    # debugger / CLI / GDB) are reflected immediately. The UI also uses
    # it to flip its Pause/Play affordances to match reality.
    emulator_run_state = Signal(bool, bool)  # (is_running, is_paused)
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
        # Last observed emulator run/pause state for the SELECTED instance.
        # Tracked so we only emit emulator_run_state on actual transitions
        # — every poll tick re-reads is_running/is_paused from the instance
        # list, but we don't want to spam the UI with redundant signals.
        # None means 'unknown' (no instance selected yet).
        self._last_running: Optional[bool] = None
        self._last_paused: Optional[bool] = None
        # True while the server is reachable. Drives silent-retry behavior:
        # when the server goes away we keep polling once per second but
        # swallow the connection errors at DEBUG level (no log spam). The
        # UI is notified once on the transition (down) and again on recovery.
        self._server_reachable: bool = True
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
        # Last TTD-poll error type we surfaced to the UI. Used to emit the
        # error signal at most once per failure streak so the UI doesn't
        # get a duplicate popup every poll tick. Reset to None whenever a
        # TTD poll succeeds or the selected instance changes.
        self._last_ttd_error: Optional[str] = None

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
        # Reset connection-state tracking so the next tick is treated as a
        # fresh reachability probe rather than a continuation of a previous
        # outage.
        self._server_reachable = True
        # Reset TTD-error marker on reconnect so the new server gets a
        # clean slate (the previous server's 404 shouldn't suppress the
        # error signal for this one).
        self._last_ttd_error = None
        logger.info("connect_to: %s", url)

    @Slot(str)
    def request_select_instance(self, instance_id: str):
        self._mutex.lock()
        self._instance_id = instance_id or None
        self._instance_info = {}
        self._pending_actions.clear()
        self._mutex.unlock()
        # Reset run/pause tracking so the first poll after a selection
        # change emits a transition (None -> actual state). Otherwise the
        # UI could keep showing the previous instance's run/pause badge.
        self._last_running = None
        self._last_paused = None
        # Reset the TTD-error marker so a 404 against the new instance
        # produces a fresh error emission rather than being suppressed.
        self._last_ttd_error = None
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

    @Slot()
    def request_emulator_pause(self):
        # Emulator-level pause — does NOT change TTD state. Used by the
        # timeline Pause button to halt the emulator without scrubbing.
        # Distinct from request_seek(): seek moves the live position to a
        # historical checkpoint; this just freezes where it currently is.
        self._enqueue("emulator_pause", {})

    @Slot()
    def request_emulator_resume(self):
        # Emulator-level resume — does NOT transition TTD state from
        # Detached to Recording. The emulator runs forward from the
        # current Detached position; OnFrameBoundary auto-pauses once
        # currentFrame > sessionEnd (see timetravelmanager.cpp), so the
        # user can keep scrubbing after the run halts.
        self._enqueue("emulator_resume", {})

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
    # Classify whether an exception represents a server-down situation
    # (TCP failure, timeout, DNS, etc.) vs. an HTTP error response
    # (4xx/5xx — the server IS up, it just refused the request).
    #
    # Without this distinction, _poll_ttd_state and _drain_action would
    # wrongly mark the server unreachable on any non-2xx response,
    # producing an infinite "server went down" / "server reachable again"
    # loop against _poll_instances (which always succeeds next tick).
    # ------------------------------------------------------------------
    @staticmethod
    def _is_connection_error(exc: Exception) -> bool:
        msg = str(exc).lower()
        return any(s in msg for s in (
            "connection refused",
            "connection reset",
            "failed to establish a new connection",
            "max retries exceeded",
            "timeout",
            "timed out",
            "name or service not known",
            "nodename nor servname provided",
            "connection aborted",
            "remote end closed",
        ))

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
        # If the server is known to be unreachable, fail the action fast
        # without firing the HTTP call (which would add another connection-
        # refused warning to the log). The UI surfaces the error; the next
        # tick will silent-retry the reachability probe.
        if not self._server_reachable:
            self.action_result.emit(verb, {}, "server unreachable")
            return True

        try:
            response = self._dispatch(verb, instance_id, args)
            self.action_result.emit(verb, response or {}, "")
        except Exception as exc:
            # If the failure looks like a connection problem, flip the
            # reachable flag so subsequent ticks silent-retry instead of
            # spamming warnings. We still surface the error to the UI for
            # this action.
            if self._is_connection_error(exc):
                if self._server_reachable:
                    self._server_reachable = False
                    self.connected.emit(False, f"server unreachable: {exc}")
                    logger.warning("action %s — server went down: %s", verb, exc)
            else:
                # Non-connection error (e.g. 409 Conflict, 400 Bad Request,
                # 404 Not Found). Log as warning; the UI shows the error
                # inline via the action_result signal below.
                logger.warning("action %s failed: %s", verb, exc)
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
        if verb == "emulator_pause":
            return self._client.pause_emulator(instance_id)
        if verb == "emulator_resume":
            return self._client.resume_emulator(instance_id)
        raise ValueError(f"unknown action verb: {verb}")

    # ------------------------------------------------------------------
    # Poll the instance list. Returns False if the call failed.
    #
    # Connection-state discipline (per user request 2026-07-22):
    #   - When the server is reachable, emit connected(True, ...) and
    #     proceed.
    #   - When the server is unreachable, emit connected(False, ...)
    #     ONCE on the down transition, then keep retrying silently
    #     (DEBUG-level log) once per second until the server comes back.
    #     This avoids filling the log with "Connection refused" warnings
    #     while still making the UI reflect the real connection state.
    # ------------------------------------------------------------------
    def _poll_instances(self) -> bool:
        try:
            data = self._client.list_instances()
        except Exception as exc:
            if self._server_reachable:
                # Transition: was up, now down. Notify once.
                self._server_reachable = False
                self.connected.emit(False, f"server unreachable: {exc}")
                logger.warning("server unreachable — entering silent retry: %s", exc)
            else:
                # Still down — silent retry (DEBUG only, no UI signal).
                logger.debug("silent retry — server still unreachable: %s", exc)
            return False

        if not self._server_reachable:
            # Transition: was down, now up. Notify on recovery.
            self._server_reachable = True
            logger.info("server reachable again — resuming normal polling")

        if not isinstance(data, list):
            data = []

        self.connected.emit(True, f"connected — {len(data)} instance(s)")
        self.instances.emit(data)

        # Validate the current selection still exists.
        if self._instance_id is not None and data:
            selected_info: Optional[dict] = None
            ids = set()
            for i, info in enumerate(data):
                for key in ("id", "emulator_id", "instance_id", "uuid"):
                    if key in info and info[key]:
                        ids.add(str(info[key]))
                ids.add(str(i))
                # Capture the info dict for the currently-selected instance
                # so we can read its is_running/is_paused fields below.
                if (selected_info is None
                        and any(str(info.get(k, "")) == self._instance_id
                                for k in ("id", "emulator_id",
                                          "instance_id", "uuid"))):
                    selected_info = info
                elif selected_info is None and str(i) == self._instance_id:
                    selected_info = info
            if self._instance_id not in ids:
                logger.info("selected instance %s disappeared", self._instance_id)
                self._mutex.lock()
                self._instance_id = None
                self._instance_info = {}
                self._mutex.unlock()
                # Selection gone — reset run/pause tracking so the next
                # selection emits a clean transition.
                self._last_running = None
                self._last_paused = None
                self.selected.emit("", {})
            elif selected_info is not None:
                # Refresh the cached info dict (model, etc.) and emit the
                # selected signal so the UI's instance-meta label stays
                # current. The info dict may carry new fields (model,
                # symbolic_id) the UI surfaces.
                self._mutex.lock()
                self._instance_info = selected_info
                self._mutex.unlock()
                self.selected.emit(self._instance_id, selected_info)
                # Detect run/pause transitions for the selected instance.
                # The engine returns is_running/is_paused per instance on
                # every poll (lifecycle_api.cpp:42-43), so we don't need
                # a separate HTTP call. Emitting only on transitions keeps
                # the UI signal volume proportional to actual state changes
                # rather than poll cadence.
                is_running = bool(selected_info.get("is_running", False))
                is_paused = bool(selected_info.get("is_paused", False))
                if (is_running != self._last_running
                        or is_paused != self._last_paused):
                    logger.info(
                        "emulator run-state transition: "
                        "running %s->%s, paused %s->%s",
                        self._last_running, is_running,
                        self._last_paused, is_paused)
                    self._last_running = is_running
                    self._last_paused = is_paused
                    self.emulator_run_state.emit(is_running, is_paused)
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

        # If the server is known to be unreachable, skip TTD-specific
        # polling entirely. _poll_instances already failed and emitted the
        # disconnected signal; running these three calls would just produce
        # three more "Connection refused" warnings per second.
        if not self._server_reachable:
            return

        # Status
        try:
            status = self._client.ttd_status(instance_id)
            self.ttd_status.emit(status or {})
            state = (status or {}).get("state", "idle")
            # Clear the surfaced-error marker so a future failure can
            # re-emit the error signal (once).
            self._last_ttd_error = None
        except Exception as exc:
            # Distinguish "server actually unreachable" from "HTTP error
            # response". A 404/500/etc. means the server IS up — it just
            # refused the request. Misclassifying HTTP errors as "server
            # down" causes an infinite connected/disconnected loop with
            # _poll_instances (which succeeds on the very next tick).
            # See _drain_action for the same classification discipline.
            if self._is_connection_error(exc):
                if self._server_reachable:
                    self._server_reachable = False
                    self.connected.emit(False, f"server unreachable: {exc}")
                    logger.warning("ttd_status — server went down mid-poll: %s", exc)
            else:
                # HTTP error (e.g. 404 if the running binary predates the
                # TTD routes, 400/500 for handler-level failures). Server is
                # reachable; do not flip _server_reachable. A 404 on a TTD
                # endpoint specifically usually means the running emulator
                # binary was built without the TTD automation surface.
                msg = str(exc)
                if "404" in msg:
                    logger.warning(
                        "ttd_status — endpoint not found on running "
                        "emulator (likely TTD routes not compiled into "
                        "this binary): %s", exc)
                else:
                    logger.warning("ttd_status — HTTP error: %s", exc)
                # Surface to the UI once via the error signal so the user
                # sees the real cause instead of a misleading
                # "disconnected" flicker every second.
                if self._last_ttd_error != "ttd_status":
                    self._last_ttd_error = "ttd_status"
                    self.error.emit(
                        f"TTD status endpoint unavailable on this "
                        f"emulator build: {exc}")
            state = "idle"

        # Position — only if still reachable.
        if self._server_reachable:
            try:
                pos = self._client.ttd_position(instance_id)
                self.ttd_position.emit(pos or {})
            except Exception as exc:
                if self._is_connection_error(exc):
                    if self._server_reachable:
                        self._server_reachable = False
                        self.connected.emit(False, f"server unreachable: {exc}")
                        logger.warning("ttd_position — server went down mid-poll: %s", exc)
                else:
                    logger.warning("ttd_position — HTTP error: %s", exc)

        # Markers — only if still reachable.
        if self._server_reachable:
            try:
                markers_resp = self._client.ttd_markers(instance_id)
                markers_list = (markers_resp or {}).get("markers", [])
                self.ttd_markers.emit(markers_list if isinstance(markers_list, list) else [])
            except Exception as exc:
                if self._is_connection_error(exc):
                    if self._server_reachable:
                        self._server_reachable = False
                        self.connected.emit(False, f"server unreachable: {exc}")
                        logger.warning("ttd_markers — server went down mid-poll: %s", exc)
                else:
                    logger.warning("ttd_markers — HTTP error: %s", exc)

        # Adapt cadence: recording/detached = 500 ms; otherwise 1 s.
        if state != self._last_state:
            logger.info("state transition %s -> %s", self._last_state, state)
            self._last_state = state

        target_interval = 500 if state in ("recording", "detached") else 1000
        if self._timer.interval() != target_interval:
            self._timer.setInterval(target_interval)
            logger.debug("poll cadence -> %d ms", target_interval)
