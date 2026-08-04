"""
MainWindow — top-level window composing all TTD Scrubber widgets.

Layout (top to bottom):
  1. Connection bar: URL field, Connect button, connection state badge.
  2. Instance picker: dropdown of emulator instances.
  3. TTD session panel: state badge, session info, Start/Stop/Invalidate.
  4. Timeline panel: slider, frame label, Step/Seek/Resume buttons.
  5. Markers panel: list of external events; double-click to seek.

All HTTP work happens on the PollWorker thread. The UI never touches
the network directly. Signals from PollWorker update the widgets.
"""

import logging

from PySide6.QtCore import Qt, Slot, QThread
from PySide6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QFormLayout,
    QLabel, QLineEdit, QPushButton, QComboBox, QSlider, QListWidget,
    QListWidgetItem, QMessageBox, QGroupBox, QStatusBar, QCheckBox,
)
from PySide6.QtGui import QColor

from poll_worker import PollWorker


logger = logging.getLogger("MainWindow")


class _HumanBytes:
    """Helper for MainWindow._fmt_bytes — kept at module scope so the
    formatting logic is unit-testable without instantiating Qt."""

    UNITS = ("B", "KB", "MB", "GB", "TB")

    @staticmethod
    def _fmt_abs(n: int) -> str:
        assert n >= 0
        if n < 1024:
            return f"{n} B"
        # Walk up the unit ladder until the value fits in [1.0, 1024.0).
        value = float(n)
        unit_idx = 0
        while value >= 1024.0 and unit_idx < len(_HumanBytes.UNITS) - 1:
            value /= 1024.0
            unit_idx += 1
        # One decimal place; drop the .0 for clean integers like '4 KB'.
        s = f"{value:.1f}"
        if s.endswith(".0"):
            s = s[:-2]
        return f"{s} {_HumanBytes.UNITS[unit_idx]}"


class MainWindow(QMainWindow):
    """Compose the TTD Scrubber UI and wire it to the PollWorker."""

    def __init__(self, base_url: str = "http://localhost:8090", parent=None):
        super().__init__(parent)
        self.setWindowTitle("TTD Scrubber")
        self.resize(720, 560)

        self._base_url = base_url
        self._suppress_slider_emit = False
        self._last_status: dict = {}
        self._last_position: dict = {}
        # UI state trackers — drive button enable/disable via
        # _update_button_states().
        self._connected: bool = False
        self._instance_selected_id: str = ""
        # Emulator run/pause state — tracked from the engine's
        # is_running/is_paused fields so the UI reflects external pauses
        # (debugger, CLI, GDB, another UI). None = unknown (no instance
        # selected yet). Both default to None rather than False so the
        # first real reading emits a transition and updates the badge.
        self._emu_running: bool = False
        self._emu_paused: bool = False
        self._emu_state_known: bool = False

        self._build_ui()
        self._wire_worker()

        # Start the worker QThread. The worker QObject has already been
        # moved to it in _wire_worker(); starting it triggers
        # on_thread_started() which creates the QTimer on the worker
        # thread.
        self._worker_thread.start()
        # Trigger the initial connect.
        self._url_field.setText(base_url)
        self._on_connect_clicked()
        # Initial button layout: everything disabled until connected
        # and an instance is selected.
        self._update_button_states()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        self._build_connection_bar(layout)
        self._build_instance_picker(layout)
        self._build_session_panel(layout)
        self._build_timeline_panel(layout)
        self._build_markers_panel(layout)

        # Status bar at the bottom for transient messages.
        self.setStatusBar(QStatusBar())

    # --- 1. Connection bar ---
    def _build_connection_bar(self, parent_layout: QVBoxLayout):
        box = QHBoxLayout()
        box.addWidget(QLabel("Server:"))
        self._url_field = QLineEdit(self._base_url)
        self._url_field.setPlaceholderText("http://localhost:8090")
        box.addWidget(self._url_field, stretch=1)
        self._connect_btn = QPushButton("Connect")
        self._connect_btn.clicked.connect(self._on_connect_clicked)
        box.addWidget(self._connect_btn)
        self._conn_badge = QLabel("disconnected")
        self._conn_badge.setStyleSheet(
            "background:#444;color:#fff;border-radius:8px;padding:2px 8px;"
        )
        box.addWidget(self._conn_badge)
        parent_layout.addLayout(box)

    # --- 2. Instance picker ---
    def _build_instance_picker(self, parent_layout: QVBoxLayout):
        box = QHBoxLayout()
        box.addWidget(QLabel("Instance:"))
        self._instance_combo = QComboBox()
        self._instance_combo.setMinimumWidth(220)
        self._instance_combo.currentIndexChanged.connect(self._on_instance_changed)
        box.addWidget(self._instance_combo, stretch=1)
        self._instance_meta = QLabel("")
        box.addWidget(self._instance_meta, stretch=2)
        parent_layout.addLayout(box)

    # --- 3. TTD session panel ---
    def _build_session_panel(self, parent_layout: QVBoxLayout):
        group = QGroupBox("TTD Session")
        outer = QVBoxLayout(group)

        # Top row: state badge + ttd_available indicator.
        top = QHBoxLayout()
        top.addWidget(QLabel("State:"))
        self._state_badge = QLabel("idle")
        self._state_badge.setStyleSheet(
            "background:#888;color:#fff;border-radius:8px;padding:2px 10px;font-weight:bold;"
        )
        top.addWidget(self._state_badge)
        # Run/pause badge — reflects the actual emulator run state from
        # the engine, NOT a UI-side guess. Updated whenever the worker's
        # emulator_run_state signal fires. Distinct from the TTD state
        # badge above: the TTD state tracks the recording flow
        # (idle/recording/detached); this badge tracks the emulator's
        # execution flow (running/paused/stopped). The two are independent
        # — you can have TTD=Detached while emulator=Running (forward
        # playback), or TTD=Recording while emulator=Paused (debugger
        # pause mid-capture).
        top.addWidget(QLabel("Run:"))
        self._run_badge = QLabel("?")
        self._run_badge.setStyleSheet(
            "background:#888;color:#fff;border-radius:8px;"
            "padding:2px 10px;font-weight:bold;"
        )
        top.addWidget(self._run_badge)
        self._ttd_avail_label = QLabel("")
        top.addStretch(1)
        top.addWidget(self._ttd_avail_label, alignment=Qt.AlignRight)
        outer.addLayout(top)

        # Prominent recorded-size banner — visible at a glance during/after
        # recording. Shows frame span and live page-store bytes.
        self._record_summary = QLabel("No recording yet")
        self._record_summary.setStyleSheet(
            "background:#f5f5f5;color:#333;border:1px solid #ccc;"
            "border-radius:4px;padding:6px 10px;font-weight:bold;"
        )
        self._record_summary.setAlignment(Qt.AlignCenter)
        outer.addWidget(self._record_summary)

        # Form: session info fields.
        form = QFormLayout()
        form.setLabelAlignment(Qt.AlignRight)
        self._lbl_start = QLabel("0")
        self._lbl_end = QLabel("0")
        self._lbl_checkpoints = QLabel("0")
        # Single, prominent session-size readout. The number shown is the
        # real heap footprint of the recorded session as reported by the
        # engine (session_heap_bytes): the COW page-store backing vector +
        # per-checkpoint metadata + journal backing. Updated live as
        # recording progresses.
        self._lbl_session_size = QLabel("0 B")
        self._lbl_session_size.setStyleSheet("font-weight:bold;")
        self._lbl_captured = QLabel("0")
        form.addRow("Start frame:", self._lbl_start)
        form.addRow("End frame:", self._lbl_end)
        form.addRow("Captured frames:", self._lbl_captured)
        form.addRow("Checkpoints:", self._lbl_checkpoints)
        form.addRow("Session size:", self._lbl_session_size)
        outer.addLayout(form)

        # Recording options row.
        opts = QHBoxLayout()
        self._chk_write_journal = QCheckBox("Enable write journal")
        self._chk_write_journal.setChecked(True)
        self._chk_write_journal.setToolTip(
            "When enabled, captures every memory write for reverse-search "
            "(FindLast). Disable for lighter memory footprint during gaming/"
            "demo playback where reverse debugging isn't needed."
        )
        opts.addWidget(self._chk_write_journal)
        opts.addStretch(1)
        outer.addLayout(opts)

        # Action buttons.
        btns = QHBoxLayout()
        self._btn_start = QPushButton("Start recording")
        self._btn_start.clicked.connect(self._on_start_clicked)
        self._btn_stop = QPushButton("Stop")
        self._btn_stop.clicked.connect(self._on_stop_clicked)
        self._btn_invalidate = QPushButton("Invalidate")
        self._btn_invalidate.clicked.connect(self._on_invalidate_clicked)
        for b in (self._btn_start, self._btn_stop, self._btn_invalidate):
            btns.addWidget(b)
        btns.addStretch(1)
        outer.addLayout(btns)

        parent_layout.addWidget(group)

    # --- 4. Timeline (playback) panel ---
    #
    # This panel is the PLAYBACK surface. Every control here is purely
    # playback — it never starts, stops, truncates, or otherwise modifies
    # a recording. Conceptually the buttons split into two groups:
    #
    #   • SCRUB (state-mutating on the engine, but never touches the
    #     recording flow): Seek, Step back, Step forward. These restore
    #     historical checkpoints and transition TTD state to Detached.
    #
    #   • TRANSPORT (emulator-level only): Play and Pause. They hit
    #     /emulator/{id}/resume|pause — NOT /ttd/resume. TTD session
    #     state is untouched; in Detached, the engine's OnFrameBoundary
    #     auto-pauses at session end so playback never silently runs
    #     past the recorded range.
    #
    # The previous design labelled the transport button "Resume from here"
    # and dispatched it to /ttd/resume (ResumeRecordingFrom). That had two
    # problems: (1) the engine truncates the future timeline and flips
    # state Detached→Recording, which disabled every scrub control, and
    # (2) the word "Resume" made the UI feel like it was creating a new
    # recording. Both are fixed here: the button is now "Play" and hits
    # /emulator/{id}/resume. There is no path from this UI to
    # /ttd/resume — the recording flow is reachable only via the
    # Start/Stop buttons in the session panel.
    def _build_timeline_panel(self, parent_layout: QVBoxLayout):
        group = QGroupBox("Playback")
        outer = QVBoxLayout(group)

        # Slider spanning the recorded frame range.
        self._slider = QSlider(Qt.Horizontal)
        self._slider.setMinimum(0)
        self._slider.setMaximum(0)
        self._slider.setValue(0)
        self._slider.sliderReleased.connect(self._on_slider_released)
        outer.addWidget(self._slider)

        # Frame readout.
        self._frame_label = QLabel("Frame: 0 / 0")
        outer.addWidget(self._frame_label)

        # Scrub controls — restore historical state, never record.
        scrub_row = QHBoxLayout()
        self._btn_step_back = QPushButton("◂ Frame")
        self._btn_step_back.clicked.connect(self._on_step_back_clicked)
        self._btn_seek = QPushButton("Seek")
        self._btn_seek.clicked.connect(self._on_seek_clicked)
        self._btn_step_fwd = QPushButton("Frame ▸")
        self._btn_step_fwd.clicked.connect(self._on_step_fwd_clicked)
        for b in (self._btn_step_back, self._btn_seek, self._btn_step_fwd):
            scrub_row.addWidget(b)
        scrub_row.addStretch(1)
        outer.addLayout(scrub_row)

        # Transport controls — emulator-level Play/Pause only.
        # Distinct row, distinct vocabulary, so the user never confuses
        # them with the recording-flow Start/Stop buttons above.
        transport_row = QHBoxLayout()
        self._btn_pause = QPushButton("⏸ Pause")
        self._btn_pause.clicked.connect(self._on_pause_clicked)
        self._btn_play = QPushButton("▶ Play")
        self._btn_play.clicked.connect(self._on_play_clicked)
        for b in (self._btn_pause, self._btn_play):
            transport_row.addWidget(b)
        transport_row.addStretch(1)
        outer.addLayout(transport_row)

        parent_layout.addWidget(group)

    # --- 5. Markers panel ---
    def _build_markers_panel(self, parent_layout: QVBoxLayout):
        group = QGroupBox("External-event markers (replay barriers)")
        outer = QVBoxLayout(group)
        self._markers_list = QListWidget()
        self._markers_list.itemDoubleClicked.connect(self._on_marker_double_clicked)
        outer.addWidget(self._markers_list)
        hint = QLabel("Double-click a marker to seek to it. Seek will stop at the barrier.")
        hint.setStyleSheet("color:#666;font-style:italic;")
        outer.addWidget(hint)
        parent_layout.addWidget(group, stretch=1)

    # ------------------------------------------------------------------
    # Wire the worker thread signals/slots to UI handlers.
    # ------------------------------------------------------------------
    def _wire_worker(self):
        # Worker-object pattern: PollWorker is a QObject moved to a
        # dedicated QThread. Cross-thread signal/slot connections are
        # queued automatically by Qt.
        self._worker_thread = QThread(self)
        self._worker = PollWorker()
        self._worker.moveToThread(self._worker_thread)

        # When the thread starts, the worker creates its QTimer.
        self._worker_thread.started.connect(self._worker.on_thread_started)
        # When the worker signals finished, the thread can quit.
        self._worker.finished.connect(self._worker_thread.quit)

        # Worker signals -> UI updates (auto queued cross-thread).
        self._worker.connected.connect(self._on_worker_connected)
        self._worker.instances.connect(self._on_worker_instances)
        self._worker.selected.connect(self._on_worker_selected)
        self._worker.ttd_status.connect(self._on_worker_ttd_status)
        self._worker.ttd_position.connect(self._on_worker_ttd_position)
        self._worker.ttd_markers.connect(self._on_worker_ttd_markers)
        self._worker.emulator_run_state.connect(self._on_worker_run_state)
        self._worker.action_result.connect(self._on_worker_action_result)
        self._worker.error.connect(self._on_worker_error)

    def closeEvent(self, event):
        # Stop the worker before the window goes away.
        if self._worker_thread.isRunning():
            # request_shutdown is a slot on the worker; calling it via
            # invokeMethod with QueuedConnection would be cleaner, but
            # since it only flips a flag and stops the timer (which is
            # safe across threads for these operations), we call it
            # directly. The finished signal quits the thread.
            self._worker.request_shutdown()
            self._worker_thread.quit()
            self._worker_thread.wait(3000)
        super().closeEvent(event)

    # ------------------------------------------------------------------
    # Click handlers (UI thread).
    # ------------------------------------------------------------------
    @Slot()
    def _on_connect_clicked(self):
        url = self._url_field.text().strip() or "http://localhost:8090"
        self._conn_badge.setText("connecting")
        self._conn_badge.setStyleSheet(
            "background:#cc8800;color:#fff;border-radius:8px;padding:2px 8px;"
        )
        # Cross-thread slot invocation: since the worker was moved to
        # its thread, AutoConnection picks QueuedConnection and the
        # slot runs on the worker thread.
        self._worker.request_connect_to(url)

    @Slot(int)
    def _on_instance_changed(self, index: int):
        if index < 0:
            return
        instance_id = self._instance_combo.itemData(index)
        if instance_id is None:
            return
        self._worker.request_select_instance(str(instance_id))

    @Slot()
    def _on_start_clicked(self):
        enable_journal = self._chk_write_journal.isChecked()
        self._worker.request_start_recording(enable_journal)

    @Slot()
    def _on_stop_clicked(self):
        self._worker.request_stop_recording()

    @Slot()
    def _on_invalidate_clicked(self):
        # Confirm before dropping all history.
        reply = QMessageBox.question(
            self,
            "Invalidate TTD session",
            "Drop all recorded history? This cannot be undone.",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No,
        )
        if reply != QMessageBox.Yes:
            return
        self._worker.request_invalidate("User requested via TTD Scrubber")

    @Slot()
    def _on_step_back_clicked(self):
        self._worker.request_step_back()

    @Slot()
    def _on_step_fwd_clicked(self):
        self._worker.request_step_forward()

    @Slot()
    def _on_seek_clicked(self):
        frame = self._slider.value()
        self._worker.request_seek(frame, 0)

    @Slot()
    def _on_pause_clicked(self):
        # Transport Pause — emulator-level only. TTD state is unchanged.
        self._worker.request_emulator_pause()

    @Slot()
    def _on_play_clicked(self):
        # Transport Play — emulator-level only. Hits /emulator/{id}/resume
        # (NOT /ttd/resume). TTD session state stays exactly where it was:
        # typically Detached after a seek. The engine's OnFrameBoundary
        # auto-pauses at session end so playback halts at the recorded
        # boundary and the user can keep scrubbing.
        #
        # There is intentionally NO path from this UI to /ttd/resume
        # (ResumeRecordingFrom). That endpoint truncates the future
        # timeline and transitions Detached→Recording, which is a
        # RECORDING-flow operation. Per the UX spec, playback and
        # recording flows are fully disconnected in this tool — the only
        # way to start a new recording is via the Start button in the
        # session panel above.
        self._worker.request_emulator_resume()

    @Slot()
    def _on_slider_released(self):
        frame = self._slider.value()
        self._worker.request_seek(frame, 0)

    @Slot()
    def _on_marker_double_clicked(self, item: QListWidgetItem):
        # Respect scrub enablement — the list stays visible (informational)
        # even when scrubbing is disabled, but a double-click must not
        # fire a seek in that case.
        state = (self._last_status or {}).get("state", "idle")
        end_frame = int((self._last_status or {}).get("current_end_frame", 0) or 0)
        if not (self._connected and self._instance_selected_id
                and end_frame > 0 and state != "recording"):
            self.statusBar().showMessage(
                "Seek disabled — start/stop recording first", 3000)
            return
        marker = item.data(Qt.UserRole) or {}
        frame = marker.get("frame", 0)
        self.statusBar().showMessage(f"Seeking to marker at frame {frame}…", 3000)
        self._worker.request_seek(int(frame), int(marker.get("tinframe", 0)))

    # ------------------------------------------------------------------
    # Worker signal handlers (UI thread).
    # ------------------------------------------------------------------
    @Slot(bool, str)
    def _on_worker_connected(self, ok: bool, message: str):
        was_connected = self._connected
        self._connected = bool(ok)
        if ok:
            self._conn_badge.setText("connected")
            self._conn_badge.setStyleSheet(
                "background:#2a8;color:#fff;border-radius:8px;padding:2px 8px;"
            )
        else:
            # Distinct 'reconnecting' styling: the worker is silently
            # retrying once per second. The user should see at a glance
            # that we lost the server and are trying to get it back —
            # not just a static 'disconnected' badge that looks permanent.
            self._conn_badge.setText("● reconnecting")
            self._conn_badge.setStyleSheet(
                "background:#cc8800;color:#fff;border-radius:8px;padding:2px 8px;"
            )
            # Overlay the session banner so the disconnect is impossible
            # to miss — otherwise the form below keeps showing stale
            # frame / checkpoint data and the disconnect only manifests
            # as a tiny badge in the top-right.
            self._record_summary.setText(
                "⚠ DISCONNECTED — server unreachable. Silent retry in progress; "
                "session data below may be stale."
            )
            self._record_summary.setStyleSheet(
                "background:#fff3cd;color:#7a5b00;border:1px solid #e0c366;"
                "border-radius:4px;padding:6px 10px;font-weight:bold;"
            )
        # On disconnect: clear the transient status-bar message so it
        # doesn't keep flashing 'connection error: …' every tick (the
        # worker only emits this signal once per transition, but the
        # status bar may still hold a stale message from earlier).
        # On reconnect: show the recovery message briefly.
        if ok and not was_connected:
            self.statusBar().showMessage("Reconnected", 3000)
            # The next ttd_status tick will overwrite the disconnect banner.
        elif not ok:
            self.statusBar().clearMessage()
        else:
            self.statusBar().showMessage(message, 3000)
        self._update_button_states()

    @Slot(list)
    def _on_worker_instances(self, instances: list):
        # Rebuild the dropdown. Block signals to avoid spurious selection.
        self._instance_combo.blockSignals(True)
        self._instance_combo.clear()
        for i, info in enumerate(instances or []):
            # Resolve id.
            instance_id = str(i)
            for key in ("id", "emulator_id", "instance_id", "uuid"):
                if key in info and info[key]:
                    instance_id = str(info[key])
                    break
            label = self._format_instance_label(info, i)
            self._instance_combo.addItem(label, instance_id)
        self._instance_combo.blockSignals(False)
        # If the list is empty, the selection is gone.
        if self._instance_combo.count() == 0:
            self._instance_selected_id = ""
            self._update_button_states()

    @staticmethod
    def _format_instance_label(info: dict, index: int) -> str:
        model = info.get("model", info.get("type", "?"))
        name = info.get("name", info.get("symbolic_id"))
        label = f"{index}: {model}"
        if name:
            label += f" ({name})"
        return label

    @Slot(str, dict)
    def _on_worker_selected(self, instance_id: str, info: dict):
        self._instance_selected_id = instance_id or ""
        if not instance_id:
            self._instance_meta.setText("no instance selected")
            # Selection cleared — reset run/pause tracking so the badge
            # returns to '?' until the next selection emits a transition.
            self._emu_state_known = False
            self._emu_running = False
            self._emu_paused = False
            self._refresh_run_badge()
            self._update_button_states()
            return
        # Find the matching combo entry.
        for i in range(self._instance_combo.count()):
            if self._instance_combo.itemData(i) == instance_id:
                self._instance_combo.blockSignals(True)
                self._instance_combo.setCurrentIndex(i)
                self._instance_combo.blockSignals(False)
                break
        model = info.get("model", info.get("type", "?"))
        self._instance_meta.setText(f"id={instance_id}  model={model}")
        # Note: we do NOT update _emu_running/_emu_paused from info here.
        # The PollWorker emits emulator_run_state on transitions (and on
        # first read after selection); letting that signal drive the
        # badge keeps a single source of truth and avoids flapping if
        # the worker's info dict and signal happen to disagree briefly.
        self._update_button_states()

    @Slot(dict)
    def _on_worker_ttd_status(self, status: dict):
        self._last_status = status or {}
        state = self._last_status.get("state", "idle")
        self._state_badge.setText(state)
        color = {
            "idle":       "#888",
            "recording":  "#2a8",
            "detached":   "#88b",
        }.get(state, "#888")
        self._state_badge.setStyleSheet(
            f"background:{color};color:#fff;border-radius:8px;"
            f"padding:2px 10px;font-weight:bold;"
        )

        avail = self._last_status.get("ttd_available")
        if avail is None:
            self._ttd_avail_label.setText("")
        elif avail:
            self._ttd_avail_label.setText("TTD available")
            self._ttd_avail_label.setStyleSheet("color:#2a8;")
        else:
            self._ttd_avail_label.setText("TTD NOT available")
            self._ttd_avail_label.setStyleSheet("color:#c33;")

        self._lbl_start.setText(str(self._last_status.get("session_start_frame", 0)))
        self._lbl_end.setText(str(self._last_status.get("current_end_frame", 0)))
        self._lbl_checkpoints.setText(str(self._last_status.get("checkpoint_count", 0)))

        # --- Session size -----------------------------------------------
        # Real heap footprint of the recorded session, as reported by the
        # engine via session_heap_bytes. This is NOT a UI-side estimate —
        # the engine sums every allocation the session owns (page store
        # backing vector, per-checkpoint metadata, journal backing, scratch
        # buffers). Distinct from page_store_used_bytes, which only counts
        # live COW slots and is misleadingly ~100% of capacity because the
        # store auto-grows to fit the working set.
        #
        # Fallback (engine builds without the field or older server): a
        # conservative estimate from the still-reported page_store_used_bytes
        # + checkpoint count.
        session_size = int(self._last_status.get("session_heap_bytes", 0) or 0)
        if session_size == 0:
            used = int(self._last_status.get("page_store_used_bytes", 0) or 0)
            checkpoint_count = int(self._last_status.get("checkpoint_count", 0) or 0)
            PER_CHECKPOINT_OVERHEAD = 1024
            session_size = used + checkpoint_count * PER_CHECKPOINT_OVERHEAD
        self._lbl_session_size.setText(self._fmt_bytes(session_size))

        # Captured frame count = current end - session start (the live
        # recording span). Zero when no session is active.
        start_frame = int(self._last_status.get("session_start_frame", 0) or 0)
        end_frame = int(self._last_status.get("current_end_frame", 0) or 0)
        captured = max(end_frame - start_frame, 0)
        self._lbl_captured.setText(str(captured))

        # Slider range: clamp min to session_start_frame so users cannot
        # drag below the recorded range. The engine returns reached=false
        # for any target before session_start_frame (treated as OutOfRange).
        if start_frame != self._slider.minimum():
            self._suppress_slider_emit = True
            self._slider.setMinimum(max(start_frame, 0))
            self._suppress_slider_emit = False
        if end_frame != self._slider.maximum():
            self._suppress_slider_emit = True
            self._slider.setMaximum(max(end_frame, start_frame))
            self._suppress_slider_emit = False

        size_str = self._fmt_bytes(session_size)

        # Prominent summary banner — visible at a glance during/after
        # recording. Lead with the size since that's what users scan for.
        if state == "recording":
            self._record_summary.setText(
                f"● RECORDING — {captured} frames, {size_str}"
            )
            self._record_summary.setStyleSheet(
                "background:#2a8;color:#fff;border:1px solid #178;"
                "border-radius:4px;padding:6px 10px;font-weight:bold;"
            )
        elif state == "detached":
            # Playback terminology only — never "Resume" (which the user
            # associated with TTD-recording-resume and made the panel feel
            # like it was starting a new recording session). Press Play to
            # run forward; the engine auto-pauses at session end.
            self._record_summary.setText(
                f"◆ DETACHED at frame {end_frame} — "
                f"{captured} frames, {size_str}. Press Play to run forward."
            )
            self._record_summary.setStyleSheet(
                "background:#88b;color:#fff;border:1px solid #669;"
                "border-radius:4px;padding:6px 10px;font-weight:bold;"
            )
        elif captured > 0:
            # Idle but history is retained after Stop.
            self._record_summary.setText(
                f"■ STOPPED — {captured} frames, {size_str}. "
                f"Scrub or Invalidate."
            )
            self._record_summary.setStyleSheet(
                "background:#f5f5f5;color:#333;border:1px solid #ccc;"
                "border-radius:4px;padding:6px 10px;font-weight:bold;"
            )
        else:
            self._record_summary.setText("No recording yet")
            self._record_summary.setStyleSheet(
                "background:#f5f5f5;color:#666;border:1px solid #ccc;"
                "border-radius:4px;padding:6px 10px;font-weight:bold;"
            )

        # Slider max is already kept in sync with end_frame in the size
        # readouts block above; nothing to do here.

        # State changed — refresh button enabled/disabled.
        self._update_button_states()

    # ------------------------------------------------------------------
    # Button state management.
    #
    # Drives every TTD-sensitive control from three trackers:
    #   self._connected              — HTTP server reachable
    #   self._instance_selected_id   — non-empty once an instance is picked
    #   state                        — 'idle' | 'recording' | 'detached'
    #   has_history                  — current_end_frame > 0
    #
    # The enablement rules enforce a strict conceptual separation between
    # the RECORDING flow (top panel) and the PLAYBACK flow (this panel):
    #
    #   RECORDING (session panel):
    #     Start       enabled when not recording
    #     Stop        enabled only when recording
    #     Invalidate  enabled when there is history
    #
    #   PLAYBACK (this panel):
    #     Scrub (slider, step, seek, marker seek)
    #                 enabled whenever history exists AND not actively
    #                 capturing. State after Stop is 'idle' (history
    #                 retained); state after Seek is 'detached'. Both
    #                 allow scrub.
    #     Transport (Pause / Play)
    #                 enabled in the Detached state. Play starts forward
    #                 execution from the seeked position; the engine's
    #                 OnFrameBoundary auto-pauses at session end so the
    #                 user can keep scrubbing. Pause halts manually.
    #                 Neither transitions TTD state — this is the fix for
    #                 the 'all scrubbing controls disabled after resume'
    #                 bug. There is intentionally no 'Resume from here'
    #                 button: that name implied TTD-resume and made the
    #                 UI feel like it was creating a new recording.
    # ------------------------------------------------------------------
    def _update_button_states(self):
        connected = bool(self._connected)
        instance_ok = bool(self._instance_selected_id)
        state = (self._last_status or {}).get("state", "idle")
        end_frame = int((self._last_status or {}).get("current_end_frame", 0) or 0)
        has_history = end_frame > 0

        # All TTD actions need both connection and instance.
        base_enabled = connected and instance_ok

        # Session panel.
        self._btn_start.setEnabled(base_enabled and state != "recording")
        self._btn_stop.setEnabled(base_enabled and state == "recording")
        self._btn_invalidate.setEnabled(base_enabled and has_history)
        # Journal checkbox only editable before recording starts.
        self._chk_write_journal.setEnabled(base_enabled and state != "recording")

        # Timeline panel — scrubbing whenever history exists and we're not
        # actively capturing. State after Stop is 'idle' (history retained);
        # state after Seek is 'detached'. Both should allow scrub.
        #
        # NOTE: scrub_enabled must NOT depend on whether the emulator is
        # currently running. After clicking Play (transport), the emulator
        # runs forward in Detached state until OnFrameBoundary auto-pauses
        # at session end. During that window scrub stays enabled — and
        # seek itself pauses the emulator first, so seeking mid-run works
        # correctly. This was the root cause of the bug where scrub
        # controls got stuck disabled after Resume.
        scrub_enabled = base_enabled and has_history and state != "recording"
        self._slider.setEnabled(scrub_enabled)
        self._btn_step_back.setEnabled(scrub_enabled)
        self._btn_step_fwd.setEnabled(scrub_enabled)
        self._btn_seek.setEnabled(scrub_enabled)

        # Transport controls (Pause / Play): emulator-level, only
        # meaningful when browsing history (Detached). In Idle/Recording
        # state these are no-ops — the user should use Stop/Start instead.
        #
        # Enablement now ALSO follows the engine-reported run/pause state:
        #   • Pause is only useful while the emulator is actually running.
        #     Clicking Pause on an already-paused emulator is a confusing
        #     no-op, so disable it.
        #   • Play is only useful while the emulator is paused (or stopped).
        #     Clicking Play on a running emulator is likewise a no-op.
        #
        # This is the fix for 'controls must follow emulator state' —
        # when a debugger / CLI / GDB pauses the emulator externally,
        # the engine flips is_paused=true, PollWorker emits
        # emulator_run_state, _on_worker_run_state updates
        # _emu_running/_emu_paused, and this method disables Pause and
        # enables Play to match. Resume from any source flips it back.
        # The TTD Scrubber UI no longer pretends to own the only control
        # surface — it follows the real emulator state.
        transport_enabled = scrub_enabled and state == "detached"
        if not transport_enabled:
            self._btn_pause.setEnabled(False)
            self._btn_play.setEnabled(False)
        else:
            # Transport is conceptually available; now narrow by run state.
            # When state is unknown (just selected, no signal yet) leave
            # both enabled — the next poll tick will emit the transition
            # and correct the enablement. This avoids a flash of disabled
            # controls on selection.
            if self._emu_state_known:
                self._btn_pause.setEnabled(self._emu_running and not self._emu_paused)
                self._btn_play.setEnabled(self._emu_paused or not self._emu_running)
            else:
                self._btn_pause.setEnabled(True)
                self._btn_play.setEnabled(True)

        # Markers list stays visible (informational) but seeks are guarded
        # in _on_marker_double_clicked — the list widget itself can't be
        # partially disabled, and users want to see markers even while
        # recording.

    @staticmethod
    def _fmt_bytes(n) -> str:
        """Format a byte count as a human-readable value.

        Uses 1024-based units (B / KB / MB / GB) with one decimal place
        for KB and above (e.g. '1.5 KB', '12.3 MB', '470 MB') and no
        decimal for bytes (e.g. '512 B'). Returns '?' for non-numeric.
        """
        try:
            n = int(n)
        except Exception:
            return "?"
        if n < 0:
            return f"-{_HumanBytes._fmt_abs(-n)}"
        return _HumanBytes._fmt_abs(n)

    @Slot(dict)
    def _on_worker_ttd_position(self, position: dict):
        self._last_position = position or {}
        cur = self._last_position.get("current", {}) or {}
        end = self._last_position.get("session_end", {}) or {}
        cur_frame = int(cur.get("frame", 0))
        end_frame = int(end.get("frame", 0))

        # Keep slider max in sync with end frame. Min is owned by
        # _on_worker_ttd_status (session_start_frame) — don't touch it
        # here, since position doesn't carry start_frame.
        if end_frame > self._slider.maximum():
            self._suppress_slider_emit = True
            self._slider.setMaximum(end_frame)
            self._suppress_slider_emit = False

        # Move the slider to current, clamped to the slider's valid range.
        # Suppress the sliderReleased signal because this is a *display*
        # update, not a user seek.
        clamped = max(self._slider.minimum(), min(cur_frame, self._slider.maximum()))
        self._suppress_slider_emit = True
        self._slider.setValue(clamped)
        self._suppress_slider_emit = False

        self._frame_label.setText(f"Frame: {cur_frame} / {end_frame}")

    @Slot(list)
    def _on_worker_ttd_markers(self, markers: list):
        self._markers_list.clear()
        for m in markers or []:
            frame = m.get("frame", 0)
            tin = m.get("tinframe", 0)
            kind = m.get("kind", "?")
            reason = m.get("reason", "")
            text = f"frame={frame} tin={tin}  {kind:<15}  {reason}"
            item = QListWidgetItem(text)
            item.setData(Qt.UserRole, m)
            self._markers_list.addItem(item)

    @Slot(bool, bool)
    def _on_worker_run_state(self, is_running: bool, is_paused: bool):
        """Engine-reported run/pause state for the selected emulator.

        Fires on every transition (including those driven by OTHER
        clients: debugger, CLI, GDB, another UI). This is the single
        source of truth for the Run badge and for transport button
        enablement — the TTD Scrubber UI must FOLLOW emulator state,
        not assume it owns the only control surface.
        """
        was_known = self._emu_state_known
        prev_running = self._emu_running
        prev_paused = self._emu_paused
        self._emu_state_known = True
        self._emu_running = bool(is_running)
        self._emu_paused = bool(is_paused)
        self._refresh_run_badge()
        # Surface the transition in the status bar so the user knows WHY
        # the controls just changed — especially important when the
        # transition was driven externally (e.g. a debugger pause).
        if not was_known:
            # First reading after selection — keep the status bar quiet.
            pass
        elif is_paused and not prev_paused:
            self.statusBar().showMessage("Emulator paused", 3000)
        elif is_running and not prev_running:
            self.statusBar().showMessage("Emulator running", 3000)
        elif not is_running and prev_running:
            self.statusBar().showMessage("Emulator stopped", 3000)
        self._update_button_states()

    def _refresh_run_badge(self):
        """Re-render the Run badge from _emu_running/_emu_paused."""
        if not self._emu_state_known:
            self._run_badge.setText("?")
            self._run_badge.setStyleSheet(
                "background:#888;color:#fff;border-radius:8px;"
                "padding:2px 10px;font-weight:bold;"
            )
            return
        if self._emu_paused:
            self._run_badge.setText("⏸ paused")
            self._run_badge.setStyleSheet(
                "background:#cc8800;color:#fff;border-radius:8px;"
                "padding:2px 10px;font-weight:bold;"
            )
        elif self._emu_running:
            self._run_badge.setText("▶ running")
            self._run_badge.setStyleSheet(
                "background:#2a8;color:#fff;border-radius:8px;"
                "padding:2px 10px;font-weight:bold;"
            )
        else:
            self._run_badge.setText("■ stopped")
            self._run_badge.setStyleSheet(
                "background:#888;color:#fff;border-radius:8px;"
                "padding:2px 10px;font-weight:bold;"
            )

    @Slot(str, dict, str)
    def _on_worker_action_result(self, verb: str, response: dict, error_msg: str):
        if error_msg:
            self.statusBar().showMessage(f"{verb} failed: {error_msg}", 5000)
            logger.warning("action %s failed: %s", verb, error_msg)
            return
        # Surface short confirmations. Seek gets extra detail about halt.
        if verb == "seek":
            halt = response.get("halt_reason", "?")
            reached = response.get("reached", False)
            arrived = (response.get("arrived_at") or {}).get("frame", "?")
            self.statusBar().showMessage(
                f"Seek: reached={reached} halt={halt} arrived_at={arrived}", 5000
            )
        elif verb == "invalidate":
            self.statusBar().showMessage("TTD session invalidated", 3000)
        elif verb == "emulator_resume":
            # Transport Play — playback-only, no recording-flow language.
            self.statusBar().showMessage(
                "Playing — auto-pauses at session end", 3000
            )
        elif verb == "emulator_pause":
            self.statusBar().showMessage("Paused", 2000)
        else:
            self.statusBar().showMessage(f"{verb} ok", 2000)

    @Slot(str)
    def _on_worker_error(self, message: str):
        self.statusBar().showMessage(f"Error: {message}", 5000)
        logger.error("worker error: %s", message)
