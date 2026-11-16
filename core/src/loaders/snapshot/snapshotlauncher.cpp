#include "stdafx.h"

#include "loaders/snapshot/snapshotlauncher.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <cctype>
#include <cstdlib>

#include "common/logger.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/media/modelswitch.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/platform.h"
#include "loaders/nex/loadernex.h"
#include "loaders/snapshot/loaderspg.h"
#include "loaders/snapshot/snapshotimage.h"
#include "loaders/snapshot/snapshotpipeline.h"
#include "loaders/snapshot/szx/loaderszx.h"

namespace
{
/// A NEX runs under NextZXOS (its nexload): the ROMs, the system variables and the DivMMC are the running system's. A
/// Next with the boot chain (NEXTBOOT) that has just powered on is still in the config mode with an empty system area:
/// the firmware fills it from the SD card. A program put there meets #FF at #0038 with its first interrupt and stays in
/// RST 38 forever (every Next game opened on a fresh machine). The machine boots first, to the NextZXOS welcome screen
/// (the ROM in its key wait: HALT with the text drawn), then the file loads into that system. A machine without the
/// boot chain (the bare ROM set) is never in the config mode and loads at once, as before
constexpr unsigned kNextBootFrames = 4500;  ///< ~90 s of machine time: the firmware and NextZXOS take ~7 s (360 frames)

bool BootNextForProgram(Emulator& emulator, std::string& error)
{
    EmulatorContext* context = emulator.GetContext();
    auto* memory = dynamic_cast<NextMemory*>(context->pMemory);
    if (memory == nullptr || !memory->InConfigMode())
        return true;
    Z80* z80 = context->pCore->GetZ80();
    auto welcome = [&]() {
        if (memory->InConfigMode() || !z80->halted)
            return false;
        const uint8_t* screen = memory->RAMPageAddress(5);
        int inked = 0;
        for (int i = 0; i < 0x1800; i++)
            inked += screen[i] != 0 ? 1 : 0;
        return inked > 2000;  // the welcome text inks ~2700 bytes
    };
    const bool turbo = emulator.IsTurboMode();
    if (!turbo)
        emulator.EnableTurboMode();
    bool met = false;
    unsigned frames = 0;
    if (emulator.IsRunning() && !emulator.IsPaused())
    {
        // A running machine boots in its own loop at turbo speed (a direct run steps instruction by instruction, near real
        // time, and leaves the machine paused): this thread watches for the welcome screen. The reads race the loop; a
        // torn look only delays the match by one poll
        const auto start = std::chrono::steady_clock::now();
        while (!(met = welcome()) && std::chrono::steady_clock::now() - start < std::chrono::seconds(60) && emulator.IsRunning())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    else
    {
        frames = emulator.RunFramesUntil(kNextBootFrames, [] { return true; }, welcome, true, met);
    }
    if (!turbo)
        emulator.DisableTurboMode();
    if (!met)
    {
        error = "the Next did not boot NextZXOS (a NEX runs under it): is the SD card of the boot chain in?";
        return false;
    }
    LOGINFO("SnapshotLauncher: the Next booted NextZXOS before the NEX (%u direct frames)", frames);
    return true;
}

/// The machine for a snapshot whose plan the running machine refused for want of memory (a 128K z80 on a 48K, a
/// Pentagon 512 sna on a Pentagon 128): the one the file says it was made on, else what the refusal needs
/// ("model:128K", "ram:512K"). False: neither names a machine this emulator has
bool MachineForRefusal(const snapshot::Image& image, const snapshot::Report& plan, const CONFIG& running,
                       std::string& model, uint32_t& ramKb)
{
    static const struct
    {
        const char* hint;
        const char* model;
        uint32_t ramKb;
    } kMadeOn[] = {{"128k", "128k", 128},          {"plus2", "PLUS2", 128},
                   {"plus2a", "PLUS2A", 128},      {"plus3", "PLUS3", 128},
                   {"pentagon128", "PENTAGON", 128}, {"pentagon512", "PENTAGON", 512},
                   {"pentagon1024", "PENTAGON", 1024}, {"scorpion256", "SCORPION", 256}};
    for (const auto& m : kMadeOn)
    {
        if (image.machineHint == m.hint)
        {
            model = m.model;
            ramKb = m.ramKb;
            return true;
        }
    }
    if (plan.needs.rfind("model:", 0) == 0)
    {
        const TMemModel* found = Config::FindModelByShortName(plan.needs.substr(6));
        if (!found)
            return false;
        model = found->ShortName;
        ramKb = found->defaultRAM;
        return true;
    }
    if (plan.needs.rfind("ram:", 0) == 0)
    {
        const uint32_t wanted = static_cast<uint32_t>(std::strtoul(plan.needs.c_str() + 4, nullptr, 10));
        // The running model with more RAM, else a Pentagon: the smallest size that holds the banks
        for (const TMemModel* candidate : {Config::FindModelByEnum(running.mem_model), Config::FindModelByShortName("PENTAGON")})
        {
            for (uint32_t size = 128; candidate && size <= 4096; size *= 2)
            {
                if (size >= wanted && (candidate->AvailRAMs & size))
                {
                    model = candidate->ShortName;
                    ramKb = size;
                    return true;
                }
            }
        }
    }
    return false;
}
}  // namespace

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
    else if (ext == "nex" || ext == "snx")
    {
        model = LoaderNex::kModel;
        ramKb = LoaderNex::kRamKb;
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
    else if (ext == "nex" || ext == "snx")
    {
        need.model = LoaderNex::kModel;
        need.ramKb = LoaderNex::kRamKb;
        need.programOnly = true;
        need.description = "ZX Spectrum Next";
        const TMemModel* next = Config::FindModelByShortName(need.model);
        need.differs = next && runningModel != next->Model;
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
    if (need.model.empty())
    {
        // A z80 / sna names no model up front: the dry plan says whether this machine can hold it, and the file's
        // machine hint which one can
        snapshot::Image image;
        snapshot::Report plan;
        bool proceeds = true;
        std::string planError;
        snapshot::Options dryRun;
        dryRun.commit = request.commit;
        if (emulator->PlanSnapshot(request.path, dryRun, image, plan, proceeds, planError) && !proceeds &&
            plan.refused && MachineForRefusal(image, plan, running, need.model, need.ramKb))
        {
            const TMemModel* target = Config::FindModelByShortName(need.model);
            need.differs = true;
            need.description = target ? szx::DescribeModel(target->Model, need.ramKb) : need.model;
        }
        else
        {
            need.model.clear();
        }
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

        // The recorded history belongs to this machine (D26): the coordinator decides (logged)
        if (std::string refusal = emulator->AskModelSwitch("to " + out.requiredModel); !refusal.empty())
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

    if (need.model == LoaderNex::kModel && !BootNextForProgram(*out.emulator, out.message))
        return out;

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
