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

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttddumpformat.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
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
        _emulator = new Emulator(LoggerLevel::LogError);
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
