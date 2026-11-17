#pragma once

/// @file gshostdriver.h
/// @brief The debugger's send (gs-debugger design §6.4, F6): a General Sound command with its pre byte and
/// parameters sent the way a careful host sends it, its replies read, all of it decoded by the firmware profile.
///
/// A byte goes in only when the mailbox allows it: data while status bit 7 is clear, a command while bit 0 is
/// clear, a reply read once the command was taken and bit 7 is set. One step a frame, polled at the frame start on
/// the emulation thread, so the card has a whole frame for each byte. Every step is live input (the TTD journal,
/// applied at the next instruction boundary): a recording replays the bytes, and while a replay owns input nothing is
/// sent. The ZX program may talk to the card at the same time; the debugger then shares the mailbox with it, as a
/// second host would.
///
/// Off (nothing queued) the frame start pays one relaxed load.
///
/// Threads: Submit / Find / Wait from any thread, Frame on the emulation thread; one mutex.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "debugger/ttd/ttdinputjournal.h"
#include "emulator/state/statenode.h"

class EmulatorContext;
class GeneralSoundCard;
struct GSFirmwareProfile;

/// What to send
struct GSSendRequest
{
    uint8_t command = 0;
    std::optional<uint8_t> pre;      ///< the data byte written before the command
    std::vector<uint8_t> params;     ///< the data bytes after it, in order
    std::optional<uint8_t> replies;  ///< reply bytes to read; the profile's count when not given (0 without one)
};

/// One send: the request, how far it got, the replies
struct GSConversation
{
    enum class State : uint8_t
    {
        Queued,   ///< waiting for the frames (a paused machine: until it runs)
        Running,
        Done,
        Failed,   ///< `error` says which byte the card did not take, or that the replay owns input
    };

    uint32_t id = 0;
    State state = State::Queued;
    GSSendRequest request;
    uint8_t repliesExpected = 0;
    std::vector<uint8_t> replies;
    bool drained = false;       ///< a reply nobody had read was read first (the data flag was set)
    uint8_t drainedValue = 0;
    uint64_t startFrame = 0;
    uint64_t endFrame = 0;
    std::string error;
    const GSFirmwareProfile* profile = nullptr;

    bool Finished() const { return state == State::Done || state == State::Failed; }
    /// One line: `#23 Get number of RAM pages -> #07 (number of RAM pages)`
    std::string Text() const;
    void ToState(StateNode& node) const;
};

const char* GSConversationStateName(GSConversation::State state);

class GSHostDriver
{
public:
    static constexpr uint32_t kStepFrames = 50;  ///< a step the mailbox does not allow for this long fails
    static constexpr size_t kKept = 16;          ///< finished conversations kept for Find
    static constexpr size_t kQueued = 16;        ///< sends waiting at most

    /// Submits one step as live input; false when refused (a replay owns input)
    using Submitter = std::function<bool(const ttd::TTDInputEvent&)>;

    /// @p submit defaults to the context's TTD hooks (SubmitLiveInput)
    explicit GSHostDriver(EmulatorContext* context, Submitter submit = {});
    GSHostDriver(const GSHostDriver&) = delete;
    GSHostDriver& operator=(const GSHostDriver&) = delete;

    /// Checks @p request against the card's firmware profile and queues it: the id, or 0 and @p error
    uint32_t Submit(const GeneralSoundCard& card, const GSSendRequest& request, std::string& error);
    /// A conversation by id (0: the last one submitted)
    std::optional<GSConversation> Find(uint32_t id) const;
    /// Find, after waiting up to @p timeoutMs for it to finish. Never on the emulation thread (it runs the frames)
    std::optional<GSConversation> Wait(uint32_t id, uint32_t timeoutMs) const;

    bool Active() const { return _active.load(std::memory_order_relaxed); }
    /// The frame start (emulation thread): at most one step of the conversation at the head of the queue
    void Frame(GeneralSoundCard& card);

private:
    enum class Step : uint8_t
    {
        Drain,      ///< a set data flag: give the card a frame, then read the stale reply
        DrainRead,
        Pre,
        Command,
        Taken,      ///< the card's RSCOM clears bit 0
        Param,
        Reply,
        ReplyRead,
    };

    const GSConversation* FindLocked(uint32_t id) const;
    void Finish(GSConversation::State state, std::string error);
    bool Send(ttd::TTDInputKind kind, uint8_t value = 0);

    EmulatorContext* _context;
    Submitter _submit;
    std::atomic<bool> _active{false};

    mutable std::mutex _mutex;
    mutable std::condition_variable _finished;
    std::deque<GSConversation> _queue;  ///< front: the one running
    std::deque<GSConversation> _done;   ///< newest last
    uint32_t _nextId = 1;
    uint32_t _lastId = 0;

    // The running conversation's progress (emulation thread, under the mutex)
    Step _step = Step::Drain;
    uint32_t _stepFrames = 0;   ///< frames the current step has waited
    size_t _param = 0;
    uint32_t _readsBefore = 0;  ///< the card's automation read count when the read was sent
};

/// What the planes get from a send or a read (CLI, Lua, Python, WebAPI; MCP through the WebAPI)
struct GSSendReply
{
    int status = 200;   ///< as HTTP: 400 a bad request, 404 no card / no such send, 409 too many waiting
    StateNode body;     ///< the conversation (GSConversation::ToState), or {error}
    uint32_t id = 0;
    std::string text;   ///< the conversation's line, or the error
};

/// Queues @p request for the machine's card and waits up to @p waitMs for it to finish (0: returns it queued; on the
/// emulation thread it never waits, that thread runs the steps)
GSSendReply GSSend(EmulatorContext* context, const GSSendRequest& request, uint32_t waitMs);
/// A send by id (0: the last one)
GSSendReply GSConversationRead(EmulatorContext* context, uint32_t id);
