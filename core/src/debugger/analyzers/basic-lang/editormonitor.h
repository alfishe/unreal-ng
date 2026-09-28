#pragma once

#include "stdafx.h"

#include "debugger/analyzers/basic-lang/romcontrolpoints.h"
#include "debugger/analyzers/ianalyzer.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class EmulatorContext;

/// Records the ROM editor's control points as the CPU executes them: the
/// cyclogram of input-verification.md (next to this file).
///
/// An analyzer on the debugger's breakpoint mechanism: Arm() activates it and
/// it requests one silent, page-specific execution breakpoint per control point
/// of every identified ROM page (no pause, no UI); Disarm() deactivates it and
/// the breakpoints go with it. Nothing is added to the CPU's per-instruction
/// path: while disarmed the monitor costs nothing, while armed the cost is the
/// breakpoint check the debugger already does. Debug mode, if the arming had to
/// switch it on, is switched off again on Disarm().
///
/// Page-specific breakpoints fire only while their ROM page is at #0000, so
/// the same address in another ROM, or RAM paged there, records nothing.
///
/// A point hit again right after itself (the editor's idle loop spins through
/// its point thousands of times a frame) is one event with a repeat count.
///
/// Runs on the thread that steps the CPU. Events are read by code on that same
/// thread (the command typer's frame step, synchronous tests).
class EditorMonitor : public IAnalyzer
{
public:
    static constexpr const char* ANALYZER_ID = "editor-input";

    struct Event
    {
        uint64_t frame = 0;          ///< emulatorState.frame_counter
        uint32_t tstate = 0;         ///< T-state within the frame
        ROMControlPoints::Point point = ROMControlPoints::Point::Count;
        ROMControlPoints::RomKind rom = ROMControlPoints::RomKind::Unknown;
        uint16_t pc = 0;
        uint8_t a = 0;               ///< key code / inserted byte at the points that carry one
        uint8_t l = 0;               ///< error code at ERROR-3
        uint8_t lastK = 0;           ///< LAST_K  ($5C08)
        uint8_t flags = 0;           ///< FLAGS   ($5C3B)
        uint8_t errNr = 0;           ///< ERR_NR  ($5C3A)
        uint32_t repeats = 0;        ///< further hits of the same point right after this one
        uint64_t lastFrame = 0;      ///< frame of the last of them
    };

    /// Events kept per arming; a verified command needs a few hundred
    static constexpr size_t MAX_EVENTS = 8192;

    explicit EditorMonitor(EmulatorContext* context);

    /// Activates the analyzer: the control-point breakpoints go in
    void Arm();
    /// Deactivates it: the breakpoints go, debug mode is restored
    void Disarm();
    bool IsArmed() const { return _armed; }

    /// ROM family paged at #0000 now (Unknown for RAM or an unknown ROM)
    ROMControlPoints::RomKind CurrentRom();

    const std::vector<Event>& Events() const { return _events; }
    bool Overflowed() const { return _overflowed; }
    void ClearEvents();

    /// region <IAnalyzer>
    void onActivate(AnalyzerManager* manager) override;
    void onDeactivate() override;
    void onBreakpointHit(uint16_t address, Z80* cpu) override;
    std::string getName() const override { return "Editor input monitor"; }
    std::string getUUID() const override { return ANALYZER_ID; }
    /// endregion </IAnalyzer>

private:
    static uint32_t Key(uint8_t page, uint16_t address) { return (static_cast<uint32_t>(page) << 16) | address; }

    /// Identifies every ROM page once (the images do not change at run time)
    void IdentifyRomPages();
    int CurrentRomPage() const;

    EmulatorContext* _context = nullptr;
    bool _armed = false;

    std::vector<ROMControlPoints::RomKind> _romPages;  ///< per ROM page, filled on first arming
    std::unordered_map<uint32_t, const ROMControlPoints::PointDef*> _points;  ///< (page, address) -> point

    bool _restoreBreakpoints = false;  ///< arming switched the breakpoints feature on
    bool _restoreDebugMode = false;    ///< ...and debug mode with it

    std::vector<Event> _events;
    bool _overflowed = false;
};
