#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <sstream>

#include "_helpers/soundcardscope.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

/// @file ttdgeneralsoundswitch_test.cpp
/// @brief Regression for a use-after-free: GS personality switching while a
/// TTD recording is active.
///
/// `TTDPeripheralRegistry` learns about `GeneralSoundCard*` exactly once, at
/// `StartRecording`/session-load time (`RegisterModelPeripherals`, private).
/// `SoundManager::switchGeneralSoundCard` deletes the outgoing card and
/// installs a new one under the same `SoundManager::getGeneralSound()`
/// pointer - but the registry's own copy of the pointer is not that indirect
/// accessor, it is the raw `GeneralSoundCard*` handed to `Register()` at
/// start time. Without `TimeTravelManager::UpdatePeripheral`, the registry
/// keeps pointing at the just-freed card, and the next checkpoint's
/// `TTDStateSize()`/`TTDSaveState()` call operates on freed memory.
///
/// All 5 automation surfaces (WebAPI/MCP/CLI/Lua/Python) can trigger a
/// personality switch, so a recording session sitting on any of them is one
/// switch call away from this - not a contrived scenario.

namespace
{
/// ATM710 ships `GSType=Z80` (data/configs/atm710/unreal.ini) - a real GS
/// card is fitted from boot, no extra wiring needed to get one.
constexpr const char* kGsCapableModel = "ATM710";
}  // namespace

class TTDGeneralSoundSwitch_Test : public ::testing::Test
{
protected:
    // Keep the General Sound / MoonSound cards the configs fit (the General Sound card is the subject):
    // declared first, so it is active before any machine is created
    SoundCardScope _soundCards;

protected:
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    SoundManager* sm = nullptr;

    void SetUp() override
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator(kGsCapableModel, LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr) << "ATM710 not provisionable in this build";

        context = emulator->GetContext();
        ASSERT_NE(context, nullptr);
        sm = context->pSoundManager;
        ASSERT_NE(sm, nullptr);
        ASSERT_NE(sm->getGeneralSound(), nullptr) << "ATM710 must fit a GS-slot card by default";
        // The tests start from the classic card (the shipped config fits NeoGS)
        ASSERT_TRUE(FitGeneralSoundCard(sm, GSTypeKind::Z80));

        FeatureManager* fm = emulator->GetFeatureManager();
        ASSERT_NE(fm, nullptr);
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
    }

    void TearDown() override
    {
        if (emulator)
            EmulatorTestHelper::CleanupEmulator(emulator);
    }
};

/// The core fix: after a switch during an active recording, the registry
/// must point at the *new* card, not the one just deleted. Checked by
/// identity, not by behaviour, so this fails deterministically (no reliance
/// on a heap allocator happening to crash on the stale pointer).
TEST_F(TTDGeneralSoundSwitch_Test, SwitchDuringRecordingRepointsRegistry)
{
    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());

    GeneralSoundCard* before = sm->getGeneralSound();
    const ttd::PeripheralId beforeId = before->TTDPeripheralId();
    ASSERT_EQ(beforeId, ttd::PeripheralId::GeneralSound) << "the fixture fits the classic card";
    ASSERT_EQ(context->pTimeTravelManager->GetPeripheralRegistry().GetDevice(beforeId),
              static_cast<ttd::TTDSerializable*>(before))
        << "registry must have picked up the boot-time card at StartRecording";

    ASSERT_TRUE(sm->switchGeneralSoundCard(GSTypeKind::LW));
    GeneralSoundCard* after = sm->getGeneralSound();
    ASSERT_NE(after, nullptr);
    EXPECT_NE(after, before) << "the switch must replace the card object (the freed pointer)";

    // LLE and LW register under different peripheral ids (GeneralSound vs
    // GeneralSoundLightweight - same split as the TurboSound/TSFM slots), so
    // the switch must move the registration, not just repoint the same slot.
    const ttd::PeripheralId afterId = after->TTDPeripheralId();
    ASSERT_EQ(afterId, ttd::PeripheralId::GeneralSoundLightweight);
    EXPECT_EQ(context->pTimeTravelManager->GetPeripheralRegistry().GetDevice(afterId),
              static_cast<ttd::TTDSerializable*>(after))
        << "UpdatePeripheral must register the new card under its own slot - "
           "otherwise the next checkpoint saves through a dangling pointer";
    EXPECT_EQ(context->pTimeTravelManager->GetPeripheralRegistry().GetDevice(beforeId), nullptr)
        << "the outgoing personality's slot must be vacated, not left pointing at the freed card";

    context->pTimeTravelManager->StopRecording();
}

/// Exercises the actual failure path: a checkpoint captured *after* the
/// switch must read the new card's live state, not crash on (or silently
/// read through) the deleted one.
TEST_F(TTDGeneralSoundSwitch_Test, CheckpointAfterSwitchCapturesTheNewCard)
{
    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    ASSERT_TRUE(sm->switchGeneralSoundCard(GSTypeKind::LW));
    GeneralSoundCard* after = sm->getGeneralSound();
    ASSERT_NE(after, nullptr);
    ASSERT_EQ(after->implementation(), GSCardImplementation::LW);

    // A direct CaptureAll, the same call TTDStateManager makes on every
    // checkpoint - this is the call that would have touched freed memory.
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    context->pTimeTravelManager->GetPeripheralRegistry().CaptureAll(blobs);

    const auto afterId = static_cast<uint8_t>(after->TTDPeripheralId());
    const auto it = blobs.find(afterId);
    ASSERT_NE(it, blobs.end()) << "the LW card's own slot must still be registered and captured post-switch";

    const auto restored = ttd::TTDPeripheralRegistry::DecodeBlob(afterId, it->second);
    EXPECT_EQ(restored.size(), after->TTDStateSize())
        << "captured blob must match the NEW (LW) card's state size, not a stale LLE one";

    context->pTimeTravelManager->StopRecording();
}

/// A switch with no recording in progress is the common case (every switch
/// in the existing GS test suite happens outside TTD) and must stay a no-op
/// for UpdatePeripheral - nothing to repoint, nothing to crash.
TEST_F(TTDGeneralSoundSwitch_Test, SwitchWithoutRecordingIsUnaffected)
{
    EXPECT_EQ(context->pTimeTravelManager->GetState(), ttd::TTDSessionState::Idle);
    ASSERT_TRUE(sm->switchGeneralSoundCard(GSTypeKind::LW));
    EXPECT_EQ(sm->getGeneralSound()->implementation(), GSCardImplementation::LW);
    EXPECT_EQ(context->pTimeTravelManager->GetState(), ttd::TTDSessionState::Idle);
}

/// NeoGS records on TTD v1: every checkpoint carries the whole card - RAM
/// and flash included (neogs-tdd.md §7.4)
TEST_F(TTDGeneralSoundSwitch_Test, NeoGSRecordsWithTheWholeCardInEveryCheckpoint)
{
    ASSERT_TRUE(sm->switchGeneralSoundCard(GSTypeKind::NGS));
    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());

    GeneralSoundCard* card = sm->getGeneralSound();
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    context->pTimeTravelManager->GetPeripheralRegistry().CaptureAll(blobs);
    const auto id = static_cast<uint8_t>(ttd::PeripheralId::NeoGS);
    ASSERT_NE(blobs.find(id), blobs.end());
    const auto restored = ttd::TTDPeripheralRegistry::DecodeBlob(id, blobs[id]);
    EXPECT_EQ(restored.size(), card->TTDStateSize());
    EXPECT_GT(restored.size(), card->getRamSizeKB() * 1024 + 512u * 1024) << "RAM and flash";
    context->pTimeTravelManager->StopRecording();
}

TEST_F(TTDGeneralSoundSwitch_Test, SwitchToNeoGSDuringRecordingRepointsRegistry)
{
    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    ASSERT_TRUE(sm->switchGeneralSoundCard(GSTypeKind::NGS));
    GeneralSoundCard* after = sm->getGeneralSound();
    EXPECT_EQ(context->pTimeTravelManager->GetPeripheralRegistry().GetDevice(ttd::PeripheralId::NeoGS),
              static_cast<ttd::TTDSerializable*>(after));
    EXPECT_EQ(context->pTimeTravelManager->GetPeripheralRegistry().GetDevice(ttd::PeripheralId::GeneralSound), nullptr);
    EmulatorTestHelper::RunFramesFast(emulator, 2); // checkpoints save through the new card
    context->pTimeTravelManager->StopRecording();
}

/// A session recorded with the classic card must not load on an instance
/// fitted with another GS-slot card: RestoreAll would restore neither
TEST_F(TTDGeneralSoundSwitch_Test, SessionFromAnotherGsPersonalityIsRefused)
{
    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    EmulatorTestHelper::RunFramesFast(emulator, 3);
    std::stringstream session;
    std::string err;
    ASSERT_TRUE(context->pTimeTravelManager->SerializeSession(session, err)) << err;
    context->pTimeTravelManager->StopRecording();

    ASSERT_TRUE(sm->switchGeneralSoundCard(GSTypeKind::LW));
    session.seekg(0);
    EXPECT_FALSE(context->pTimeTravelManager->DeserializeSession(session, err));
    EXPECT_NE(err.find("General Sound slot mismatch: recorded with GS, fitted: GS lightweight"), std::string::npos) << err;

    ASSERT_TRUE(sm->switchGeneralSoundCard(GSTypeKind::Z80));
    session.clear();
    session.seekg(0);
    EXPECT_TRUE(context->pTimeTravelManager->DeserializeSession(session, err)) << err;
}
