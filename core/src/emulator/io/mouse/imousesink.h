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
};
