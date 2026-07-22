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

from PySide6.QtCore import Qt, Slot
from PySide6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QFormLayout,
    QLabel, QLineEdit, QPushButton, QComboBox, QSlider, QListWidget,
    QListWidgetItem, QMessageBox, QGroupBox, QStatusBar,
)
from PySide6.QtGui import QColor

from poll_worker import PollWorker


logger = logging.getLogger("MainWindow")


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

        self._build_ui()
        self._wire_worker()

        # Kick off the worker thread.
        self._worker.start()
        # Trigger the initial connect.
        self._url_field.setText(base_url)
        self._on_connect_clicked()

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
        self._ttd_avail_label = QLabel("")
        top.addStretch(1)
        top.addWidget(self._ttd_avail_label, alignment=Qt.AlignRight)
        outer.addLayout(top)

        # Form: session info fields.
        form = QFormLayout()
        form.setLabelAlignment(Qt.AlignRight)
        self._lbl_start = QLabel("0")
        self._lbl_end = QLabel("0")
        self._lbl_checkpoints = QLabel("0")
        self._lbl_pagestore = QLabel("0 / 0 (0%)")
        self._lbl_baseline = QLabel("0")
        form.addRow("Start frame:", self._lbl_start)
        form.addRow("End frame:", self._lbl_end)
        form.addRow("Checkpoints:", self._lbl_checkpoints)
        form.addRow("Page store:", self._lbl_pagestore)
        form.addRow("Baseline frames:", self._lbl_baseline)
        outer.addLayout(form)

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

    # --- 4. Timeline panel ---
    def _build_timeline_panel(self, parent_layout: QVBoxLayout):
        group = QGroupBox("Timeline")
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

        # Step/Seek/Resume buttons.
        btns = QHBoxLayout()
        self._btn_step_back = QPushButton("< Frame")
        self._btn_step_back.clicked.connect(self._on_step_back_clicked)
        self._btn_seek = QPushButton("Seek")
        self._btn_seek.clicked.connect(self._on_seek_clicked)
        self._btn_step_fwd = QPushButton("Frame >")
        self._btn_step_fwd.clicked.connect(self._on_step_fwd_clicked)
        self._btn_resume = QPushButton("Resume from here")
        self._btn_resume.clicked.connect(self._on_resume_clicked)
        for b in (self._btn_step_back, self._btn_seek, self._btn_step_fwd, self._btn_resume):
            btns.addWidget(b)
        btns.addStretch(1)
        outer.addLayout(btns)

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
        self._worker = PollWorker(self)
        # Forward UI requests into worker slots (queued, cross-thread).
        # No explicit connect() needed for these — instead, the click
        # handlers call worker.on_*(...) directly.

        # Worker signals -> UI updates.
        self._worker.connected.connect(self._on_worker_connected)
        self._worker.instances.connect(self._on_worker_instances)
        self._worker.selected.connect(self._on_worker_selected)
        self._worker.ttd_status.connect(self._on_worker_ttd_status)
        self._worker.ttd_position.connect(self._on_worker_ttd_position)
        self._worker.ttd_markers.connect(self._on_worker_ttd_markers)
        self._worker.action_result.connect(self._on_worker_action_result)
        self._worker.error.connect(self._on_worker_error)

    def closeEvent(self, event):
        # Stop the worker before the window goes away.
        if self._worker.isRunning():
            self._worker.on_shutdown()
            self._worker.quit()
            self._worker.wait(3000)
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
        self._worker.on_connect_to(url)

    @Slot(int)
    def _on_instance_changed(self, index: int):
        if index < 0:
            return
        instance_id = self._instance_combo.itemData(index)
        if instance_id is None:
            return
        self._worker.on_select_instance(str(instance_id))

    @Slot()
    def _on_start_clicked(self):
        self._worker.on_start_recording()

    @Slot()
    def _on_stop_clicked(self):
        self._worker.on_stop_recording()

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
        self._worker.on_invalidate("User requested via TTD Scrubber")

    @Slot()
    def _on_step_back_clicked(self):
        self._worker.on_step_back()

    @Slot()
    def _on_step_fwd_clicked(self):
        self._worker.on_step_forward()

    @Slot()
    def _on_seek_clicked(self):
        frame = self._slider.value()
        self._worker.on_seek(frame, 0)

    @Slot()
    def _on_resume_clicked(self):
        frame = self._slider.value()
        self._worker.on_resume(frame, 0)

    @Slot()
    def _on_slider_released(self):
        frame = self._slider.value()
        self._worker.on_seek(frame, 0)

    @Slot()
    def _on_marker_double_clicked(self, item: QListWidgetItem):
        marker = item.data(Qt.UserRole) or {}
        frame = marker.get("frame", 0)
        self.statusBar().showMessage(f"Seeking to marker at frame {frame}…", 3000)
        self._worker.on_seek(int(frame), int(marker.get("tinframe", 0)))

    # ------------------------------------------------------------------
    # Worker signal handlers (UI thread).
    # ------------------------------------------------------------------
    @Slot(bool, str)
    def _on_worker_connected(self, ok: bool, message: str):
        if ok:
            self._conn_badge.setText("connected")
            self._conn_badge.setStyleSheet(
                "background:#2a8;color:#fff;border-radius:8px;padding:2px 8px;"
            )
        else:
            self._conn_badge.setText("disconnected")
            self._conn_badge.setStyleSheet(
                "background:#c33;color:#fff;border-radius:8px;padding:2px 8px;"
            )
        self.statusBar().showMessage(message, 3000)

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
        if not instance_id:
            self._instance_meta.setText("no instance selected")
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
        self._lbl_baseline.setText(str(self._last_status.get("baseline_frames_captured", 0)))

        used = self._last_status.get("page_store_used_bytes", 0)
        total = self._last_status.get("page_store_bytes", 0)
        pct = (100.0 * used / total) if total else 0.0
        self._lbl_pagestore.setText(
            f"{self._fmt_bytes(used)} / {self._fmt_bytes(total)} ({pct:.1f}%)"
        )

        # Slider max = end frame; grows as recording continues.
        end_frame = int(self._last_status.get("current_end_frame", 0))
        if end_frame != self._slider.maximum():
            self._suppress_slider_emit = True
            self._slider.setMaximum(max(end_frame, 0))
            self._suppress_slider_emit = False

    @staticmethod
    def _fmt_bytes(n) -> str:
        try:
            n = int(n)
        except Exception:
            return "?"
        for unit in ("B", "KB", "MB", "GB"):
            if n < 1024 or unit == "GB":
                return f"{n} {unit}"
            n //= 1024
        return f"{n} GB"

    @Slot(dict)
    def _on_worker_ttd_position(self, position: dict):
        self._last_position = position or {}
        cur = self._last_position.get("current", {}) or {}
        end = self._last_position.get("session_end", {}) or {}
        cur_frame = int(cur.get("frame", 0))
        end_frame = int(end.get("frame", 0))

        # Keep slider max in sync with end frame.
        if end_frame > self._slider.maximum():
            self._suppress_slider_emit = True
            self._slider.setMaximum(end_frame)
            self._suppress_slider_emit = False

        # Move the slider to current. Suppress the sliderReleased signal
        # because this is a *display* update, not a user seek.
        self._suppress_slider_emit = True
        self._slider.setValue(cur_frame)
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
        elif verb == "resume":
            self.statusBar().showMessage(
                f"Resumed at frame {response.get('frame', '?')}", 3000
            )
        else:
            self.statusBar().showMessage(f"{verb} ok", 2000)

    @Slot(str)
    def _on_worker_error(self, message: str):
        self.statusBar().showMessage(f"Error: {message}", 5000)
        logger.error("worker error: %s", message)
