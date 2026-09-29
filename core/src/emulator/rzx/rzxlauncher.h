#pragma once

/// @file rzxlauncher.h
/// @brief The one entry point every surface (WebAPI, MCP, CLI, Lua, Python,
/// Qt) uses to play an RZX recording, so they all behave the same (design
/// §13): play on the given emulator, or, when the recording was made on
/// another model, switch the model first (ModelSwitch, media kept) and play
/// on the new machine. Also the shared text forms of the options and status.
///
/// Worked example: Play({emulatorId: "e1" (a 48K), path: "dizzy128.rzx"})
/// -> PlayRzx says ModelMismatch "128k" -> ModelSwitch builds a 128K "e2",
/// e1 is released -> e2 starts and plays; the result carries e2 and
/// modelSwitched = true, previousEmulatorId = "e1".

#include <functional>
#include <memory>
#include <string>

#include "emulator/rzx/rzxsession.h"

class Emulator;

namespace rzx
{
    struct LaunchRequest
    {
        std::string emulatorId;
        std::string path;           ///< the .rzx file
        PlayerOptions options;
        bool switchModel = true;    ///< on a model mismatch: switch (true) or refuse (false)
        /// The old machine is about to be released (a GUI unbinds its views)
        std::function<void(Emulator& old)> beforeRelease;
    };

    struct LaunchResult
    {
        PlayResult play;
        std::shared_ptr<Emulator> emulator;  ///< the machine that plays (the new one after a switch)
        bool modelSwitched = false;
        std::string previousEmulatorId;      ///< set when the model was switched
    };

    class RzxLauncher
    {
    public:
        static LaunchResult Play(const LaunchRequest& request);

        /// "strict" / "tolerant"
        static bool ParseDesyncMode(const std::string& text, DesyncMode& mode);
        static const char* DesyncModeName(DesyncMode mode);

        /// One line: "playing frame 1200 / 32315 (3.7%), block 1 / 1, 0 desyncs"
        static std::string StatusLine(const SessionStatus& status);
        /// Several lines for CLI / Lua / Python printouts
        static std::string StatusText(const SessionStatus& status);
    };
}  // namespace rzx
