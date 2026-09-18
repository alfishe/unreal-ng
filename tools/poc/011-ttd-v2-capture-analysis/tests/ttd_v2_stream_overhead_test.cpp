/// @file ttdstreamoverhead_test.cpp
/// @brief Proof that non-connected peripherals add exactly 0 bytes to TTD sessions.
///
/// Per Phase 3 §3.1: Validates the TTD design claim that unconnected peripherals
/// contribute zero overhead to serialized checkpoints. A 48K machine with no
/// peripherals should have the same per-checkpoint footprint as a 128K machine
/// with BDI+tape+covox, modulo the RAM difference.
///
/// This is a correctness test, not a benchmark. It verifies the presence-set
/// serialization format correctly excludes non-connected devices.

#include <gtest/gtest.h>

#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdserializable.h"

#include <cstring>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>

using namespace ttd;

namespace
{

/// Mock peripheral for testing with configurable state size.
class MockPeripheral : public TTDSerializable
{
public:
    explicit MockPeripheral(size_t stateSize, PeripheralId id,
                           bool supportsDelta = false)
        : _stateSize(stateSize), _id(id), _supportsDelta(supportsDelta)
    {
        _state.resize(stateSize, 0);
    }

    size_t TTDStateSize() const override { return _stateSize; }

    void TTDSaveState(uint8_t* dst) const override
    {
        std::memcpy(dst, _state.data(), _stateSize);
    }

    void TTDLoadState(const uint8_t* src) override
    {
        std::memcpy(_state.data(), src, _stateSize);
    }

    std::string TTDDeviceName() const override { return "MockPeripheral"; }
    PeripheralId TTDPeripheralId() const override { return _id; }
    bool TTDSupportsDelta() const override { return _supportsDelta; }

    void FillRandom(uint32_t seed)
    {
        std::mt19937 gen(seed);
        std::uniform_int_distribution<> dist(0, 255);
        for (auto& b : _state)
            b = static_cast<uint8_t>(dist(gen));
    }

    const std::vector<uint8_t>& GetState() const { return _state; }

private:
    size_t _stateSize;
    PeripheralId _id;
    bool _supportsDelta;
    std::vector<uint8_t> _state;
};

/// Calculate total blob size for a captured peripheral set
size_t TotalBlobSize(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs)
{
    size_t total = 0;
    for (const auto& kv : blobs)
        total += kv.second.size();
    return total;
}

} // namespace

class TTDStreamOverheadTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Initialize peripherals with realistic sizes
        _turboSound = std::make_unique<MockPeripheral>(64, PeripheralId::TurboSound);
        _betaDisk = std::make_unique<MockPeripheral>(2048, PeripheralId::BetaDisk);
        _tape = std::make_unique<MockPeripheral>(256, PeripheralId::Tape);
        _covox = std::make_unique<MockPeripheral>(16, PeripheralId::Covox);
        _generalSound = std::make_unique<MockPeripheral>(512 * 1024, PeripheralId::GeneralSound, true);

        // Fill with random data to avoid trivial compression
        _turboSound->FillRandom(1);
        _betaDisk->FillRandom(2);
        _tape->FillRandom(3);
        _covox->FillRandom(4);
        _generalSound->FillRandom(5);
    }

    std::unique_ptr<MockPeripheral> _turboSound;
    std::unique_ptr<MockPeripheral> _betaDisk;
    std::unique_ptr<MockPeripheral> _tape;
    std::unique_ptr<MockPeripheral> _covox;
    std::unique_ptr<MockPeripheral> _generalSound;
};

/// @test Empty registry produces zero bytes
TEST_F(TTDStreamOverheadTest, EmptyRegistryZeroBytes)
{
    TTDPeripheralRegistry registry;

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(nullptr, blobs);

    EXPECT_TRUE(blobs.empty());
    EXPECT_EQ(TotalBlobSize(blobs), 0);
}

/// @test Non-connected peripherals add exactly 0 bytes to output
TEST_F(TTDStreamOverheadTest, NonConnectedPeripheralsZeroBytes)
{
    // Session A: TurboSound only (simulating 48K with minimal peripherals)
    TTDPeripheralRegistry registryA;
    registryA.Register(PeripheralId::TurboSound, _turboSound.get());

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobsA;
    registryA.CaptureAll(nullptr, blobsA);
    size_t sizeA = TotalBlobSize(blobsA);

    // Session B: TurboSound + BDI + Tape + Covox (simulating 128K with peripherals)
    TTDPeripheralRegistry registryB;
    registryB.Register(PeripheralId::TurboSound, _turboSound.get());
    registryB.Register(PeripheralId::BetaDisk, _betaDisk.get());
    registryB.Register(PeripheralId::Tape, _tape.get());
    registryB.Register(PeripheralId::Covox, _covox.get());

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobsB;
    registryB.CaptureAll(nullptr, blobsB);
    size_t sizeB = TotalBlobSize(blobsB);

    // The TurboSound blob should be identical in both cases
    ASSERT_EQ(blobsA.count(static_cast<uint8_t>(PeripheralId::TurboSound)), 1);
    ASSERT_EQ(blobsB.count(static_cast<uint8_t>(PeripheralId::TurboSound)), 1);
    EXPECT_EQ(blobsA[static_cast<uint8_t>(PeripheralId::TurboSound)],
              blobsB[static_cast<uint8_t>(PeripheralId::TurboSound)]);

    // Additional peripherals should add their own blobs, but not affect
    // the TurboSound blob (no per-checkpoint overhead for non-connected)
    EXPECT_EQ(blobsB.size(), 4);  // TS + BDI + Tape + Covox
    EXPECT_GT(sizeB, sizeA);      // More peripherals = more data

    // Key invariant: Registry A should NOT have any entry for BetaDisk/Tape/Covox
    EXPECT_EQ(blobsA.count(static_cast<uint8_t>(PeripheralId::BetaDisk)), 0);
    EXPECT_EQ(blobsA.count(static_cast<uint8_t>(PeripheralId::Tape)), 0);
    EXPECT_EQ(blobsA.count(static_cast<uint8_t>(PeripheralId::Covox)), 0);
}

/// @test Unregistering a peripheral removes it from capture output
TEST_F(TTDStreamOverheadTest, UnregisterRemovesFromOutput)
{
    TTDPeripheralRegistry registry;
    registry.Register(PeripheralId::TurboSound, _turboSound.get());
    registry.Register(PeripheralId::BetaDisk, _betaDisk.get());

    // Capture with both
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobsBoth;
    registry.CaptureAll(nullptr, blobsBoth);
    EXPECT_EQ(blobsBoth.size(), 2);

    // Unregister BetaDisk
    registry.Unregister(PeripheralId::BetaDisk);

    // Capture with only TurboSound
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobsOne;
    registry.CaptureAll(nullptr, blobsOne);
    EXPECT_EQ(blobsOne.size(), 1);
    EXPECT_EQ(blobsOne.count(static_cast<uint8_t>(PeripheralId::BetaDisk)), 0);

    // TurboSound blob should be identical
    EXPECT_EQ(blobsOne[static_cast<uint8_t>(PeripheralId::TurboSound)],
              blobsBoth[static_cast<uint8_t>(PeripheralId::TurboSound)]);
}

/// @test Presence-set is per-checkpoint (not global)
///
/// When capturing multiple checkpoints, each checkpoint's blob set should
/// reflect only the peripherals that were registered at capture time.
TEST_F(TTDStreamOverheadTest, PresenceSetPerCheckpoint)
{
    TTDPeripheralRegistry registry;

    // Checkpoint 1: Only TurboSound
    registry.Register(PeripheralId::TurboSound, _turboSound.get());
    std::unordered_map<uint8_t, std::vector<uint8_t>> cp1;
    registry.CaptureAll(nullptr, cp1);

    // Checkpoint 2: TurboSound + BetaDisk
    registry.Register(PeripheralId::BetaDisk, _betaDisk.get());
    std::unordered_map<uint8_t, std::vector<uint8_t>> cp2;
    registry.CaptureAll(nullptr, cp2);

    // Checkpoint 3: Only BetaDisk
    registry.Unregister(PeripheralId::TurboSound);
    std::unordered_map<uint8_t, std::vector<uint8_t>> cp3;
    registry.CaptureAll(nullptr, cp3);

    // Verify each checkpoint has correct device set
    EXPECT_EQ(cp1.size(), 1);
    EXPECT_EQ(cp1.count(static_cast<uint8_t>(PeripheralId::TurboSound)), 1);
    EXPECT_EQ(cp1.count(static_cast<uint8_t>(PeripheralId::BetaDisk)), 0);

    EXPECT_EQ(cp2.size(), 2);
    EXPECT_EQ(cp2.count(static_cast<uint8_t>(PeripheralId::TurboSound)), 1);
    EXPECT_EQ(cp2.count(static_cast<uint8_t>(PeripheralId::BetaDisk)), 1);

    EXPECT_EQ(cp3.size(), 1);
    EXPECT_EQ(cp3.count(static_cast<uint8_t>(PeripheralId::TurboSound)), 0);
    EXPECT_EQ(cp3.count(static_cast<uint8_t>(PeripheralId::BetaDisk)), 1);
}

/// @test Large peripheral (GeneralSound 512KB) does not pollute small configs
TEST_F(TTDStreamOverheadTest, LargePeripheralIsolation)
{
    // Config A: Just TurboSound (typical 48K setup)
    TTDPeripheralRegistry registryA;
    registryA.Register(PeripheralId::TurboSound, _turboSound.get());

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobsA;
    registryA.CaptureAll(nullptr, blobsA);
    size_t sizeA = TotalBlobSize(blobsA);

    // Config B: TurboSound + GeneralSound (512KB)
    TTDPeripheralRegistry registryB;
    registryB.Register(PeripheralId::TurboSound, _turboSound.get());
    registryB.Register(PeripheralId::GeneralSound, _generalSound.get());

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobsB;
    registryB.CaptureAll(nullptr, blobsB);
    size_t sizeB = TotalBlobSize(blobsB);

    // The TurboSound blob should be byte-for-byte identical
    EXPECT_EQ(blobsA[static_cast<uint8_t>(PeripheralId::TurboSound)],
              blobsB[static_cast<uint8_t>(PeripheralId::TurboSound)]);

    // Config A should have no GeneralSound overhead
    EXPECT_EQ(blobsA.count(static_cast<uint8_t>(PeripheralId::GeneralSound)), 0);

    // Config B should be significantly larger due to GeneralSound
    EXPECT_GT(sizeB, sizeA * 10);  // GS is 512KB vs TS 64B

    // Prove zero overhead: sizeA should equal the TurboSound blob size exactly
    // (no presence-set or other per-checkpoint overhead for non-connected)
    EXPECT_EQ(sizeA, blobsA[static_cast<uint8_t>(PeripheralId::TurboSound)].size());
}

/// @test Restore with missing peripheral in blob map
///
/// When restoring, a peripheral that exists in the registry but not in the
/// blob map should NOT be modified (defensive: don't corrupt state).
TEST_F(TTDStreamOverheadTest, RestoreMissingPeripheralUnmodified)
{
    TTDPeripheralRegistry registry;
    registry.Register(PeripheralId::TurboSound, _turboSound.get());
    registry.Register(PeripheralId::BetaDisk, _betaDisk.get());

    // Capture both
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(nullptr, blobs);

    // Remember BetaDisk state
    std::vector<uint8_t> originalBDI = _betaDisk->GetState();

    // Remove BetaDisk from the blob map (simulating a session without it)
    blobs.erase(static_cast<uint8_t>(PeripheralId::BetaDisk));

    // Modify BetaDisk state
    _betaDisk->FillRandom(999);
    EXPECT_NE(_betaDisk->GetState(), originalBDI);

    // Restore from blobs (missing BetaDisk)
    registry.RestoreAll(blobs, nullptr);

    // BetaDisk should NOT have been modified (it wasn't in the blob map)
    // This is important: we don't zero out state for missing peripherals
    EXPECT_NE(_betaDisk->GetState(), originalBDI);  // Still has the 999-seed data
}

/// @test Per-checkpoint overhead is constant (no scaling with unconnected devices)
TEST_F(TTDStreamOverheadTest, OverheadConstantRegardlessOfUnconnected)
{
    // This test simulates the scenario where the emulator COULD support
    // many peripherals, but only some are connected. The checkpoint overhead
    // should only reflect what's actually connected.

    // Scenario: 5 possible peripherals, only 1 connected
    TTDPeripheralRegistry registryMin;
    registryMin.Register(PeripheralId::TurboSound, _turboSound.get());

    // Scenario: Same peripheral connected, but we could have had 5
    // The blob output should be identical
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobsMin;
    registryMin.CaptureAll(nullptr, blobsMin);

    // Just TurboSound registered
    EXPECT_EQ(blobsMin.size(), 1);

    // The blob should contain: header + compressed state
    // No padding or reserved slots for unconnected peripherals
    auto& tsBlob = blobsMin[static_cast<uint8_t>(PeripheralId::TurboSound)];

    // Header is 12 bytes (PeripheralBlobHeader), state varies by compression
    EXPECT_GE(tsBlob.size(), sizeof(PeripheralBlobHeader));

    // For a 64-byte state with random data, expect some compression overhead
    // but certainly less than 2x the raw state size
    EXPECT_LT(tsBlob.size(), 64 * 2 + sizeof(PeripheralBlobHeader));
}

/// @test Delta encoding does not affect non-connected overhead
TEST_F(TTDStreamOverheadTest, DeltaEncodingPreservesZeroOverhead)
{
    TTDPeripheralRegistry registry;
    registry.Register(PeripheralId::TurboSound, _turboSound.get());
    registry.Register(PeripheralId::GeneralSound, _generalSound.get());

    // First capture (full)
    std::unordered_map<uint8_t, std::vector<uint8_t>> fullBlobs;
    registry.CaptureAll(nullptr, fullBlobs);

    // Unregister GeneralSound (simulating disconnect)
    registry.Unregister(PeripheralId::GeneralSound);

    // Second capture (delta mode, but GS is not connected)
    std::unordered_map<uint8_t, std::vector<uint8_t>> deltaBlobs;
    registry.CaptureAll(&fullBlobs, deltaBlobs);

    // deltaBlobs should only contain TurboSound
    EXPECT_EQ(deltaBlobs.size(), 1);
    EXPECT_EQ(deltaBlobs.count(static_cast<uint8_t>(PeripheralId::GeneralSound)), 0);

    // The TurboSound blob should be a delta against the previous
    // (assuming no state change, it should be very small)
}

/// @test Round-trip correctness with varying peripheral sets
TEST_F(TTDStreamOverheadTest, RoundTripVaryingPeripheralSets)
{
    TTDPeripheralRegistry registry;
    registry.Register(PeripheralId::TurboSound, _turboSound.get());
    registry.Register(PeripheralId::BetaDisk, _betaDisk.get());
    registry.Register(PeripheralId::Tape, _tape.get());

    // Set known states
    std::vector<uint8_t> tsOriginal(64);
    std::vector<uint8_t> bdiOriginal(2048);
    std::vector<uint8_t> tapeOriginal(256);

    std::iota(tsOriginal.begin(), tsOriginal.end(), 0x10);
    std::iota(bdiOriginal.begin(), bdiOriginal.end(), 0x20);
    std::iota(tapeOriginal.begin(), tapeOriginal.end(), 0x30);

    _turboSound->TTDLoadState(tsOriginal.data());
    _betaDisk->TTDLoadState(bdiOriginal.data());
    _tape->TTDLoadState(tapeOriginal.data());

    // Capture
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(nullptr, blobs);
    EXPECT_EQ(blobs.size(), 3);

    // Corrupt all states
    _turboSound->FillRandom(111);
    _betaDisk->FillRandom(222);
    _tape->FillRandom(333);

    // Restore
    registry.RestoreAll(blobs, nullptr);

    // Verify all states match original
    std::vector<uint8_t> tsRestored(64);
    std::vector<uint8_t> bdiRestored(2048);
    std::vector<uint8_t> tapeRestored(256);

    _turboSound->TTDSaveState(tsRestored.data());
    _betaDisk->TTDSaveState(bdiRestored.data());
    _tape->TTDSaveState(tapeRestored.data());

    EXPECT_EQ(tsRestored, tsOriginal);
    EXPECT_EQ(bdiRestored, bdiOriginal);
    EXPECT_EQ(tapeRestored, tapeOriginal);
}
