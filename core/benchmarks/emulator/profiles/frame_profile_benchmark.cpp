/// @file frame_profile_benchmark.cpp
/// @brief Host cost of one emulated frame per machine and per run profile (BM_Prof_*).
///
/// A ladder: each row changes one thing against its neighbour, so the difference between two rows is the cost of
/// that thing. Every row creates the machine from its shipped configs/<model>/unreal.ini, boots 300 frames (past
/// the ROM and the card POSTs), then times MainLoop::RunFrame(): turbo render decimation and every frame hook run
/// exactly as in the paced loop. Guide, method and recorded results: README.md next to this file.
///
///   ShipNormal         shipped config, features at their defaults, 1x                    what a 1x frame costs
///   ShipTurbo          shipped config, turbo without audio                                the app's max speed
///   BareTurbo          ShipTurbo + screenhq, soundhq, sound, hud, debugmode off
///   ShipTurboDebugHud  ShipTurbo + debugmode, breakpoints, hud on                         a debugging session
///   WorstNormal        shipped config + debugmode, memorytracking, calltrace, TTD recording, 1x
///   TtdRecordNormal    shipped config + a TTD recording (debugmode, timetravel), 1x
///   TtdRecordSoundOff  TtdRecordNormal with the sound feature off (a headless automation instance)
///   TtdRecordSoundLQ   TtdRecordNormal with soundhq off
///   NoCardsTurbo       AY in the AY socket, no bus cards, turbo                           (slot trees only)
///   Only<Card>Turbo    NoCardsTurbo + one card (neogs, gs, moonsound, soundrive; tsfm in the AY socket)
///   MaxTurbo           NoCardsTurbo + everything BareTurbo switches off
///   <Machine>_<Program>_<Profile>   one of the above with a program loaded after the boot (FRAME_PROFILE_WORKLOAD)
///
/// The file also builds in trees from before the ZX-bus slots (2026-10-03): the slot rows are left out there, so
/// one source measures an old and a new commit (README.md, "Comparing two commits").
///
/// Counter `fps`: frames per second of host CPU time, single thread.
///
/// Under callgrind the collection is switched on for the timed loop only (--collect-atstart=no). Note that the
/// adaptive turbo render cadence measures frames per wall-clock second: under valgrind it decimates by about 1, so
/// a callgrind profile of a turbo row renders every frame and shows screen drawing the real run does not pay.

#include <benchmark/benchmark.h>

#include <functional>
#include <memory>
#include <string>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "loaders/benchmark_path_helper.h"

#if __has_include("emulator/slots/slotconfig.h")
#define FRAME_PROFILE_HAS_SLOTS 1
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/platform.h"
#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotmanager.h"
#endif

#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#define FRAME_PROFILE_TOGGLE_COLLECT() CALLGRIND_TOGGLE_COLLECT
#else
#define FRAME_PROFILE_TOGGLE_COLLECT() ((void)0)
#endif

namespace
{
enum class RunProfile
{
    ShipNormal,
    ShipTurbo,
    BareTurbo,
    ShipTurboDebugHud,
    WorstNormal,
    NoCardsTurbo,
    OnlyCardTurbo,
    OnlyCardNormal,  ///< as OnlyCardTurbo at 1x: the card renders its sound
    MaxTurbo,
    TtdRecordNormal,
    TtdRecordSoundOff,
    TtdRecordSoundLQ,
};

bool RecordsTtd(RunProfile profile)
{
    return profile == RunProfile::WorstNormal || profile == RunProfile::TtdRecordNormal ||
           profile == RunProfile::TtdRecordSoundOff || profile == RunProfile::TtdRecordSoundLQ;
}

bool ReplacesCards(RunProfile profile)
{
    return profile == RunProfile::NoCardsTurbo || profile == RunProfile::OnlyCardTurbo ||
           profile == RunProfile::OnlyCardNormal || profile == RunProfile::MaxTurbo;
}

void SetFeature(EmulatorContext* context, const char* feature, bool on)
{
    context->pFeatureManager->setFeature(feature, on);
}

/// A program running instead of the idle ROM: loaded after the boot frames, then `settleFrames` more run before the
/// timed loop
struct Workload
{
    const char* snapshot = nullptr;  ///< loaded after the boot frames (.spg, .nex, .sna ...): testdata path or absolute
    int settleFrames = 0;
};

void RunFrameProfile(benchmark::State& state, const char* model, RunProfile profile, const char* onlyCard = nullptr,
                     const Workload& workload = {})
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator;

#ifdef FRAME_PROFILE_HAS_SLOTS
    std::function<void(CONFIG&)> configOverride;
    if (ReplacesCards(profile))
    {
        const std::string card = onlyCard ? onlyCard : "";
        configOverride = [card](CONFIG& config) {
            // The INI is already projected when the override runs: the new set goes through UseSlots, assigning
            // config.slotConfig alone changes nothing
            SlotConfig slots;
            slots.section = true;

            SlotConfigEntry socket;
            socket.slot = "ay-socket";
            socket.card = card == "tsfm" ? "tsfm" : "ay";
            slots.entries.push_back(socket);

            if (!card.empty() && card != "tsfm")
            {
                SlotConfigEntry bus;
                bus.slot = "zxbus.1";
                bus.card = card;
                if (card == "soundrive")
                    bus.options = "mode=both";
                slots.entries.push_back(bus);
            }

            slots.builtIns = config.slotConfig.builtIns;  // the board's own switchable devices stay as shipped
            SlotManager::UseSlots(slots, config);
        };
    }
    emulator = manager->CreateEmulatorWithModel("bench-frame-profile", model, LoggerLevel::LogNone, nullptr, configOverride);
#else
    if (ReplacesCards(profile))
    {
        state.SkipWithError("needs a tree with ZX-bus slots");
        return;
    }
    emulator = manager->CreateEmulatorWithModel("bench-frame-profile", model, LoggerLevel::LogNone);
#endif

    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    MainLoopCUT* mainLoop = reinterpret_cast<MainLoopCUT*>(context->pMainLoop);

    if (profile == RunProfile::BareTurbo || profile == RunProfile::MaxTurbo)
    {
        SetFeature(context, Features::kScreenHQ, false);
        SetFeature(context, Features::kSoundHQ, false);
        SetFeature(context, Features::kSoundGeneration, false);
#ifdef FRAME_PROFILE_HAS_SLOTS
        SetFeature(context, Features::kHud, false);
#endif
        SetFeature(context, Features::kDebugMode, false);
        context->pMemory->UpdateFeatureCache();
    }

    // Past the ROM and the card POSTs (classic GS ~26 frames, NeoGS loader, TR-DOS menu)
    for (int i = 0; i < 300; i++)
        mainLoop->RunFramePublic();

    if (workload.snapshot)
    {
        const std::string path = workload.snapshot[0] == '/' ? std::string(workload.snapshot)
                                                             : BenchmarkPathHelper::RequireTestDataFile(workload.snapshot);
        if (!emulator->LoadSnapshot(path))
            BenchmarkPathHelper::FailSetup("snapshot load failed: " + path);
        if (emulator->IsPaused())
            emulator->Resume();  // a load leaves the machine paused
    }
    for (int i = 0; i < workload.settleFrames; i++)
        mainLoop->RunFramePublic();

    if (profile == RunProfile::TtdRecordSoundOff)
        SetFeature(context, Features::kSoundGeneration, false);
    if (profile == RunProfile::TtdRecordSoundLQ)
        SetFeature(context, Features::kSoundHQ, false);
    if (RecordsTtd(profile))
    {
        SetFeature(context, Features::kDebugMode, true);
        if (profile == RunProfile::WorstNormal)
        {
            SetFeature(context, Features::kMemoryTracking, true);
            SetFeature(context, Features::kCallTrace, true);
        }
        SetFeature(context, Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();

        bool started = false;
#ifdef FRAME_PROFILE_HAS_SLOTS
        if (context->pTimeTravelController)
            started = context->pTimeTravelController->StartRecording();
#endif
        if (!started && context->pTimeTravelManager)
            context->pTimeTravelManager->StartRecording();

        // Let the recording settle (first checkpoints intern every page)
        for (int i = 0; i < 20; i++)
            mainLoop->RunFramePublic();
    }

    if (profile == RunProfile::ShipTurboDebugHud)
    {
        SetFeature(context, Features::kDebugMode, true);
        SetFeature(context, Features::kBreakpoints, true);
#ifdef FRAME_PROFILE_HAS_SLOTS
        SetFeature(context, Features::kHud, true);
#endif
        context->pMemory->UpdateFeatureCache();
    }

    const bool turbo = profile != RunProfile::ShipNormal && !RecordsTtd(profile) && profile != RunProfile::OnlyCardNormal;
    if (turbo)
        context->pCore->EnableTurboMode();

    FRAME_PROFILE_TOGGLE_COLLECT();
    for (auto _ : state)
        mainLoop->RunFramePublic();
    FRAME_PROFILE_TOGGLE_COLLECT();

    if (turbo)
        context->pCore->DisableTurboMode();

    state.counters["fps"] = benchmark::Counter(static_cast<double>(state.iterations()), benchmark::Counter::kIsRate);
    manager->RemoveEmulator(emulator->GetUUID());
}
}  // namespace

#define FRAME_PROFILE(name, model, profile)                                                    \
    static void BM_Prof_##name(benchmark::State& s) { RunFrameProfile(s, model, profile); }   \
    BENCHMARK(BM_Prof_##name)->Unit(benchmark::kMicrosecond)->MinTime(1.0)

#define FRAME_PROFILE_WORKLOAD(name, model, profile, ...)                                                              \
    static void BM_Prof_##name(benchmark::State& s) { RunFrameProfile(s, model, profile, nullptr, Workload{__VA_ARGS__}); } \
    BENCHMARK(BM_Prof_##name)->Unit(benchmark::kMicrosecond)->MinTime(1.0)

#define FRAME_PROFILE_CARD(name, model, card)                                                                  \
    static void BM_Prof_##name(benchmark::State& s) { RunFrameProfile(s, model, RunProfile::OnlyCardTurbo, card); } \
    BENCHMARK(BM_Prof_##name)->Unit(benchmark::kMicrosecond)->MinTime(1.0)

#define FRAME_PROFILE_CARD_NORMAL(name, model, card)                                                            \
    static void BM_Prof_##name(benchmark::State& s) { RunFrameProfile(s, model, RunProfile::OnlyCardNormal, card); } \
    BENCHMARK(BM_Prof_##name)->Unit(benchmark::kMicrosecond)->MinTime(1.0)

// Present in every tree
FRAME_PROFILE(Pentagon_ShipNormal, "PENTAGON", RunProfile::ShipNormal);
FRAME_PROFILE(Pentagon_ShipTurbo, "PENTAGON", RunProfile::ShipTurbo);
FRAME_PROFILE(Pentagon_BareTurbo, "PENTAGON", RunProfile::BareTurbo);
FRAME_PROFILE(Pentagon_ShipTurboDebugHud, "PENTAGON", RunProfile::ShipTurboDebugHud);
FRAME_PROFILE(Pentagon_WorstNormal, "PENTAGON", RunProfile::WorstNormal);
FRAME_PROFILE(Pentagon_TtdRecordNormal, "PENTAGON", RunProfile::TtdRecordNormal);
FRAME_PROFILE(Pentagon_TtdRecordSoundOff, "PENTAGON", RunProfile::TtdRecordSoundOff);
FRAME_PROFILE(Pentagon_TtdRecordSoundLQ, "PENTAGON", RunProfile::TtdRecordSoundLQ);
FRAME_PROFILE(48K_ShipTurbo, "48K", RunProfile::ShipTurbo);
FRAME_PROFILE(128K_ShipTurbo, "128K", RunProfile::ShipTurbo);
FRAME_PROFILE(Scorpion_ShipTurbo, "SCORPION", RunProfile::ShipTurbo);
// The AY machines at 1x (the plain AY device, not the TurboSound FM)
FRAME_PROFILE(128K_ShipNormal, "128K", RunProfile::ShipNormal);
FRAME_PROFILE(Scorpion_ShipNormal, "SCORPION", RunProfile::ShipNormal);

#ifdef FRAME_PROFILE_HAS_SLOTS
// One card at a time: the cost of a fitted card that nobody uses
FRAME_PROFILE(Pentagon_NoCardsTurbo, "PENTAGON", RunProfile::NoCardsTurbo);
FRAME_PROFILE_CARD(Pentagon_OnlyNeoGS_Turbo, "PENTAGON", "neogs");
FRAME_PROFILE_CARD(Pentagon_OnlyGS_Turbo, "PENTAGON", "gs");
FRAME_PROFILE_CARD(Pentagon_OnlyMoonSound_Turbo, "PENTAGON", "moonsound");
FRAME_PROFILE_CARD(Pentagon_OnlyTSFM_Turbo, "PENTAGON", "tsfm");
FRAME_PROFILE_CARD(Pentagon_OnlySounDrive_Turbo, "PENTAGON", "soundrive");
// One card at 1x, rendering its sound (the AY socket a plain AY)
FRAME_PROFILE_CARD_NORMAL(Pentagon_OnlyMultiSound_Normal, "PENTAGON", "multisound");
FRAME_PROFILE_CARD_NORMAL(Pentagon_OnlyGS_Normal, "PENTAGON", "gs");
FRAME_PROFILE_CARD_NORMAL(Pentagon_OnlyNeoGS_Normal, "PENTAGON", "neogs");
FRAME_PROFILE_CARD_NORMAL(Pentagon_OnlyMoonSound_Normal, "PENTAGON", "moonsound");
FRAME_PROFILE_CARD_NORMAL(Pentagon_NoCards_Normal, "PENTAGON", "");
FRAME_PROFILE(Pentagon_MaxTurbo, "PENTAGON", RunProfile::MaxTurbo);
FRAME_PROFILE(48K_MaxTurbo, "48K", RunProfile::MaxTurbo);

// Heavy machines
FRAME_PROFILE(TSConf_ShipTurbo, "TSL", RunProfile::ShipTurbo);
FRAME_PROFILE(TSConf_MaxTurbo, "TSL", RunProfile::MaxTurbo);
FRAME_PROFILE(ATM3_ShipTurbo, "ATM3", RunProfile::ShipTurbo);
FRAME_PROFILE(ATM3_MaxTurbo, "ATM3", RunProfile::MaxTurbo);
FRAME_PROFILE(ATM710_ShipTurbo, "ATM710", RunProfile::ShipTurbo);
FRAME_PROFILE(Profi_ShipTurbo, "PROFI", RunProfile::ShipTurbo);
FRAME_PROFILE(TSConf_ShipNormal, "TSL", RunProfile::ShipNormal);
FRAME_PROFILE(TSConf_BareTurbo, "TSL", RunProfile::BareTurbo);
FRAME_PROFILE(Sprinter_ShipNormal, "SPRINTER", RunProfile::ShipNormal);
FRAME_PROFILE(Sprinter_ShipTurbo, "SPRINTER", RunProfile::ShipTurbo);
FRAME_PROFILE(Sprinter_BareTurbo, "SPRINTER", RunProfile::BareTurbo);
FRAME_PROFILE(Next_ShipNormal, "NEXT", RunProfile::ShipNormal);
FRAME_PROFILE(Next_ShipTurbo, "NEXT", RunProfile::ShipTurbo);
FRAME_PROFILE(Next_BareTurbo, "NEXT", RunProfile::BareTurbo);

// Heavy machines running a program: TSU sprites over tiles (the TS-Conf SDK example)
FRAME_PROFILE_WORKLOAD(TSConf_Sprites_ShipNormal, "TSL", RunProfile::ShipNormal, "machines/tsconf/spg/sprites.spg", 50);
FRAME_PROFILE_WORKLOAD(TSConf_Sprites_ShipTurbo, "TSL", RunProfile::ShipTurbo, "machines/tsconf/spg/sprites.spg", 50);

// A TurboSound FM tune (both chips, FM and SSG) under a TTD recording: what each host sound setting costs
FRAME_PROFILE_WORKLOAD(Pentagon_TsfmTune_ShipNormal, "PENTAGON", RunProfile::ShipNormal, "sound/tsfm/tech_support.sna", 50);
FRAME_PROFILE_WORKLOAD(Pentagon_TsfmTune_TtdRecordNormal, "PENTAGON", RunProfile::TtdRecordNormal,
                       "sound/tsfm/tech_support.sna", 50);
FRAME_PROFILE_WORKLOAD(Pentagon_TsfmTune_TtdRecordSoundOff, "PENTAGON", RunProfile::TtdRecordSoundOff,
                       "sound/tsfm/tech_support.sna", 50);
FRAME_PROFILE_WORKLOAD(Pentagon_TsfmTune_TtdRecordSoundLQ, "PENTAGON", RunProfile::TtdRecordSoundLQ,
                       "sound/tsfm/tech_support.sna", 50);
#endif
