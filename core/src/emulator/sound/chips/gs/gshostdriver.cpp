#include "emulator/sound/chips/gs/gshostdriver.h"

#include <bit>
#include <chrono>
#include <cstdio>

#include "debugger/ttd/timetravelhooks.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

namespace
{

constexpr size_t kMaxBytes = 16;  ///< params and replies of one send, at most

std::string Hex(uint8_t value)
{
    char text[4];
    std::snprintf(text, sizeof(text), "#%02X", value);
    return text;
}

/// The profile's command, nullptr when the profile does not name it
const GSCommandInfo* InfoOf(const GSConversation& c)
{
    if (!c.profile)
        return nullptr;
    const GSCommandInfo& info = c.profile->Command(c.request.command);
    return info.name ? &info : nullptr;
}

/// The name of the byte at @p index of the command's bytes (pre first, then post)
const char* ParamName(const GSCommandInfo* info, size_t index, const char* fallback)
{
    return info && index < 4 && info->params[index].name ? info->params[index].name : fallback;
}

/// The post bytes' names, for an error: "(track high)"
std::string PostNames(const GSCommandInfo& info, size_t count)
{
    std::string names;
    for (size_t i = 0; i < count; i++)
    {
        const char* name = ParamName(&info, info.pre + i, nullptr);
        if (!name)
            continue;
        names += names.empty() ? " (" : ", ";
        names += name;
    }
    return names.empty() ? names : names + ")";
}

} // namespace

const char* GSConversationStateName(GSConversation::State state)
{
    switch (state)
    {
        case GSConversation::State::Queued: return "queued";
        case GSConversation::State::Running: return "running";
        case GSConversation::State::Done: return "done";
        case GSConversation::State::Failed: return "failed";
    }
    return "?";
}

/// region <Conversation>

std::string GSConversation::Text() const
{
    const GSCommandInfo* info = InfoOf(*this);
    std::string text = Hex(request.command);
    if (info)
        text += std::string(" ") + info->name;
    if (request.pre)
        text += std::string(", ") + ParamName(info, 0, "pre") + " " + Hex(*request.pre);
    const size_t first = info ? info->pre : (request.pre ? 1 : 0);
    for (size_t i = 0; i < request.params.size(); i++)
        text += std::string(", ") + ParamName(info, first + i, "param") + " " + Hex(request.params[i]);
    if (!replies.empty())
    {
        text += " ->";
        for (uint8_t reply : replies)
            text += " " + Hex(reply);
        if (info && info->replyMeaning)
            text += std::string(" (") + info->replyMeaning + ")";
    }
    if (drained)
        text += ", a stale reply " + Hex(drainedValue) + " read first";
    switch (state)
    {
        case State::Queued: return text + " (queued: sent while the machine runs)";
        case State::Running: return text + " (running)";
        case State::Failed: return text + " - failed: " + error;
        case State::Done: break;
    }
    return text;
}

void GSConversation::ToState(StateNode& node) const
{
    const GSCommandInfo* info = InfoOf(*this);
    node["id"] = int64_t(id);
    node["state"] = std::string(GSConversationStateName(state));
    node["command"] = int(request.command);
    if (info)
        node["name"] = std::string(info->name);
    if (request.pre)
    {
        StateNode pre = StateNode::Object();
        pre["value"] = int(*request.pre);
        if (const char* name = ParamName(info, 0, nullptr))
            pre["name"] = std::string(name);
        node["pre"] = pre;
    }
    StateNode params = StateNode::Array();
    const size_t first = info ? info->pre : (request.pre ? 1 : 0);
    for (size_t i = 0; i < request.params.size(); i++)
    {
        StateNode param = StateNode::Object();
        param["value"] = int(request.params[i]);
        if (const char* name = ParamName(info, first + i, nullptr))
            param["name"] = std::string(name);
        params.push(param);
    }
    node["params"] = params;
    StateNode values = StateNode::Array();
    for (uint8_t reply : replies)
        values.push(int(reply));
    node["replies"] = values;
    node["replies_expected"] = int(repliesExpected);
    if (info && info->replyMeaning)
        node["reply_meaning"] = std::string(info->replyMeaning);
    if (drained)
        node["drained"] = int(drainedValue);
    node["start_frame"] = int64_t(startFrame);
    node["end_frame"] = int64_t(endFrame);
    if (!error.empty())
        node["error"] = error;
    node["profile"] = profile ? std::string(profile->id) : std::string();
    node["text"] = Text();
}

/// endregion </Conversation>

/// region <Driver>

GSHostDriver::GSHostDriver(EmulatorContext* context, Submitter submit) : _context(context), _submit(std::move(submit))
{
    if (!_submit)
    {
        _submit = [context](const ttd::TTDInputEvent& ev) {
            return context && context->pTimeTravelHooks && context->pTimeTravelHooks->SubmitLiveInput(ev);
        };
    }
}

uint32_t GSHostDriver::Submit(const GeneralSoundCard& card, const GSSendRequest& request, std::string& error)
{
    GSConversation c;
    c.request = request;
    c.profile = GSFirmwareProfileOf(card);
    const GSCommandInfo* info = InfoOf(c);
    const std::string command = Hex(request.command) + (info ? std::string(" ") + info->name : std::string());
    if (request.params.size() > kMaxBytes || request.replies.value_or(0) > kMaxBytes)
    {
        error = "at most 16 parameter bytes and 16 replies";
        return 0;
    }
    if (info)
    {
        // The handshake the firmware expects (the profile, §6.1): a wrong count would leave the card waiting for a
        // byte, or take the next command's byte as this one's parameter
        if (info->pre && !request.pre)
        {
            error = command + " takes a pre byte (" + ParamName(info, 0, "pre") + ")";
            return 0;
        }
        if (!info->pre && request.pre)
        {
            error = command + " takes no pre byte";
            return 0;
        }
        size_t post = info->post;
        if (info->post == GSCommandInfo::kVariablePost)
            // #0D: a byte per channel of the mask's low nibble; #6B: its high byte is optional
            post = request.command == 0x0D ? static_cast<size_t>(std::popcount(static_cast<unsigned>(*request.pre & 0x0F)))
                                           : std::min<size_t>(request.params.size(), 1);
        if (request.params.size() != post)
        {
            error = command + " takes " + std::to_string(post) + " parameter byte" + (post == 1 ? "" : "s") +
                    " after it" + PostNames(*info, post) + ", got " + std::to_string(request.params.size());
            return 0;
        }
        c.repliesExpected = request.replies.value_or(info->reply == GSCommandInfo::kVariablePost ? 0 : info->reply);
    }
    else
    {
        // No profile (NeoGS firmware) or a command it does not name: the bytes as given, replies only when asked
        c.repliesExpected = request.replies.value_or(0);
    }

    std::lock_guard lock(_mutex);
    if (_queue.size() >= kQueued)
    {
        error = std::to_string(kQueued) + " sends are already waiting (the machine is paused?)";
        return 0;
    }
    c.id = _nextId++;
    _lastId = c.id;
    _queue.push_back(std::move(c));
    _active.store(true, std::memory_order_relaxed);
    return _lastId;
}

const GSConversation* GSHostDriver::FindLocked(uint32_t id) const
{
    if (id == 0)
        id = _lastId;
    for (const GSConversation& c : _queue)
        if (c.id == id)
            return &c;
    for (const GSConversation& c : _done)
        if (c.id == id)
            return &c;
    return nullptr;
}

std::optional<GSConversation> GSHostDriver::Find(uint32_t id) const
{
    std::lock_guard lock(_mutex);
    const GSConversation* c = FindLocked(id);
    return c ? std::optional<GSConversation>(*c) : std::nullopt;
}

std::optional<GSConversation> GSHostDriver::Wait(uint32_t id, uint32_t timeoutMs) const
{
    std::unique_lock lock(_mutex);
    if (id == 0)
        id = _lastId;
    _finished.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
        const GSConversation* c = FindLocked(id);
        return !c || c->Finished();
    });
    const GSConversation* c = FindLocked(id);
    return c ? std::optional<GSConversation>(*c) : std::nullopt;
}

bool GSHostDriver::Send(ttd::TTDInputKind kind, uint8_t value)
{
    ttd::TTDInputEvent ev;
    ev.kind = kind;
    ev.value = value;
    return _submit(ev);
}

void GSHostDriver::Finish(GSConversation::State state, std::string error)
{
    GSConversation c = std::move(_queue.front());
    _queue.pop_front();
    c.state = state;
    c.error = std::move(error);
    c.endFrame = _context ? _context->emulatorState.frame_counter : 0;
    _done.push_back(std::move(c));
    while (_done.size() > kKept)
        _done.pop_front();
    _active.store(!_queue.empty(), std::memory_order_relaxed);
    _finished.notify_all();
}

void GSHostDriver::Frame(GeneralSoundCard& card)
{
    std::lock_guard lock(_mutex);
    if (_queue.empty())
    {
        _active.store(false, std::memory_order_relaxed);
        return;
    }
    GSConversation& c = _queue.front();
    if (c.state == GSConversation::State::Queued)
    {
        c.state = GSConversation::State::Running;
        c.startFrame = _context ? _context->emulatorState.frame_counter : 0;
        _step = Step::Drain;
        _stepFrames = 0;
        _param = 0;
    }

    const uint8_t status = card.getStatusRaw();
    const bool dataFlag = (status & 0x80) != 0;
    const bool commandFlag = (status & 0x01) != 0;
    const auto next = [&](Step step) {
        _step = step;
        _stepFrames = 0;
    };
    const auto send = [&](ttd::TTDInputKind kind, uint8_t value, Step then) {
        if (kind == ttd::TTDInputKind::GSReadData)
            _readsBefore = card.automationReads();
        if (!Send(kind, value))
        {
            Finish(GSConversation::State::Failed, "refused: a TTD replay owns input");
            return;
        }
        next(then);
    };
    const auto wait = [&](const char* what) {
        if (++_stepFrames > kStepFrames)
            Finish(GSConversation::State::Failed, what);
    };

    // The steps that need no byte go on in the same frame; a byte sent ends the frame's work
    for (;;)
    {
        switch (_step)
        {
            case Step::Drain:
                if (!dataFlag)
                {
                    next(c.request.pre ? Step::Pre : Step::Command);
                    continue;
                }
                // A host byte not taken yet, or a reply nobody read: a frame is the card's time to take a byte, so
                // a flag still set after it is a reply
                if (_stepFrames++ == 0)
                    return;
                send(ttd::TTDInputKind::GSReadData, 0, Step::DrainRead);
                return;

            case Step::DrainRead:
                if (card.automationReads() == _readsBefore)
                    return wait("the read was not applied");
                c.drained = true;
                c.drainedValue = card.lastAutomationRead();
                next(c.request.pre ? Step::Pre : Step::Command);
                return;  // the flag read this frame is the stale one: look again next frame

            case Step::Pre:
                if (dataFlag)
                    return wait("the card did not take the data byte before the pre byte (status bit 7 stays set)");
                send(ttd::TTDInputKind::GSData, *c.request.pre, Step::Command);
                return;

            case Step::Command:
                if (commandFlag)
                    return wait("the card did not take the previous command (status bit 0 stays set)");
                send(ttd::TTDInputKind::GSCommand, c.request.command, Step::Taken);
                return;

            case Step::Taken:
                if (commandFlag)
                    return wait("the card did not take the command (status bit 0 stays set)");
                next(c.request.params.empty() ? Step::Reply : Step::Param);
                continue;

            case Step::Param:
                if (dataFlag)
                    return wait("the card did not take a parameter byte (status bit 7 stays set)");
                send(ttd::TTDInputKind::GSData, c.request.params[_param],
                     _param + 1 < c.request.params.size() ? Step::Param : Step::Reply);
                _param++;
                return;

            case Step::Reply:
                if (c.replies.size() >= c.repliesExpected)
                {
                    Finish(GSConversation::State::Done, {});
                    return;
                }
                // A handler that reads its data byte after the reply clears the flag itself
                // (GSCommandInfo::replyUnflagged): the card took the command, the reply is in the register
                if (!dataFlag &&
                    !(c.replies.empty() && c.profile && c.profile->Command(c.request.command).replyUnflagged))
                    return wait("no reply came (status bit 7 stays clear)");
                send(ttd::TTDInputKind::GSReadData, 0, Step::ReplyRead);
                return;

            case Step::ReplyRead:
                if (card.automationReads() == _readsBefore)
                    return wait("the read was not applied");
                c.replies.push_back(card.lastAutomationRead());
                next(Step::Reply);
                if (c.replies.size() >= c.repliesExpected)
                    continue;
                return;  // the flag read this frame is the one just cleared: the next reply next frame
        }
    }
}

/// endregion </Driver>

/// region <Planes>

namespace
{

GSSendReply Refused(int status, std::string message)
{
    GSSendReply reply;
    reply.status = status;
    reply.text = message;
    reply.body = StateNode::Object();
    reply.body["error"] = std::move(message);
    return reply;
}

GSSendReply Answer(const GSConversation& c)
{
    GSSendReply reply;
    reply.id = c.id;
    reply.text = c.Text();
    reply.body = StateNode::Object();
    c.ToState(reply.body);
    return reply;
}

const char* const kNoCard = "General Sound card not fitted (plug one: slots plug zxbus.next gs | gs-lw | neogs)";

} // namespace

GSSendReply GSSend(EmulatorContext* context, const GSSendRequest& request, uint32_t waitMs)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    GeneralSoundCard* card = sm ? sm->getGeneralSound() : nullptr;
    const std::shared_ptr<GSHostDriver> driver = sm ? sm->gsHostDriver() : nullptr;
    if (!card || !driver)
        return Refused(404, kNoCard);
    std::string error;
    const uint32_t id = driver->Submit(*card, request, error);
    if (id == 0)
        return Refused(error.find("waiting") != std::string::npos ? 409 : 400, error);
    const std::optional<GSConversation> c = waitMs && !sm->onEmulationThread() ? driver->Wait(id, waitMs) : driver->Find(id);
    return c ? Answer(*c) : Refused(404, "the send is gone");
}

GSSendReply GSConversationRead(EmulatorContext* context, uint32_t id)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    const std::shared_ptr<GSHostDriver> driver = sm ? sm->gsHostDriver() : nullptr;
    if (!driver)
        return Refused(404, kNoCard);
    const std::optional<GSConversation> c = driver->Find(id);
    if (!c)
        return Refused(404, id ? "no send #" + std::to_string(id) + " (the last 16 are kept)" : "nothing sent yet");
    return Answer(*c);
}

/// endregion </Planes>
