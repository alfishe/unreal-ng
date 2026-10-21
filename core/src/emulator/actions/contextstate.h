#pragma once

/// @file contextstate.h
/// @brief The context of each instance (control surfaces architecture §6): one string naming what the surfaces are
/// doing on it now - `play`, `ttd`, `ttd.timecode`, `debug.disasm` - so a Surface Dial, a Stream Deck and the GUI act
/// as one instrument (a context layer `@ttd` of a debug profile makes the Dial seek; a deck page names its context).
///
/// Anyone sets it: the `ctx.set` action on every plane (a Stream Deck page, the Qt client, a script); the last setter
/// wins (owner decision Q-5). A change posts NC_SURFACE_CONTEXT (SurfaceContextPayload) on the default message
/// center; followers (the Qt owner pushing it into the router, the surface hub sending `ctx`) react to it.
///
/// Until somebody sets one (or after `ctx.set ""`), the context is the default (owner decision Q-7): `ttd` when the
/// instance has a time travel history, else `play`. The default follows the history without a notification.
///
/// Process-wide, keyed by instance id, thread-safe; reads never touch the machine (the history check reads the TTD's
/// published cursor). Headless as well: the core owns it, not the Qt client.

#include <map>
#include <mutex>
#include <string>
#include <string_view>

class Emulator;

namespace actions
{

struct ContextInfo
{
    std::string context;  ///< in force: the one set, else the default
    bool set = false;     ///< somebody set it (false: the default)
    std::string by;       ///< who set it ("companion:<id>", "webapi:<client>", "gui", "host-device:<id>"); "" by default
    std::string fallback; ///< the default it returns to (ctx.set "")
};

class ContextState
{
public:
    static constexpr const char* kPlay = "play";
    static constexpr const char* kTtd = "ttd";
    static constexpr size_t kMaxLength = 64;

    /// The process's contexts
    static ContextState& Instance();

    /// The context of `instanceId` (`emulator` for the default; null: `play`)
    ContextInfo Get(const std::string& instanceId, Emulator* emulator) const;
    /// Set it ("" returns to the default). False with `error` for a name that is not lowercase letters, digits, '.',
    /// '_', '-' (at most kMaxLength, starting with a letter). A change of the context in force posts
    /// NC_SURFACE_CONTEXT; setting the one in force again only records the setter
    bool Set(const std::string& instanceId, Emulator* emulator, const std::string& context, const std::string& by,
             std::string& error);
    /// The instance is gone (EmulatorManager)
    void Drop(const std::string& instanceId);

    /// The default (Q-7): `ttd` when `emulator` has a time travel history, else `play`
    static std::string DefaultFor(Emulator* emulator);
    static bool IsValidName(std::string_view context, std::string& error);

private:
    struct Entry
    {
        std::string context;
        std::string by;
    };
    mutable std::mutex _mutex;
    std::map<std::string, Entry> _entries;
};

}  // namespace actions
