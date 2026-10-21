#include "emulator/actions/contextstate.h"

#include "3rdparty/message-center/messagecenter.h"
#include "common/uuid.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

namespace actions
{

ContextState& ContextState::Instance()
{
    // Never destroyed: actions workers and the static EmulatorManager may still use it at exit
    static ContextState* state = new ContextState();
    return *state;
}

std::string ContextState::DefaultFor(Emulator* emulator)
{
    EmulatorContext* context = emulator != nullptr ? emulator->GetContext() : nullptr;
    const ttd::ITimeTravelHooks* hooks = context != nullptr ? context->pTimeTravelHooks : nullptr;
    if (hooks != nullptr && hooks->PublishedCursor().history)
        return kTtd;
    return kPlay;
}

bool ContextState::IsValidName(std::string_view context, std::string& error)
{
    if (context.size() > kMaxLength)
    {
        error = "context: at most " + std::to_string(kMaxLength) + " characters";
        return false;
    }
    for (size_t i = 0; i < context.size(); i++)
    {
        const char c = context[i];
        const bool letter = c >= 'a' && c <= 'z';
        const bool other = (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!letter && (i == 0 || !other))
        {
            error = "context '" + std::string(context) +
                    "': lowercase letters, digits, '.', '_', '-', starting with a letter (play, ttd, ttd.timecode)";
            return false;
        }
    }
    return true;
}

ContextInfo ContextState::Get(const std::string& instanceId, Emulator* emulator) const
{
    ContextInfo info;
    info.fallback = DefaultFor(emulator);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _entries.find(instanceId);
        if (it != _entries.end())
        {
            info.context = it->second.context;
            info.by = it->second.by;
            info.set = true;
            return info;
        }
    }
    info.context = info.fallback;
    return info;
}

bool ContextState::Set(const std::string& instanceId, Emulator* emulator, const std::string& context,
                       const std::string& by, std::string& error)
{
    if (!IsValidName(context, error))
        return false;
    const std::string fallback = DefaultFor(emulator);
    std::string previous;
    std::string now;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _entries.find(instanceId);
        previous = it != _entries.end() ? it->second.context : fallback;
        if (context.empty())
        {
            if (it != _entries.end())
                _entries.erase(it);
            now = fallback;
        }
        else
        {
            _entries[instanceId] = {context, by};
            now = context;
        }
    }
    if (now != previous)
    {
        MessageCenter::DefaultMessageCenter().Post(
            NC_SURFACE_CONTEXT, new SurfaceContextPayload(unreal::UUID(instanceId), now, previous, by));
    }
    return true;
}

void ContextState::Drop(const std::string& instanceId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _entries.erase(instanceId);
}

}  // namespace actions
