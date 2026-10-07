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
#include "loaders/snapshot/snapshotpipeline.h"
#include "loaders/snapshot/szx/loaderszx.h"

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

bool SnapshotLauncher::NeedOf(const std::string& path, MEM_MODEL runningModel, uint32_t runningRamKb, Need& need,
                              std::string& error)
{
    need = Need{};
    std::string ext = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == "spg")
    {
        if (!LoaderSPG::Probe(path, error))
            return false;
        need.model = LoaderSPG::kModel;
        need.ramKb = LoaderSPG::kRamKb;
        need.programOnly = true;
        need.description = "TS-Conf";
        const TMemModel* tsconf = Config::FindModelByShortName(need.model);
        need.differs = tsconf && runningModel != tsconf->Model;
    }
    else if (ext == "szx")
    {
        szx::Machine machine;
        if (!LoaderSZX::ProbeMachine(path, machine, error))
            return false;
        const TMemModel* target = Config::FindModelByEnum(machine.model);
        need.model = target ? target->ShortName : "";
        need.ramKb = machine.ramKb;
        need.description = szx::DescribeModel(machine.model, machine.ramKb);
        need.differs = !LoaderSZX::Suits(runningModel, runningRamKb, machine);
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

    // A ZX-Poly module takes a .zxp only and is never replaced by another model (the group would lose a module)
    if (snapshot::IsZXPolyModule(*emulator->GetContext()))
    {
        const std::string::size_type dot = request.path.find_last_of('.');
        out.message = snapshot::ZXPolyRefusal(dot == std::string::npos ? std::string("snapshot") : request.path.substr(dot + 1));
        return out;
    }

    std::string error;
    const CONFIG& running = emulator->GetContext()->config;
    Need need;
    if (!NeedOf(request.path, running.mem_model, running.ramsize, need, error))
    {
        out.message = error;
        return out;
    }
    out.requiredModel = need.model;
    out.requiredRamKb = need.ramKb;

    if (need.differs && !need.model.empty())
    {
        // Not asked: a TS-Conf program switches (it runs nowhere else); a snapshot of another machine does only when the
        // configuration says so
        const bool allowed = request.switchModel ? *request.switchModel : (need.programOnly || running.snapshot_switch_model);
        if (!allowed)
        {
            out.modelMismatch = true;
            out.message = need.programOnly
                              ? "the file runs on " + need.model + " only (this machine is " +
                                    Config::GetModelFullName(running.mem_model) + "); switch the model first"
                              : "the snapshot was saved on a " + need.description + ", this machine is a " +
                                    szx::DescribeModel(running.mem_model, running.ramsize) +
                                    ": switch the model first, or load with switch_model=true (or set [SNAPSHOT] SwitchModel=1)";
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
