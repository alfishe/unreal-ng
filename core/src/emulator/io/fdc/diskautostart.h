#pragma once

#include <string>

#include "emulator/emulatorcontext.h"

class DiskImage;
class Z80;

/// TR-DOS disk autostart: decides what to run from a mounted disk and starts it without key injection.
///
/// Mechanism (design: docs/inprogress/2026-09-18-trdos-autostart):
///  - the machine is reset straight into TR-DOS (reset ROM mode RM_DOS, PC = 0), TR-DOS cold-starts by
///    itself and executes RUN "boot";
///  - for a disk with a single BASIC program the cold-start line is rewritten by a one-shot hook at $02EF
///    into RUN "<name>" (no disk changes at all);
///  - for a disk with several BASIC programs and no boot file the bundled commander is injected as a
///    virtual boot (image stays marked unmodified)
class DiskAutostart
{
public:
    enum class Action
    {
        MountOnly,       // Nothing to start (or nothing we can start): the disk is just mounted
        Boot,            // Disk has boot.B: reset into TR-DOS, TR-DOS runs it
        BootNamed,       // Single BASIC program: RUN "<name>" through the one-shot hook
        BootCommander,   // Several BASIC programs: bundled commander injected as boot.B
        BootGenerated,   // Fallback: generated one-line boot.B runs the single BASIC program
        Unsupported      // Machine cannot run TR-DOS
    };

    struct Plan
    {
        Action action = Action::MountOnly;
        std::string bootName;  // BootNamed / BootGenerated: trimmed BASIC file name
        std::string message;   // Human readable outcome (HUD / log)
    };

    /// ROM offsets of the bare-RUN "boot" line builder shared by all TR-DOS ROMs
    static constexpr uint16_t BOOT_LINE_BUILDER = 0x027B;
    static constexpr uint16_t COMMAND_LOOP_ENTRY = 0x02EF;

public:
    explicit DiskAutostart(EmulatorContext* context);

    /// True when the machine has a TR-DOS ROM set and the Beta interface enabled
    bool IsTrdosCapable(std::string* reason = nullptr) const;

    /// True when the TR-DOS ROM carries the known boot line builder at $027B (the hook can be applied)
    bool IsNameHookSupported() const;

    /// Decide what to do with the image. Does not modify anything
    Plan MakePlan(DiskImage& image) const;

    /// MakePlan + inject the boot file when required. The emulator must be paused. Falls back
    /// (commander -> named -> generated) when a step is impossible; the returned plan is what will run
    Plan Prepare(DiskImage& image);

    /// Arm the one-shot hook for BootNamed (call after the reset, before the CPU runs)
    void Arm(const std::string& name);
    void Disarm();
    bool IsArmed() const { return _armed; }

    /// Z80Step hook at PC == $02EF. Rewrites the cold-start RUN "boot" line into RUN "<name>"
    bool HandleCommandLoopHook(Z80& cpu);

private:
    EmulatorContext* _context = nullptr;
    bool _armed = false;
    std::string _name;
};
