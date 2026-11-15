#pragma once

#include <string>
#include <vector>

#include "emulator/emulatorcontext.h"

namespace unrealdisk { class DiskImage; }
using unrealdisk::DiskImage;
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
        BootServiceMenu, // ZX-Evo BaseConf: reset into the service ROM (ERS), its "TR-DOS boot" runs drive A
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

    /// The ZX-Evo EVO Reset Service (rom/zxevo-fe.rom) waits for a key at this PC (after its HALT, window 0 on ROM), in its main
    /// menu and in the boot list "TR-DOS boot" shows
    static constexpr uint16_t SERVICE_MENU_IDLE = 0x6117;

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

    /// Arm the same one-shot hook with an arbitrary, already tokenized TR-DOS command line (e.g. the
    /// bytes of `CAT`): TR-DOS cold-starts after Reset(RM_DOS) and executes it instead of RUN "boot".
    /// @p label is used for logging only
    void ArmCommand(const std::vector<uint8_t>& commandLine, const std::string& label);
    void Disarm();
    bool IsArmed() const { return _armed; }

    /// Every autostart (after the reset, before the CPU runs): the cold start's first arrival at the command loop
    /// hands the paging to the model (PortDecoder::FinishAutostartColdStart). Disarm() drops it too
    void ArmColdStartFinish() { _coldStartFinishPending = true; }
    bool IsColdStartFinishPending() const { return _coldStartFinishPending; }

    /// Whether HandleCommandLoopHook has anything left to do
    bool IsHookPending() const { return _armed || _coldStartFinishPending; }

    /// ZX-Evo BaseConf (BootServiceMenu): after the reset into the service ROM, the menu is driven as a user would:
    /// `1` (TR-DOS drive A, kept in the CMOS), ENTER on "TR-DOS boot", ENTER on the first file of the disk's boot list
    /// (boot when the disk has one). Gives up when the menu does not show within a few seconds (another RESET target)
    void ArmServiceMenuBoot();
    bool IsServiceMenuBootPending() const { return _serviceStage != ServiceStage::Idle; }

    /// Z80Step hook at PC == SERVICE_MENU_IDLE: the next key of the sequence once the menu has drawn
    void HandleServiceMenuIdle();

    /// Z80Step hook at PC == $02EF. Finishes the cold start for the model, then rewrites the cold-start RUN "boot" line
    /// into RUN "<name>". True when it changed the machine
    bool HandleCommandLoopHook(Z80& cpu);

private:
    enum class ServiceStage
    {
        Idle,
        SelectDrive,  // the main menu: `1` = TR-DOS drive A
        BootItem,     // ENTER on the first entry, "TR-DOS boot"
        RunFile,      // the boot list: ENTER on the selected file
    };

    /// Frames the menu gets to draw after a key before the next one (the ERS's scan sees each tap)
    static constexpr uint64_t SERVICE_KEY_GAP_FRAMES = 25;
    /// Frames after the reset before the sequence gives up (the RESET target in the CMOS may not be the menu)
    static constexpr uint64_t SERVICE_TIMEOUT_FRAMES = 500;

    EmulatorContext* _context = nullptr;
    ServiceStage _serviceStage = ServiceStage::Idle;
    uint64_t _serviceSince = 0;      // frame of the last key (or of the arming)
    uint64_t _serviceArmedAt = 0;
    bool _armed = false;
    bool _coldStartFinishPending = false;
    std::string _name;                // Log label (file name for BootNamed)
    std::vector<uint8_t> _commandLine;  // Tokenized line the hook writes over the cold-start RUN "boot"
};
