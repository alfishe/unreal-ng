/// @file ttdtsfm_test.cpp
/// @brief TSFM TTD session-kind guard (TSFM design §8.2, plan P3).
///
/// P3 scope: the guard itself - a session recorded with one TurboSound-slot
/// device (legacy = blob id 0, TSFM = id 4) must be refused when loaded into
/// an instance running the other. Until the real TSFM device lands (P4/P5)
/// the guard is exercised with a fake id-4 slot device and, end to end, with
/// a real recorded session whose baseline TurboSound blob is forged as a
/// TSFM blob. P5 extends this file with payload/seek tests against the real
/// device.

#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttddumpformat.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/soundmanager.h"

using BlobMap = std::unordered_map<uint8_t, std::vector<uint8_t>>;

/// Minimal TurboSound-slot stand-in: the guard only asks for the id
/// (and the blob map, which the tests build by hand).
class FakeTurboSoundSlotDevice final : public ttd::TTDSerializable
{
public:
    explicit FakeTurboSoundSlotDevice(ttd::PeripheralId id) : _id(id) {}

    size_t TTDStateSize() const override { return sizeof(uint32_t); }
    void TTDSaveState(uint8_t* dst) const override
    {
        const uint32_t marker = 0x5A5A5A5Au;
        std::memcpy(dst, &marker, sizeof marker);
    }
    void TTDLoadState(const uint8_t* /*src*/) override {}
    std::string TTDDeviceName() const override { return "FakeTurboSoundSlotDevice"; }
    ttd::PeripheralId TTDPeripheralId() const override { return _id; }

private:
    ttd::PeripheralId _id;
};

class TtdTsfm_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;

    void SetUp() override
    {
        // The session-kind guard models the legacy slot; the shipped default
        // is FM now, so stage the default machine's ini with TurboSound=AY
        _emulator = new Emulator(LoggerLevel::LogError);
        _emulator->SetCustomConfigPath(
            EmulatorTestHelper::StageTurboSoundKindConfig(TurboSoundKind::AY));
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);

        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    void RunFrames(size_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }

    /// Record + serialize a short real session on the legacy device.
    std::string MakeLegacySession()
    {
        EXPECT_TRUE(_ttd->StartRecording());
        RunFrames(2);
        _ttd->StopRecording();

        std::ostringstream out(std::ios::binary);
        std::string err;
        EXPECT_TRUE(_ttd->SerializeSession(out, err)) << err;
        return out.str();
    }

    /// Locate the baseline checkpoint's TurboSound blob inside a serialized
    /// .ttd stream. Mirrors DeserializeSession's reads exactly (header PODs,
    /// page store slots, checkpoint 0's fixed fields and RAM refs, then the
    /// blob map: count u16, per blob id u8 + length u32 + body). The guard
    /// reads only the map key, but the blob's own header id is patched too
    /// (see SessionKindMismatchRefused) so the forged stream matches what a
    /// real TSFM recorder would have written.
    /// @param keyPos  Offset of the blob map's id byte for the TurboSound blob.
    /// @param blobPos Offset of the blob body's first byte (its header id).
    static bool FindBaselineTurboSoundBlob(const std::string& s, size_t& keyPos, size_t& blobPos)
    {
        size_t p = 0;
        auto skip = [&p](size_t n) { p += n; };

        // Header (field order and sizes mirror the reader in DeserializeSession)
        skip(4);   // magic
        skip(2);   // schema version
        skip(2);   // flags
        skip(1);   // model id
        const size_t modelRamPagesAt = p;
        skip(2);   // model ram pages
        skip(2);   // cpu state size
        skip(2);   // chipset state size
        skip(8);   // rom signature
        skip(8);   // captured at ms
        if (p > s.size())
            return false;

        const size_t emulatorIdLen = static_cast<size_t>(static_cast<uint8_t>(s[p]));
        skip(1 + emulatorIdLen);
        skip(1);   // session state
        skip(8);   // session start
        skip(8);   // session end
        if (p + 4 > s.size())
            return false;
        uint32_t pageStoreCount = 0;
        std::memcpy(&pageStoreCount, s.data() + p, sizeof pageStoreCount);
        skip(4);
        skip(4);   // checkpoint count
        skip(8);   // reserved

        if (p > s.size())
            return false;

        // Page store: per slot enc u8 + refcount/prevSlot/crc/payloadSize u32
        // each + payload
        for (uint32_t i = 0; i < pageStoreCount; ++i)
        {
            if (p + 17 > s.size())
                return false;
            skip(1);   // encoding
            skip(4);   // refcount
            skip(4);   // prev slot
            skip(4);   // crc
            uint32_t payloadSize = 0;
            std::memcpy(&payloadSize, s.data() + p, sizeof payloadSize);
            skip(4);
            skip(payloadSize);
        }

        // Checkpoint 0 fixed fields
        skip(8);   // time.frame (u64)
        skip(8);   // globalT (u64)
        skip(1);   // frame kind
        skip(8);   // keyframe anchor (u64)
        skip(sizeof(ttd::TTDCpuState));
        skip(sizeof(ttd::TTDChipsetState));

        // RAM page refs: modelRamPages * kSubPagesPerEmuPage u32 slots
        uint16_t modelRamPages = 0;
        std::memcpy(&modelRamPages, s.data() + modelRamPagesAt, sizeof modelRamPages);
        skip(static_cast<size_t>(modelRamPages) * ttd::dump::kSubPagesPerEmuPage * 4);

        if (p + 2 > s.size())
            return false;
        uint16_t blobCount = 0;
        std::memcpy(&blobCount, s.data() + p, sizeof blobCount);
        skip(2);

        // Blob map: per blob id u8 + WriteBlob framing (length u32 + body)
        for (uint16_t b = 0; b < blobCount; ++b)
        {
            if (p + 5 > s.size())
                return false;
            const size_t idAt = p;
            skip(1);
            const size_t lenAt = p;
            skip(4);
            uint32_t blobLen = 0;
            std::memcpy(&blobLen, s.data() + lenAt, sizeof blobLen);
            if (p + blobLen > s.size())
                return false;

            uint8_t id = 0;
            std::memcpy(&id, s.data() + idAt, sizeof id);
            if (id == static_cast<uint8_t>(ttd::PeripheralId::TurboSound))
            {
                keyPos = idAt;
                blobPos = lenAt + sizeof blobLen;
                return true;
            }
            skip(blobLen);
        }
        return false;
    }
};

// ===========================================================================
// Decision core (TimeTravelManager::TurboSoundSessionKindMatches)
// ===========================================================================

TEST_F(TtdTsfm_Test, KindMatchesLegacySessionOnLegacyDevice)
{
    BlobMap blobs{{static_cast<uint8_t>(ttd::PeripheralId::TurboSound), {}}};
    FakeTurboSoundSlotDevice legacy(ttd::PeripheralId::TurboSound);
    EXPECT_TRUE(ttd::TimeTravelManager::TurboSoundSessionKindMatches(blobs, legacy));
}

TEST_F(TtdTsfm_Test, KindMatchesFmSessionOnFmDevice)
{
    BlobMap blobs{{static_cast<uint8_t>(ttd::PeripheralId::TSFM), {}}};
    FakeTurboSoundSlotDevice fm(ttd::PeripheralId::TSFM);
    EXPECT_TRUE(ttd::TimeTravelManager::TurboSoundSessionKindMatches(blobs, fm));
}

TEST_F(TtdTsfm_Test, KindRefusesFmSessionOnLegacyDevice)
{
    BlobMap blobs{{static_cast<uint8_t>(ttd::PeripheralId::TSFM), {}}};
    FakeTurboSoundSlotDevice legacy(ttd::PeripheralId::TurboSound);
    EXPECT_FALSE(ttd::TimeTravelManager::TurboSoundSessionKindMatches(blobs, legacy));
}

TEST_F(TtdTsfm_Test, KindRefusesLegacySessionOnFmDevice)
{
    BlobMap blobs{{static_cast<uint8_t>(ttd::PeripheralId::TurboSound), {}}};
    FakeTurboSoundSlotDevice fm(ttd::PeripheralId::TSFM);
    EXPECT_FALSE(ttd::TimeTravelManager::TurboSoundSessionKindMatches(blobs, fm));
}

TEST_F(TtdTsfm_Test, KindIgnoresSessionWithoutSlotBlob)
{
    // Recorded on a machine without any slot device: not a mismatch
    // (RestoreAll's missingBlobs path already names that situation)
    BlobMap blobs{{static_cast<uint8_t>(ttd::PeripheralId::BetaDisk), {}}};
    FakeTurboSoundSlotDevice legacy(ttd::PeripheralId::TurboSound);
    FakeTurboSoundSlotDevice fm(ttd::PeripheralId::TSFM);
    EXPECT_TRUE(ttd::TimeTravelManager::TurboSoundSessionKindMatches(blobs, legacy));
    EXPECT_TRUE(ttd::TimeTravelManager::TurboSoundSessionKindMatches(blobs, fm));
}

TEST_F(TtdTsfm_Test, KindRefusesDefensiveBothIdsSession)
{
    // One device occupies the slot; a session carrying both ids cannot come
    // from a healthy writer - refuse rather than guess
    BlobMap blobs{{static_cast<uint8_t>(ttd::PeripheralId::TurboSound), {}},
                  {static_cast<uint8_t>(ttd::PeripheralId::TSFM), {}}};
    FakeTurboSoundSlotDevice legacy(ttd::PeripheralId::TurboSound);
    EXPECT_FALSE(ttd::TimeTravelManager::TurboSoundSessionKindMatches(blobs, legacy));
}

// ===========================================================================
// End to end: DeserializeSession wiring (plan gate: SessionKindMismatchRefused)
// ===========================================================================

TEST_F(TtdTsfm_Test, SessionKindMismatchRefused)
{
    const std::string data = MakeLegacySession();
    ASSERT_FALSE(data.empty());

    // Sanity first: the same-kind session loads (the guard must not
    // false-positive on a normal legacy recording)
    {
        std::istringstream in(data, std::ios::binary);
        std::string err;
        EXPECT_TRUE(_ttd->DeserializeSession(in, err)) << err;
    }
    _ttd->InvalidateSession("test");

    // Forge the baseline TurboSound blob (id 0) as a TSFM blob (id 4): patch
    // the blob map key and the blob header's own id byte. Everything else
    // stays byte-identical, so a refusal can only come from the guard.
    std::string forged = data;
    size_t keyPos = 0;
    size_t blobPos = 0;
    ASSERT_TRUE(FindBaselineTurboSoundBlob(forged, keyPos, blobPos));
    forged[keyPos] = static_cast<uint8_t>(ttd::PeripheralId::TSFM);
    forged[blobPos] = static_cast<uint8_t>(ttd::PeripheralId::TSFM);

    {
        std::istringstream in(forged, std::ios::binary);
        std::string err;
        EXPECT_FALSE(_ttd->DeserializeSession(in, err));
        EXPECT_NE(err.find("TurboSound slot mismatch"), std::string::npos) << err;
    }

    // The live machine state is untouched by the refused load (restores only
    // happen on SeekTo, never during DeserializeSession)
    EXPECT_EQ(_context->pSoundManager->getTurboSound()->TTDPeripheralId(),
              ttd::PeripheralId::TurboSound);
}

// ===========================================================================
// Regression coverage for the P5 gap (fixed 2026-09-23): until now,
// SoundChip_TurboSoundFM::TTDStateSize() was the P3-era stub (always 0), so
// TTDPeripheralRegistry::CaptureAll (which skips any device reporting 0
// bytes) never wrote a TurboSound/TSFM blob to ANY checkpoint - confirmed
// against a real user recording (scratch/tsfm-issues.ttd: 0/1169
// checkpoints). §8.2's real TTDStateSize/TTDSaveState/TTDLoadState/
// TTDHashState are now implemented in soundchip_turbosoundfm.cpp (v4,
// 2008-byte payload: board latches + the render-loop's free-running
// _samplePhase/_decimationPhase accumulators + per-chip address/fmClockPhase/
// timers/busy + ymfm::ym2203::save_restore() + the SSG half's existing
// AY8910 serializer). The render-phase fields were added after
// SeekTo_NoiseGeneratorStateDeterministicFromDeltaFrame below caught a real
// generator-phase drift bug in the original 1142-byte v1 payload.
// See docs/inprogress/2026-09-10-turbosound-fm/ttd-fm-state-gap.md for the
// full root-cause writeup and layout. These tests are the regression gate
// that should have caught the original stub - keep them green.
// ===========================================================================

/// region <SoundChip_TurboSoundFM serializer tests>

class TTD_TurboSoundFM_Serializer_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    SoundChip_TurboSoundFM* _fmA = nullptr;
    SoundChip_TurboSoundFM* _fmB = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _fmA = new SoundChip_TurboSoundFM(_context);
        _fmB = new SoundChip_TurboSoundFM(_context);
    }

    void TearDown() override
    {
        delete _fmA;
        delete _fmB;
        delete _context;
    }
};

TEST_F(TTD_TurboSoundFM_Serializer_Test, TTDStateSize_IsNonZero)
{
    // Design §8.2 (v5) pins this at 2008 bytes (1 version + 1 board + 48
    // render-phase accumulators (samplePhase, decimationPhase, 4 per-
    // decimator phases) + 2 x (1 address + 4 fmClockPhase + 2x4 timers + 4
    // busy + 2 ymfmSize + 494 ymfm payload + 73 AY payload) + 778 timeline
    // tail (render cursor + pending timed SSG writes) + 8 frame-progress
    // tail (render position + samples produced this frame)). A
    // registered TTD-capable device that always reports 0 bytes is invisible
    // to TTDPeripheralRegistry::CaptureAll - it is never
    // written to any checkpoint, which is the root cause of this bug.
    EXPECT_GT(_fmA->TTDStateSize(), 0u)
        << "SoundChip_TurboSoundFM::TTDStateSize() is still the P3 stub (returns "
           "0) - CaptureAll silently skips this device on every checkpoint. "
           "See ttd-fm-state-gap.md for the full layout to implement.";
}

TEST_F(TTD_TurboSoundFM_Serializer_Test, RoundTrip_SsgRegistersPreserved)
{
    // Write distinct SSG register patterns into both chips via the real
    // register-write path (through the SSG half each TsfmChip already owns),
    // then prove they survive a TTD save/load cycle. This is deliberately
    // the weakest possible bar (CPU-visible registers only, not FM synth
    // phase) - the outer device must delegate to the SSG's own AY8910
    // serializer for each chip.
    SoundChip_AY8910* ssg0 = _fmA->getChip(0);
    SoundChip_AY8910* ssg1 = _fmA->getChip(1);
    ASSERT_NE(ssg0, nullptr);
    ASSERT_NE(ssg1, nullptr);
    ssg0->writeRegister(AY_A_FINE, 0x11);
    ssg1->writeRegister(AY_A_FINE, 0x22);

    ASSERT_GT(_fmA->TTDStateSize(), 0u)
        << "cannot round-trip a 0-byte payload; fix TTDStateSize first";

    std::vector<uint8_t> saved(_fmA->TTDStateSize());
    _fmA->TTDSaveState(saved.data());
    _fmB->TTDLoadState(saved.data());

    SoundChip_AY8910* b0 = _fmB->getChip(0);
    SoundChip_AY8910* b1 = _fmB->getChip(1);
    EXPECT_EQ(b0->getRegisters()[AY_A_FINE], 0x11)
        << "TSFM chip 0 SSG register file not restored";
    EXPECT_EQ(b1->getRegisters()[AY_A_FINE], 0x22)
        << "TSFM chip 1 SSG register file not restored";
}

/// endregion </SoundChip_TurboSoundFM serializer tests>

/// region <TimeTravelManager integration: TSFM blob is populated>

TEST(TTD_TSFM_ManagerIntegration_Test, CaptureNow_PopulatesTsfmStateBlob)
{
    // Mirrors TTD_AY_ManagerIntegration_Test.CaptureNow_PopulatesAyStateBlob
    // (ttdayserializer_test.cpp) but on the FM slot kind - this is the exact
    // configuration of the originally reported recording (scratch/tsfm-issues.ttd).
    Emulator emulator(LoggerLevel::LogError);
    emulator.SetCustomConfigPath(
        EmulatorTestHelper::StageTurboSoundKindConfig(TurboSoundKind::FM));
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pTimeTravelManager, nullptr);

    SoundManager* sm = context->pSoundManager;
    ASSERT_NE(sm, nullptr);
    ASSERT_NE(sm->getTurboSound(), nullptr)
        << "Test precondition: TSFM must be created by Init()";
    ASSERT_EQ(sm->getTurboSound()->TTDPeripheralId(), ttd::PeripheralId::TSFM)
        << "Test precondition: the FM slot kind must register under id 4";

    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());

    ASSERT_GE(context->pTimeTravelManager->GetCheckpointCount(), 1u);
    const ttd::TTDCheckpoint* cp = context->pTimeTravelManager->GetCheckpoint(0);
    ASSERT_NE(cp, nullptr);

    const auto tsfmBlob = cp->peripheralBlobs.find(
        static_cast<uint8_t>(ttd::PeripheralId::TSFM));
    ASSERT_NE(tsfmBlob, cp->peripheralBlobs.end())
        << "TSFM must register itself and appear in the checkpoint - "
           "TTDPeripheralRegistry::CaptureAll skips any device whose "
           "TTDStateSize()==0, which is the bug this test guards against. See "
           "docs/inprogress/2026-09-10-turbosound-fm/ttd-fm-state-gap.md.";
    const auto tsfmState = ttd::TTDPeripheralRegistry::DecodeBlob(
        static_cast<uint8_t>(ttd::PeripheralId::TSFM), tsfmBlob->second);
    // v5: 2 version/board + 48 render phases + 2 x 586 chip payloads (73-byte
    // AY payload each) + 778 timeline tail (render cursor + pending SSG writes)
    // + 8 frame-progress tail
    EXPECT_EQ(tsfmState.size(), 2008u)
        << "TSFM blob must contain the full §8.2 (v5) payload";

    emulator.Stop();
    emulator.Release();
}

/// Drives a real chiptune (not a synthetic register poke) into both YM2203s
/// - SSG registers, FM operator phase/envelope, timers - before a TTD
/// checkpoint is taken. testdata/sound/tsfm/tech_support.sna is paused right
/// after tape load, at the BASIC entry point, with both chips still at reset
/// (SOURCES.md); running it forward lets the player's own init + a few
/// frames of playback build up real state, the same way the originally
/// reported scratch/tsfm-issues.ttd recording was made.
TEST(TTD_TSFM_ManagerIntegration_Test, CaptureNow_PopulatesRealDemoPlaybackState)
{
    Emulator emulator(LoggerLevel::LogError);
    emulator.SetCustomConfigPath(
        EmulatorTestHelper::StageTurboSoundKindConfig(TurboSoundKind::FM));
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pTimeTravelManager, nullptr);

    const auto sna = TestPathHelper::FindProjectRoot() / "testdata/sound/tsfm/tech_support.sna";
    ASSERT_TRUE(emulator.LoadSnapshot(sna.string()))
        << "fixture missing: " << sna.string();

    // Let the player's init routine run and start driving both YM2203s.
    emulator.RunNFrames(50, /*skipBreakpoints=*/true);

    auto* fm = dynamic_cast<SoundChip_TurboSoundFM*>(context->pSoundManager->getTurboSound());
    ASSERT_NE(fm, nullptr) << "Test precondition: FM slot kind must be active";

    // Precondition sanity: prove the demo actually left the SSG half non-reset
    // by frame 50, so this test exercises real state and not a fresh chip.
    SoundChip_AY8910* ssg0 = fm->getChip(0);
    ASSERT_NE(ssg0, nullptr);
    bool anyNonZeroRegister = false;
    for (int r = 0; r < 14; ++r)
    {
        if (ssg0->getRegisters()[r] != 0)
            anyNonZeroRegister = true;
    }
    ASSERT_TRUE(anyNonZeroRegister)
        << "Test precondition failed: chip 0's SSG registers are still all-zero "
           "at frame 50 - the demo hasn't started playing, this run wouldn't "
           "exercise real synth state";

    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    ASSERT_GE(context->pTimeTravelManager->GetCheckpointCount(), 1u);
    const ttd::TTDCheckpoint* cp = context->pTimeTravelManager->GetCheckpoint(0);
    ASSERT_NE(cp, nullptr);

    const auto tsfmBlob = cp->peripheralBlobs.find(
        static_cast<uint8_t>(ttd::PeripheralId::TSFM));
    EXPECT_NE(tsfmBlob, cp->peripheralBlobs.end())
        << "Real demo playback drove both YM2203s into non-trivial state "
           "(SSG registers confirmed non-zero above; the FM engine has live "
           "operator/envelope/timer state too); a scrub back to this "
           "checkpoint must restore the sound chips along with CPU/RAM/video.";

    emulator.Stop();
    emulator.Release();
}

/// The strongest test in this file, and the direct check for "some effects
/// are not properly restored on seek": loads the same real chiptune, records
/// past at least two keyframes (kKeyFrameInterval = 50 frames, "once per
/// second" at 50 fps), then for a representative sample of BOTH keyframe
/// (I-frame) and delta-frame (P-frame) checkpoints, seeks there and checks
/// that the live TSFM device's re-serialized state is byte-identical to what
/// was captured at record time. Peripheral blobs are NOT delta-encoded like
/// RAM pages (TTDPeripheralRegistry::CaptureAll runs unconditionally on every
/// checkpoint - see TimeTravelManager::CaptureNow), so every checkpoint,
/// keyframe or not, must carry and restore a full, independent TSFM payload;
/// this test verifies that is actually true end to end through the real
/// SeekTo path, not just through TTDSaveState/TTDLoadState in isolation.
TEST(TTD_TSFM_ManagerIntegration_Test, SeekTo_RestoresTsfmStateBitIdenticalOnKeyAndDeltaFrames)
{
    Emulator emulator(LoggerLevel::LogError);
    emulator.SetCustomConfigPath(
        EmulatorTestHelper::StageTurboSoundKindConfig(TurboSoundKind::FM));
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ttd::TimeTravelManager* ttdMgr = context->pTimeTravelManager;
    ASSERT_NE(ttdMgr, nullptr);
    FeatureManager* featureManager = emulator.GetFeatureManager();
    ASSERT_NE(featureManager, nullptr);
    featureManager->setFeature(Features::kDebugMode, true);
    featureManager->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();

    const auto sna = TestPathHelper::FindProjectRoot() / "testdata/sound/tsfm/tech_support.sna";
    ASSERT_TRUE(emulator.LoadSnapshot(sna.string()))
        << "fixture missing: " << sna.string();

    auto* fm = dynamic_cast<SoundChip_TurboSoundFM*>(context->pSoundManager->getTurboSound());
    ASSERT_NE(fm, nullptr) << "Test precondition: FM slot kind must be active";

    ASSERT_TRUE(ttdMgr->StartRecording());
    emulator.RunNFrames(140, /*skipBreakpoints=*/true);  // spans 2 keyframe boundaries (interval 50)
    ttdMgr->StopRecording();
    ASSERT_GE(ttdMgr->GetCheckpointCount(), 141u);

    // Sample a mix of keyframe and delta-frame checkpoints: 0 (baseline,
    // always a keyframe), 50 and 100 (keyframes), plus 25, 75 and 139
    // (ordinary delta frames in between).
    const size_t indices[] = {0, 25, 50, 75, 100, 139};
    for (size_t idx : indices)
    {
        const ttd::TTDCheckpoint* cp = ttdMgr->GetCheckpoint(idx);
        ASSERT_NE(cp, nullptr) << "checkpoint " << idx;

        const auto blobIt = cp->peripheralBlobs.find(static_cast<uint8_t>(ttd::PeripheralId::TSFM));
        ASSERT_NE(blobIt, cp->peripheralBlobs.end())
            << "checkpoint " << idx << " (" << (cp->frameKind == ttd::TTDFrameKind::KeyFrame ? "I" : "P")
            << "-frame) is missing its TSFM blob";
        const auto expected = ttd::TTDPeripheralRegistry::DecodeBlob(
            static_cast<uint8_t>(ttd::PeripheralId::TSFM), blobIt->second);
        ASSERT_EQ(expected.size(), fm->TTDStateSize())
            << "checkpoint " << idx << ": decoded blob size mismatch";

        ASSERT_TRUE(ttdMgr->SeekTo({cp->time.frame, 0}))
            << "SeekTo checkpoint " << idx << " (frame " << cp->time.frame << ") failed";

        std::vector<uint8_t> live(fm->TTDStateSize());
        fm->TTDSaveState(live.data());

        EXPECT_EQ(live, expected)
            << "checkpoint " << idx << " (frame " << cp->time.frame << ", "
            << (cp->frameKind == ttd::TTDFrameKind::KeyFrame ? "I" : "P") << "-frame): live TSFM state after "
               "SeekTo does not match what was captured at record time - some "
               "part of the FM/SSG state did not round-trip through this "
               "checkpoint kind.";
    }

    emulator.Stop();
    emulator.Release();
}

/// Targeted final check on the noise generator specifically (period, counter,
/// LFSR): a report that "noise sometimes takes more than a second [= one
/// kKeyFrameInterval] to stabilize" after a scrub would be explained if noise
/// state were only fully correct at keyframes and needed to "catch up" on
/// delta frames. Forces continuously-evolving noise state (independent of
/// whether this tune happens to touch the noise channel in this window),
/// then checks two things the whole-blob byte-identity test above doesn't
/// isolate:
///   1. The noise sub-state round-trips byte-identical through BOTH a
///      keyframe and a delta-frame checkpoint on its own.
///   2. Forward simulation resumed FROM a delta-frame seek reproduces the
///      exact same noise trajectory as the original, un-seeked run all the
///      way to the next keyframe - i.e. it does not take until the next
///      keyframe to "catch up". This is the direct test for the reported
///      symptom.
TEST(TTD_TSFM_ManagerIntegration_Test, SeekTo_NoiseGeneratorStateDeterministicFromDeltaFrame)
{
    Emulator emulator(LoggerLevel::LogError);
    emulator.SetCustomConfigPath(
        EmulatorTestHelper::StageTurboSoundKindConfig(TurboSoundKind::FM));
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ttd::TimeTravelManager* ttdMgr = context->pTimeTravelManager;
    ASSERT_NE(ttdMgr, nullptr);
    FeatureManager* featureManager = emulator.GetFeatureManager();
    ASSERT_NE(featureManager, nullptr);
    featureManager->setFeature(Features::kDebugMode, true);
    featureManager->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();

    const auto sna = TestPathHelper::FindProjectRoot() / "testdata/sound/tsfm/tech_support.sna";
    ASSERT_TRUE(emulator.LoadSnapshot(sna.string()))
        << "fixture missing: " << sna.string();

    auto* fm = dynamic_cast<SoundChip_TurboSoundFM*>(context->pSoundManager->getTurboSound());
    ASSERT_NE(fm, nullptr) << "Test precondition: FM slot kind must be active";
    SoundChip_AY8910* ssg0 = fm->getChip(0);
    ASSERT_NE(ssg0, nullptr);

    ASSERT_TRUE(ttdMgr->StartRecording());
    emulator.RunNFrames(60, /*skipBreakpoints=*/true);

    // Force active, continuously-evolving noise state on chip 0, independent
    // of whether this tune's own composition uses the noise channel in this
    // window: fast period so the LFSR advances quickly and visibly.
    ssg0->writeRegister(AY_MIXER_CONTROL, 0x00);  // tone + noise all enabled
    ssg0->writeRegister(AY_NOISE_PERIOD, 0x01);
    ssg0->writeRegister(AY_A_VOLUME, 0x0F);

    emulator.RunNFrames(80, /*skipBreakpoints=*/true);  // now at frame 140, crossing the frame-100 keyframe
    ttdMgr->StopRecording();
    ASSERT_GE(ttdMgr->GetCheckpointCount(), 141u);

    // chip0's SSG payload offset within the 2008 B TSFM (v5) blob (the v4/v5
    // additions are appended at the end, so this offset is unchanged; the 57
    // bytes read below are the registers + generator state),
    // independently derived from design §8.2 (not imported from production
    // code - the point is to catch a production layout bug, not assume it's
    // right): version(1) + board(1) + samplePhase(8) + decimationPhase(8) +
    // 4 decimator phases(32) + chip0[address(1)+fmClockPhase(4)+timer0(4)+
    // timer1(4)+busy(4)+ymfmSize(2)+ymfm(494)] = 50 + 513 = 563.
    constexpr size_t kChip0SsgOffset = 2 + 8 + 8 + 32 + 1 + 4 + 4 + 4 + 4 + 2 + 494;
    static_assert(kChip0SsgOffset == 563, "offset arithmetic check");

    auto ssgBytesAtCheckpoint = [&](size_t idx) -> std::vector<uint8_t>
    {
        const ttd::TTDCheckpoint* cp = ttdMgr->GetCheckpoint(idx);
        if (!cp)
            return {};
        const auto it = cp->peripheralBlobs.find(static_cast<uint8_t>(ttd::PeripheralId::TSFM));
        if (it == cp->peripheralBlobs.end())
            return {};
        const auto full = ttd::TTDPeripheralRegistry::DecodeBlob(
            static_cast<uint8_t>(ttd::PeripheralId::TSFM), it->second);
        if (full.size() < kChip0SsgOffset + 57)
            return {};
        return std::vector<uint8_t>(full.begin() + static_cast<long>(kChip0SsgOffset),
                                     full.begin() + static_cast<long>(kChip0SsgOffset) + 57);
    };

    const std::vector<uint8_t> refAt61 = ssgBytesAtCheckpoint(61);
    const std::vector<uint8_t> refAt100 = ssgBytesAtCheckpoint(100);
    const std::vector<uint8_t> refAt140 = ssgBytesAtCheckpoint(140);
    ASSERT_EQ(refAt61.size(), 57u);
    ASSERT_EQ(refAt100.size(), 57u);
    ASSERT_EQ(refAt140.size(), 57u);

    // Sanity: the noise LFSR really is evolving between these points -
    // otherwise the rest of this test would pass without exercising anything.
    EXPECT_NE(refAt61, refAt100)
        << "test precondition: noise state did not evolve between frames 61 and 100 "
           "- noise isn't actually active, strengthen the forcing above";
    EXPECT_NE(refAt100, refAt140)
        << "test precondition: noise state did not evolve between frames 100 and 140";

    // 1) Keyframe restore: seek straight to the keyframe at 100 and check the
    //    noise sub-state alone matches exactly.
    ASSERT_TRUE(ttdMgr->SeekTo({100, 0}));
    {
        // Registers + generator state: the first 57 bytes of the AY payload
        std::vector<uint8_t> live(ssg0->TTDStateSize());
        ssg0->TTDSaveState(live.data());
        live.resize(57);
        EXPECT_EQ(live, refAt100)
            << "noise generator state (period/counter/out/LFSR) did not restore "
               "bit-identical on the keyframe at frame 100";
    }

    // 2) Delta-frame restore + forward determinism: seek to the delta frame
    //    right after the forced write (61), confirm it too restores exactly,
    //    then run forward to 140 in the NEW (post-seek) timeline and compare
    //    against what the ORIGINAL run recorded at 140. A bug that only
    //    fully restored noise state at keyframes would show up here as a
    //    mismatch that "heals" once frame 100 is crossed again.
    ASSERT_TRUE(ttdMgr->SeekTo({61, 0}));
    {
        // Registers + generator state: the first 57 bytes of the AY payload
        std::vector<uint8_t> live(ssg0->TTDStateSize());
        ssg0->TTDSaveState(live.data());
        live.resize(57);
        EXPECT_EQ(live, refAt61)
            << "noise generator state did not restore bit-identical on the "
               "delta frame at frame 61";
    }
    emulator.RunNFrames(79, /*skipBreakpoints=*/true);  // 61 -> 140 in the new timeline
    {
        // Registers + generator state: the first 57 bytes of the AY payload
        std::vector<uint8_t> live(ssg0->TTDStateSize());
        ssg0->TTDSaveState(live.data());
        live.resize(57);
        EXPECT_EQ(live, refAt140)
            << "noise generator diverged from the original recording after "
               "resuming from a delta-frame seek - it should reproduce the "
               "exact same trajectory (full determinism), not 'catch up' only "
               "once a keyframe passes. This is the direct test for a report "
               "that noise sometimes takes more than a second (one keyframe "
               "interval) to stabilize after a scrub.";
    }

    emulator.Stop();
    emulator.Release();
}

/// Direct regression test for the audible-clicks-after-seek report
/// (scratch/tsfm-issues3.ttd, "last 2-3 seconds"). The generator state fix
/// above (_samplePhase/_decimationPhase) made the SYNTH state exactly
/// correct after a seek, but TTDLoadState never touched the OUTPUT-STAGE
/// pipeline (decimator FIR history, hold register, LQ boxcar, word queue) -
/// those keep whatever they held LIVE right before the seek, i.e. audio
/// content from a completely different point in the tune. Feeding the
/// freshly-restored (different-timeline) generator output through a
/// decimator whose FIR taps still hold unrelated old audio is exactly what
/// produces an audible click/discontinuity right at the seek point. Verifies
/// TTDLoadState now flushes that pipeline to silence, the same way reset()
/// already does (minus the parts that would erase the just-restored chip
/// state).
TEST(TTD_TSFM_ManagerIntegration_Test, SeekTo_FlushesOutputStageToAvoidClickFromStaleHistory)
{
    Emulator emulator(LoggerLevel::LogError);
    emulator.SetCustomConfigPath(
        EmulatorTestHelper::StageTurboSoundKindConfig(TurboSoundKind::FM));
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ttd::TimeTravelManager* ttdMgr = context->pTimeTravelManager;
    ASSERT_NE(ttdMgr, nullptr);
    FeatureManager* featureManager = emulator.GetFeatureManager();
    ASSERT_NE(featureManager, nullptr);
    featureManager->setFeature(Features::kDebugMode, true);
    featureManager->setFeature(Features::kTimeTravel, true);
    featureManager->setFeature(Features::kSoundHQ, true);  // exercise the real decimator path
    context->pMemory->UpdateFeatureCache();

    const auto sna = TestPathHelper::FindProjectRoot() / "testdata/sound/tsfm/tech_support.sna";
    ASSERT_TRUE(emulator.LoadSnapshot(sna.string()))
        << "fixture missing: " << sna.string();

    auto* fm = dynamic_cast<SoundChip_TurboSoundFM*>(context->pSoundManager->getTurboSound());
    ASSERT_NE(fm, nullptr) << "Test precondition: FM slot kind must be active";

    ASSERT_TRUE(ttdMgr->StartRecording());
    emulator.RunNFrames(140, /*skipBreakpoints=*/true);  // real playback drives the decimators live

    // Precondition: the live output stage actually holds non-trivial state
    // right now - otherwise flushing it would be a no-op and this test
    // wouldn't exercise anything.
    TsfmOutputState* out0 = fm->outputState(0);
    TsfmOutputState* out1 = fm->outputState(1);
    ASSERT_NE(out0, nullptr);
    ASSERT_NE(out1, nullptr);
    const bool livePipelineIsWarm =
        out0->hold != 0.0 || out0->lqCount != 0 ||
        out1->hold != 0.0 || out1->lqCount != 0;
    EXPECT_TRUE(livePipelineIsWarm)
        << "test precondition: output stage is already silent/idle before the "
           "seek - strengthen playback above so this test actually exercises "
           "the flush";

    ttdMgr->StopRecording();
    ASSERT_TRUE(ttdMgr->SeekTo({70, 0}));

    // After a restore, the output stage must be flushed to a clean, silent
    // state - not left holding audio from wherever the live device was right
    // before the seek.
    EXPECT_EQ(out0->hold, 0.0) << "chip 0 hold register not flushed on restore";
    EXPECT_EQ(out0->lqSum, 0.0) << "chip 0 LQ boxcar sum not flushed on restore";
    EXPECT_EQ(out0->lqCount, 0u) << "chip 0 LQ boxcar count not flushed on restore";
    EXPECT_EQ(out1->hold, 0.0) << "chip 1 hold register not flushed on restore";
    EXPECT_EQ(out1->lqSum, 0.0) << "chip 1 LQ boxcar sum not flushed on restore";
    EXPECT_EQ(out1->lqCount, 0u) << "chip 1 LQ boxcar count not flushed on restore";
    // The FM output coupling is part of the output stage: a charged capacitor
    // from the pre-seek audio would bleed into the restored timeline
    EXPECT_EQ(out0->lastFed, 0.0) << "chip 0 coupling output not flushed on restore";
    EXPECT_EQ(out1->lastFed, 0.0) << "chip 1 coupling output not flushed on restore";
    EXPECT_EQ(out0->coupling.filter(0.0), 0.0) << "chip 0 coupling state not flushed on restore";
    EXPECT_EQ(out1->coupling.filter(0.0), 0.0) << "chip 1 coupling state not flushed on restore";

    // The decimators' FIR history must be cleared too (not just the simple
    // scalar fields above): a decimator carrying stale taps produces a
    // click as they mix with new content. Observable proxy: a freshly-reset
    // decimator with nothing fed into it yet reports no output ready.
    EXPECT_FALSE(fm->getChip(0)->decimatorLeft().hasOutput())
        << "chip 0 SSG-left decimator not flushed - stale FIR history would "
           "click against the freshly-restored generator output";
    EXPECT_FALSE(fm->getChip(1)->decimatorLeft().hasOutput())
        << "chip 1 SSG-left decimator not flushed";

    emulator.Stop();
    emulator.Release();
}

/// The render-phase fix restores the device's _samplePhase to its recorded
/// value; the mixer's own frame sample accumulator (SoundManager) was left at
/// its live pre-seek value. From the seek on, the two counts disagreed every
/// few frames - the mixer read one never-rendered, zero sample past the
/// device's last one: a click every ~6 frames for the rest of the session
/// (BW Demo report, "after ttd rewind clicking continues all over").
TEST(TTD_TSFM_ManagerIntegration_Test, SeekTo_KeepsDeviceAndMixerSampleCountsEqual)
{
    Emulator emulator(LoggerLevel::LogError);
    emulator.SetCustomConfigPath(
        EmulatorTestHelper::StageTurboSoundKindConfig(TurboSoundKind::FM));
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ttd::TimeTravelManager* ttdMgr = context->pTimeTravelManager;
    ASSERT_NE(ttdMgr, nullptr);
    FeatureManager* featureManager = emulator.GetFeatureManager();
    ASSERT_NE(featureManager, nullptr);
    featureManager->setFeature(Features::kDebugMode, true);
    featureManager->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();

    const auto sna = TestPathHelper::FindProjectRoot() / "testdata/sound/tsfm/tech_support.sna";
    ASSERT_TRUE(emulator.LoadSnapshot(sna.string()))
        << "fixture missing: " << sna.string();

    auto* fm = dynamic_cast<SoundChip_TurboSoundFM*>(context->pSoundManager->getTurboSound());
    ASSERT_NE(fm, nullptr) << "Test precondition: FM slot kind must be active";

    // The mixer hands each finished frame to the audio callback from inside
    // SoundManager::handleFrameEnd, before the next frame starts - the device's
    // rendered count still belongs to that same frame there
    struct CountCheck
    {
        SoundChip_TurboSoundFM* fm = nullptr;
        int mismatches = 0;
        static void Callback(void* obj, int16_t* /*samples*/, size_t numSamples)
        {
            auto* self = static_cast<CountCheck*>(obj);
            if (numSamples / 2 != self->fm->getRenderedSamplesThisFrame())
                self->mismatches++;
        }
    } check;
    check.fm = fm;
    context->pAudioCallback.store(&CountCheck::Callback, std::memory_order_release);
    context->pAudioManagerObj.store(&check, std::memory_order_release);

    auto countDisagreements = [&](int frames, int* firstMismatch) -> int
    {
        int mismatches = 0;
        for (int i = 0; i < frames; i++)
        {
            check.mismatches = 0;
            emulator.RunNFrames(1, /*skipBreakpoints=*/true);
            if (check.mismatches > 0 && mismatches++ == 0)
                *firstMismatch = i;
        }
        return mismatches;
    };

    ASSERT_TRUE(ttdMgr->StartRecording());
    int first = -1;
    ASSERT_EQ(countDisagreements(100, &first), 0)
        << "precondition: device and mixer sample counts disagree before any seek (first at frame " << first << ")";
    ttdMgr->StopRecording();

    // Frame 30 sits at a different fractional sample phase than the live
    // head (frame ~100): at 44.1 kHz the pattern period is 125 frames
    ASSERT_TRUE(ttdMgr->SeekTo({30, 0}));
    first = -1;
    const int mismatches = countDisagreements(60, &first);
    EXPECT_EQ(mismatches, 0) << mismatches << " of 60 frames after the seek rendered a different count than "
                             << "the mixer consumed (first " << first << " frames after the seek)";

    context->pAudioCallback.store(nullptr, std::memory_order_release);
    context->pAudioManagerObj.store(nullptr, std::memory_order_release);
    emulator.Stop();
    emulator.Release();
}

/// endregion </TimeTravelManager integration>
