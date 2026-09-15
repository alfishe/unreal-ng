#!/usr/bin/env python3
"""
Service Monitor Mouse Detection Test

Tests Kempston mouse detection via the Scorpion ProfROM Service Monitor.
Uses WebAPI to control the emulator and verify mouse port decoding.
"""

import json
import sys
import time
import urllib.request

API_BASE = "http://localhost:8090/api/v1"


def api_get(path: str):
    """GET request to WebAPI."""
    try:
        with urllib.request.urlopen(f"{API_BASE}{path}") as resp:
            return json.load(resp)
    except Exception as e:
        print(f"GET {path} failed: {e}")
        return None


def api_post(path: str, data: dict = None):
    """POST request to WebAPI."""
    try:
        req = urllib.request.Request(
            f"{API_BASE}{path}",
            data=json.dumps(data).encode() if data else None,
            headers={"Content-Type": "application/json"} if data else {},
            method="POST"
        )
        with urllib.request.urlopen(req) as resp:
            return json.load(resp)
    except Exception as e:
        print(f"POST {path} failed: {e}")
        return None


def api_put(path: str, data: dict):
    """PUT request to WebAPI."""
    try:
        req = urllib.request.Request(
            f"{API_BASE}{path}",
            data=json.dumps(data).encode(),
            headers={"Content-Type": "application/json"},
            method="PUT"
        )
        with urllib.request.urlopen(req) as resp:
            return json.load(resp)
    except Exception as e:
        print(f"PUT {path} failed: {e}")
        return None


def get_emulator_id() -> str:
    """Get the first running emulator's ID."""
    result = api_get("/emulator")
    if result and "emulators" in result and result["emulators"]:
        return result["emulators"][0]["id"]
    return None


def read_memory(emu_id: str, addr: int, size: int = 1) -> list:
    """Read memory bytes."""
    result = api_get(f"/emulator/{emu_id}/memory/read/0x{addr:04X}?size={size}")
    return result.get("data", []) if result else []


def enable_porttrace(emu_id: str) -> bool:
    """Enable and start port trace feature."""
    api_put(f"/emulator/{emu_id}/feature/porttrace", {"enabled": True})
    api_post(f"/emulator/{emu_id}/profiler/porttrace/start")
    api_post(f"/emulator/{emu_id}/profiler/porttrace/clear")
    return True


def trigger_magic_nmi(emu_id: str) -> bool:
    """Trigger Scorpion Magic NMI to enter Service Monitor."""
    result = api_post(f"/emulator/{emu_id}/nmi", {"magic": True})
    return result and result.get("status") == "success"


def get_porttrace_events(emu_id: str, limit: int = 10000) -> list:
    """Get port trace events."""
    result = api_get(f"/emulator/{emu_id}/profiler/porttrace/events?limit={limit}")
    return result.get("events", []) if result else []


def check_e03b(emu_id: str) -> dict:
    """Read Service Monitor control byte E03B."""
    data = read_memory(emu_id, 0xE03B, 1)
    if not data:
        return {"raw": 0, "mouse": False, "joystick": False}
    val = data[0]
    return {
        "raw": val,
        "mouse": bool(val & 0x20),
        "joystick": bool(val & 0x40),
    }


def main():
    print("=== Service Monitor Mouse Detection Test ===\n")

    # Get emulator
    emu_id = get_emulator_id()
    if not emu_id:
        print("ERROR: No emulator found. Start emulator with Scorpion + ProfROM config.")
        sys.exit(1)
    print(f"Emulator ID: {emu_id}")

    # Enable port trace
    print("\nEnabling port trace...")
    enable_porttrace(emu_id)

    # Check initial state
    e03b = check_e03b(emu_id)
    print(f"E03B before NMI: 0x{e03b['raw']:02X}")

    # Trigger Magic NMI
    print("\nTriggering Magic NMI (Service Monitor)...")
    if not trigger_magic_nmi(emu_id):
        print("ERROR: Failed to trigger NMI")
        sys.exit(1)

    # Wait for Service Monitor to initialize and run probes
    time.sleep(2)

    # Check E03B after probe
    e03b = check_e03b(emu_id)
    print(f"\nE03B after NMI: 0x{e03b['raw']:02X}")
    print(f"  Mouse detected: {e03b['mouse']}")
    print(f"  Joystick detected: {e03b['joystick']}")

    # Get port trace events
    events = get_porttrace_events(emu_id)
    print(f"\nPort trace: {len(events)} events")

    # Find mouse port events (low byte 0xDF)
    mouse_ports = []
    for e in events:
        port = int(e["raw_port"], 16)
        if (port & 0xFF) == 0xDF:
            mouse_ports.append({
                "port": port,
                "value": e.get("value"),
                "device": e.get("device"),
                "pc": e.get("pc"),
            })

    print(f"Mouse port events (0x??DF): {len(mouse_ports)}")
    for mp in mouse_ports[:10]:
        print(f"  0x{mp['port']:04X} val={mp['value']} dev={mp['device']} PC={mp['pc']}")

    # Summary
    print("\n=== Summary ===")
    if e03b["mouse"]:
        print("PASS: Mouse detected by Service Monitor")
    else:
        print("FAIL: Mouse NOT detected")
        if len(mouse_ports) == 0:
            print("  - No mouse port reads in trace (port decode issue?)")
        else:
            print(f"  - {len(mouse_ports)} mouse port reads recorded")

    return 0 if e03b["mouse"] else 1


if __name__ == "__main__":
    sys.exit(main())
