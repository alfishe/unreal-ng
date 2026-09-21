#include "emulatortesthelper.h"

#include "base/featuremanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "_helpers/testpathhelper.h"

#include <filesystem>
#include <fstream>

namespace
{
    /// Stage a scratch copy of the model's shipped ini with the TurboSound=
    /// line rewritten to the requested kind (either direction - the shipped
    /// value differs per machine now). Returns an empty path on failure
    std::string StageTurboSoundKindIni(const std::string& modelConfigFolder, TurboSoundKind kind)
    {
        namespace fs = std::filesystem;

        const fs::path source =
            TestPathHelper::FindProjectRoot() / "data" / "configs" / modelConfigFolder / "unreal.ini";
        if (!fs::exists(source))
            return std::string();

        std::string ini;
        {
            std::ifstream in(source, std::ios::binary);
            ini.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        const std::string to = std::string("TurboSound=") + (kind == TurboSoundKind::FM ? "FM" : "AY");
        const size_t atAy = ini.find("TurboSound=AY");
        const size_t atFm = ini.find("TurboSound=FM");
        if (atAy != std::string::npos)
            ini.replace(atAy, to.size(), to);  // both slot literals are the same length
        else if (atFm != std::string::npos)
            ini.replace(atFm, to.size(), to);
        else
            return std::string();

        const fs::path target =
            TestPathHelper::GetUniqueTestScratchPath("forced-slot-" + modelConfigFolder + ".ini");
        {
            std::ofstream out(target, std::ios::binary);
            out.write(ini.data(), static_cast<std::streamsize>(ini.size()));
        }

        return target.string();
    }
}

// Static member initialization
std::unordered_map<uint32_t, BreakpointCallback> EmulatorTestHelper::_breakpointCallbacks;

Emulator* EmulatorTestHelper::CreateStandardEmulator(const std::string& modelName, LoggerLevel logLevel)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();

    // Create emulator with model name if specified
    std::shared_ptr<Emulator> emulator;
    if (!modelName.empty())
    {
        emulator = manager->CreateEmulatorWithModel("test-emulator", modelName, logLevel);
    }
    else
    {
        emulator = manager->CreateEmulator("test-emulator", logLevel);
    }

    if (!emulator)
    {
        return nullptr;
    }

    // EmulatorManager already calls Init() during creation
    // Force deterministic boot: 48K BASIC (RESET=BASIC) regardless of the
    // staged unreal.ini, which CMake re-copies on every build (RESET=128)
    emulator->GetContext()->config.reset_rom = RM_SOS;
    emulator->Reset();

    // Test-speed defaults (2026-09-13): the two per-frame "HQ" features are
    // OFF for tests. Per-t-state screen rendering (screenhq) and the FIR
    // audio path (soundhq) cost about a third of every emulated frame, and
    // the full suite has a 30 s sequential budget. Tests that exercise those
    // paths opt in explicitly:
    //   context->pFeatureManager->setFeature(Features::kScreenHQ, true);
    //   context->pFeatureManager->setFeature(Features::kSoundHQ, true);
    // (or ITurboSoundDevice::setHQEnabled for the sound device alone).
    // Product defaults are untouched - see FeatureManager registration.
    if (FeatureManager* features = emulator->GetContext()->pFeatureManager)
    {
        features->setFeature(Features::kScreenHQ, false);
        features->setFeature(Features::kSoundHQ, false);
    }

    // Return raw pointer (caller must use CleanupEmulator to properly release)
    return emulator.get();
}

Emulator* EmulatorTestHelper::CreateEmulatorWithTurboSoundKind(const std::string& modelName,
                                                                 TurboSoundKind kind,
                                                                 LoggerLevel logLevel)
{
    // Empty model name = the bare-Init default (Emulator's own preferred
    // model is MM_PENTAGON), so resolve the folder the same way Init would
    const std::string folder = modelName.empty()
        ? Config::GetConfigFolderForModel(MM_PENTAGON, 0)
        : [&]() -> std::string
          {
              const TMemModel* modelInfo = Config::FindModelByShortName(modelName);
              return modelInfo ? Config::GetConfigFolderForModel(modelInfo->Model, modelInfo->defaultRAM)
                               : std::string();
          }();
    if (folder.empty())
        return nullptr;

    const std::string staged = StageTurboSoundKindIni(folder, kind);
    if (staged.empty())
        return nullptr;

    // Same boot shape as the TSFM suites' own helpers: the staged ini
    // self-identifies the machine, no preferred model needed
    Emulator* emulator = new Emulator(logLevel);
    emulator->SetCustomConfigPath(staged);
    if (!emulator->Init())
    {
        emulator->Release();
        delete emulator;
        return nullptr;
    }
    return emulator;
}

std::string EmulatorTestHelper::StageTurboSoundKindConfig(TurboSoundKind kind)
{
    return StageTurboSoundKindIni(Config::GetConfigFolderForModel(MM_PENTAGON, 0), kind);
}

Emulator* EmulatorTestHelper::CreateDebugEmulator(const std::vector<std::string>& features,
                                                  const std::string& modelName, LoggerLevel logLevel)
{
    // Start with standard emulator
    Emulator* emulator = CreateStandardEmulator(modelName, logLevel);
    if (!emulator)
    {
        return nullptr;
    }

    // Enable debug mode
    emulator->DebugOn();

    // Enable requested features
    EmulatorContext* context = emulator->GetContext();
    if (context && context->pFeatureManager)
    {
        for (const auto& feature : features)
        {
            if (feature == "breakpoints")
            {
                context->pFeatureManager->setFeature(Features::kBreakpoints, true);
            }
            else if (feature == "debugmode")
            {
                context->pFeatureManager->setFeature(Features::kDebugMode, true);
            }
            // Add more feature mappings as needed
        }
    }

    return emulator;
}

bool EmulatorTestHelper::EnableDebugFeatures(Emulator* emulator)
{
    if (!emulator)
    {
        return false;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pFeatureManager)
    {
        return false;
    }

    // Enable master debug mode
    emulator->DebugOn();

    // Enable breakpoints and debug mode features
    context->pFeatureManager->setFeature(Features::kDebugMode, true);
    context->pFeatureManager->setFeature(Features::kBreakpoints, true);

    return true;
}

uint32_t EmulatorTestHelper::SetupExecutionBreakpoint(Emulator* emulator, uint16_t address, BreakpointCallback callback)
{
    if (!emulator)
    {
        return 0;
    }

    // Ensure debug features are enabled
    EnableDebugFeatures(emulator);

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pDebugManager->GetBreakpointsManager())
    {
        return 0;
    }

    // Create breakpoint via BreakpointManager
    // Use "test_helper" as owner ID for test-created breakpoints
    uint32_t bpId = context->pDebugManager->GetBreakpointsManager()->AddExecutionBreakpoint(address, "test_helper");

    if (bpId != 0)
    {
        // Store callback for this breakpoint
        _breakpointCallbacks[bpId] = std::move(callback);
    }

    return bpId;
}

void EmulatorTestHelper::RemoveBreakpoint(Emulator* emulator, uint32_t breakpointId)
{
    if (!emulator || breakpointId == 0)
    {
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (context && context->pDebugManager->GetBreakpointsManager())
    {
        context->pDebugManager->GetBreakpointsManager()->RemoveBreakpointByID(breakpointId);
    }

    // Remove stored callback
    _breakpointCallbacks.erase(breakpointId);
}

void EmulatorTestHelper::CleanupEmulator(Emulator* emulator)
{
    if (emulator)
    {
        // Clear any breakpoint callbacks for this emulator
        // (In a multi-emulator scenario, we'd need to track which callbacks belong to which emulator)
        _breakpointCallbacks.clear();

        std::string uuid = emulator->GetUUID();
        EmulatorManager* manager = EmulatorManager::GetInstance();
        manager->RemoveEmulator(uuid);
    }
}

void EmulatorTestHelper::OnBreakpointHit(uint32_t bpId, Z80* cpu, Memory* memory)
{
    auto it = _breakpointCallbacks.find(bpId);
    if (it != _breakpointCallbacks.end())
    {
        // Call the registered callback
        bool bypass = it->second(cpu, memory);

        if (bypass)
        {
            // Caller requested bypass - pop return address and set PC
            // This is the common pattern for keyboard wait bypass
            uint16_t retAddr = memory->DirectReadFromZ80Memory(cpu->sp);
            retAddr |= memory->DirectReadFromZ80Memory(cpu->sp + 1) << 8;
            cpu->sp += 2;
            cpu->pc = retAddr;
        }
    }
}

void EmulatorTestHelper::RunFramesFast(Emulator* emulator, int frameCount)
{
    if (!emulator)
        return;

    emulator->RunNFrames(static_cast<unsigned>(frameCount), true);
}

int EmulatorTestHelper::RunUntil(Emulator* emulator,
                                  std::function<bool()> condition,
                                  int maxFrames,
                                  int checkInterval)
{
    if (!emulator)
        return 0;

    for (int i = 0; i < maxFrames; i++)
    {
        emulator->RunFrame(true);
        if ((i + 1) % checkInterval == 0 && condition())
        {
            return i + 1;
        }
    }
    return maxFrames;
}

uint8_t EmulatorTestHelper::ReadSysVar(Emulator* emulator, uint16_t address)
{
    if (!emulator)
        return 0;
    return emulator->GetContext()->pMemory->DirectReadFromZ80Memory(address);
}

uint16_t EmulatorTestHelper::ReadSysVar16(Emulator* emulator, uint16_t address)
{
    if (!emulator)
        return 0;
    Memory* mem = emulator->GetContext()->pMemory;
    return mem->DirectReadFromZ80Memory(address) |
           (mem->DirectReadFromZ80Memory(address + 1) << 8);
}

bool EmulatorTestHelper::IsBASICReady(Emulator* emulator)
{
    // Check multiple conditions to ensure BASIC is truly ready:
    // 1. ERR_NR == 0x00 (error code 1 = "OK")
    // 2. PROG pointer is set to the exact 48K ROM startup value (0x5CCB) -
    //    a loose ">= 0x5C00" range check is a false-positive trap: Memory
    //    randomizes page 5 (screen + low sysvars, RandomizeMemoryContent)
    //    with rand() to simulate realistic power-on garbage, and any value
    //    in 0x5C00..0xFFFF (~64% of the 16-bit space) satisfied the old
    //    check before the ROM had written PROG for real. Whether that
    //    coincidence happens depends on rand()'s cumulative state, which
    //    carries across every Emulator created in the process - so this
    //    fired (or not) depending on prior tests' call order.
    uint8_t errNr = ReadSysVar(emulator, SystemVariables48k::ERR_NR);
    uint16_t prog = ReadSysVar16(emulator, SystemVariables48k::PROG);
    return errNr == 0x00 && prog == 0x5CCB;
}

bool EmulatorTestHelper::RunUntilBASICReady(Emulator* emulator, int maxFrames, int* framesRun)
{
    if (!emulator)
        return false;

    // Real 48K ROM cold boot never reaches BASIC in under ~25 frames (other
    // tests in this suite document ~80-120 frames as the normal range) -
    // skip the exact-match ready check below that floor so it can only ever
    // observe genuine ROM-written sysvars, never the pre-boot random fill.
    constexpr int kMinBootFrames = 25;

    for (int i = 0; i < maxFrames; i++)
    {
        emulator->RunFrame(true);  // true = turbo mode (no frame rate limiting)
        // Check every 5 frames (very fast check - just two memory reads)
        if ((i + 1) >= kMinBootFrames && (i + 1) % 5 == 0 && IsBASICReady(emulator))
        {
            if (framesRun)
                *framesRun = i + 1;
            return true;
        }
    }
    if (framesRun)
        *framesRun = maxFrames;
    return false;
}
