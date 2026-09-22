#pragma once

#include <functional>
#include <string>

#include "stdafx.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/cpu/core.h"
#include "emulator/spectrumconstants.h"
#include "emulator/io/fdc/wd1793.h"

/// TRDOSTestHelper: drives the real TR-DOS ROM in an emulator for integration tests.
///
/// Commands are started the way disk autostart (drag'n'drop) does it: the machine is reset straight
/// into TR-DOS, TR-DOS cold-starts by itself, and a one-shot hook swaps its default `RUN "boot"` for the
/// requested command. There is no keyboard injection, no screen scraping and no BASIC prompt involved,
/// so the command is guaranteed to reach TR-DOS.
///
/// Execution is fully synchronous (MainLoop::RunFrame()): no emulator thread, no frame pacing, no
/// wall-clock waits. Every time limit is emulated time, so a test costs host CPU time only.
///
/// Typical use:
/// @code
///   TRDOSTestHelper trdos(emulator);
///   trdos.executeCommand("CAT");                       // fixed emulated-time budget
///   trdos.formatDisk();                                // FORMAT until TR-DOS reports completion
///   trdos.startCommand("FORMAT \"x\""); trdos.skipFormatTypePrompt();
///   trdos.runUntil([&] { return done(); }, 60 * Z80_FREQUENCY);   // custom completion / error checks
/// @endcode
///
/// Sound generation is switched off for the emulator when the helper is created: TR-DOS tests verify
/// disk and FDC behaviour, and emulating the sound chips costs a large share of the host CPU time
/// (pass muteSound = false to keep it).
///
/// The helper must be destroyed before the emulator (it removes the CPU hook it installs).
class TRDOSTestHelper
{
    /// region <Constants>
public:
    /// Default emulated-time budget of executeCommand(): ~3 emulated seconds
    static constexpr uint64_t MAX_EXECUTION_CYCLES = 10'000'000;

    /// Default emulated-time budget of a full 80T DS FORMAT: ~140 emulated seconds (it needs ~65)
    static constexpr uint64_t FORMAT_MAX_CYCLES = 500'000'000;

    /// Frames between two completion checks of runUntil(): 0.5 emulated second
    static constexpr unsigned DEFAULT_POLL_FRAMES = 25;
    /// endregion </Constants>

    /// region <Fields>
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    Z80* _z80 = nullptr;

    bool _promptHookInstalled = false;
    bool _formatPromptSkipped = false;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    /// @param emulator Emulator created through EmulatorTestHelper (registered with EmulatorManager) and
    ///        with a TR-DOS ROM set
    /// @param muteSound Disable sound generation (default) to save host CPU time
    explicit TRDOSTestHelper(Emulator* emulator, bool muteSound = true);
    virtual ~TRDOSTestHelper();

    TRDOSTestHelper(const TRDOSTestHelper&) = delete;
    TRDOSTestHelper& operator=(const TRDOSTestHelper&) = delete;
    /// endregion </Constructors / Destructors>

    /// region <Running TR-DOS commands>
public:
    /// Reset into TR-DOS and make its cold start execute @p command (any TR-DOS command line, e.g.
    /// "CAT" or "FORMAT \"NAME\""). Does not run the CPU - follow with runFrames() / runUntil()
    void startCommand(const std::string& command);

    /// Skip TR-DOS' interactive format-type prompt at $1EDD (selects 80T DS) so FORMAT runs unattended.
    /// The skipped CALL $3200 normally initialises the sector tables, they are supplied here.
    /// Call after startCommand(); the hook is removed by the destructor
    void skipFormatTypePrompt();

    /// True once skipFormatTypePrompt() actually fired (FORMAT reached the prompt)
    bool formatPromptSkipped() const { return _formatPromptSkipped; }

    /// startCommand() + run for a fixed emulated-time budget
    /// @return Emulated T-states executed, 0 when the helper is not usable
    uint64_t executeCommand(const std::string& command, uint64_t maxCycles = MAX_EXECUTION_CYCLES);

    /// FORMAT "testdisk" on the disk in drive A through the real ROM, until TR-DOS reports completion
    /// @param cyclesUsed Optional: emulated T-states executed
    /// @return true when the format completed within @p maxCycles
    bool formatDisk(uint64_t maxCycles = FORMAT_MAX_CYCLES, uint64_t* cyclesUsed = nullptr);
    /// endregion </Running TR-DOS commands>

    /// region <Execution control>
public:
    /// Run whole frames synchronously
    /// @return Emulated T-states executed
    uint64_t runFrames(unsigned frames);

    /// Run in steps of @p pollFrames until @p done returns true or @p maxCycles of emulated time passed
    /// @param cyclesUsed Optional: emulated T-states executed
    /// @return true when @p done returned true, false on emulated-time timeout
    bool runUntil(const std::function<bool()>& done, uint64_t maxCycles, unsigned pollFrames = DEFAULT_POLL_FRAMES,
                  uint64_t* cyclesUsed = nullptr);
    /// endregion </Execution control>

    /// region <State>
public:
    /// OCR of the current screen (32x24 text)
    std::string screenText() const;

    /// True when TR-DOS reports a finished FORMAT on screen (completion or the "repeat" prompt)
    bool formatFinishedOnScreen() const;

    /// Check if TR-DOS ROM is currently active
    bool isTRDOSActive() const;

    /// Verify TR-DOS system variables are initialized
    bool verifyTRDOSVariables() const;

    /// Get current TR-DOS error code (ERR_NR system variable)
    uint8_t getTRDOSError() const;

    /// Check if execution stopped (HALT)
    bool isExecutionStopped() const;
    /// endregion </State>

private:
    bool usable() const;
    void removePromptHook();
};
