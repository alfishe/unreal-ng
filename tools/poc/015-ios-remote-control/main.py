#!/usr/bin/env python3
"""
iOS Remote Control PoC (PySide6)
================================
A desktop GUI tool to connect to a remote iOS Unreal-NG emulator host via WebAPI:
- Status polling & emulator instance enumeration
- Media pushing: Snapshots (.sna/.z80), Disks (.trd/.scl/.dsk/.fdi), Tapes (.tap/.tzx)
- Active Keyboard Bridge: Intercepts host keyboard key presses and bridges them to iOS core via WebAPI MessageCenter
"""

import sys
import time
import os
import json
import threading
from typing import Optional, Dict, Any, List

import requests
from PySide6.QtCore import Qt, QThread, Signal, Slot, QSize, QTimer
from PySide6.QtGui import QColor, QFont, QKeyEvent, QPainter, QPen, QBrush, QPalette, QDragEnterEvent, QDropEvent
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QLabel,
    QLineEdit, QPushButton, QComboBox, QTabWidget, QGroupBox, QFileDialog,
    QTextEdit, QSplitter, QFrame, QGridLayout, QSizePolicy, QStyleOption, QStyle,
    QCheckBox
)

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8090

# --- Qt Key Mapping to ZX Spectrum Key Names ---
QT_KEY_MAP = {
    Qt.Key_A: "a", Qt.Key_B: "b", Qt.Key_C: "c", Qt.Key_D: "d", Qt.Key_E: "e",
    Qt.Key_F: "f", Qt.Key_G: "g", Qt.Key_H: "h", Qt.Key_I: "i", Qt.Key_J: "j",
    Qt.Key_K: "k", Qt.Key_L: "l", Qt.Key_M: "m", Qt.Key_N: "n", Qt.Key_O: "o",
    Qt.Key_P: "p", Qt.Key_Q: "q", Qt.Key_R: "r", Qt.Key_S: "s", Qt.Key_T: "t",
    Qt.Key_U: "u", Qt.Key_V: "v", Qt.Key_W: "w", Qt.Key_X: "x", Qt.Key_Y: "y",
    Qt.Key_Z: "z",
    Qt.Key_0: "0", Qt.Key_1: "1", Qt.Key_2: "2", Qt.Key_3: "3", Qt.Key_4: "4",
    Qt.Key_5: "5", Qt.Key_6: "6", Qt.Key_7: "7", Qt.Key_8: "8", Qt.Key_9: "9",
    Qt.Key_Space: "space",
    Qt.Key_Return: "enter",
    Qt.Key_Enter: "enter",
    Qt.Key_Backspace: "delete",
    Qt.Key_Shift: "caps",
    Qt.Key_Control: "symbol",
    Qt.Key_Alt: "symbol",
    Qt.Key_Meta: "symbol",
    Qt.Key_Up: "up",
    Qt.Key_Down: "down",
    Qt.Key_Left: "left",
    Qt.Key_Right: "right",
    Qt.Key_Escape: "break",
    Qt.Key_Tab: "edit"
}


class APIWorker(QThread):
    """Background worker for non-blocking HTTP requests."""
    result_signal = Signal(str, bool, str, dict)  # action, success, message, data

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
        except requests.exceptions.Timeout:
            self.result_signal.emit(self.action, False, "Target IP unreachable or WebAPI read timeout", {})
        except Exception as e:
            self.result_signal.emit(self.action, False, str(e), {})


class KeyboardBridgeWidget(QFrame):
    """Interactive focus area for catching physical & Bluetooth keyboard events."""
    key_event_signal = Signal(str, str)  # key_name, action ("press" or "release")

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFocusPolicy(Qt.StrongFocus)
        self.setMinimumHeight(140)
        self.active_keys = set()

    def paintEvent(self, event):
        super().paintEvent(event)
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        rect = self.rect()
        is_focused = self.hasFocus()
        palette = self.palette()
        is_dark = palette.color(QPalette.Window).lightness() < 128

        if is_focused:
            bg_color = palette.color(QPalette.Highlight).darker(130) if is_dark else palette.color(QPalette.Highlight).lighter(140)
            border_color = palette.color(QPalette.Highlight)
            text_color = palette.color(QPalette.HighlightedText) if is_dark else palette.color(QPalette.Highlight).darker(200)
            sub_text_color = palette.color(QPalette.HighlightedText) if is_dark else palette.color(QPalette.Highlight).darker(150)
        else:
            bg_color = palette.color(QPalette.AlternateBase) if palette.color(QPalette.AlternateBase).isValid() else palette.color(QPalette.Window)
            border_color = palette.color(QPalette.Mid)
            text_color = palette.color(QPalette.WindowText)
            sub_text_color = palette.color(QPalette.PlaceholderText)

        painter.setBrush(QBrush(bg_color))
        painter.setPen(QPen(border_color, 2 if is_focused else 1))
        painter.drawRoundedRect(rect.adjusted(2, 2, -2, -2), 8, 8)

        painter.setFont(QFont("sans-serif", 12, QFont.Bold if is_focused else QFont.Normal))
        painter.setPen(text_color)

        status_str = "⌨ KEYBOARD BRIDGE ACTIVE" if is_focused else "⌨ CLICK TO ACTIVATE KEYBOARD BRIDGE"
        sub_str = "Press physical/Bluetooth keys here to send to iOS target" if is_focused else "Click inside this box to capture host keyboard events"

        painter.drawText(rect.adjusted(0, -15, 0, 0), Qt.AlignCenter, status_str)

        painter.setFont(QFont("sans-serif", 10))
        painter.setPen(sub_text_color)
        painter.drawText(rect.adjusted(0, 25, 0, 0), Qt.AlignCenter, sub_str)

        if self.active_keys:
            active_str = "Held Keys: " + ", ".join(sorted(list(self.active_keys)))
            painter.setFont(QFont("monospace", 10))
            painter.setPen(palette.color(QPalette.Highlight) if not is_focused else text_color)
            painter.drawText(rect.adjusted(12, 10, -12, -10), Qt.AlignBottom | Qt.AlignLeft, active_str)

    def mousePressEvent(self, event):
        self.setFocus()
        self.update()

    def focusInEvent(self, event):
        self.update()

    def focusOutEvent(self, event):
        self.active_keys.clear()
        self.update()

    def keyPressEvent(self, event: QKeyEvent):
        if event.isAutoRepeat():
            return
        key_name = QT_KEY_MAP.get(event.key())
        if key_name:
            self.active_keys.add(key_name)
            self.update()
            self.key_event_signal.emit(key_name, "press")
        else:
            super().keyPressEvent(event)

    def keyReleaseEvent(self, event: QKeyEvent):
        if event.isAutoRepeat():
            return
        key_name = QT_KEY_MAP.get(event.key())
        if key_name:
            self.active_keys.discard(key_name)
            self.update()
            self.key_event_signal.emit(key_name, "release")
        else:
            super().keyReleaseEvent(event)


class RemoteControlApp(QMainWindow):
    """Main Application Window for iOS Remote Control."""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("Unreal-NG iOS Remote Controller (WebAPI PoC)")
        self.resize(960, 680)
        self.setAcceptDrops(True)

        self.current_emu_id: Optional[str] = None
        self._pending_create_model: Optional[str] = None
        self.workers: List[APIWorker] = []

        self.init_ui()

    def init_ui(self):
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_layout = QVBoxLayout(main_widget)
        main_layout.setContentsMargins(16, 16, 16, 16)
        main_layout.setSpacing(12)

        # 1. Connection & Machine Control Header
        conn_group = QGroupBox("Target Emulator Connection & Machine Config")
        conn_layout = QVBoxLayout(conn_group)
        conn_layout.setSpacing(8)

        # Row 1: Connection & Active Instance
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
        self.connect_btn.setObjectName("actionBtn")
        self.connect_btn.clicked.connect(self.fetch_status)
        conn_row1.addWidget(self.connect_btn)

        conn_row1.addSpacing(12)
        conn_row1.addWidget(QLabel("Instance:"))
        self.emu_combo = QComboBox()
        self.emu_combo.setMinimumWidth(200)
        self.emu_combo.currentIndexChanged.connect(self.on_emu_selected)
        conn_row1.addWidget(self.emu_combo)

        self.reset_btn = QPushButton("Reset Emulator")
        self.reset_btn.setToolTip("Trigger hardware reset (POST /api/v1/emulator/{id}/reset)")
        self.reset_btn.clicked.connect(self.reset_emulator)
        conn_row1.addWidget(self.reset_btn)
        conn_row1.addStretch()

        # Row 2: Machine Creation & Model Selection
        conn_row2 = QHBoxLayout()
        conn_row2.addWidget(QLabel("Machine Model:"))
        self.model_combo = QComboBox()
        self.model_combo.setMinimumWidth(220)
        self.model_combo.setToolTip("Select machine hardware model to create")
        conn_row2.addWidget(self.model_combo)

        self.create_emu_btn = QPushButton("Create & Switch Machine")
        self.create_emu_btn.setToolTip("Dispose current active machine instance and create a new instance with the selected model")
        self.create_emu_btn.clicked.connect(self.create_selected_machine)
        conn_row2.addWidget(self.create_emu_btn)
        conn_row2.addStretch()

        conn_layout.addLayout(conn_row1)
        conn_layout.addLayout(conn_row2)
        main_layout.addWidget(conn_group)

        # 2. Splitter (Controls + Log)
        splitter = QSplitter(Qt.Vertical)
        top_widget = QWidget()
        top_layout = QHBoxLayout(top_widget)
        top_layout.setContentsMargins(0, 0, 0, 0)

        # Left Column: Media Ingestion
        media_group = QGroupBox("Media Operations")
        media_layout = QVBoxLayout(media_group)

        self.tabs = QTabWidget()

        # Tab 1: Snapshots
        snap_tab = QWidget()
        snap_layout = QVBoxLayout(snap_tab)
        snap_file_layout = QHBoxLayout()
        self.snap_path_input = QLineEdit()
        self.snap_path_input.setPlaceholderText("Select .sna or .z80 snapshot...")
        snap_browse_btn = QPushButton("Browse...")
        snap_browse_btn.clicked.connect(lambda: self.browse_file(self.snap_path_input, "Snapshots (*.sna *.z80)"))
        snap_file_layout.addWidget(self.snap_path_input)
        snap_file_layout.addWidget(snap_browse_btn)

        self.snap_upload_btn = QPushButton("Push Snapshot & Run")
        self.snap_upload_btn.setObjectName("actionBtn")
        self.snap_upload_btn.clicked.connect(self.upload_snapshot)

        snap_layout.addLayout(snap_file_layout)
        snap_layout.addWidget(self.snap_upload_btn)
        snap_layout.addStretch()
        self.tabs.addTab(snap_tab, "Snapshots")

        # Tab 2: Disks
        disk_tab = QWidget()
        disk_layout = QVBoxLayout(disk_tab)

        disk_opts_layout = QHBoxLayout()
        self.autorun_cb = QCheckBox("Autorun Mode (Autostart)")
        self.autorun_cb.setChecked(True)
        self.autorun_cb.setToolTip("Automatically reset machine into TR-DOS and autorun disk on insertion")
        self.fastdisk_cb = QCheckBox("Fast Disk Load")
        self.fastdisk_cb.setChecked(True)
        self.fastdisk_cb.setToolTip("Enable fast disk I/O feature prior to insertion")
        disk_opts_layout.addWidget(self.autorun_cb)
        disk_opts_layout.addWidget(self.fastdisk_cb)
        disk_opts_layout.addStretch()

        disk_file_layout = QHBoxLayout()
        self.disk_path_input = QLineEdit()
        self.disk_path_input.setPlaceholderText("Select .trd, .scl, .dsk, or .fdi...")
        disk_browse_btn = QPushButton("Browse...")
        disk_browse_btn.clicked.connect(lambda: self.browse_file(self.disk_path_input, "Disks (*.trd *.scl *.dsk *.fdi)"))
        disk_file_layout.addWidget(self.disk_path_input)
        disk_file_layout.addWidget(disk_browse_btn)

        disk_ctrl_layout = QHBoxLayout()
        disk_ctrl_layout.addWidget(QLabel("Drive:"))
        self.drive_combo = QComboBox()
        self.drive_combo.addItems(["Drive A (0)", "Drive B (1)", "Drive C (2)", "Drive D (3)"])
        disk_ctrl_layout.addWidget(self.drive_combo)

        self.disk_insert_btn = QPushButton("Insert Disk")
        self.disk_insert_btn.setObjectName("actionBtn")
        self.disk_insert_btn.clicked.connect(self.insert_disk)
        self.disk_eject_btn = QPushButton("Eject Disk")
        self.disk_eject_btn.clicked.connect(self.eject_disk)
        disk_ctrl_layout.addWidget(self.disk_insert_btn)
        disk_ctrl_layout.addWidget(self.disk_eject_btn)

        disk_layout.addLayout(disk_opts_layout)
        disk_layout.addLayout(disk_file_layout)
        disk_layout.addLayout(disk_ctrl_layout)
        disk_layout.addStretch()
        self.tabs.addTab(disk_tab, "Disks")

        # Tab 3: Tapes
        tape_tab = QWidget()
        tape_layout = QVBoxLayout(tape_tab)
        tape_file_layout = QHBoxLayout()
        self.tape_path_input = QLineEdit()
        self.tape_path_input.setPlaceholderText("Select .tap or .tzx...")
        tape_browse_btn = QPushButton("Browse...")
        tape_browse_btn.clicked.connect(lambda: self.browse_file(self.tape_path_input, "Tapes (*.tap *.tzx)"))
        tape_file_layout.addWidget(self.tape_path_input)
        tape_file_layout.addWidget(tape_browse_btn)

        tape_ctrl_layout = QHBoxLayout()
        self.tape_insert_btn = QPushButton("Insert Tape")
        self.tape_insert_btn.setObjectName("actionBtn")
        self.tape_insert_btn.clicked.connect(self.insert_tape)
        self.tape_eject_btn = QPushButton("Eject Tape")
        self.tape_eject_btn.clicked.connect(self.eject_tape)
        tape_ctrl_layout.addWidget(self.tape_insert_btn)
        tape_ctrl_layout.addWidget(self.tape_eject_btn)

        tape_layout.addLayout(tape_file_layout)
        tape_layout.addLayout(tape_ctrl_layout)
        tape_layout.addStretch()
        self.tabs.addTab(tape_tab, "Tapes")

        media_layout.addWidget(self.tabs)
        top_layout.addWidget(media_group, 1)

        # Right Column: Active Keyboard Bridge & Quick Keys
        kbd_group = QGroupBox("Hardware Keyboard Bridge & Onscreen Controls")
        kbd_layout = QVBoxLayout(kbd_group)

        self.bridge_widget = KeyboardBridgeWidget()
        self.bridge_widget.key_event_signal.connect(self.on_keyboard_event)
        kbd_layout.addWidget(self.bridge_widget)

        # Quick Key Grid
        quick_grid = QGridLayout()
        quick_grid.setSpacing(6)
        keys_def = [
            ("Break (Caps+Space)", "break", 0, 0),
            ("Edit (Caps+1)", "edit", 0, 1),
            ("Caps Shift", "caps", 0, 2),
            ("Symbol Shift", "symbol", 0, 3),
            ("Delete", "delete", 1, 0),
            ("Enter", "enter", 1, 1),
            ("Space", "space", 1, 2),
            ("Release All", "release_all", 1, 3),
            ("▲ Up", "up", 2, 0),
            ("▼ Down", "down", 2, 1),
            ("◀ Left", "left", 2, 2),
            ("▶ Right", "right", 2, 3),
            ("⚡ Reset Hardware", "reset_machine", 3, 0),
        ]

        for label, key_code, r, c in keys_def:
            btn = QPushButton(label)
            btn.setObjectName("quickKeyBtn")
            btn.clicked.connect(lambda checked, k=key_code: self.send_quick_key(k))
            quick_grid.addWidget(btn, r, c)

        kbd_layout.addLayout(quick_grid)
        top_layout.addWidget(kbd_group, 1)

        splitter.addWidget(top_widget)

        # Log Console
        log_group = QGroupBox("WebAPI Activity & Event Log")
        log_layout = QVBoxLayout(log_group)
        self.log_console = QTextEdit()
        self.log_console.setReadOnly(True)
        log_layout.addWidget(self.log_console)

        splitter.addWidget(log_group)
        splitter.setSizes([420, 180])

        main_layout.addWidget(splitter)

        # Auto-connect on startup
        QTimer.singleShot(500, self.fetch_status)

    def log(self, message: str, level: str = "INFO"):
        ts = time.strftime("%H:%M:%S")
        is_dark = self.palette().color(QPalette.Window).lightness() < 128
        color = ("#38BDF8" if is_dark else "#0284C7") if level == "INFO" else ("#10B981" if is_dark else "#059669") if level == "SUCCESS" else ("#F43F5E" if is_dark else "#DC2626")
        ts_color = "#94A3B8" if is_dark else "#64748B"
        self.log_console.append(f'<span style="color: {ts_color};">[{ts}]</span> <span style="color: {color}; font-weight: bold;">[{level}]</span> {message}')

    def get_base_url(self) -> str:
        host = self.ip_input.text().strip()
        if not host or host.endswith("."):
            host = DEFAULT_HOST
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
                self.emu_combo.blockSignals(True)
                self.emu_combo.clear()
                for emu in data["emulators"]:
                    emu_id = emu.get("id", "")
                    model = emu.get("model", "")
                    state = emu.get("state", "")
                    self.emu_combo.addItem(f"{model} ({emu_id[:8]}...) - {state}", emu_id)
                self.emu_combo.blockSignals(False)

                if self.emu_combo.count() > 0:
                    idx = -1
                    if self.current_emu_id:
                        idx = self.emu_combo.findData(self.current_emu_id)
                    if idx < 0:
                        idx = 0
                    self.emu_combo.blockSignals(True)
                    self.emu_combo.setCurrentIndex(idx)
                    self.emu_combo.blockSignals(False)
                    self.current_emu_id = self.emu_combo.itemData(idx)
                    self.log(f"Connected to target: {len(data['emulators'])} instance(s). Activated #{self.current_emu_id[:8]}.", "SUCCESS")
                else:
                    self.current_emu_id = None
                    self.log("Connected to target: No active emulator instances running.", "INFO")
            else:
                self.emu_combo.clear()
                self.current_emu_id = None
                self.log(f"Failed to connect: {msg}", "ERROR")

        elif action == "fetch_models":
            if success and "models" in data:
                current_data = self.model_combo.currentData()
                self.model_combo.blockSignals(True)
                self.model_combo.clear()

                for model in data["models"]:
                    if model.get("creatable", True):
                        name = model.get("name", "")
                        full_name = model.get("full_name", "") or name
                        ram_sizes = model.get("available_ram_sizes_kb", [])
                        if not ram_sizes:
                            ram_sizes = [model.get("default_ram_kb", 0)]

                        if len(ram_sizes) > 1:
                            for ram in ram_sizes:
                                label = f"{full_name} {ram}K"
                                item_data = {"model": name, "ram_size": ram}
                                self.model_combo.addItem(label, item_data)
                        else:
                            ram = ram_sizes[0] if ram_sizes else 0
                            label = f"{full_name} {ram}K" if (ram and ram not in (48, 128)) else full_name
                            item_data = {"model": name, "ram_size": ram} if ram else {"model": name}
                            self.model_combo.addItem(label, item_data)

                self.model_combo.blockSignals(False)

                if self.model_combo.count() > 0:
                    idx = -1
                    if current_data:
                        for i in range(self.model_combo.count()):
                            if self.model_combo.itemData(i) == current_data:
                                idx = i
                                break
                    if idx < 0:
                        for i in range(self.model_combo.count()):
                            d = self.model_combo.itemData(i)
                            if isinstance(d, dict) and d.get("model") == "PENTAGON" and d.get("ram_size") == 128:
                                idx = i
                                break
                    if idx >= 0:
                        self.model_combo.setCurrentIndex(idx)

        elif action == "dispose_old_emu":
            self.log(f"Old emulator instance disposed ({msg}).", "INFO")
            if hasattr(self, "_pending_create_payload") and self._pending_create_payload:
                payload = self._pending_create_payload
                label = getattr(self, "_pending_create_label", "selected configuration")
                start_url = f"{self.get_base_url()}/api/v1/emulator/start"
                self.log(f"Starting new machine configuration '{label}'...", "INFO")
                self.run_async("start_new_emu", start_url, "POST", json_data=payload)

        elif action == "start_new_emu":
            if success and "id" in data:
                new_id = data["id"]
                label = getattr(self, "_pending_create_label", "selected configuration")
                self.current_emu_id = new_id
                self.log(f"Successfully created & started machine configuration '{label}' (#{new_id[:8]}).", "SUCCESS")
                self.fetch_status()
            else:
                self.log(f"Failed to create new machine configuration: {msg}", "ERROR")

        elif action == "enable_fastdisk":
            if success:
                self.log("Fast disk load feature enabled.", "SUCCESS")
            else:
                self.log(f"Failed to set fast disk option: {msg}", "ERROR")

        elif action == "reset_emulator":
            if success:
                self.log("Target emulator reset successfully.", "SUCCESS")
            else:
                self.log(f"Reset failed: {msg}", "ERROR")

        elif action.startswith("key_"):
            if not success:
                self.log(f"Keyboard event failed: {msg}", "ERROR")

        elif action == "insert_disk":
            if success:
                msg_extra = ""
                if data.get("autostarted"):
                    msg_extra = f" [{data.get('autostart_message', 'Autostarted')}]"
                self.log(f"Disk inserted successfully{msg_extra}: {msg}", "SUCCESS")
            else:
                self.log(f"Disk insert failed: {msg}", "ERROR")

        elif action in ("upload_snapshot", "eject_disk", "insert_tape", "eject_tape"):
            if success:
                self.log(f"{action} successful: {msg}", "SUCCESS")
            else:
                self.log(f"{action} failed: {msg}", "ERROR")

    def fetch_status(self):
        url = f"{self.get_base_url()}/api/v1/emulator"
        self.log(f"Connecting to WebAPI at {url}...")
        self.run_async("fetch_status", url, "GET")
        if self.model_combo.count() == 0:
            models_url = f"{self.get_base_url()}/api/v1/emulator/models"
            self.run_async("fetch_models", models_url, "GET")

    def create_selected_machine(self):
        data = self.model_combo.currentData()
        payload = {}
        if isinstance(data, dict):
            payload = dict(data)
        elif isinstance(data, str):
            payload = {"model": data}
        else:
            self.log("No machine model selected.", "ERROR")
            return

        display_label = self.model_combo.currentText()
        self._pending_create_payload = payload
        self._pending_create_label = display_label
        if self.current_emu_id:
            self.log(f"Disposing active machine instance #{self.current_emu_id[:8]}...", "INFO")
            dispose_url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}"
            self.run_async("dispose_old_emu", dispose_url, "DELETE")
        else:
            self.log(f"Creating & starting new machine configuration '{display_label}'...", "INFO")
            start_url = f"{self.get_base_url()}/api/v1/emulator/start"
            self.run_async("start_new_emu", start_url, "POST", json_data=payload)

    def on_emu_selected(self, index: int):
        if index >= 0 and index < self.emu_combo.count():
            self.current_emu_id = self.emu_combo.itemData(index)
            self.log(f"Selected emulator instance: {self.current_emu_id[:8]}", "INFO")

    def browse_file(self, target_line_edit: QLineEdit, filter_str: str):
        file_path, _ = QFileDialog.getOpenFileName(self, "Select File", "", filter_str)
        if file_path:
            target_line_edit.setText(file_path)

    # --- Media Operations ---

    def upload_snapshot(self):
        if not self.current_emu_id:
            self.log("No active emulator selected.", "ERROR")
            return
        path = self.snap_path_input.text().strip()
        if not path or not os.path.exists(path):
            self.log("Invalid snapshot file path.", "ERROR")
            return

        url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/snapshot/load"
        filename = os.path.basename(path)
        self.log(f"Uploading snapshot: {filename}...")
        with open(path, "rb") as f:
            raw_data = f.read()
        self.run_async("upload_snapshot", url, "POST", raw_body=raw_data, extra_headers={"X-Filename": filename})

    def insert_disk(self):
        if not self.current_emu_id:
            self.log("No active emulator selected.", "ERROR")
            return
        path = self.disk_path_input.text().strip()
        if not path or not os.path.exists(path):
            self.log("Invalid disk file path.", "ERROR")
            return
        drive_idx = self.drive_combo.currentIndex()

        # 1. Fast Disk Load (On by default)
        if self.fastdisk_cb.isChecked():
            fastdisk_url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/feature/fastdisk"
            self.run_async("enable_fastdisk", fastdisk_url, "POST", json_data={"enabled": True})

        # 2. Autorun Mode (On by default for Drive 0)
        extra_headers = {"X-Filename": os.path.basename(path)}
        if self.autorun_cb.isChecked() and drive_idx == 0:
            extra_headers["X-Autostart"] = "true"

        url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/disk/{drive_idx}/insert"
        filename = os.path.basename(path)
        opt_info = []
        if self.autorun_cb.isChecked() and drive_idx == 0:
            opt_info.append("Autorun")
        if self.fastdisk_cb.isChecked():
            opt_info.append("FastDisk")
        opt_str = f" ({', '.join(opt_info)})" if opt_info else ""

        self.log(f"Inserting disk {filename} into Drive {drive_idx}{opt_str}...")
        with open(path, "rb") as f:
            raw_data = f.read()
        self.run_async("insert_disk", url, "POST", raw_body=raw_data, extra_headers=extra_headers)

    def eject_disk(self):
        if not self.current_emu_id:
            return
        drive_idx = self.drive_combo.currentIndex()
        url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/disk/{drive_idx}/eject"
        self.run_async("eject_disk", url, "POST", json_data={})

    def insert_tape(self):
        if not self.current_emu_id:
            self.log("No active emulator selected.", "ERROR")
            return
        path = self.tape_path_input.text().strip()
        if not path or not os.path.exists(path):
            self.log("Invalid tape file path.", "ERROR")
            return

        url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/tape/load"
        filename = os.path.basename(path)
        self.log(f"Inserting tape {filename}...")
        with open(path, "rb") as f:
            raw_data = f.read()
        self.run_async("insert_tape", url, "POST", raw_body=raw_data, extra_headers={"X-Filename": filename})

    def eject_tape(self):
        if not self.current_emu_id:
            return
        url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/tape/eject"
        self.run_async("eject_tape", url, "POST", json_data={})

    def reset_emulator(self):
        if not self.current_emu_id:
            self.log("No active emulator selected.", "ERROR")
            return
        url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/reset"
        self.log(f"Resetting target emulator {self.current_emu_id[:8]}...", "INFO")
        self.run_async("reset_emulator", url, "POST", json_data={})

    # --- Keyboard Bridge ---

    @Slot(str, str)
    def on_keyboard_event(self, key_name: str, action: str):
        if not self.current_emu_id:
            return
        url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/keyboard/{action}"
        self.log(f"Key {action.upper()}: {key_name}", "INFO")
        self.run_async(f"key_{action}", url, "POST", json_data={"key": key_name})

    def send_quick_key(self, key_code: str):
        if not self.current_emu_id:
            return
        if key_code == "reset_machine":
            self.reset_emulator()
        elif key_code == "release_all":
            url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/keyboard/release_all"
            self.run_async("key_release_all", url, "POST", json_data={})
        else:
            url = f"{self.get_base_url()}/api/v1/emulator/{self.current_emu_id}/keyboard/tap"
            self.log(f"Quick Key TAP: {key_code}", "INFO")
            self.run_async("key_tap", url, "POST", json_data={"key": key_code, "hold_frames": 2})

    # --- Drag & Drop Support ---

    def dragEnterEvent(self, event: QDragEnterEvent):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            super().dragEnterEvent(event)

    def dragMoveEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            super().dragMoveEvent(event)

    def dropEvent(self, event: QDropEvent):
        if not event.mimeData().hasUrls():
            return

        urls = event.mimeData().urls()
        for url in urls:
            file_path = url.toLocalFile()
            if not file_path or not os.path.exists(file_path):
                continue

            ext = os.path.splitext(file_path)[1].lower()

            # 1. Snapshots (.sna, .z80)
            if ext in ('.sna', '.z80'):
                self.snap_path_input.setText(file_path)
                self.tabs.setCurrentIndex(0)
                self.log(f"Dropped snapshot file: {os.path.basename(file_path)}", "INFO")
                if self.current_emu_id:
                    self.upload_snapshot()
                else:
                    self.log("Snapshot path set. Connect to target emulator instance to push.", "INFO")

            # 2. Disks (.trd, .scl, .dsk, .fdi)
            elif ext in ('.trd', '.scl', '.dsk', '.fdi'):
                self.disk_path_input.setText(file_path)
                self.tabs.setCurrentIndex(1)
                self.log(f"Dropped disk image: {os.path.basename(file_path)}", "INFO")
                if self.current_emu_id:
                    self.insert_disk()
                else:
                    self.log("Disk path set. Connect to target emulator instance to insert.", "INFO")

            # 3. Tapes (.tap, .tzx)
            elif ext in ('.tap', '.tzx'):
                self.tape_path_input.setText(file_path)
                self.tabs.setCurrentIndex(2)
                self.log(f"Dropped tape image: {os.path.basename(file_path)}", "INFO")
                if self.current_emu_id:
                    self.insert_tape()
                else:
                    self.log("Tape path set. Connect to target emulator instance to insert.", "INFO")

            else:
                self.log(f"Unsupported file format dropped: {os.path.basename(file_path)} ({ext})", "ERROR")


def main():
    app = QApplication(sys.argv)
    window = RemoteControlApp()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
