#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/profi/ttdprofipaging.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/profifixture.h"

/// @brief TTD serializer for the Profi model state: #DFFD latch and the hi-res palette.
class TTDProfiPaging_Test : public ProfiMachineFixture
{
protected:
    EmulatorState& State() { return _context->emulatorState; }
};

/// @brief The decoder declares the state it owns and provides the matching serializer
///        (TTD refuses to record when a declared id has no serializer)
TEST_F(TTDProfiPaging_Test, DecoderDeclaresProfiPaging)
{
    const std::vector<ttd::PeripheralId> ids = _context->pPortDecoder->GetTTDModelStateIds();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], ttd::PeripheralId::ProfiPaging);

    auto serializers = _context->pPortDecoder->CreateTTDSerializers();
    ASSERT_EQ(serializers.size(), 1u);
    EXPECT_EQ(serializers[0]->TTDPeripheralId(), ttd::PeripheralId::ProfiPaging);
    EXPECT_EQ(serializers[0]->TTDDeviceName(), "ProfiPaging");
}

/// @brief Save -> mutate -> load restores #DFFD and every palette entry
TEST_F(TTDProfiPaging_Test, SaveLoadRoundTrip)
{
    ttd::TTDProfiPaging serializer(_context);

    State().pDFFD = 0xA5;
    for (uint8_t i = 0; i < 16; i++)
        State().profiPalette[i] = static_cast<uint8_t>(0x10 + i);

    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());

    State().pDFFD = 0;
    std::memset(State().profiPalette, 0, sizeof(State().profiPalette));

    serializer.TTDLoadState(blob.data());

    EXPECT_EQ(State().pDFFD, 0xA5);
    for (uint8_t i = 0; i < 16; i++)
        EXPECT_EQ(State().profiPalette[i], 0x10 + i) << "palette entry " << static_cast<int>(i);
}

/// @brief The divergence hash covers every field of the blob
TEST_F(TTDProfiPaging_Test, HashSensitiveToEveryField)
{
    ttd::TTDProfiPaging serializer(_context);
    const uint64_t baseline = serializer.TTDHashState();

    State().pDFFD ^= 0x01;
    EXPECT_NE(serializer.TTDHashState(), baseline) << "pDFFD";
    State().pDFFD ^= 0x01;
    EXPECT_EQ(serializer.TTDHashState(), baseline);

    for (uint8_t i = 0; i < 16; i++)
    {
        State().profiPalette[i] ^= 0x40;
        EXPECT_NE(serializer.TTDHashState(), baseline) << "palette entry " << static_cast<int>(i);
        State().profiPalette[i] ^= 0x40;
    }
}

/// @brief T4 (2026-09-25 reconciliation report section 4.2): the DOS-latch/session
///        flags (CF_TRDOS, CF_DOSPORTS) are folded into the hash even though they are
///        NOT part of the serialized blob - MachineStateSnapshot itself never hashes
///        `flags`, so a checkpoint differing only in the DOS latch was otherwise
///        invisible to the divergence detector. Unrelated flag bits must not perturb it.
TEST_F(TTDProfiPaging_Test, HashSensitiveToDosLatchFlagsButNotToOthers)
{
    ttd::TTDProfiPaging serializer(_context);
    State().flags &= ~(CF_TRDOS | CF_DOSPORTS);
    const uint64_t baseline = serializer.TTDHashState();

    State().flags |= CF_TRDOS;
    EXPECT_NE(serializer.TTDHashState(), baseline) << "CF_TRDOS";
    State().flags &= ~CF_TRDOS;
    EXPECT_EQ(serializer.TTDHashState(), baseline);

    State().flags |= CF_DOSPORTS;
    EXPECT_NE(serializer.TTDHashState(), baseline) << "CF_DOSPORTS";
    State().flags &= ~CF_DOSPORTS;
    EXPECT_EQ(serializer.TTDHashState(), baseline);

    // A flag outside the folded mask (e.g. CF_LEAVEDOSADR, re-derived by
    // Memory::UpdateZ80Banks on every restore) must not change the hash
    State().flags |= CF_LEAVEDOSADR;
    EXPECT_EQ(serializer.TTDHashState(), baseline) << "CF_LEAVEDOSADR is not part of the folded mask";
    State().flags &= ~CF_LEAVEDOSADR;
}

/// @brief T4 must not change what gets saved/restored or the blob size - only the hash
TEST_F(TTDProfiPaging_Test, DosLatchFlagsAreNotPartOfTheSavedBlob)
{
    ttd::TTDProfiPaging serializer(_context);
    ASSERT_EQ(serializer.TTDStateSize(), 34u) << "T4 must not resize the persisted blob";

    State().flags &= ~(CF_TRDOS | CF_DOSPORTS);
    std::vector<uint8_t> blobFlagsOff(serializer.TTDStateSize());
    serializer.TTDSaveState(blobFlagsOff.data());

    State().flags |= (CF_TRDOS | CF_DOSPORTS);
    std::vector<uint8_t> blobFlagsOn(serializer.TTDStateSize());
    serializer.TTDSaveState(blobFlagsOn.data());

    EXPECT_EQ(blobFlagsOff, blobFlagsOn) << "the DOS-latch flags must not leak into the saved blob";
}

/// @brief Restore through the serializer plus the paging re-decode rebuilds the RAM windows
TEST_F(TTDProfiPaging_Test, RestoreRebuildsBankMap)
{
    ttd::TTDProfiPaging serializer(_context);

    _context->pPortDecoder->DecodePortOut(0xDFFD, 0x0B, 0x0000);   // high bits 3, SCO
    _context->pPortDecoder->DecodePortOut(0x7FFD, 0x02, 0x0000);
    const uint8_t tag4000 = BankTag(0x4000);
    const uint8_t tagC000 = BankTag(0xC000);

    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());

    _context->pPortDecoder->DecodePortOut(0xDFFD, 0x00, 0x0000);
    ASSERT_NE(BankTag(0x4000), tag4000);

    serializer.TTDLoadState(blob.data());
    _memory->UpdateZ80Banks();

    EXPECT_EQ(BankTag(0x4000), tag4000);
    EXPECT_EQ(BankTag(0xC000), tagC000);
}

/// End-to-end through the real TimeTravelManager on a real PROFI machine: recording must be accepted
/// (the declared model state has a serializer), and a seek must restore #DFFD, the palette and the
/// bank map derived from them. Without the serializer the seek keeps the CURRENT #DFFD and the machine
/// silently lands in the wrong memory and video mode.
TEST(TTDProfiPagingSeek_Test, SeekRestoresDffdPaletteAndBankMap)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PROFI", LoggerLevel::LogError);
    if (emulator == nullptr)
        GTEST_SKIP() << "PROFI is not creatable (missing configs/profi or data/rom/profi.rom)";

    FeatureManager* fm = emulator->GetFeatureManager();
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kTimeTravel, true);

    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;
    Memory* memory = context->pMemory;
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;

    ASSERT_TRUE(ttd->StartRecording()) << "recording refused: Profi declares state without a serializer?";

    // Drive the machine off its power-on values: high RAM bits 5, SCO, SCR, hi-res, custom palette
    context->pPortDecoder->DecodePortOut(0xDFFD, 0x80 | 0x40 | 0x08 | 0x05, 0x0000);
    context->pPortDecoder->DecodePortOut(0x7FFD, 0x03, 0x0000);
    for (uint8_t i = 0; i < 16; i++)
        state.profiPalette[i] = static_cast<uint8_t>(0x20 + i);

    const uint8_t dffdAtCapture = state.pDFFD;
    const uint8_t* bank1AtCapture = memory->MapZ80AddressToPhysicalAddress(0x4000);
    const uint8_t* bank2AtCapture = memory->MapZ80AddressToPhysicalAddress(0x8000);
    const uint8_t* bank3AtCapture = memory->MapZ80AddressToPhysicalAddress(0xC000);

    ttd->OnFrameBoundary();
    ASSERT_GE(ttd->GetCheckpointCount(), 1u);
    const uint64_t frame = state.frame_counter;
    ttd->StopRecording();

    // Move everything somewhere else
    context->pPortDecoder->DecodePortOut(0xDFFD, 0x00, 0x0000);
    context->pPortDecoder->DecodePortOut(0x7FFD, 0x00, 0x0000);
    std::memset(state.profiPalette, 0, sizeof(state.profiPalette));
    ASSERT_NE(state.pDFFD, dffdAtCapture);

    ttd::TTDTimePoint target;
    target.frame = frame;
    target.tInFrame = 0;
    ASSERT_TRUE(ttd->SeekTo(target));

    EXPECT_EQ(state.pDFFD, dffdAtCapture) << "#DFFD not restored by the seek";
    for (uint8_t i = 0; i < 16; i++)
        EXPECT_EQ(state.profiPalette[i], 0x20 + i) << "palette entry " << static_cast<int>(i);
    EXPECT_EQ(memory->MapZ80AddressToPhysicalAddress(0x4000), bank1AtCapture);
    EXPECT_EQ(memory->MapZ80AddressToPhysicalAddress(0x8000), bank2AtCapture);
    EXPECT_EQ(memory->MapZ80AddressToPhysicalAddress(0xC000), bank3AtCapture)
        << "map restored but the paging decode was not rebuilt from #DFFD";

    EmulatorTestHelper::CleanupEmulator(emulator);
}
