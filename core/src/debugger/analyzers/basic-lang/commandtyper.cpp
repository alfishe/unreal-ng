#include "commandtyper.h"

#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

#include <chrono>
#include <thread>
#include "emulator/memory/memory.h"

using ROMControlPoints::Point;
using ROMControlPoints::RomKind;

namespace
{
constexpr uint16_t SYS_FLAGS = 0x5C3B;
constexpr uint16_t SYS_MODE = 0x5C41;
constexpr uint16_t SYS_FLAGS2 = 0x5C6A;
constexpr uint8_t CODE_ENTER = 0x0D;
constexpr uint8_t CODE_EXTEND = 0x0E;
constexpr int MAX_NO_EFFECT_RETRIES = 2;
constexpr int MAX_CLEAR_KEYS = 600;
constexpr uint8_t CODE_RIGHT = 0x09;
constexpr uint8_t CODE_DELETE = 0x0C;
constexpr uint16_t SYS_E_LINE = 0x5C59;
constexpr uint16_t SYS_K_CUR = 0x5C5B;

// 128K editor workspace in RAM bank 7 (#C000-based offsets)
constexpr size_t EDITOR128_SLEB = 0xEC16 - 0xC000;        // screen line edit buffer, 35-byte rows
constexpr size_t EDITOR128_ROW_BYTES = 35;                 // 32 characters + 3 data bytes
constexpr size_t EDITOR128_CURSOR_ROW = 0xF6EE - 0xC000;
constexpr size_t EDITOR128_CURSOR_COL = 0xF6EF - 0xC000;
constexpr uint8_t EDITOR128_SINGLE_ROW = 0x09;             // first + last row of a line
constexpr size_t EDITOR128_MENU_INDEX = 0xEC0C - 0xC000;    // highlighted menu item
constexpr size_t EDITOR128_FLAGS = 0xEC0D - 0xC000;         // bit 1: a menu is shown
constexpr uint8_t MENU_ITEM_128_BASIC = 1;                  // 128K, Pentagon and Scorpion menus
constexpr uint8_t CODE_DOWN = 0x0A;
constexpr uint8_t CODE_UP = 0x0B;

std::string Hex(uint8_t value)
{
    char text[4];
    snprintf(text, sizeof(text), "#%02X", value);
    return text;
}
} // anonymous namespace

CommandTyper::CommandTyper(EmulatorContext* context) : _context(context)
{
}

bool CommandTyper::Request(const std::string& command, const Options& options)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_requested || _status == Status::Running)
        return false;

    _command = command;
    _options = options;
    _requested = true;
    _abortRequested = false;
    _status = Status::Running;
    _result = Result();
    return true;
}

void CommandTyper::Abort()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_status == Status::Running)
        _abortRequested = true;
}

CommandTyper::Status CommandTyper::GetStatus() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _status;
}

CommandTyper::Result CommandTyper::GetResult() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _result;
}

uint8_t CommandTyper::SysVar(uint16_t address) const
{
    return _context->pMemory->DirectReadFromZ80Memory(address);
}

const uint8_t* CommandTyper::Basic48Page()
{
    // The keyboard routine and its tables live in the 48 BASIC ROM on every
    // machine (the 128K and +3 editors call it there)
    Memory* memory = _context->pMemory;
    for (int page = 0; page < 64; page++)
    {
        const uint8_t* bytes = memory->ROMPageHostAddress(static_cast<uint8_t>(page));
        if (bytes != nullptr && ROMControlPoints::Identify(bytes) == RomKind::Basic48)
            return bytes;
    }
    return nullptr;
}

std::vector<uint8_t> CommandTyper::BytesFor(RomKind editor) const
{
    // The 48K editor stores keywords as tokens, entered with one key; the
    // 128K editor stores the letters and tokenises at ENTER
    if (editor == RomKind::Basic48)
        return BasicEncoder::tokenizeImmediate(_command);
    return std::vector<uint8_t>(_command.begin(), _command.end());
}

void CommandTyper::Fail(Failure failure, const std::string& message)
{
    ReleaseHeld();
    _result.outcome = Outcome::Failed;
    _result.failure = failure;
    _result.message = message;
    Succeed(Outcome::Failed);
}

void CommandTyper::Succeed(Outcome outcome)
{
    ReleaseHeld();
    EditorMonitor* monitor = _context->pDebugManager ? _context->pDebugManager->GetEditorMonitor() : nullptr;
    if (monitor != nullptr)
    {
        _result.cyclogram = monitor->Events();
        monitor->Disarm();
    }
    _result.outcome = outcome;
    _result.frames = _context->emulatorState.frame_counter - _startFrame;
    _status = Status::Done;
}

void CommandTyper::ReleaseHeld()
{
    DebugKeyboardManager* keys = _context->pDebugManager ? _context->pDebugManager->GetKeyboardManager() : nullptr;
    if (keys != nullptr)
    {
        // Reverse order: the shift last, so a lone key never shows unshifted
        for (auto it = _held.rbegin(); it != _held.rend(); ++it)
            keys->ReleaseKey(*it);
    }
    _held.clear();
}

std::optional<std::pair<ZXKeysEnum, uint8_t>> CommandTyper::NextClearKey(bool& failed)
{
    failed = false;
    Memory& memory = *_context->pMemory;

    if (_result.editor == RomKind::Basic48)
    {
        // 48K editor: E_LINE .. #0D is the line, K_CUR the cursor in it
        const uint16_t eLine = static_cast<uint16_t>(SysVar(SYS_E_LINE) | (SysVar(SYS_E_LINE + 1) << 8));
        const uint16_t kCur = static_cast<uint16_t>(SysVar(SYS_K_CUR) | (SysVar(SYS_K_CUR + 1) << 8));
        if (memory.DirectReadFromZ80Memory(eLine) == CODE_ENTER)
            return std::nullopt;

        uint16_t end = eLine;
        for (int i = 0; i < 0x4000 && memory.DirectReadFromZ80Memory(end) != CODE_ENTER; i++)
            end++;
        if (kCur < end)
            return std::make_pair(ZXKEY_EXT_RIGHT, CODE_RIGHT);
        return std::make_pair(ZXKEY_EXT_DELETE, CODE_DELETE);
    }

    if (_result.editor == RomKind::Editor128)
    {
        const uint8_t* bank7 = memory.RAMPageAddress(7);

        // The main menu reads keys in the same loop as the editor: leave it
        // for 128 BASIC first (cursor to the item, ENTER)
        if (bank7[EDITOR128_FLAGS] & 0x02)
        {
            const uint8_t item = bank7[EDITOR128_MENU_INDEX];
            if (item < MENU_ITEM_128_BASIC)
                return std::make_pair(ZXKEY_EXT_DOWN, CODE_DOWN);
            if (item > MENU_ITEM_128_BASIC)
                return std::make_pair(ZXKEY_EXT_UP, CODE_UP);
            return std::make_pair(ZXKEY_ENTER, CODE_ENTER);
        }

        // 128K editor: the cursor row of the screen line edit buffer in bank 7
        const uint8_t row = bank7[EDITOR128_CURSOR_ROW];
        const uint8_t col = bank7[EDITOR128_CURSOR_COL];
        if (row >= 21 || col > 32)
        {
            failed = true;
            return std::nullopt;
        }
        const uint8_t* chars = bank7 + EDITOR128_SLEB + row * EDITOR128_ROW_BYTES;
        if (chars[32] != EDITOR128_SINGLE_ROW)
        {
            failed = true;  // a line over several rows: not handled
            return std::nullopt;
        }
        int end = 0;
        for (int i = 0; i < 32; i++)
        {
            if (chars[i] != 0)
                end = i + 1;
        }
        if (end == 0)
            return std::nullopt;
        if (col < end)
            return std::make_pair(ZXKEY_EXT_RIGHT, CODE_RIGHT);
        return std::make_pair(ZXKEY_EXT_DELETE, CODE_DELETE);
    }

    return std::nullopt;
}

bool CommandTyper::PlanNext()
{
    _planned = Planned();

    // First make sure we type into an empty line, not after someone's text
    if (_clearing)
    {
        bool failed = false;
        const auto key = NextClearKey(failed);
        if (failed)
        {
            Fail(Failure::LineNotEmpty, "the edit line holds text that cannot be cleared safely");
            return false;
        }
        if (key)
        {
            if (++_clearKeys > MAX_CLEAR_KEYS)
            {
                Fail(Failure::LineNotEmpty, "the edit line did not get empty");
                return false;
            }
            _planned.keys = { key->first };
            _planned.code = key->second;
            _planned.editKey = true;
            return true;
        }
        _clearing = false;
    }

    if (_next >= _bytes.size())
    {
        if (!_options.pressEnter)
        {
            Succeed(Outcome::Typed);
            return false;
        }
        _planned.keys = { ZXKEY_ENTER };
        _planned.code = CODE_ENTER;
        _planned.enter = true;
        return true;
    }

    const uint8_t wanted = _bytes[_next];
    ZXKeyDecoder::EditorState state;
    state.mode = SysVar(SYS_MODE);
    state.flags = SysVar(SYS_FLAGS);
    state.flags2 = SysVar(SYS_FLAGS2);

    const std::optional<ZXKeyDecoder::Press> press = ZXKeyDecoder::Find(Basic48Page(), wanted, state);
    if (!press)
    {
        const char* mode = state.mode == 1 ? "E" : (state.mode == 2 ? "G" : ((state.flags & 0x08) ? "L" : "K"));
        Fail(Failure::CannotType, "no key gives " + Hex(wanted) + " at position " + std::to_string(_next) +
                                      " in " + mode + " mode");
        return false;
    }

    if (press->extendedFirst)
    {
        _planned.keys = { ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT };
        _planned.code = CODE_EXTEND;
        _planned.modeKey = true;
        return true;
    }

    _planned.keys = press->keys;
    _planned.code = wanted;
    _planned.insert = wanted;
    return true;
}

void CommandTyper::PressPlanned()
{
    DebugKeyboardManager* keys = _context->pDebugManager->GetKeyboardManager();
    for (ZXKeysEnum key : _planned.keys)
    {
        keys->PressKey(key);
        _held.push_back(key);
    }
    _sawTaken = false;
}

bool CommandTyper::ConsumeEvents()
{
    EditorMonitor* monitor = _context->pDebugManager->GetEditorMonitor();
    const std::vector<EditorMonitor::Event>& events = monitor->Events();

    if (monitor->Overflowed())
    {
        Fail(Failure::NoOutcome, "cyclogram overflow");
        return false;
    }

    for (; _seen < events.size() && _status == Status::Running; _seen++)
    {
        const EditorMonitor::Event& e = events[_seen];

        switch (_step)
        {
            case Step::WaitIdle:
                if (e.point == Point::EditorIdle)
                {
                    if (_result.editor == RomKind::Unknown)
                    {
                        _result.editor = e.rom;
                        if (e.rom == RomKind::Plus3Rom0)
                        {
                            Fail(Failure::Unsupported, "+3 editor: control points not verified yet");
                            return false;
                        }
                        _bytes = BytesFor(e.rom);
                    }
                    _sawIdle = true;
                }
                break;

            case Step::Hold:
                if (e.point == Point::KeyTaken)
                {
                    if (e.lastK != _planned.code)
                    {
                        Fail(Failure::WrongKey, "the ROM took " + Hex(e.lastK) + " instead of " + Hex(_planned.code));
                        return false;
                    }
                    ReleaseHeld();
                    _step = Step::Effect;
                    _stepFrames = 0;
                }
                break;

            case Step::Effect:
                if (e.point == Point::Rasp)
                {
                    Fail(Failure::Rejected, "the editor refused " + Hex(_planned.code) + " (error beep)");
                    return false;
                }
                if (e.point == Point::LineFull)
                {
                    Fail(Failure::LineFull, "the edit line is full");
                    return false;
                }
                if (_planned.enter && e.point == Point::Enter)
                {
                    _step = Step::Outcome;
                    _stepFrames = 0;
                    break;
                }
                if (!_planned.enter && !_planned.modeKey && e.point == Point::CharInserted)
                {
                    if (e.a != _planned.insert)
                    {
                        Fail(Failure::WrongChar, "the editor inserted " + Hex(e.a) + " instead of " + Hex(_planned.insert));
                        return false;
                    }
                    _next++;
                    _result.bytesTyped++;
                    _retries = 0;
                    _step = Step::WaitIdle;
                    _stepFrames = 0;
                    _sawIdle = false;
                    break;
                }
                if (e.point == Point::EditorIdle)
                {
                    if (_planned.editKey)
                    {
                        // Cursor / delete done: the next plan re-reads the line
                        _step = Step::WaitIdle;
                        _stepFrames = 0;
                        _sawIdle = true;
                        break;
                    }
                    if (_planned.modeKey && SysVar(SYS_MODE) == 1)
                    {
                        // In E mode now: this idle is where the key goes
                        _step = Step::WaitIdle;
                        _stepFrames = 0;
                        _sawIdle = true;
                        break;
                    }
                    if (_planned.enter)
                        break;  // ENTER's point comes after the editor loop

                    // Taken, back to idle, nothing done: the 48K editor spends
                    // the first key after a report clearing the lower screen
                    if (++_retries > MAX_NO_EFFECT_RETRIES)
                    {
                        Fail(Failure::NoEffect, "the ROM took " + Hex(_planned.code) + " but nothing was inserted");
                        return false;
                    }
                    _step = Step::WaitIdle;
                    _stepFrames = 0;
                    _sawIdle = true;
                }
                break;

            case Step::Outcome:
                switch (e.point)
                {
                    case Point::SyntaxResult:
                        _result.errNr = e.errNr;
                        if (e.errNr != 0xFF)
                        {
                            _result.message = "syntax error, report code " + Hex(static_cast<uint8_t>(e.errNr + 1));
                            Succeed(Outcome::SyntaxError);
                            return false;
                        }
                        break;
                    case Point::LineStored:
                        Succeed(Outcome::Stored);
                        return false;
                    case Point::ExecStart:
                        if (_options.waitForReport)
                        {
                            _step = Step::Report;
                            _stepFrames = 0;
                            break;
                        }
                        Succeed(Outcome::Started);
                        return false;
                    case Point::TrDosLineBack:
                        _result.trdos = true;
                        break;
                    case Point::TrDosSyntaxError:
                        Fail(Failure::TrDosRejected, "TR-DOS: syntax error");
                        return false;
                    case Point::TrDosError:
                        // Before the command table search found anything: not a
                        // command, or TR-DOS failed first (it activates the
                        // drive before the search: no disk gives ERR_NR 26)
                        _result.errNr = e.errNr;
                        Fail(Failure::TrDosRejected, "TR-DOS error before the command was recognised, ERR_NR " +
                                                         std::to_string(e.errNr));
                        return false;
                    case Point::TrDosDispatch:
                        _result.trdos = true;
                        break;
                    case Point::TrDosFound:
                        _result.trdos = true;
                        if (_options.waitForReport)
                        {
                            _step = Step::Report;
                            _stepFrames = 0;
                            break;
                        }
                        Succeed(Outcome::TrDosCommand);
                        return false;
                    default:
                        break;
                }
                break;

            case Step::Report:
                if (e.point == Point::Report || (_result.trdos && e.point == Point::TrDosPrompt))
                {
                    _result.errNr = e.errNr;
                    Succeed(Outcome::Finished);
                    return false;
                }
                if (_result.trdos && e.point == Point::TrDosError)
                {
                    _result.errNr = e.errNr;
                    _result.message = "the TR-DOS command failed";
                    Succeed(Outcome::Finished);
                    return false;
                }
                break;

            default:
                break;
        }
    }
    return _status == Status::Running;
}

void CommandTyper::OnFrame()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_status != Status::Running)
        return;

    if (_requested)
    {
        _requested = false;
        _step = Step::Start;
    }

    if (_abortRequested)
    {
        _abortRequested = false;
        Fail(Failure::Aborted, "aborted");
        return;
    }

    Step_();
}

void CommandTyper::Step_()
{
    DebugManager* debug = _context->pDebugManager;
    EditorMonitor* monitor = debug ? debug->GetEditorMonitor() : nullptr;
    DebugKeyboardManager* keys = debug ? debug->GetKeyboardManager() : nullptr;

    if (_step == Step::Start)
    {
        if (monitor == nullptr || keys == nullptr)
        {
            Fail(Failure::UnknownTarget, "no debugger");
            return;
        }
        if (_context->pTimeTravelManager && _context->pTimeTravelManager->OwnsInput())
        {
            Fail(Failure::InputLocked, "time travel replay owns the keyboard");
            return;
        }
        if (keys->IsSequenceRunning())
        {
            Fail(Failure::KeyboardBusy, "other keyboard input is still being typed");
            return;
        }

        monitor->ClearEvents();
        monitor->Arm();
        _seen = 0;
        _next = 0;
        _bytes.clear();
        _held.clear();
        _retries = 0;
        _clearing = true;
        _clearKeys = 0;
        _sawIdle = false;
        _startFrame = _context->emulatorState.frame_counter;
        _step = Step::WaitIdle;
        _stepFrames = 0;
        return;
    }

    if (!ConsumeEvents())
        return;

    _stepFrames++;

    // Press the next key once the editor is idle and the key has been
    // released long enough for the ROM to take it as a new press
    if (_step == Step::WaitIdle && _sawIdle)
    {
        if (!PlanNext())
            return;

        bool released = true;
        for (ZXKeysEnum key : _planned.keys)
        {
            if (key != ZXKEY_CAPS_SHIFT && key != ZXKEY_SYM_SHIFT &&
                keys->FramesSinceReleased(key) < DebugKeyboardManager::REPRESS_RELEASED_FRAMES)
                released = false;
        }
        if (released)
        {
            PressPlanned();
            _step = Step::Hold;
            _stepFrames = 0;
            _sawIdle = false;
        }
        return;
    }

    // Time limits, in frames
    switch (_step)
    {
        case Step::WaitIdle:
            if (_stepFrames > _options.idleFrames)
            {
                if (monitor->CurrentRom() == RomKind::Unknown && _result.editor == RomKind::Unknown)
                    Fail(Failure::UnknownTarget, "no known editor ROM at #0000");
                else
                    Fail(Failure::Busy, "the editor is not waiting for a key");
            }
            break;
        case Step::Hold:
            if (_stepFrames > _options.takenFrames)
                Fail(Failure::NotTaken, "the ROM did not take " + Hex(_planned.code));
            break;
        case Step::Effect:
            // ENTER on the 128K menu starts the editor, which clears and redraws
            // the screen before it reads keys again
            if (_stepFrames > ((_planned.editKey && _planned.code == CODE_ENTER) ? _options.outcomeFrames
                                                                                : _options.effectFrames))
            {
                if (_planned.enter)
                    Fail(Failure::NoOutcome, "ENTER was taken but the editor did not handle it");
                else
                    Fail(Failure::NoEffect, "the ROM took " + Hex(_planned.code) + " but nothing followed");
            }
            break;
        case Step::Outcome:
            if (_stepFrames > _options.outcomeFrames)
            {
                if (_result.trdos)
                    Succeed(Outcome::TrDosCommand);  // dispatched, still running
                else
                    Fail(Failure::NoOutcome, "no syntax result, stored line or command start after ENTER");
            }
            break;
        case Step::Report:
            if (_stepFrames > _options.reportFrames)
            {
                _result.message = "started; no report within the time limit";
                Succeed(Outcome::Started);
            }
            break;
        default:
            break;
    }
}

CommandTyper::Result CommandTyper::TypeAndWait(Emulator& emulator, const std::string& command,
                                               const Options& options, uint32_t timeoutMs)
{
    Result result;
    EmulatorContext* context = emulator.GetContext();
    CommandTyper* typer = (context && context->pDebugManager) ? context->pDebugManager->GetCommandTyper() : nullptr;
    if (typer == nullptr)
    {
        result.outcome = Outcome::Failed;
        result.failure = Failure::UnknownTarget;
        result.message = "no debugger";
        return result;
    }

    if (!emulator.IsRunning() || emulator.IsPaused())
    {
        result.outcome = Outcome::Failed;
        result.failure = Failure::EmulatorPaused;
        result.message = "the emulator is not running: resume it first";
        return result;
    }

    if (!typer->Request(command, options))
    {
        result.outcome = Outcome::Failed;
        result.failure = Failure::KeyboardBusy;
        result.message = "another command is still being typed";
        return result;
    }

    // Automation thread: the emulator thread advances the typer each frame
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (typer->GetStatus() == Status::Running)
    {
        if (std::chrono::steady_clock::now() >= deadline || emulator.IsPaused() || !emulator.IsRunning())
        {
            typer->Abort();
            const auto abortDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            while (typer->GetStatus() == Status::Running && std::chrono::steady_clock::now() < abortDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));

            result = typer->GetResult();
            result.outcome = Outcome::Failed;
            result.failure = emulator.IsPaused() || !emulator.IsRunning() ? Failure::EmulatorPaused : Failure::TimedOut;
            result.message = result.failure == Failure::TimedOut ? "no outcome within the time limit"
                                                                 : "the emulator was paused while typing";
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return typer->GetResult();
}

bool CommandTyper::Succeeded(const Result& result)
{
    switch (result.outcome)
    {
        case Outcome::Typed:
        case Outcome::Started:
        case Outcome::Finished:
        case Outcome::Stored:
        case Outcome::TrDosCommand:
            return true;
        default:
            return false;
    }
}

const char* CommandTyper::OutcomeName(Outcome outcome)
{
    switch (outcome)
    {
        case Outcome::Typed:        return "typed";
        case Outcome::Started:      return "started";
        case Outcome::Finished:     return "finished";
        case Outcome::Stored:       return "stored";
        case Outcome::SyntaxError:  return "syntax_error";
        case Outcome::TrDosCommand: return "trdos_command";
        case Outcome::Failed:       return "failed";
        default:                    return "none";
    }
}

const char* CommandTyper::FailureName(Failure failure)
{
    switch (failure)
    {
        case Failure::InputLocked:   return "input_locked";
        case Failure::KeyboardBusy:  return "keyboard_busy";
        case Failure::UnknownTarget: return "unknown_target";
        case Failure::Unsupported:   return "unsupported_editor";
        case Failure::Busy:          return "busy";
        case Failure::CannotType:    return "cannot_type";
        case Failure::NotTaken:      return "not_taken";
        case Failure::WrongKey:      return "wrong_key";
        case Failure::WrongChar:     return "wrong_char";
        case Failure::NoEffect:      return "no_effect";
        case Failure::Rejected:      return "rejected";
        case Failure::LineFull:      return "line_full";
        case Failure::NoOutcome:     return "no_outcome";
        case Failure::TrDosRejected: return "trdos_rejected";
        case Failure::LineNotEmpty:  return "line_not_empty";
        case Failure::EmulatorPaused: return "emulator_paused";
        case Failure::TimedOut:      return "timed_out";
        case Failure::Aborted:       return "aborted";
        default:                     return "none";
    }
}
