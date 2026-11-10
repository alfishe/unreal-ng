/// @file ttdcontrollerbookmarks.cpp
/// @brief Agent bookmarks with notes (docs/inprogress/2026-11-03-ttd-agent-bookmarks/design.md): add with the
/// moment's context and watched bytes, edit. The context is taken once, when the bookmark is added: the current
/// position as the machine stands, another moment by one silent seek with the machine put back.

#include "stdafx.h"
#include "timetravelcontroller.h"

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "emulator/emulatorcontext.h"
#include "emulator/readoutschanged.h"
#include "emulator/memory/memory.h"

namespace ttd
{

bool TimeTravelController::AddAgentBookmark(TTDBookmark bookmark, bool captureContext, std::string* err)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // A bookmark into empty history dangles immediately — there is no
    // checkpoint to return to. Refuse at creation instead of at seek time.
    if (_line->timeline.empty())
    {
        if (err)
            *err = "no recorded history to bookmark (start recording first)";
        MLOGWARNING("TimeTravelController::AddBookmark — rejected: timeline is empty");
        return false;
    }

    // Same principle for a position past the session end: the bookmark can
    // never be reached, so it must never be created.
    // While recording the present is in reach (a seek there pauses the recording at it)
    const TTDTimePoint& time = bookmark.time;
    const TTDTimePoint end = _state == TTDSessionState::Recording ? CurrentPosition() : SessionEndPosition();
    if (end < time)
    {
        if (err)
            *err = "bookmark position (frame=" + std::to_string(time.frame) +
                   ", tInFrame=" + std::to_string(time.tInFrame) +
                   ") is beyond the session end (frame=" + std::to_string(end.frame) + ")";
        MLOGWARNING("TimeTravelController::AddBookmark — rejected: frame %llu beyond session end %llu",
                    static_cast<unsigned long long>(time.frame),
                    static_cast<unsigned long long>(end.frame));
        return false;
    }

    // The bounds and the label first: a bookmark that will be refused costs no seek
    bookmark.context = TTDBookmarkContext{};
    for (TTDBookmarkWatch& w : bookmark.watches)
        w.bytes.clear();
    if (!ValidateBookmark(bookmark, err))
        return false;
    TTDBookmark existing;
    if (_bookmarks.Find(bookmark.label, existing))
    {
        if (err)
            *err = "bookmark label '" + bookmark.label + "' already exists at frame " +
                   std::to_string(existing.time.frame);
        return false;
    }
    bookmark.branch = BranchHolding(bookmark.time);
    if (captureContext)
        CaptureBookmarkMoment(bookmark);
    const bool added = _bookmarks.Add(bookmark, err);
    if (added)
        NotifyReadoutsChanged(_context, "ttd.bookmarks");
    return added;
}

bool TimeTravelController::EditBookmark(const std::string& label, const std::function<void(TTDBookmark&)>& edit,
                                        std::string* err)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    const bool edited = _bookmarks.Edit(label, edit, err);
    if (edited)
        NotifyReadoutsChanged(_context, "ttd.bookmarks");   // a rename changes the list the surfaces show
    return edited;
}

bool TimeTravelController::CaptureBookmarkMoment(TTDBookmark& bookmark)
{
    Memory* memory = _context ? _context->pMemory : nullptr;
    if (!memory || !_context->pEmulator || _line->timeline.empty() || bookmark.time < _line->timeline.front().time ||
        bookmark.time.tInFrame >= FrameSpan())
        return false;

    auto read = [&]() {
        LiveStateSnapshot here;
        SaveLiveState(here);
        const TTDCpuState& cpu = here.cpu;
        TTDBookmarkContext& c = bookmark.context;
        c.valid = true;
        c.pc = cpu.pc;
        c.sp = cpu.sp;
        c.af = cpu.af;
        c.bc = cpu.bc;
        c.de = cpu.de;
        c.hl = cpu.hl;
        c.ix = cpu.ix;
        c.iy = cpu.iy;
        c.iff1 = cpu.iff1;
        c.im = cpu.im;
        c.p7FFD = here.chipset.p7FFD;
        const MemoryPageDescriptor page = memory->MapZ80AddressToPhysicalPage(cpu.pc);
        c.codeKind = static_cast<uint8_t>(page.mode);
        c.codePage = static_cast<uint16_t>(page.page);
        std::vector<uint8_t> code(4);
        for (size_t i = 0; i < code.size(); ++i)
            code[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(cpu.pc + i));
        if (Z80Disassembler* disassembler =
                _context->pDebugManager ? _context->pDebugManager->GetDisassembler().get() : nullptr)
        {
            uint8_t len = 0;
            c.instruction = disassembler->disassembleSingleCommand(code, cpu.pc, &len);
            if (c.instruction.size() > kMaxBookmarkInstructionLength)
                c.instruction.resize(kMaxBookmarkInstructionLength);
        }
        for (TTDBookmarkWatch& w : bookmark.watches)
        {
            w.bytes.resize(w.length);
            for (uint8_t i = 0; i < w.length; ++i)
                w.bytes[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(w.address + i));
        }
    };

    // The current position: the machine as it stands (a live recording keeps running)
    if (CurrentPosition() == bookmark.time)
    {
        read();
        return true;
    }

    // Another moment: one silent seek, the machine put back verbatim (as ReadCpuView)
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    const TTDSessionState stateBefore = _state;
    SaveLiveState(_liveSnapshot);
    ReplayModeScope replay(*this);
    TTDSeekResult seek;
    const bool reached = SeekToInternal(bookmark.time, &seek) && seek.reached &&
                         seek.haltReason == TTDSeekHaltReason::Target;
    if (reached)
        read();
    RestoreLiveState(_liveSnapshot);
    replay.Exit();
    SetState(stateBefore);
    return reached;
}

}  // namespace ttd
