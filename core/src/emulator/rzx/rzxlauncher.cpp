#include "emulator/rzx/rzxlauncher.h"

#include <cstdio>

#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/media/modelswitch.h"

namespace rzx
{
    LaunchResult RzxLauncher::Play(const LaunchRequest& request)
    {
        LaunchResult out;
        EmulatorManager& manager = *EmulatorManager::GetInstance();
        std::shared_ptr<Emulator> emulator = manager.GetEmulator(request.emulatorId);
        if (!emulator)
        {
            out.play.error = PlayError::Refused;
            out.play.message = "no emulator '" + request.emulatorId + "'";
            return out;
        }

        out.emulator = emulator;
        out.play = emulator->PlayRzx(request.path, request.options);
        if (out.play.error != PlayError::ModelMismatch || !request.switchModel)
            return out;

        // The recording needs another model: a new machine of that model, the
        // media carried over (ModelSwitch), then the playback there
        ModelSwitchRequest switchRequest;
        switchRequest.emulatorId = emulator->GetId();
        switchRequest.model = out.play.requiredModel;
        switchRequest.ramKb = out.play.requiredRamKb;
        switchRequest.stranded = StrandedMedia::Keep;
        switchRequest.beforeRelease = request.beforeRelease;
        const std::string previousId = emulator->GetId();
        const bool wasRunning = emulator->IsRunning();
        emulator.reset();
        out.emulator.reset();

        const ModelSwitchResult switched = ModelSwitch::Run(switchRequest);
        if (!switched.result.Ok() || !switched.emulator)
        {
            out.emulator = manager.GetEmulator(previousId);
            out.play.message += "; the model switch failed: " + switched.result.message;
            return out;
        }

        out.modelSwitched = true;
        out.switchedToModel = switchRequest.model;
        out.previousEmulatorId = previousId;
        out.emulator = switched.emulator;
        // The new machine runs if the old one did (Start() would run the loop on this thread)
        if (wasRunning)
            out.emulator->StartAsync();
        out.play = out.emulator->PlayRzx(request.path, request.options);
        return out;
    }

    bool RzxLauncher::ParseDesyncMode(const std::string& text, DesyncMode& mode)
    {
        if (text == "strict")
            mode = DesyncMode::Strict;
        else if (text == "tolerant")
            mode = DesyncMode::Tolerant;
        else
            return false;
        return true;
    }

    const char* RzxLauncher::DesyncModeName(DesyncMode mode)
    {
        return mode == DesyncMode::Strict ? "strict" : "tolerant";
    }

    std::string RzxLauncher::StatusLine(const SessionStatus& status)
    {
        if (!status.loaded)
            return "no RZX recording played";

        const PlayerStatus& player = status.player;
        char text[256];
        const double percent = player.totalFrames ? 100.0 * static_cast<double>(player.frame) /
                                                        static_cast<double>(player.totalFrames)
                                                  : 0.0;
        std::snprintf(text, sizeof(text), "%s frame %llu / %llu (%.1f%%), block %u / %u, %llu desync%s",
                      StateName(player.state), static_cast<unsigned long long>(player.frame),
                      static_cast<unsigned long long>(player.totalFrames), percent, player.block + 1, player.blocks,
                      static_cast<unsigned long long>(player.desyncs), player.desyncs == 1 ? "" : "s");
        std::string line = text;
        if (!player.stopReason.empty())
            line += ": " + player.stopReason;
        return line;
    }

    std::string RzxLauncher::StatusText(const SessionStatus& status)
    {
        if (!status.loaded)
            return "No RZX recording played on this machine\n";

        const PlayerStatus& player = status.player;
        std::string text;
        text += "RZX:       " + status.path + "\n";
        text += "State:     " + StatusLine(status) + "\n";
        if (!status.creator.empty())
            text += "Creator:   " + status.creator + " (RZX " + status.version + ")\n";
        if (!status.snapshot.empty())
            text += "Snapshot:  " + status.snapshot + "\n";
        text += "Options:   desync " + std::string(DesyncModeName(status.options.desyncMode)) +
                (status.options.eiShortFrameBlocksInt ? ", EI short frame blocks INT" : "") +
                (status.options.ldAirParityQuirk ? ", LD A,I/R parity quirk" : "") +
                (status.options.ignoreLaterSnapshots ? ", later snapshots ignored" : "") + "\n";
        text += "Interrupts:" + std::string(" ") + std::to_string(player.interrupts) + ", drift " +
                std::to_string(player.drift) + " T (max " + std::to_string(player.maxDrift) + " T)\n";
        if (player.snapshotsApplied > 0)
            text += "Snapshots: " + std::to_string(player.snapshotsApplied) + " applied between input blocks\n";
        text += "Keyframes: " + std::to_string(player.keyframes) + " (" + std::to_string(player.keyframeBytes / 1024) +
                " KB, every " + std::to_string(player.keyframeInterval) + " frames)\n";
        if (player.desyncs > 0)
        {
            const Desync& d = player.firstDesync;
            char line[160];
            std::snprintf(line, sizeof(line), "First desync: %s in frame %llu (block %u): expected %u, got %u, PC #%04X\n",
                          DesyncName(d.kind), static_cast<unsigned long long>(d.frame), d.block + 1, d.expected,
                          d.actual, d.pc);
            text += line;
        }
        return text;
    }
}  // namespace rzx
