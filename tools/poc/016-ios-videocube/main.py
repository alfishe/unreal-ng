#!/usr/bin/env python3
"""
3D Video Cube iOS Remote Control & Verification Tool (PySide6)
================================================================
A desktop GUI tool to connect to a remote 3D Video Cube iOS Unreal-NG host via WebAPI:
- Enumeration & management of 6 live cube face emulator instances
- Auto-initialization of 6 cube faces with distinct random demo snapshots picked from the videowall whitelist (tools/verification/videowall/whitelist.txt)
- Face-targeted media pushing: Snapshots (.sna/.z80), Disks (.trd/.scl/.dsk/.fdi), Tapes (.tap/.tzx)
- Dynamic instance re-creation & configuration switching for specific cube faces
- Active Hardware Keyboard Bridge for targeted face instance
"""

import sys
import time
import os
import json
import random
import signal
from typing import Optional, Dict, Any, List

import requests
from PySide6.QtCore import Qt, QThread, Signal, Slot, QTimer
from PySide6.QtGui import QFont, QKeyEvent, QPainter, QPen, QBrush, QPalette
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QLabel,
    QLineEdit, QPushButton, QComboBox, QTabWidget, QGroupBox, QFileDialog,
    QTextEdit, QSplitter, QFrame, QGridLayout, QCheckBox
)

DEFAULT_HOST = "127.0.0.1"
# Standard WebAPI port - the cube app exposes all 6 instances here like any
# other Unreal-NG host
DEFAULT_PORT = 8090

DEMO_SNAPSHOTS = [
    ("Face 0 (Front)", "testdata/loaders/sna/7threality.sna"),
    ("Face 1 (Back)", "testdata/loaders/sna/across-the-edge-second.sna"),
    ("Face 2 (Left)", "testdata/loaders/sna/aleste1.sna"),
    ("Face 3 (Right)", "testdata/loaders/sna/atarin.sna"),
    ("Face 4 (Top)", "testdata/loaders/sna/vibrations.sna"),
    ("Face 5 (Bottom)", "testdata/loaders/sna/eyeache1.sna")
]

FACE_COUNT = 6
SNAPSHOT_DIR_REL = os.path.join("testdata", "loaders", "sna")
WHITELIST_REL = os.path.join("tools", "verification", "videowall", "whitelist.txt")
FACE_NAMES = ["Face 0 (Front)", "Face 1 (Back)", "Face 2 (Left)",
              "Face 3 (Right)", "Face 4 (Top)", "Face 5 (Bottom)"]


def load_whitelisted_snapshots(root_dir: str) -> List[str]:
    """Absolute paths of whitelisted demo snapshots that exist on disk.

    Reads tools/verification/videowall/whitelist.txt (one filename per line,
    '#' comments allowed) and resolves each entry against testdata/loaders/sna.
    Falls back to the built-in DEMO_SNAPSHOTS set when the whitelist file is
    missing or yields nothing usable."""
    candidates: List[str] = []
    whitelist_path = os.path.join(root_dir, WHITELIST_REL)
    snapshot_dir = os.path.join(root_dir, SNAPSHOT_DIR_REL)
    if os.path.isfile(whitelist_path):
        with open(whitelist_path, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                name = line.strip()
                if not name or name.startswith("#"):
                    continue
                path = os.path.join(snapshot_dir, name)
                if os.path.isfile(path):
                    candidates.append(path)
    if not candidates:
        candidates = [os.path.join(root_dir, rel) for _, rel in DEMO_SNAPSHOTS]
    return [p for p in candidates if os.path.isfile(p)]

QT_KEY_MAP = {
    Qt.Key_A: "a", Qt.Key_B: "b", Qt.Key_C: "c", Qt.Key_D: "d", Qt.Key_E: "e",
    Qt.Key_F: "f", Qt.Key_G: "g", Qt.Key_H: "h", Qt.Key_I: "i", Qt.Key_J: "j",
    Qt.Key_K: "k", Qt.Key_L: "l", Qt.Key_M: "m", Qt.Key_N: "n", Qt.Key_O: "o",
    Qt.Key_P: "p", Qt.Key_Q: "q", Qt.Key_R: "r", Qt.Key_S: "s", Qt.Key_T: "t",
    Qt.Key_U: "u", Qt.Key_V: "v", Qt.Key_W: "w", Qt.Key_X: "x", Qt.Key_Y: "y",
    Qt.Key_Z: "z",
    Qt.Key_0: "0", Qt.Key_1: "1", Qt.Key_2: "2", Qt.Key_3: "3", Qt.Key_4: "4",
    Qt.Key_5: "5", Qt.Key_6: "6", Qt.Key_7: "7", Qt.Key_8: "8", Qt.Key_9: "9",
    Qt.Key_Space: "space", Qt.Key_Return: "enter", Qt.Key_Enter: "enter",
    Qt.Key_Backspace: "delete", Qt.Key_Shift: "caps", Qt.Key_Control: "symbol",
    Qt.Key_Alt: "symbol", Qt.Key_Meta: "symbol", Qt.Key_Up: "up",
    Qt.Key_Down: "down", Qt.Key_Left: "left", Qt.Key_Right: "right",
    Qt.Key_Escape: "break", Qt.Key_Tab: "edit"
}


class APIWorker(QThread):
    """Background worker for non-blocking HTTP requests."""
    result_signal = Signal(str, bool, str, dict)

    def __init__(self, action: str, url: str, method: str = "GET", json_data: Optional[dict] = None,
                 files: Optional[dict] = None, raw_body: Optional[bytes] = None, extra_headers: Optional[dict] = None):
        super().__init__()
        self.action = action
        self.url = url
        self.method = method
        self.json_data = json_data
        self.files = files
        self.raw_body = raw_body
        self.extra_headers = extra_headers or {}

    def run(self):
        try:
            headers = dict(self.extra_headers)
            proxies = {"http": None, "https": None}
            start_time = time.time()
            if self.method.upper() == "GET":
                resp = requests.get(self.url, proxies=proxies, timeout=(3, 5))
            elif self.method.upper() == "POST":
                if self.raw_body is not None:
                    headers["Content-Type"] = "application/octet-stream"
                    resp = requests.post(self.url, data=self.raw_body, headers=headers, proxies=proxies, timeout=(3, 30))
                elif self.files:
                    resp = requests.post(self.url, files=self.files, proxies=proxies, timeout=(3, 10))
                else:
                    headers["Content-Type"] = "application/json"
                    resp = requests.post(self.url, json=self.json_data, headers=headers, proxies=proxies, timeout=(3, 5))
            else:
                resp = requests.request(self.method, self.url, proxies=proxies, timeout=(3, 5))

            elapsed_ms = int((time.time() - start_time) * 1000)
            data = {}
            try:
                data = resp.json()
            except Exception:
                data = {"raw": resp.text}

            success = (200 <= resp.status_code < 300)
            msg = f"HTTP {resp.status_code} ({elapsed_ms}ms)"
            self.result_signal.emit(self.action, success, msg, data)
        except Exception as e:
            self.result_signal.emit(self.action, False, str(e), {})


class KeyboardBridgeWidget(QFrame):
    """Interactive focus area for catching physical & Bluetooth keyboard events."""
    key_event_signal = Signal(str, str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFocusPolicy(Qt.StrongFocus)
        self.setMinimumHeight(120)
        self.active_keys = set()

    def paintEvent(self, event):
        super().paintEvent(event)
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        rect = self.rect()
        is_focused = self.hasFocus()
        palette = self.palette()
        is_dark = palette.color(QPalette.Window).lightness() < 128

        bg_color = palette.color(QPalette.Highlight).darker(130) if is_focused and is_dark else palette.color(QPalette.AlternateBase)
        border_color = palette.color(QPalette.Highlight) if is_focused else palette.color(QPalette.Mid)
        text_color = palette.color(QPalette.HighlightedText) if is_focused and is_dark else palette.color(QPalette.WindowText)

        painter.setBrush(QBrush(bg_color))
        painter.setPen(QPen(border_color, 2 if is_focused else 1))
        painter.drawRoundedRect(rect.adjusted(2, 2, -2, -2), 8, 8)

        painter.setFont(QFont("Helvetica", 11, QFont.Bold if is_focused else QFont.Normal))
        painter.setPen(text_color)

        status_str = "⌨ TARGETED KEYBOARD BRIDGE ACTIVE" if is_focused else "⌨ CLICK TO ACTIVATE KEYBOARD BRIDGE FOR TARGETED FACE"
        painter.drawText(rect.adjusted(0, -10, 0, 0), Qt.AlignCenter, status_str)

    def mousePressEvent(self, event):
        self.setFocus()
        self.update()

    def keyPressEvent(self, event: QKeyEvent):
        if event.isAutoRepeat(): return
        key_name = QT_KEY_MAP.get(event.key())
        if key_name:
            self.active_keys.add(key_name)
            self.update()
            self.key_event_signal.emit(key_name, "press")

    def keyReleaseEvent(self, event: QKeyEvent):
        if event.isAutoRepeat(): return
        key_name = QT_KEY_MAP.get(event.key())
        if key_name:
            self.active_keys.discard(key_name)
            self.update()
            self.key_event_signal.emit(key_name, "release")


class VideoCubeControlApp(QMainWindow):
    """Main Application Window for 3D Video Cube iOS Remote Controller."""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("Unreal-NG 3D Video Cube iOS Controller (WebAPI PoC)")
        self.resize(980, 720)

        self.current_emu_id: Optional[str] = None
        self.emulator_instances: List[Dict[str, Any]] = []
        self.workers: List[APIWorker] = []

        self.setAcceptDrops(True)
        self.init_ui()

    def init_ui(self):
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_layout = QVBoxLayout(main_widget)
        main_layout.setContentsMargins(16, 16, 16, 16)
        main_layout.setSpacing(12)

        # 1. Connection & Cube Instances Header
        conn_group = QGroupBox("Target 3D Video Cube Connection & Face Instance Control")
        conn_layout = QVBoxLayout(conn_group)

        conn_row1 = QHBoxLayout()
        conn_row1.addWidget(QLabel("LAN IP:"))
        self.ip_input = QLineEdit(DEFAULT_HOST)
        self.ip_input.setFixedWidth(130)
        conn_row1.addWidget(self.ip_input)

        conn_row1.addWidget(QLabel("Port:"))
        self.port_input = QLineEdit(str(DEFAULT_PORT))
        self.port_input.setFixedWidth(60)
        conn_row1.addWidget(self.port_input)

        self.connect_btn = QPushButton("Connect / Refresh")
        self.connect_btn.clicked.connect(self.fetch_status)
        conn_row1.addWidget(self.connect_btn)

        conn_row1.addSpacing(12)
        conn_row1.addWidget(QLabel("Target Cube Face:"))
        self.emu_combo = QComboBox()
        self.emu_combo.setMinimumWidth(260)
        self.emu_combo.currentIndexChanged.connect(self.on_emu_selected)
        conn_row1.addWidget(self.emu_combo)

        self.reset_btn = QPushButton("Reset Face Core")
        self.reset_btn.clicked.connect(self.reset_emulator)
        conn_row1.addWidget(self.reset_btn)

        self.singlesync_cb = QCheckBox("SingleSync Mode")
        self.singlesync_cb.setToolTip("Project single emulator face to all 6 cube faces")
        self.singlesync_cb.toggled.connect(self.on_singlesync_toggled)
        conn_row1.addWidget(self.singlesync_cb)
        conn_row1.addStretch()

        # Row 2: Machine Model Creation & Overscan
        conn_row2 = QHBoxLayout()
        conn_row2.addWidget(QLabel("New Machine Model:"))
        self.model_combo = QComboBox()
        self.model_combo.setMinimumWidth(220)
        conn_row2.addWidget(self.model_combo)

        self.overscan_cb = QCheckBox("Overscan Mode (384x304)")
        self.overscan_cb.setChecked(True)
        self.overscan_cb.toggled.connect(self.on_overscan_toggled)
        conn_row2.addWidget(self.overscan_cb)

        self.create_emu_btn = QPushButton("Re-Create Targeted Face Instance")
        self.create_emu_btn.clicked.connect(self.recreate_targeted_machine)
        conn_row2.addWidget(self.create_emu_btn)

        # Auto Demo Preset Initializer Button
        self.auto_demo_btn = QPushButton("🚀 Initialize 6 Cube Faces with Demo Snapshots")
        self.auto_demo_btn.setStyleSheet("font-weight: bold; background-color: #0284C7; color: white;")
        self.auto_demo_btn.clicked.connect(self.auto_initialize_demo_snapshots)
        conn_row2.addWidget(self.auto_demo_btn)

        conn_layout.addLayout(conn_row1)
        conn_layout.addLayout(conn_row2)
        main_layout.addWidget(conn_group)

        # 2. Splitter (Media Tabs + Keyboard Log)
        splitter = QSplitter(Qt.Vertical)
        top_widget = QWidget()
        top_layout = QHBoxLayout(top_widget)
        top_layout.setContentsMargins(0, 0, 0, 0)

        # Left Column: Media Operations
        media_group = QGroupBox("Targeted Face Media Operations")
        media_layout = QVBoxLayout(media_group)
        self.tabs = QTabWidget()

        # Tab 1: Snapshots
        snap_tab = QWidget()
        snap_layout = QVBoxLayout(snap_tab)
        snap_file_layout = QHBoxLayout()
        self.snap_path_input = QLineEdit()
        self.snap_path_input.setPlaceholderText("Select or Drag & Drop .sna / .z80 snapshot here...")
        snap_browse_btn = QPushButton("Browse...")
        snap_browse_btn.clicked.connect(lambda: self.browse_file(self.snap_path_input, "Snapshots (*.sna *.z80)"))
        snap_file_layout.addWidget(self.snap_path_input)
        snap_file_layout.addWidget(snap_browse_btn)

        self.snap_upload_btn = QPushButton("Push Snapshot to Selected Cube Face")
        self.snap_upload_btn.clicked.connect(self.upload_snapshot)

        snap_layout.addLayout(snap_file_layout)
        snap_layout.addWidget(self.snap_upload_btn)
        snap_layout.addStretch()
        self.tabs.addTab(snap_tab, "Snapshots")

        # Tab 2: Disks
        disk_tab = QWidget()
        disk_layout = QVBoxLayout(disk_tab)
        disk_opts_layout = QHBoxLayout()
        self.autorun_cb = QCheckBox("Autorun Mode")
        self.autorun_cb.setChecked(True)
        disk_opts_layout.addWidget(self.autorun_cb)
        disk_opts_layout.addStretch()

        disk_file_layout = QHBoxLayout()
        self.disk_path_input = QLineEdit()
        self.disk_path_input.setPlaceholderText("Select or Drag & Drop .trd, .scl, .dsk, .fdi disk here...")
        disk_browse_btn = QPushButton("Browse...")
        disk_browse_btn.clicked.connect(lambda: self.browse_file(self.disk_path_input, "Disks (*.trd *.scl *.dsk *.fdi)"))
        disk_file_layout.addWidget(self.disk_path_input)
        disk_file_layout.addWidget(disk_browse_btn)

        disk_ctrl_layout = QHBoxLayout()
        self.disk_insert_btn = QPushButton("Insert Disk to Target Face")
        self.disk_insert_btn.clicked.connect(self.insert_disk)
        disk_ctrl_layout.addWidget(self.disk_insert_btn)

        disk_layout.addLayout(disk_opts_layout)
        disk_layout.addLayout(disk_file_layout)
        disk_layout.addLayout(disk_ctrl_layout)
        disk_layout.addStretch()
        self.tabs.addTab(disk_tab, "Disks")

        media_layout.addWidget(self.tabs)
        top_layout.addWidget(media_group, 1)

        # Right Column: Keyboard Bridge
        kbd_group = QGroupBox("Targeted Cube Face Hardware Keyboard")
        kbd_layout = QVBoxLayout(kbd_group)

        self.bridge_widget = KeyboardBridgeWidget()
        self.bridge_widget.key_event_signal.connect(self.on_keyboard_event)
        kbd_layout.addWidget(self.bridge_widget)

        top_layout.addWidget(kbd_group, 1)
        splitter.addWidget(top_widget)

        # Log Console
        log_group = QGroupBox("WebAPI Event & Log Output")
        log_layout = QVBoxLayout(log_group)
        self.log_console = QTextEdit()
        self.log_console.setReadOnly(True)
        log_layout.addWidget(self.log_console)

        splitter.addWidget(log_group)
        splitter.setSizes([420, 180])
        main_layout.addWidget(splitter)

        QTimer.singleShot(500, self.fetch_status)

    def log(self, message: str, level: str = "INFO"):
        ts = time.strftime("%H:%M:%S")
        is_dark = self.palette().color(QPalette.Window).lightness() < 128
        color = ("#38BDF8" if is_dark else "#0284C7") if level == "INFO" else ("#10B981" if is_dark else "#059669") if level == "SUCCESS" else ("#F43F5E" if is_dark else "#DC2626")
        self.log_console.append(f'<span style="color: #94A3B8;">[{ts}]</span> <span style="color: {color}; font-weight: bold;">[{level}]</span> {message}')

    def get_base_url(self) -> str:
        host = self.ip_input.text().strip() or DEFAULT_HOST
        port = self.port_input.text().strip() or str(DEFAULT_PORT)
        return f"http://{host}:{port}"

    def run_async(self, action: str, url: str, method: str = "GET", json_data: Optional[dict] = None,
                  files: Optional[dict] = None, raw_body: Optional[bytes] = None, extra_headers: Optional[dict] = None):
        worker = APIWorker(action, url, method, json_data, files, raw_body, extra_headers)
        worker.result_signal.connect(self.on_api_result)
        self.workers.append(worker)
        worker.start()

    @Slot(str, bool, str, dict)
    def on_api_result(self, action: str, success: bool, msg: str, data: dict):
        if action == "fetch_status":
            if success and "emulators" in data:
                self.emulator_instances = data["emulators"]
                self.emu_combo.blockSignals(True)
                self.emu_combo.clear()

                face_names = FACE_NAMES
                for i, emu in enumerate(self.emulator_instances):
                    emu_id = emu.get("id", "")
                    model = emu.get("model", "")
                    state = "running" if emu.get("is_running") else str(emu.get("state", "?"))
                    face_label = face_names[i] if i < len(face_names) else f"Instance {i}"
                    self.emu_combo.addItem(f"{face_label}: {model} (#{emu_id[:8]}) [{state}]", emu_id)

                self.emu_combo.blockSignals(False)

                if self.emu_combo.count() > 0:
                    self.current_emu_id = self.emu_combo.itemData(0)

                if len(self.emulator_instances) != FACE_COUNT:
                    self.log(
                        f"Expected {FACE_COUNT} cube face instances but WebAPI reports "
                        f"{len(self.emulator_instances)}. If the cube is rendering locally, "
                        "a stale app process is still holding the WebAPI port: kill the app "
                        "(swipe it away), relaunch, and reconnect.", "ERROR")
                else:
                    self.log(f"Connected to 3D Video Cube: {len(self.emulator_instances)} instance(s) running.", "SUCCESS")

        elif action == "auto_demo_push":
            if success:
                self.log("Demo snapshot initialized successfully.", "SUCCESS")
            else:
                self.log(f"Demo snapshot initialization failed: {msg}", "ERROR")

        elif action == "recreate_instance":
            if success and "id" in data:
                new_id = data["id"]
                self.current_emu_id = new_id
                self.log(f"Successfully re-created targeted cube face instance (#{new_id[:8]}).", "SUCCESS")
                self.fetch_status()
            else:
                self.log(f"Failed to re-create face instance: {msg}", "ERROR")

        elif action in ("upload_snapshot", "insert_disk", "reset_emulator", "set_overscan", "set_singlesync"):
            if success:
                self.log(f"Operation {action} succeeded on targeted face.", "SUCCESS")
            else:
                self.log(f"Operation {action} failed: {msg}", "ERROR")

    def fetch_status(self):
        url = f"{self.get_base_url()}/api/v1/emulator"
        self.log(f"Connecting to WebAPI at {url}...")
        self.run_async("fetch_status", url, "GET")
        if self.model_combo.count() == 0:
            models_url = f"{self.get_base_url()}/api/v1/emulator/models"
            self.run_async("fetch_models", models_url, "GET")

    def auto_initialize_demo_snapshots(self):
        root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
        candidates = load_whitelisted_snapshots(root_dir)
        if not candidates:
            self.log("No whitelisted demo snapshots found on disk.", "ERROR")
            return

        instances = self.emulator_instances[:FACE_COUNT]
        if len(self.emulator_instances) != FACE_COUNT:
            # Fail fast: pushing into a foreign host (e.g. a desktop unreal-qt
            # that owns the port) reports 200 OK while the cube shows nothing
            self.log(
                f"Refusing to auto-initialize: the connected server exposes "
                f"{len(self.emulator_instances)} instance(s), expected {FACE_COUNT}. "
                f"This is NOT the cube app. The cube app serves its WebAPI on port "
                f"{DEFAULT_PORT} with exactly 6 faces - rebuild/reinstall the app, "
                f"launch it, and reconnect on that port.", "ERROR")
            return
        pick_count = min(len(instances), len(candidates))
        # Distinct random assignment: a fresh, non-repeating set on every click
        picks = random.sample(candidates, pick_count)

        self.log(f"🚀 Auto-initializing {pick_count} cube face(s) with distinct random whitelisted snapshots...", "INFO")
        for i, (emu, abs_path) in enumerate(zip(instances, picks)):
            emu_id = emu.get("id", "")
            face_label = FACE_NAMES[i] if i < len(FACE_NAMES) else f"Instance {i}"

            self.log(f"Pushing '{os.path.basename(abs_path)}' to {face_label} (#{emu_id[:8]})...", "INFO")
            url = f"{self.get_base_url()}/api/v1/emulator/{emu_id}/snapshot/load"
            with open(abs_path, "rb") as f:
                raw = f.read()
            self.run_async("auto_demo_push", url, "POST", raw_body=raw, extra_headers={"X-Filename": os.path.basename(abs_path)})

    def recreate_targeted_machine(self):
        if not self.current_emu_id: return
        data = self.model_combo.currentData()
        payload = {}
        if isinstance(data, dict): payload = dict(data)
        elif isinstance(data, str): payload = {"model": data}

        payload["overscan"] = self.overscan_cb.isChecked()
        if self.overscan_cb.isChecked(): payload["viewport"] = "symmetric_horizontal"

        self.log(f"Disposing active face instance #{self.current_emu_id[:8]} and re-creating...", "INFO")
        dispose_url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}"
        self.run_async("recreate_dispose", dispose_url, "DELETE")

        start_url = f"{self.get_base_url()}/api/v1/emulator/start"
        self.run_async("recreate_instance", start_url, "POST", json_data=payload)

    def on_emu_selected(self, index: int):
        if index >= 0 and index < self.emu_combo.count():
            self.current_emu_id = self.emu_combo.itemData(index)
            self.log(f"Selected target face instance: #{self.current_emu_id[:8]}", "INFO")
            if self.singlesync_cb.isChecked():
                self.on_singlesync_toggled(True)
            else:
                url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}"
                self.run_async("select_emulator", url, "GET")

    def on_singlesync_toggled(self, checked: bool):
        url = f"{self.get_base_url()}/api/v1/videowall/singlesync"
        payload = {"enable": checked}
        if self.current_emu_id:
            payload["emulator_id"] = self.current_emu_id
        self.run_async("set_singlesync", url, "POST", json_data=payload)

    def on_overscan_toggled(self, checked: bool):
        if self.current_emu_id:
            url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/overscan"
            payload = {"enabled": checked, "viewport": "symmetric_horizontal" if checked else "full"}
            self.run_async("set_overscan", url, "POST", json_data=payload)

    def reset_emulator(self):
        if self.current_emu_id:
            url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/reset"
            self.run_async("reset_emulator", url, "POST")

    def upload_snapshot(self):
        if not self.current_emu_id: return
        file_path = self.snap_path_input.text().strip()
        if not file_path or not os.path.exists(file_path):
            self.log("Invalid snapshot path", "ERROR")
            return
        url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/snapshot/load"
        with open(file_path, "rb") as f: raw = f.read()
        self.run_async("upload_snapshot", url, "POST", raw_body=raw, extra_headers={"X-Filename": os.path.basename(file_path)})

    def insert_disk(self):
        if not self.current_emu_id: return
        file_path = self.disk_path_input.text().strip()
        if not file_path or not os.path.exists(file_path):
            self.log("Invalid disk path", "ERROR")
            return
        url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/disk/0"
        with open(file_path, "rb") as f: files = {"file": (os.path.basename(file_path), f, "application/octet-stream")}
        self.run_async("insert_disk", url, "POST", files=files)

    def on_keyboard_event(self, key_name: str, action: str):
        if not self.current_emu_id: return
        url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/keyboard"
        payload = {"key": key_name, "action": action}
        self.run_async(f"key_{action}", url, "POST", json_data=payload)

    def browse_file(self, target_line_edit: QLineEdit, filter_str: str):
        file_path, _ = QFileDialog.getOpenFileName(self, "Select File", "", filter_str)
        if file_path: target_line_edit.setText(file_path)

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            urls = event.mimeData().urls()
            valid_exts = ('.sna', '.z80', '.trd', '.scl', '.dsk', '.fdi', '.img')
            if any(url.toLocalFile().lower().endswith(valid_exts) for url in urls):
                event.acceptProposedAction()

    def dropEvent(self, event):
        if not self.current_emu_id:
            self.log("Drag & Drop ignored: No targeted cube face instance selected.", "ERROR")
            return

        urls = event.mimeData().urls()
        for url in urls:
            filepath = url.toLocalFile()
            if not filepath or not os.path.exists(filepath):
                continue
            ext = os.path.splitext(filepath)[1].lower()
            if ext in ('.sna', '.z80'):
                self.snap_path_input.setText(filepath)
                self.tabs.setCurrentIndex(0)
                self.upload_snapshot()
                self.log(f"Drag & Drop: Pushed '{os.path.basename(filepath)}' ONLY to selected face #{self.current_emu_id[:8]}", "INFO")
                break
            elif ext in ('.trd', '.scl', '.dsk', '.fdi', '.img'):
                self.disk_path_input.setText(filepath)
                self.tabs.setCurrentIndex(1)
                self.insert_disk()
                self.log(f"Drag & Drop: Inserted disk '{os.path.basename(filepath)}' ONLY to selected face #{self.current_emu_id[:8]}", "INFO")
                break


def sigint_handler(sig, frame):
    print("\n[Ctrl+C] Exiting 3D Video Cube Controller...")
    QApplication.quit()
    sys.exit(0)


def main():
    signal.signal(signal.SIGINT, sigint_handler)
    app = QApplication(sys.argv)

    # Periodic QTimer allows Python interpreter to process C-level signals (Ctrl+C)
    timer = QTimer()
    timer.start(200)
    timer.timeout.connect(lambda: None)

    window = VideoCubeControlApp()
    window.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
