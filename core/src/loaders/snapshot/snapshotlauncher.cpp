#include "stdafx.h"

#include "loaders/snapshot/snapshotlauncher.h"

#include <algorithm>
#include <cctype>

#include "debugger/ttd/timetravelhooks.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/media/modelswitch.h"
#include "emulator/platform.h"
#include "loaders/snapshot/loaderspg.h"

bool SnapshotLauncher::RequiredModel(const std::string& path, std::string& model, uint32_t& ramKb, std::string& error)
{
    model.clear();
    ramKb = 0;
    std::string ext = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == "spg")
    {
        if (!LoaderSPG::Probe(path, error))
            return false;
        model = LoaderSPG::kModel;
        ramKb = LoaderSPG::kRamKb;
    }
    return true;
}

SnapshotLoadResult SnapshotLauncher::Load(const SnapshotLoadRequest& request)
{
    SnapshotLoadResult out;
    EmulatorManager& manager = *EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager.GetEmulator(request.emulatorId);
    if (!emulator || !emulator->GetContext())
    {
        out.message = "no emulator '" + request.emulatorId + "'";
        return out;
    }
    out.emulator = emulator;

    std::string error;
    if (!RequiredModel(request.path, out.requiredModel, out.requiredRamKb, error))
    {
        out.message = error;
        return out;
    }

    const TMemModel* required = out.requiredModel.empty() ? nullptr : Config::FindModelByShortName(out.requiredModel);
    const CONFIG& running = emulator->GetContext()->config;
    if (required && running.mem_model != required->Model)
    {
        if (!request.switchModel)
        {
            out.modelMismatch = true;
            out.message = "the file runs on " + out.requiredModel + " only (this machine is " +
                          Config::GetModelFullName(running.mem_model) + "); switch the model first";
            return out;
        }

        // The recorded history belongs to this machine (D26): not while TTD records
        if (std::string refusal = emulator->RecordingGuard(ttd::TTDGuardedAction::SwitchModel); !refusal.empty())
        {
            out.message = refusal;
            return out;
        }

        ModelSwitchRequest switchRequest;
        switchRequest.emulatorId = emulator->GetId();
        switchRequest.model = out.requiredModel;
        switchRequest.ramKb = out.requiredRamKb;
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
            out.message = "the model switch to " + out.requiredModel + " failed: " + switched.result.message;
            return out;
        }
        out.modelSwitched = true;
        out.previousEmulatorId = previousId;
        out.emulator = switched.emulator;
        // The new machine runs if the old one did (Start() would run the loop on this thread)
        if (wasRunning)
            out.emulator->StartAsync();
    }

    snapshot::Options options;
    options.commit = request.commit;
    out.ok = out.emulator->LoadSnapshot(request.path, {}, options);
    out.report = out.emulator->LastSnapshotReport();
    if (!out.ok)
    {
        // The pipeline's reason when it refused; otherwise the loader's, which is in the log
        out.message = out.report.refused ? "refused '" + request.path + "': " + out.report.reason
                                         : "failed to load '" + request.path + "' (the log has the loader's reason)";
    }
    return out;
}

bool SnapshotLauncher::Inspect(const std::string& emulatorId, const std::string& path, const std::string& commit,
                               StateNode& result, std::string& error)
{
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(emulatorId);
    if (!emulator || !emulator->GetContext())
    {
        error = "no emulator '" + emulatorId + "'";
        return false;
    }
    snapshot::Options options;
    options.commit = commit;
    return emulator->InspectSnapshot(path, options, result, error);
}
