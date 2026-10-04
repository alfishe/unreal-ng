#pragma once

/// @file imousesink.h
/// @brief An emulated mouse device as the mouse manager sees it
/// (docs/inprogress/2026-10-02-mouse-manager/design.md §3.1).
///
/// The manager calls a sink on the emulator thread, at an instruction
/// boundary, once per input. Every sink of the machine gets every input: a
/// machine with two mouse views (Sprinter: the Kempston port and the serial
/// mouse) gets one mouse, seen two ways.

#include <cstdint>

#include "emulator/io/mouse/mousedevicestatus.h"

class IMouseSink
{
public:
    virtual ~IMouseSink() = default;

    /// Fitted: a program can read this device. The front end captures the host
    /// mouse only when some sink of the machine is fitted
    virtual bool IsMouseFitted() const = 0;

    /// In use: a program is using this device right now. A fitted device may be unused
    /// (the 128K ROM never reads the mouse) or shadowed (the Kempston mouse ports belong
    /// to Beta Disk while TR-DOS is active). The front end captures the host mouse only
    /// while some sink is in use; asked at the click and while captured. Devices that
    /// cannot tell are in use whenever fitted
    virtual bool IsMouseInUse() const { return IsMouseFitted(); }
    /// A device that can tell counts as polled when a program read it within this many frames
    /// (EmulatorState::frame_counter), about a second
    static constexpr uint64_t kPolledWithinFrames = 50;

    /// Motion in emulated pixels: dx > 0 = right, dy > 0 = up
    virtual void OnMouseMotion(int dx, int dy) = 0;
    /// The buttons held now, active low: D0 left, D1 right, D2 middle (0 = pressed)
    virtual void OnMouseButtons(uint8_t activeLowMask) = 0;
    /// Wheel notches: > 0 = away from the user
    virtual void OnMouseWheel(int steps) = 0;
    /// Debug write of the Kempston X / Y counters (automation `counters`);
    /// devices without such counters ignore it
    virtual void OnMouseCounters([[maybe_unused]] uint8_t x, [[maybe_unused]] uint8_t y) {}

    /// The machine's ports read this device. False for a Kempston interface object on a machine whose
    /// mouse ports read its own board mouse: the manager skips it (not offered, not counted as fitted)
    virtual bool IsMouseWired() const { return true; }

    /// region <What the automation sees (docs/inprogress/2026-10-03-mouse-api-routing/design.md)>
    /// The device as automation status reports it: id, fitted, the registers and the
    /// device's own line or queue. Debug read: does not count as polling
    virtual MouseDeviceStatus DescribeMouse() const
    {
        MouseDeviceStatus status;
        status.id = "mouse";
        status.name = "mouse";
        status.fitted = IsMouseFitted();
        status.inUse = IsMouseInUse();
        return status;
    }
    /// Motion applied that the program has not taken yet (Kempston: X or Y not read since the move;
    /// serial: a packet on the wire or motion not yet in one). Automation glides wait for it
    /// before the next step, so an 8-bit counter never jumps by more than one step between reads
    virtual bool HasUnreadMotion() const { return false; }
    /// The largest move (emulated pixels, per axis) a program can tell from a move the other way
    /// between two reads: 127 for an 8-bit counter; less when the device scales motion (a PS/2
    /// mouse at a finer resolution). Automation glides step by at most this much
    virtual int MotionStepLimit() const { return 127; }
    /// endregion
};
