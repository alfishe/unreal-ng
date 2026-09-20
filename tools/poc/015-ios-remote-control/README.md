# POC 015: iOS Remote Control & LAN Keyboard Bridge

## Goal & Purpose

This Proof of Concept (POC) investigates and demonstrates remote control of headless `unreal-ios` application instances over LAN via WebAPI (`:8090`).

It provides a desktop PySide6 application that allows users to:
1. Connect to any remote iOS target (e.g. iPad / iPhone running `UnrealNG` at `172.16.9.223:8090` or `127.0.0.1:8090`).
2. Enumerate active emulator instances and inspect runtime state.
3. Push media files (snapshots `.sna`/`.z80`, disks `.trd`/`.scl`/`.dsk`/`.fdi`, tapes `.tap`/`.tzx`) over LAN.
4. Active Keyboard Bridge: Intercept physical host keyboard key presses (or connected Bluetooth keyboard) and bridge them in real time to the remote iOS emulator core via WebAPI `MessageCenter` events (`MC_KEY_PRESSED` / `MC_KEY_RELEASED`).

---

## Technical Architecture

### WebAPI Endpoints Used

| Feature | HTTP Method | Endpoint | Payload / Parameters |
|---------|-------------|----------|----------------------|
| Instance Enumeration | `GET` | `/api/v1/emulator` | None |
| Reset Emulator | `POST` | `/api/v1/emulator/{id}/reset` | Empty JSON `{}` |
| Snapshot Push | `POST` | `/api/v1/emulator/{id}/snapshot/load` | JSON `{"path": "..."}` or Multipart file upload |
| Disk Insert | `POST` | `/api/v1/emulator/{id}/disk/{drive}/insert` | JSON `{"path": "..."}` or Multipart file upload |
| Disk Eject | `POST` | `/api/v1/emulator/{id}/disk/{drive}/eject` | Empty JSON `{}` |
| Tape Load | `POST` | `/api/v1/emulator/{id}/tape/load` | JSON `{"path": "..."}` or Multipart file upload |
| Tape Eject | `POST` | `/api/v1/emulator/{id}/tape/eject` | Empty JSON `{}` |
| Key Press | `POST` | `/api/v1/emulator/{id}/keyboard/press` | JSON `{"key": "<key_name>"}` |
| Key Release | `POST` | `/api/v1/emulator/{id}/keyboard/release` | JSON `{"key": "<key_name>"}` |
| Key Tap | `POST` | `/api/v1/emulator/{id}/keyboard/tap` | JSON `{"key": "<key_name>", "hold_frames": 2}` |
| Release All Keys | `POST` | `/api/v1/emulator/{id}/keyboard/release_all` | Empty JSON `{}` |

---

## Key Translation Matrix

The `KeyboardBridgeWidget` translates host Qt key codes into ZX Spectrum key names recognized by `DebugKeyboardManager::ResolveKeyName`:

| Host Key (Qt / Physical) | ZX Spectrum Key Name | Target MessageCenter Event |
|-------------------------|---------------------|----------------------------|
| `A` – `Z` | `"a"` – `"z"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `0` – `9` | `"0"` – `"9"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `Space` | `"space"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `Return` / `Enter` | `"enter"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `Backspace` | `"delete"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `Shift` (Left/Right) | `"caps"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `Ctrl` / `Alt` / `Cmd` | `"symbol"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `Up Arrow` | `"up"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `Down Arrow` | `"down"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `Left Arrow` | `"left"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `Right Arrow` | `"right"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `Escape` | `"break"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |
| `Tab` | `"edit"` | `MC_KEY_PRESSED` / `MC_KEY_RELEASED` |

---

## Running the POC

```bash
python3 tools/poc/015-ios-remote-control/main.py
```

### Usage Instructions
1. Enter the target iOS LAN IP (e.g., `172.16.9.223` or `127.0.0.1`) and Port (`8090`).
2. Click **Connect / Refresh**. The application queries `GET /api/v1/emulator` and populates active instances.
3. Click inside the **Keyboard Bridge Widget**. The border highlights indicating active focus.
4. Press physical keys on your computer keyboard or Bluetooth keyboard. Key press/release events are dispatched to the target via WebAPI and processed by the core via `MessageCenter` (`MC_KEY_PRESSED` / `MC_KEY_RELEASED`).
5. **Media Drag & Drop / Upload**: Drop any snapshot (`.sna`/`.z80`), disk (`.trd`/`.scl`/`.dsk`/`.fdi`), or tape (`.tap`/`.tzx`) directly anywhere on the application window. The app automatically detects the format, switches tabs, populates the file path, and uploads/inserts it into the active emulator instance.
