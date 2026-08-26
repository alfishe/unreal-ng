#!/usr/bin/env python3
"""porttrace_gui.py — PySide6 GUI for the Port Diagnostic Recorder.

Graphical companion to porttrace_capture.py / porttrace_convert.py:

  * connect to a running emulator's WebAPI, pick the instance
  * enable the `porttrace` feature, choose preset / buffer capacity / overflow
  * start / pause / resume / stop / clear capture with live counters
    (events, produced, evicted, filtered, per-frame activity)
  * save the trace server-side in any format (json / csv / bin / binz)
  * convert saved trace files offline (json / csv / markdown / text / binz,
    plus summary and decode-strictness analysis) — no emulator needed

Requires PySide6 (`pip install PySide6`). Everything else is stdlib +
the sibling porttrace_convert module.

Design: docs/inprogress/2026-08-24-diagnostic-observability/
"""

import io
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import porttrace_convert  # noqa: E402
from porttrace_capture import ApiError, WebApi  # noqa: E402

try:
    from PySide6.QtCore import Qt, QTimer  # noqa: E402
    from PySide6.QtGui import QFont  # noqa: E402
    from PySide6.QtWidgets import (  # noqa: E402
        QApplication, QComboBox, QFileDialog, QFormLayout, QGridLayout, QGroupBox,
        QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMessageBox, QPlainTextEdit,
        QPushButton, QSpinBox, QStatusBar, QTabWidget, QVBoxLayout, QWidget,
    )
except ModuleNotFoundError:
    print(f"PySide6 is not installed for this interpreter:\n  {sys.executable}\n\n"
          f"Fix one of these ways:\n"
          f"  pip install PySide6                     # into the current interpreter\n"
          f"  pyenv shell 3.12.3                      # if another pyenv version has it\n"
          f"  ~/.pyenv/versions/3.12.3/bin/python3 {Path(__file__).name}\n\n"
          f"(An active virtualenv - e.g. jupyter_env - hides packages installed in\n"
          f" the base interpreter.)", file=sys.stderr)
    sys.exit(1)

PRESETS = ["all", "ay-only", "fdc-only", "no-fdc", "no-fe", "sound", "paging",
           "outs-only", "ins-only", "unmapped"]
SAVE_FORMATS = ["json", "csv", "bin", "binz"]
CONVERT_FORMATS = ["text", "json", "csv", "markdown", "binz", "summary", "strictness"]


class CaptureTab(QWidget):
    """Live capture control: connection, filter/buffer config, session buttons,
    polled counters, server-side save."""

    def __init__(self, status_bar: QStatusBar):
        super().__init__()
        self._status_bar = status_bar
        self._api = None
        self._emulator = ""
        self._base = ""

        layout = QVBoxLayout(self)

        # ── Connection ──
        conn_group = QGroupBox("Connection")
        conn = QHBoxLayout(conn_group)
        self.url_edit = QLineEdit("http://localhost:8090")
        self.connect_btn = QPushButton("Connect")
        self.emulator_combo = QComboBox()
        self.emulator_combo.setMinimumWidth(280)
        conn.addWidget(QLabel("WebAPI:"))
        conn.addWidget(self.url_edit, stretch=1)
        conn.addWidget(self.connect_btn)
        conn.addWidget(QLabel("Emulator:"))
        conn.addWidget(self.emulator_combo, stretch=1)
        layout.addWidget(conn_group)

        # ── Filter / buffer configuration ──
        config_group = QGroupBox("Configuration (applied on Start)")
        config = QFormLayout(config_group)
        self.preset_combo = QComboBox()
        self.preset_combo.addItems(PRESETS)
        self.include_edit = QLineEdit()
        self.include_edit.setPlaceholderText(
            "custom include rules, e.g.: port=FFFD,direction=out; port=BFFD,direction=out")
        self.exclude_edit = QLineEdit()
        self.exclude_edit.setPlaceholderText("exclude rules, e.g.: device=WD1793_Data")
        self.capacity_spin = QSpinBox()
        self.capacity_spin.setRange(1024, 16 * 1024 * 1024)
        self.capacity_spin.setValue(4 * 1024 * 1024)
        self.capacity_spin.setGroupSeparatorShown(True)
        self.overflow_combo = QComboBox()
        self.overflow_combo.addItems(["ring (evict oldest)", "stop (keep start of run)"])
        config.addRow("Preset:", self.preset_combo)
        config.addRow("Include rules:", self.include_edit)
        config.addRow("Exclude rules:", self.exclude_edit)
        config.addRow("Capacity (events):", self.capacity_spin)
        config.addRow("Overflow:", self.overflow_combo)
        layout.addWidget(config_group)

        # ── Session control ──
        session_group = QGroupBox("Capture session")
        session = QHBoxLayout(session_group)
        self.start_btn = QPushButton("Start")
        self.pause_btn = QPushButton("Pause")
        self.resume_btn = QPushButton("Resume")
        self.stop_btn = QPushButton("Stop")
        self.clear_btn = QPushButton("Clear")
        for btn in (self.start_btn, self.pause_btn, self.resume_btn, self.stop_btn, self.clear_btn):
            session.addWidget(btn)
        layout.addWidget(session_group)

        # ── Live counters ──
        counters_group = QGroupBox("Live counters")
        grid = QGridLayout(counters_group)
        mono = QFont("Menlo")
        mono.setStyleHint(QFont.Monospace)
        self._counter_labels = {}
        fields = [("state", "State"), ("events", "Events"), ("capacity", "Capacity"),
                  ("total_produced", "Produced"), ("total_evicted", "Evicted"),
                  ("total_filtered", "Filtered out"), ("overflow", "Overflow"),
                  ("filter", "Filter"), ("activity", "This frame")]
        for row, (key, title) in enumerate(fields):
            grid.addWidget(QLabel(title + ":"), row, 0)
            value = QLabel("—")
            value.setFont(mono)
            value.setTextInteractionFlags(Qt.TextSelectableByMouse)
            grid.addWidget(value, row, 1)
            self._counter_labels[key] = value
        grid.setColumnStretch(1, 1)
        layout.addWidget(counters_group)

        # ── Save ──
        save_group = QGroupBox("Save trace (server-side)")
        save = QHBoxLayout(save_group)
        self.save_path_edit = QLineEdit(str(Path.home() / "porttrace.json"))
        self.save_browse_btn = QPushButton("…")
        self.save_format_combo = QComboBox()
        self.save_format_combo.addItems(SAVE_FORMATS)
        self.save_btn = QPushButton("Save")
        save.addWidget(QLabel("Path:"))
        save.addWidget(self.save_path_edit, stretch=1)
        save.addWidget(self.save_browse_btn)
        save.addWidget(QLabel("Format:"))
        save.addWidget(self.save_format_combo)
        save.addWidget(self.save_btn)
        layout.addWidget(save_group)
        layout.addStretch(1)

        # Wiring
        self.connect_btn.clicked.connect(self.on_connect)
        self.start_btn.clicked.connect(self.on_start)
        self.pause_btn.clicked.connect(lambda: self._session_action("pause"))
        self.resume_btn.clicked.connect(lambda: self._session_action("resume"))
        self.stop_btn.clicked.connect(lambda: self._session_action("stop"))
        self.clear_btn.clicked.connect(lambda: self._session_action("clear"))
        self.save_browse_btn.clicked.connect(self.on_browse_save)
        self.save_btn.clicked.connect(self.on_save)
        self.emulator_combo.currentIndexChanged.connect(self.on_emulator_changed)

        self._set_session_enabled(False)

        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(500)
        self._poll_timer.timeout.connect(self.on_poll)

    # ── helpers ──

    def _set_session_enabled(self, enabled: bool):
        for btn in (self.start_btn, self.pause_btn, self.resume_btn, self.stop_btn,
                    self.clear_btn, self.save_btn):
            btn.setEnabled(enabled)

    def _error(self, exc):
        self._status_bar.showMessage(str(exc), 10000)
        QMessageBox.warning(self, "Port trace", str(exc))

    def _info(self, message: str):
        self._status_bar.showMessage(message, 5000)

    # ── slots ──

    def on_connect(self):
        try:
            self._api = WebApi(self.url_edit.text().strip())
            listing = self._api.get("/api/v1/emulator")
            emulators = listing.get("emulators", [])
            self.emulator_combo.clear()
            for emu in emulators:
                state = "running" if emu.get("is_running") else emu.get("state", "?")
                self.emulator_combo.addItem(f"{emu['id']}  ({state})", emu["id"])
            if not emulators:
                self._info("Connected — no emulator instances (start one via CLI/GUI)")
                self._set_session_enabled(False)
                return
            self.emulator_combo.setCurrentIndex(0)
            self.on_emulator_changed(0)
            self._info(f"Connected: {len(emulators)} emulator(s)")
        except ApiError as exc:
            self._api = None
            self._error(exc)

    def on_emulator_changed(self, index: int):
        if not self._api or index < 0:
            return
        self._emulator = self.emulator_combo.itemData(index) or ""
        if not self._emulator:
            return
        self._base = f"/api/v1/emulator/{self._emulator}/profiler/porttrace"
        try:
            # Enable the runtime feature; harmless if already on
            self._api.put(f"/api/v1/emulator/{self._emulator}/feature/porttrace", {"enabled": True})
            self._set_session_enabled(True)
            self._poll_timer.start()
            self._info("porttrace feature enabled")
        except ApiError as exc:
            self._set_session_enabled(False)
            self._error(exc)

    def on_start(self):
        if not self._api:
            return
        try:
            self._api.post(f"{self._base}/stop")  # clean slate (config needs stopped)
            self._api.post(f"{self._base}/config", {
                "capacity": self.capacity_spin.value(),
                "overflow": "ring" if self.overflow_combo.currentIndex() == 0 else "stop",
            })

            include = [porttrace_convert_rule(r) for r in _split_rules(self.include_edit.text())]
            exclude = [porttrace_convert_rule(r) for r in _split_rules(self.exclude_edit.text())]
            if include or exclude:
                self._api.post(f"{self._base}/filter", {"include": include, "exclude": exclude})
            else:
                self._api.post(f"{self._base}/filter", {"preset": self.preset_combo.currentText()})

            self._api.post(f"{self._base}/start")
            self._info("Capture started")
        except (ApiError, ValueError) as exc:
            self._error(exc)

    def _session_action(self, action: str):
        if not self._api:
            return
        try:
            result = self._api.post(f"{self._base}/{action}")
            if action == "stop":
                self._info(f"Stopped: {result.get('events', '?')} events captured")
            else:
                self._info(action.capitalize())
        except ApiError as exc:
            self._error(exc)

    def on_poll(self):
        if not self._api or not self._emulator:
            return
        try:
            session = self._api.get(f"{self._base}/status").get("session", {})
        except ApiError:
            return  # transient; keep last values

        state = session.get("state", "?")
        if session.get("auto_stopped"):
            state += "  [BUFFER FULL — auto-stopped]"
        values = {
            "state": state,
            "events": f"{session.get('events', 0):,}",
            "capacity": f"{session.get('capacity', 0):,}",
            "total_produced": f"{session.get('total_produced', 0):,}",
            "total_evicted": f"{session.get('total_evicted', 0):,}",
            "total_filtered": f"{session.get('total_filtered', 0):,}",
            "overflow": session.get("overflow", "—"),
            "filter": session.get("filter", "—"),
        }
        activity = session.get("activity", {})
        values["activity"] = (f"frame {activity.get('frame', 0)}: "
                              f"in={activity.get('in', 0)} out={activity.get('out', 0)} "
                              f"unmapped={activity.get('unmapped_in', 0)}+{activity.get('unmapped_out', 0)} "
                              f"gated={activity.get('beta128_gated', 0)}")
        for key, text in values.items():
            self._counter_labels[key].setText(str(text))

    def on_browse_save(self):
        path, _ = QFileDialog.getSaveFileName(self, "Save trace as", self.save_path_edit.text(),
                                              "Trace files (*.json *.csv *.bin *.binz)")
        if path:
            self.save_path_edit.setText(path)

    def on_save(self):
        if not self._api:
            return
        try:
            fmt = self.save_format_combo.currentText()
            result = self._api.post(f"{self._base}/save",
                                    {"path": self.save_path_edit.text().strip(), "format": fmt})
            self._info(f"Saved {result.get('saved', '?')} events to {result.get('path')}")
        except ApiError as exc:
            self._error(exc)


class ConvertTab(QWidget):
    """Offline conversion / analysis of saved trace files via porttrace_convert."""

    def __init__(self, status_bar: QStatusBar):
        super().__init__()
        self._status_bar = status_bar

        layout = QVBoxLayout(self)

        picker_group = QGroupBox("Input trace (json / csv / bin / binz)")
        picker = QHBoxLayout(picker_group)
        self.input_edit = QLineEdit()
        self.input_browse_btn = QPushButton("…")
        self.format_combo = QComboBox()
        self.format_combo.addItems(CONVERT_FORMATS)
        self.convert_btn = QPushButton("Preview")
        self.export_btn = QPushButton("Export…")
        picker.addWidget(self.input_edit, stretch=1)
        picker.addWidget(self.input_browse_btn)
        picker.addWidget(QLabel("To:"))
        picker.addWidget(self.format_combo)
        picker.addWidget(self.convert_btn)
        picker.addWidget(self.export_btn)
        layout.addWidget(picker_group)

        self.output_view = QPlainTextEdit()
        self.output_view.setReadOnly(True)
        mono = QFont("Menlo")
        mono.setStyleHint(QFont.Monospace)
        self.output_view.setFont(mono)
        self.output_view.setPlaceholderText(
            "Open a saved trace and press Preview.\n"
            "'summary' and 'strictness' show analyses; other formats show the converted text.\n"
            "binz preview shows the decoded event table; use Export… to write the compressed file.")
        layout.addWidget(self.output_view, stretch=1)

        self.input_browse_btn.clicked.connect(self.on_browse)
        self.convert_btn.clicked.connect(self.on_preview)
        self.export_btn.clicked.connect(self.on_export)

    def _load(self):
        path = Path(self.input_edit.text().strip()).expanduser()
        if not path.exists():
            raise ValueError(f"No such file: {path}")
        return porttrace_convert.read_any(path), path

    def on_browse(self):
        path, _ = QFileDialog.getOpenFileName(self, "Open trace", "",
                                              "Trace files (*.json *.csv *.bin *.binz);;All files (*)")
        if path:
            self.input_edit.setText(path)

    def on_preview(self):
        try:
            (session, events), _ = self._load()
        except (ValueError, RuntimeError, OSError, KeyError) as exc:
            self.output_view.setPlainText(f"error: {exc}")
            return

        fmt = self.format_combo.currentText()
        buf = io.StringIO()
        if fmt == "summary":
            porttrace_convert.write_summary(session, events, buf)
        elif fmt == "strictness":
            porttrace_convert.write_strictness(session, events, buf)
        elif fmt == "json":
            porttrace_convert.write_json(session, events, buf)
        elif fmt == "csv":
            porttrace_convert.write_csv(session, events, buf)
        elif fmt == "markdown":
            porttrace_convert.write_markdown(session, events, buf)
        else:  # text, and the on-screen stand-in for binz
            porttrace_convert.write_text(session, events, buf)

        # Very large traces: cap the preview, full content comes via Export
        text = buf.getvalue()
        if len(text) > 2_000_000:
            text = text[:2_000_000] + f"\n… preview truncated ({len(events)} events); use Export…"
        self.output_view.setPlainText(text)
        self._status_bar.showMessage(f"{len(events)} events loaded", 5000)

    def on_export(self):
        try:
            (session, events), input_path = self._load()
        except (ValueError, RuntimeError, OSError, KeyError) as exc:
            self.output_view.setPlainText(f"error: {exc}")
            return

        fmt = self.format_combo.currentText()
        suffix = {"json": ".json", "csv": ".csv", "markdown": ".md", "text": ".txt",
                  "binz": ".binz", "summary": ".txt", "strictness": ".txt"}[fmt]
        suggested = str(input_path.with_suffix(suffix))
        path, _ = QFileDialog.getSaveFileName(self, "Export as", suggested)
        if not path:
            return

        try:
            if fmt == "binz":
                porttrace_convert.write_binz(session, events, Path(path))
            else:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    {"summary": porttrace_convert.write_summary,
                     "strictness": porttrace_convert.write_strictness,
                     "json": porttrace_convert.write_json,
                     "csv": porttrace_convert.write_csv,
                     "markdown": porttrace_convert.write_markdown,
                     "text": porttrace_convert.write_text}[fmt](session, events, f)
            self._status_bar.showMessage(f"Exported {len(events)} events to {path}", 8000)
        except (RuntimeError, OSError) as exc:
            QMessageBox.warning(self, "Export failed", str(exc))


def _split_rules(text: str):
    return [part.strip() for part in text.split(";") if part.strip()]


def porttrace_convert_rule(spec: str) -> dict:
    """Reuse porttrace_capture's rule grammar for the GUI's rule line edits."""
    from porttrace_capture import parse_rule
    return parse_rule(spec)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Unreal-NG Port Trace")
        self.resize(900, 640)

        status_bar = QStatusBar()
        self.setStatusBar(status_bar)

        tabs = QTabWidget()
        self.capture_tab = CaptureTab(status_bar)
        self.convert_tab = ConvertTab(status_bar)
        tabs.addTab(self.capture_tab, "Capture")
        tabs.addTab(self.convert_tab, "Convert / Analyze")
        self.setCentralWidget(tabs)


def main() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
