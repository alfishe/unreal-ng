#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/atm/ttdatmpaging.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// @file ttdatmpaging_test.cpp
/// @brief TTD capture of ATM Turbo 2+ / ATM3 / ZX-Evo state.
///
/// pFFF7 IS the ATM memory map. TTDChipsetState deliberately does not carry it
/// (the common format is model-agnostic), so if this serializer regresses a
/// seek restores the standard 128K latches and lands on entirely different
/// memory - with no error anywhere.

namespace
{
Emulator* MakeAtm(const char* model = "ATM710")
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
    if (emulator == nullptr)
        return nullptr;

    FeatureManager* fm = emulator->GetFeatureManager();
    if (fm)
    {
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
    }
    return emulator;
}
}  // namespace

/// Blobs are copied by member-wise assignment and hashed byte-wise, so implicit
/// padding would carry uninitialized bytes into the stream and the hash.
TEST(TtdAtmPagingLayout_Test, BlobIsPaddingFree)
{
    using ttd::AtmPagingState;

    EXPECT_EQ(sizeof(AtmPagingState), 44u);
    EXPECT_TRUE(std::is_trivially_copyable_v<AtmPagingState>);
    EXPECT_TRUE(std::is_standard_layout_v<AtmPagingState>);

    alignas(AtmPagingState) uint8_t raw[sizeof(AtmPagingState)];
    std::memset(raw, 0xAA, sizeof(raw));
    auto* dst = new (raw) AtmPagingState;

    AtmPagingState src{};
    for (int i = 0; i < 8; ++i)
        src.pFFF7[i] = 0x11111111u * static_cast<uint32_t>(i + 1);
    src.aFF77 = 0xDEADBEEFu;
    src.pBD = 0x1234;
    src.pBE = 0x56;
    src.pBF = 0x78;
    src.aFE = 0x9A;
    src.aFB = 0xBC;
    src.atmMemSwapped = 1;
    src.cmos_addr = 0xDE;

    *dst = src;
    EXPECT_EQ(std::memcmp(dst, &src, sizeof(AtmPagingState)), 0)
        << "AtmPagingState has padding that assignment does not copy";
}

/// The whole memory map plus every latch must survive a round trip.
TEST(TtdAtmPaging_Test, RoundTripCarriesMemoryMapAndLatches)
{
    Emulator* emulator = MakeAtm();
    ASSERT_NE(emulator, nullptr) << "ATM710 could not be created";

    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;

    for (int i = 0; i < 8; ++i)
        state.pFFF7[i] = 0xA0000000u + static_cast<unsigned>(i);
    state.aFF77 = 0x0000002Au;
    state.pBD = 0xBEEF;
    state.pBE = 0x5A;
    state.pBF = 0x01;   // shaden
    state.aFE = 0x20;
    state.aFB = 0x40;
    state.atmMemSwapped = true;
    state.cmos_addr = 0x0D;

    ttd::TTDAtmPaging serializer(context);
    uint8_t blob[sizeof(ttd::AtmPagingState)] = {};
    serializer.TTDSaveState(blob);

    // Scribble every field the blob owns.
    for (int i = 0; i < 8; ++i)
        state.pFFF7[i] = 0;
    state.aFF77 = 0;
    state.pBD = 0;
    state.pBE = 0;
    state.pBF = 0;
    state.aFE = 0;
    state.aFB = 0;
    state.atmMemSwapped = false;
    state.cmos_addr = 0;

    serializer.TTDLoadState(blob);

    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(state.pFFF7[i], 0xA0000000u + static_cast<unsigned>(i))
            << "pFFF7[" << i << "] - the ATM memory map must round-trip";
    EXPECT_EQ(state.aFF77, 0x0000002Au);
    EXPECT_EQ(state.pBD, 0xBEEF);
    EXPECT_EQ(state.pBE, 0x5A);
    EXPECT_EQ(state.pBF, 0x01);
    EXPECT_EQ(state.aFE, 0x20);
    EXPECT_EQ(state.aFB, 0x40);
    EXPECT_TRUE(state.atmMemSwapped);
    EXPECT_EQ(state.cmos_addr, 0x0D);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// Every byte the blob carries must move the hash, or a divergence in that
/// field goes unnoticed by the oracle.
TEST(TtdAtmPaging_Test, HashRespondsToEveryCarriedField)
{
    Emulator* emulator = MakeAtm();
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;
    ttd::TTDAtmPaging serializer(context);

    const uint64_t base = serializer.TTDHashState();

    state.pFFF7[3] ^= 0x00000010u;
    EXPECT_NE(serializer.TTDHashState(), base) << "pFFF7 not hashed";
    state.pFFF7[3] ^= 0x00000010u;

    state.aFF77 ^= 0x01u;
    EXPECT_NE(serializer.TTDHashState(), base) << "aFF77 not hashed";
    state.aFF77 ^= 0x01u;

    state.pBD ^= 0x0100;
    EXPECT_NE(serializer.TTDHashState(), base) << "pBD not hashed";
    state.pBD ^= 0x0100;

    state.pBE ^= 0x01;
    EXPECT_NE(serializer.TTDHashState(), base) << "pBE not hashed";
    state.pBE ^= 0x01;

    state.pBF ^= 0x01;
    EXPECT_NE(serializer.TTDHashState(), base) << "pBF not hashed";
    state.pBF ^= 0x01;

    state.aFE ^= 0x01;
    EXPECT_NE(serializer.TTDHashState(), base) << "aFE not hashed";
    state.aFE ^= 0x01;

    state.aFB ^= 0x01;
    EXPECT_NE(serializer.TTDHashState(), base) << "aFB not hashed";
    state.aFB ^= 0x01;

    state.atmMemSwapped = !state.atmMemSwapped;
    EXPECT_NE(serializer.TTDHashState(), base) << "atmMemSwapped not hashed";
    state.atmMemSwapped = !state.atmMemSwapped;

    state.cmos_addr ^= 0x01;
    EXPECT_NE(serializer.TTDHashState(), base) << "cmos_addr not hashed";
    state.cmos_addr ^= 0x01;

    EXPECT_EQ(serializer.TTDHashState(), base) << "hash is not a pure function of state";

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The framework only ever talks to the registry, so the full path must work
/// through CaptureAll / RestoreAll rather than the device API.
TEST(TtdAtmPaging_Test, RegistryCaptureAllRestoresMemoryMap)
{
    Emulator* emulator = MakeAtm();
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;

    ttd::TTDPeripheralRegistry registry;
    ttd::TTDAtmPaging serializer(context);
    registry.Register(ttd::PeripheralId::AtmPaging, &serializer);

    state.pFFF7[0] = 0x12345678u;
    state.atmMemSwapped = true;

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    ASSERT_EQ(blobs.count(static_cast<uint8_t>(ttd::PeripheralId::AtmPaging)), 1u);

    state.pFFF7[0] = 0;
    state.atmMemSwapped = false;

    const auto report = registry.RestoreAll(blobs);
    EXPECT_EQ(report.restored, 1u);
    EXPECT_TRUE(report.Complete());
    EXPECT_EQ(state.pFFF7[0], 0x12345678u);
    EXPECT_TRUE(state.atmMemSwapped);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The decoder must declare AND implement its state - nothing else registers
/// it, and a declaration without a serializer refuses recording by design.
TEST(TtdAtmPaging_Test, AtmDecodersDeclareAndProvideTheSerializer)
{
    for (const char* model : {"ATM710", "ATM3"})
    {
        Emulator* emulator = MakeAtm(model);
        if (emulator == nullptr)
            continue;  // model not provisionable in this build

        EmulatorContext* context = emulator->GetContext();
        ASSERT_NE(context->pPortDecoder, nullptr) << model;

        const auto declared = context->pPortDecoder->GetTTDModelStateIds();
        EXPECT_NE(std::find(declared.begin(), declared.end(), ttd::PeripheralId::AtmPaging),
                  declared.end())
            << model << " must declare AtmPaging state";

        // Recording must therefore start: the guard refuses a model whose
        // declared state has no serializer behind it.
        EXPECT_TRUE(context->pTimeTravelManager->StartRecording())
            << model << ": declared state has no serializer";
        EXPECT_TRUE(context->pTimeTravelManager->GetPeripheralRegistry()
                        .IsRegistered(ttd::PeripheralId::AtmPaging))
            << model << ": AtmPaging not registered for the session";
        context->pTimeTravelManager->StopRecording();

        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

/// End-to-end: the contract that actually matters. Record, change the memory
/// map, seek back, and both the map and the bank mapping derived from it must
/// be the recorded ones again. Without the serializer the seek restores the
/// standard 128K latches and the machine silently lands on other memory.
TEST(TtdAtmPaging_Test, SeekRestoresMemoryMapAndResultingBanks)
{
    Emulator* emulator = MakeAtm();
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;
    Memory* memory = context->pMemory;
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;

    ASSERT_TRUE(ttd->StartRecording());

    // Drive the map off its power-on values so a serializer that captured
    // nothing would still be caught.
    for (int i = 0; i < 8; ++i)
        state.pFFF7[i] = 0xC0000000u + static_cast<unsigned>(i);
    state.atmMemSwapped = true;
    memory->UpdateZ80Banks();

    const unsigned mapAtCapture0 = state.pFFF7[0];
    const uint8_t* bankAtCapture = memory->MapZ80AddressToPhysicalAddress(0x8000);

    ttd->OnFrameBoundary();
    ASSERT_GE(ttd->GetCheckpointCount(), 1u);
    const uint64_t frame = state.frame_counter;

    ttd->StopRecording();

    // Move the map somewhere else entirely.
    for (int i = 0; i < 8; ++i)
        state.pFFF7[i] = 0x10000000u + static_cast<unsigned>(i);
    state.atmMemSwapped = false;
    memory->UpdateZ80Banks();
    ASSERT_NE(state.pFFF7[0], mapAtCapture0);

    ttd::TTDTimePoint target;
    target.frame = frame;
    target.tInFrame = 0;
    ASSERT_TRUE(ttd->SeekTo(target));

    EXPECT_EQ(state.pFFF7[0], mapAtCapture0) << "ATM memory map not restored by the seek";
    EXPECT_TRUE(state.atmMemSwapped) << "address-swap flag not restored";
    EXPECT_EQ(memory->MapZ80AddressToPhysicalAddress(0x8000), bankAtCapture)
        << "map restored but the paging decode was not rebuilt from it";

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A seek across a CPU speed change must restore the clock, not leave the
/// machine at whatever speed it happens to be running now.
///
/// The turbo derivation runs only on the port write that causes it
/// (PortDecoder_ATM710::updateTurboMode); RestoreCheckpoint re-runs the paging
/// decode but not that, so the multiplier has to be captured. It also feeds the
/// audio descale (EmulatorState::AudioTstate), so getting it wrong after a seek
/// mispitches the AY and beeper as well as running the CPU at the wrong speed.
TEST(TtdAtmPaging_Test, SeekRestoresCpuClockAcrossTurboChange)
{
    Emulator* emulator = MakeAtm();
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;

    ASSERT_TRUE(ttd->StartRecording());

    // Record at 14 MHz (ATM turbo: CPU x4 inside an unchanged 20 ms frame).
    state.current_z80_frequency_multiplier = 4;
    state.next_z80_frequency_multiplier = 4;
    state.hw_turbo_shift = 2;
    state.hw_turbo_shift_applied = 2;

    ttd->OnFrameBoundary();
    ASSERT_GE(ttd->GetCheckpointCount(), 1u);
    const uint64_t frame = state.frame_counter;

    ttd->StopRecording();

    // Drop back to 3.5 MHz, as a guest write to #FF77/#EFF7 would.
    state.current_z80_frequency_multiplier = 1;
    state.next_z80_frequency_multiplier = 1;
    state.hw_turbo_shift = 0;
    state.hw_turbo_shift_applied = 0;

    ttd::TTDTimePoint target;
    target.frame = frame;
    target.tInFrame = 0;
    ASSERT_TRUE(ttd->SeekTo(target));

    EXPECT_EQ(state.current_z80_frequency_multiplier, 4) << "CPU clock not restored by the seek";
    EXPECT_EQ(state.hw_turbo_shift_applied, 2) << "hardware turbo shift not restored";
    EXPECT_EQ(state.AudioTstate(1000u), 250u)
        << "audio descale wrong after seek - AY/beeper would be mispitched";
    EXPECT_EQ(state.HostSpeedMultiplier(), 1)
        << "hardware turbo must not be counted as host fast-forward";

    EmulatorTestHelper::CleanupEmulator(emulator);
}
