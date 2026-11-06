#pragma once

/// @file ttdsession.h
/// @brief The instance's time-travel session, the engine's TimeTravelController
/// (EmulatorContext::pTimeTravelController). The core talks through
/// ITimeTravelHooks and the surfaces through TTDControl; this is for the few
/// clients that need more (the Qt panel, the DeZog and GDB adapters, a few CLI
/// reports).
///
/// Worked example:
///   const uint64_t end = ttd::WithTimeTravelSession(context, 0ull, [](auto& session) {
///       return session.SessionEndPosition().frame;
///   });

#include <string>
#include <utility>
#include <vector>

#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulatorcontext.h"

namespace ttd
{

/// True when @p context has a time-travel session object
inline bool HasTimeTravelSession(const EmulatorContext* context)
{
    return context && context->pTimeTravelController;
}

/// Call @p f with the instance's session; @p fallback when there is none
template <class R, class F>
R WithTimeTravelSession(EmulatorContext* context, R fallback, F&& f)
{
    if (context && context->pTimeTravelController)
        return f(*context->pTimeTravelController);
    return fallback;
}

/// The same for code that returns nothing
template <class F>
void WithTimeTravelSession(EmulatorContext* context, F&& f)
{
    if (context && context->pTimeTravelController)
        f(*context->pTimeTravelController);
}

/// The instance's session as one object for clients that call it as
/// `mgr->Method()`: the Qt panel and status bar, the DeZog adapter, the CLI
/// and GDB reports. Valid while the instance lives; false (operator bool) when
/// it has none
class TTDSessionRef
{
public:
    explicit TTDSessionRef(EmulatorContext* context) : _context(HasTimeTravelSession(context) ? context : nullptr) {}
    explicit operator bool() const { return _context != nullptr; }
    TTDSessionRef* operator->() { return this; }
    const TTDSessionRef* operator->() const { return this; }

    // State and status
    TTDSessionState GetState() const { return Call(TTDSessionState::Idle, [](auto& s) { return s.GetState(); }); }
    bool IsRecording() const { return Call(false, [](auto& s) { return s.IsRecording(); }); }
    /// A background session (the black box, a debugger's history)
    bool IsBackgroundSession() const { return Call(false, [](auto& s) { return s.IsBackgroundSession(); }); }
    TTDSessionInfo ReadSessionInfo() const { return Call(TTDSessionInfo{}, [](auto& s) { return s.ReadSessionInfo(); }); }
    TTDSessionInfo GetSessionInfo() const { return Call(TTDSessionInfo{}, [](auto& s) { return s.GetSessionInfo(); }); }
    TTDSessionInfo GetPublishedSessionInfo() const
    {
        return Call(TTDSessionInfo{}, [](auto& s) { return s.GetPublishedSessionInfo(); });
    }
    std::string GetUnavailableReason() const { return Call(std::string(), [](auto& s) { return s.GetUnavailableReason(); }); }
    size_t GetCheckpointCount() const { return Call(size_t(0), [](auto& s) { return s.GetCheckpointCount(); }); }
    uint64_t GetEarliestRecordedFrame() const { return Call(uint64_t(0), [](auto& s) { return s.GetEarliestRecordedFrame(); }); }
    TTDTimePoint CurrentPosition() const { return Call(TTDTimePoint{}, [](auto& s) { return s.CurrentPosition(); }); }
    TTDTimePoint SessionEndPosition() const { return Call(TTDTimePoint{}, [](auto& s) { return s.SessionEndPosition(); }); }
    std::vector<TTDBookmark> GetBookmarks() const
    {
        return Call(std::vector<TTDBookmark>{}, [](auto& s) { return s.GetBookmarks(); });
    }
    const TTDExternalEventJournal& GetExternalEvents() const
    {
        return _context->pTimeTravelController->GetExternalEvents();
    }

    // Actions
    bool StartRecording() { return Call(false, [](auto& s) { return s.StartRecording(); }); }
    void StopRecording() { Do([](auto& s) { s.StopRecording(); }); }
    void InvalidateSession(const char* reason) { Do([reason](auto& s) { s.InvalidateSession(reason); }); }
    void SetHistoryLimit(uint64_t maxFrames, uint64_t maxBytes)
    {
        Do([maxFrames, maxBytes](auto& s) { s.SetHistoryLimit(maxFrames, maxBytes); });
    }
    bool HoldBackgroundRecording() { return Call(false, [](auto& s) { return s.HoldBackgroundRecording(); }); }
    void ReleaseBackgroundRecording() { Do([](auto& s) { s.ReleaseBackgroundRecording(); }); }
    void RecordExternalEvent(TTDExternalEventKind kind, const char* reason)
    {
        Do([&](auto& s) { s.RecordExternalEvent(kind, reason); });
    }
    void SetSessionSourcePath(const std::string& path) { Do([&](auto& s) { s.SetSessionSourcePath(path); }); }
    void ClearFrameCache() { Do([](auto& s) { s.ClearFrameCache(); }); }
    const TTDFrameCache* GetFrameCache(uint64_t frame)
    {
        return Call(static_cast<const TTDFrameCache*>(nullptr), [frame](auto& s) { return s.GetFrameCache(frame); });
    }

private:
    template <class R, class F>
    R Call(R fallback, F&& f) const
    {
        return WithTimeTravelSession(_context, std::move(fallback), std::forward<F>(f));
    }
    template <class F>
    void Do(F&& f) const
    {
        WithTimeTravelSession(_context, std::forward<F>(f));
    }
    EmulatorContext* _context;
};

/// The read-only reports' name for it
using TTDSessionView = TTDSessionRef;

}  // namespace ttd
