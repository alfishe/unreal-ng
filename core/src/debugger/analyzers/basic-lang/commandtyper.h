#pragma once

#include "stdafx.h"

#include "debugger/analyzers/basic-lang/editormonitor.h"
#include "debugger/analyzers/basic-lang/romcontrolpoints.h"
#include "debugger/analyzers/basic-lang/zxkeydecoder.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <string>
#include <vector>

class Emulator;
class EmulatorContext;

/// Types a command into the ROM editor and proves every step
/// (input-verification.md §5-§8, next to this file).
///
/// All input goes through the keyboard matrix like a person's. The typer
/// watches the editor's control points (EditorMonitor) and never guesses a
/// delay: it waits for the editor to be idle, holds each key until the ROM has
/// taken exactly the expected code, checks the byte the editor inserted, and
/// after ENTER reports what the ROM did with the line. Anything else stops it
/// with a named failure and the cyclogram as evidence.
///
/// Threading: Request() may be called from any thread; everything else runs
/// in OnFrame() on the thread that runs the frames (MainLoop frame end), which
/// is the thread the monitor records on.
class CommandTyper
{
public:
    enum class Status : uint8_t
    {
        Idle,       ///< nothing requested
        Running,    ///< typing or waiting for the outcome
        Done,       ///< finished; see Result
    };

    /// What the ROM did with the line
    enum class Outcome : uint8_t
    {
        None,
        Typed,        ///< typed without ENTER (pressEnter = false)
        Started,      ///< direct command started running
        Finished,     ///< ...and reported (waitForReport); ERR_NR has the report
        Stored,       ///< numbered line stored in the program
        SyntaxError,  ///< the editor rejected the line: nothing ran
        TrDosCommand, ///< TR-DOS took the line as one of its commands
        Failed,       ///< see Failure
    };

    enum class Failure : uint8_t
    {
        None,
        InputLocked,    ///< TTD replay owns the keyboard
        KeyboardBusy,   ///< other automation is still typing
        UnknownTarget,  ///< no known editor ROM at #0000
        Unsupported,    ///< an editor we identify but have not verified (+3)
        Busy,           ///< the editor never became idle: something else runs
        CannotType,     ///< no key gives the needed byte in the editor's mode
        NotTaken,       ///< a held key was never taken by the ROM
        WrongKey,       ///< the ROM took a different code than intended
        WrongChar,      ///< the editor inserted a different byte
        NoEffect,       ///< the key was taken but nothing happened, twice
        Rejected,       ///< the editor refused the key (error beep)
        LineFull,       ///< the edit line is full
        NoOutcome,      ///< ENTER was taken but no result followed
        TrDosRejected,  ///< TR-DOS: not a command / syntax error
        LineNotEmpty,   ///< the edit line holds text that could not be cleared
        EmulatorPaused, ///< frames are not running: nothing can be typed or proven
        TimedOut,       ///< TypeAndWait gave up waiting (wall clock)
        Aborted,
    };

    struct Options
    {
        bool pressEnter = true;         ///< false: type the line and leave it
        bool waitForReport = false;     ///< after Started, wait for the report
        uint16_t idleFrames = 150;      ///< for the editor to be idle before a key
        uint16_t takenFrames = 60;      ///< for a held key to be taken (covers DI stretches)
        uint16_t effectFrames = 25;     ///< for a taken key's effect
        uint16_t outcomeFrames = 100;   ///< for ENTER's outcome
        uint32_t reportFrames = 3000;   ///< for the report after Started (waitForReport)
    };

    struct Result
    {
        Outcome outcome = Outcome::None;
        Failure failure = Failure::None;
        std::string message;            ///< plain-language detail, empty on success
        ROMControlPoints::RomKind editor = ROMControlPoints::RomKind::Unknown;
        bool trdos = false;             ///< the line went to the TR-DOS prompt
        uint8_t errNr = 0xFF;           ///< ERR_NR at the outcome (report = errNr + 1)
        size_t bytesTyped = 0;          ///< bytes confirmed inserted
        uint64_t frames = 0;            ///< frames from start to outcome
        std::vector<EditorMonitor::Event> cyclogram;
    };

    explicit CommandTyper(EmulatorContext* context);

    /// Starts typing `command` (plain text, e.g. `LOAD ""`). False when a
    /// command is still running.
    bool Request(const std::string& command, const Options& options);
    bool Request(const std::string& command) { return Request(command, Options{}); }

    /// Advances the typer by one frame; from MainLoop at frame end
    void OnFrame();

    void Abort();
    Status GetStatus() const;
    Result GetResult() const;

    /// For automation threads (WebAPI, CLI): requests `command` on the running
    /// emulator and waits for the outcome, at most `timeoutMs` of wall-clock
    /// time. The emulator must be running: a paused machine runs no frames, so
    /// the result is EmulatorPaused at once. Never call it on the thread that
    /// runs the frames.
    static Result TypeAndWait(Emulator& emulator, const std::string& command, const Options& options,
                              uint32_t timeoutMs = 30000);

    /// True for the outcomes where the ROM did what was asked
    static bool Succeeded(const Result& result);

    static const char* OutcomeName(Outcome outcome);
    static const char* FailureName(Failure failure);

private:
    enum class Step : uint8_t
    {
        Start,          ///< arm the monitor, check preconditions
        WaitIdle,       ///< editor idle, then plan and press the next key
        Hold,           ///< keys held until taken
        Effect,         ///< released; wait for what the editor did
        Outcome,        ///< ENTER taken; wait for the result
        Report,         ///< Started; wait for the report
    };

    // Planned key for the current byte
    struct Planned
    {
        std::vector<ZXKeysEnum> keys;
        uint8_t code = 0;          ///< code the ROM must take
        bool modeKey = false;      ///< E mode switch: effect is MODE = 1
        bool editKey = false;      ///< cursor / delete while clearing the line
        bool enter = false;
        uint8_t insert = 0;        ///< byte the editor must insert
    };

    void Step_();
    bool ConsumeEvents();          // false when finished
    void Fail(Failure failure, const std::string& message);
    void Succeed(Outcome outcome);
    bool PlanNext();
    /// Next key that clears the edit line (cursor right to its end, then
    /// DELETE); nullopt when the line is empty. Fails when it cannot tell.
    std::optional<std::pair<ZXKeysEnum, uint8_t>> NextClearKey(bool& failed);
    void PressPlanned();
    void ReleaseHeld();
    const uint8_t* Basic48Page();
    std::vector<uint8_t> BytesFor(ROMControlPoints::RomKind editor) const;
    uint8_t SysVar(uint16_t address) const;

    EmulatorContext* _context = nullptr;
    mutable std::mutex _mutex;

    // Request (under _mutex)
    bool _requested = false;
    bool _abortRequested = false;
    std::string _command;
    Options _options;

    // Run state (emulator thread)
    Status _status = Status::Idle;
    Step _step = Step::Start;
    Result _result;
    std::vector<uint8_t> _bytes;   // what the editor must receive
    size_t _next = 0;              // index into _bytes; _bytes.size() = ENTER
    Planned _planned;
    std::vector<ZXKeysEnum> _held;
    size_t _seen = 0;              // monitor events processed
    uint32_t _stepFrames = 0;      // frames in the current step
    uint64_t _startFrame = 0;
    int _retries = 0;
    bool _clearing = true;
    int _clearKeys = 0;
    bool _sawIdle = false;
    bool _sawTaken = false;
};
