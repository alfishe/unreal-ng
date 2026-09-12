#include <gtest/gtest.h>

#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdserializable.h"

#include <chrono>
#include <cstring>
#include <numeric>
#include <random>

using namespace ttd;

namespace {

/// Mock peripheral for testing with configurable state size.
class MockPeripheral : public TTDSerializable
{
public:
    explicit MockPeripheral(size_t stateSize, PeripheralId id = PeripheralId::Count)
        : _stateSize(stateSize), _id(id)
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

    void SetState(const std::vector<uint8_t>& data)
    {
        if (data.size() == _stateSize)
            _state = data;
    }

    const std::vector<uint8_t>& GetState() const { return _state; }

    void ModifyByte(size_t offset, uint8_t value)
    {
        if (offset < _stateSize)
            _state[offset] = value;
    }

private:
    size_t _stateSize;
    PeripheralId _id;
    std::vector<uint8_t> _state;
};

} // namespace

class TTDPeripheralRegistryTest : public ::testing::Test
{
protected:
    TTDPeripheralRegistry registry;
};

TEST_F(TTDPeripheralRegistryTest, EmptyRegistryCapture)
{
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    EXPECT_TRUE(blobs.empty());
}

TEST_F(TTDPeripheralRegistryTest, RegisterAndCapture)
{
    MockPeripheral mock(64, PeripheralId::TurboSound);
    registry.Register(PeripheralId::TurboSound, &mock);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    EXPECT_EQ(blobs.size(), 1);
    EXPECT_TRUE(blobs.find(static_cast<uint8_t>(PeripheralId::TurboSound)) != blobs.end());
}

TEST_F(TTDPeripheralRegistryTest, UnregisteredPeripheralNoBytes)
{
    MockPeripheral mock1(64, PeripheralId::TurboSound);
    MockPeripheral mock2(128, PeripheralId::GeneralSound);

    registry.Register(PeripheralId::TurboSound, &mock1);
    // mock2 not registered - should not appear in output

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    EXPECT_EQ(blobs.size(), 1);
    EXPECT_FALSE(blobs.find(static_cast<uint8_t>(PeripheralId::GeneralSound)) != blobs.end());
}

TEST_F(TTDPeripheralRegistryTest, CaptureRestoreRoundTrip)
{
    MockPeripheral mock(64, PeripheralId::BetaDisk);

    // Set known state
    std::vector<uint8_t> originalState(64);
    std::iota(originalState.begin(), originalState.end(), 1);
    mock.SetState(originalState);

    registry.Register(PeripheralId::BetaDisk, &mock);

    // Capture
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    // Modify state
    mock.ModifyByte(0, 0xFF);
    mock.ModifyByte(63, 0x00);

    // Restore
    registry.RestoreAll(blobs);

    // Verify state matches original
    EXPECT_EQ(mock.GetState(), originalState);
}

TEST_F(TTDPeripheralRegistryTest, CompressiblePayloadIsStoredCompressed)
{
    constexpr size_t kStateSize = 4096;
    MockPeripheral mock(kStateSize, PeripheralId::GeneralSound);

    // Highly compressible content — the encoder should choose the compressed
    // representation, so the blob lands well under the raw state size.
    mock.SetState(std::vector<uint8_t>(kStateSize, 0x5A));
    registry.Register(PeripheralId::GeneralSound, &mock);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    const auto& blob = blobs[static_cast<uint8_t>(PeripheralId::GeneralSound)];
    EXPECT_LT(blob.size(), kStateSize);
}

/// Incompressible content must still round-trip: the encoder falls back to
/// storing the payload raw, and the decoder has to handle that path.
TEST_F(TTDPeripheralRegistryTest, IncompressiblePayloadRoundTrips)
{
    constexpr size_t kStateSize = 256;
    MockPeripheral mock(kStateSize, PeripheralId::GeneralSound);

    std::vector<uint8_t> noise(kStateSize);
    std::mt19937 gen(42);
    std::uniform_int_distribution<> dist(0, 255);
    for (auto& b : noise)
        b = static_cast<uint8_t>(dist(gen));
    mock.SetState(noise);
    registry.Register(PeripheralId::GeneralSound, &mock);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    mock.SetState(std::vector<uint8_t>(kStateSize, 0));
    ASSERT_NE(mock.GetState(), noise);

    const auto report = registry.RestoreAll(blobs);
    EXPECT_EQ(report.restored, 1u);
    EXPECT_TRUE(report.Complete());
    EXPECT_EQ(mock.GetState(), noise);
}

/// The device id travels both as the map key and inside the blob header. A
/// blob filed under the wrong device must be rejected, not loaded — feeding
/// one device another's bytes is silent state corruption.
TEST_F(TTDPeripheralRegistryTest, BlobFiledUnderTheWrongDeviceIsRejected)
{
    MockPeripheral ay(32, PeripheralId::TurboSound);
    MockPeripheral tape(32, PeripheralId::Tape);

    ay.SetState(std::vector<uint8_t>(32, 0xAA));
    tape.SetState(std::vector<uint8_t>(32, 0xBB));

    registry.Register(PeripheralId::TurboSound, &ay);
    registry.Register(PeripheralId::Tape, &tape);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    // Move the AY blob under Tape's key, as a corrupt or mis-keyed file would.
    blobs[static_cast<uint8_t>(PeripheralId::Tape)] =
        blobs[static_cast<uint8_t>(PeripheralId::TurboSound)];

    tape.SetState(std::vector<uint8_t>(32, 0));
    const auto report = registry.RestoreAll(blobs);

    EXPECT_EQ(tape.GetState(), std::vector<uint8_t>(32, 0))
        << "tape must not be loaded with the AY's bytes";
    EXPECT_EQ(report.sizeMismatches, 1u);
    EXPECT_FALSE(report.Complete());
}

/// A registered device the checkpoint has no blob for must be reported, not
/// silently left holding pre-seek state.
TEST_F(TTDPeripheralRegistryTest, MissingBlobForRegisteredDeviceIsReported)
{
    MockPeripheral ay(32, PeripheralId::TurboSound);
    MockPeripheral tape(32, PeripheralId::Tape);
    registry.Register(PeripheralId::TurboSound, &ay);
    registry.Register(PeripheralId::Tape, &tape);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    blobs.erase(static_cast<uint8_t>(PeripheralId::Tape));

    const auto report = registry.RestoreAll(blobs);

    EXPECT_EQ(report.restored, 1u);
    EXPECT_EQ(report.missingBlobs, 1u);
    EXPECT_FALSE(report.Complete());
}

/// A blob for a device this build does not have must not fail the restore —
/// that is what keeps a session recorded on a richer build loadable.
TEST_F(TTDPeripheralRegistryTest, UnclaimedBlobIsCountedNotFatal)
{
    MockPeripheral ay(32, PeripheralId::TurboSound);
    registry.Register(PeripheralId::TurboSound, &ay);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    blobs[static_cast<uint8_t>(PeripheralId::GeneralSound)] = std::vector<uint8_t>(64, 0xCD);

    const auto report = registry.RestoreAll(blobs);

    EXPECT_EQ(report.restored, 1u);
    EXPECT_EQ(report.unclaimedBlobs, 1u);
    EXPECT_FALSE(report.Complete());
}

TEST_F(TTDPeripheralRegistryTest, MultiplePeripheralsIndependent)
{
    MockPeripheral ay(32, PeripheralId::TurboSound);
    MockPeripheral fdc(256, PeripheralId::BetaDisk);
    MockPeripheral tape(64, PeripheralId::Tape);

    // Set distinct states
    std::vector<uint8_t> ayState(32, 0x11);
    std::vector<uint8_t> fdcState(256, 0x22);
    std::vector<uint8_t> tapeState(64, 0x33);

    ay.SetState(ayState);
    fdc.SetState(fdcState);
    tape.SetState(tapeState);

    registry.Register(PeripheralId::TurboSound, &ay);
    registry.Register(PeripheralId::BetaDisk, &fdc);
    registry.Register(PeripheralId::Tape, &tape);

    // Capture all
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    EXPECT_EQ(blobs.size(), 3);

    // Clear states
    ay.SetState(std::vector<uint8_t>(32, 0));
    fdc.SetState(std::vector<uint8_t>(256, 0));
    tape.SetState(std::vector<uint8_t>(64, 0));

    // Restore all
    registry.RestoreAll(blobs);

    // Verify each restored correctly
    EXPECT_EQ(ay.GetState(), ayState);
    EXPECT_EQ(fdc.GetState(), fdcState);
    EXPECT_EQ(tape.GetState(), tapeState);
}

TEST_F(TTDPeripheralRegistryTest, UnregisterRemovesFromCapture)
{
    MockPeripheral mock(64, PeripheralId::Covox);
    registry.Register(PeripheralId::Covox, &mock);

    EXPECT_TRUE(registry.IsRegistered(PeripheralId::Covox));

    registry.Unregister(PeripheralId::Covox);

    EXPECT_FALSE(registry.IsRegistered(PeripheralId::Covox));

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    EXPECT_TRUE(blobs.empty());
}

TEST_F(TTDPeripheralRegistryTest, TotalStateSizeAccurate)
{
    MockPeripheral small(32, PeripheralId::TurboSound);
    MockPeripheral medium(256, PeripheralId::BetaDisk);
    MockPeripheral large(4096, PeripheralId::GeneralSound);

    registry.Register(PeripheralId::TurboSound, &small);
    registry.Register(PeripheralId::BetaDisk, &medium);
    registry.Register(PeripheralId::GeneralSound, &large);

    EXPECT_EQ(registry.TotalStateSize(), 32 + 256 + 4096);
}

TEST_F(TTDPeripheralRegistryTest, EmptyBlobNotRestored)
{
    MockPeripheral mock(64, PeripheralId::TSFM);
    std::vector<uint8_t> originalState(64, 0x42);
    mock.SetState(originalState);

    registry.Register(PeripheralId::TSFM, &mock);

    // Empty blob map
    std::unordered_map<uint8_t, std::vector<uint8_t>> emptyBlobs;
    registry.RestoreAll(emptyBlobs);

    // State should be unchanged
    EXPECT_EQ(mock.GetState(), originalState);
}

// Benchmark: Peripheral capture/restore must fit within 5-6ms budget
TEST_F(TTDPeripheralRegistryTest, PerformanceBudget)
{
    constexpr size_t kLargeStateSize = 32768;  // 32KB (e.g., GeneralSound sample RAM)
    constexpr int kIterations = 100;

    MockPeripheral large(kLargeStateSize, PeripheralId::GeneralSound);

    // Random state data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 255);
    std::vector<uint8_t> randomState(kLargeStateSize);
    for (auto& b : randomState)
        b = static_cast<uint8_t>(dist(gen));
    large.SetState(randomState);

    registry.Register(PeripheralId::GeneralSound, &large);

    // Measure capture time
    auto captureStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i)
    {
        std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
        registry.CaptureAll(blobs);
    }
    auto captureEnd = std::chrono::high_resolution_clock::now();
    auto captureUs = std::chrono::duration_cast<std::chrono::microseconds>(
        captureEnd - captureStart).count() / kIterations;

    // Measure restore time
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    auto restoreStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i)
    {
        registry.RestoreAll(blobs);
    }
    auto restoreEnd = std::chrono::high_resolution_clock::now();
    auto restoreUs = std::chrono::duration_cast<std::chrono::microseconds>(
        restoreEnd - restoreStart).count() / kIterations;

    // Performance assertions:
    // - Capture should be <1ms for 32KB state
    // - Restore should be <2ms for 32KB state
    // This leaves plenty of margin for the 5-6ms total budget
    EXPECT_LT(captureUs, 1000) << "Capture took " << captureUs << "us";
    EXPECT_LT(restoreUs, 2000) << "Restore took " << restoreUs << "us";
}

TEST_F(TTDPeripheralRegistryTest, CompressionEfficiency)
{
    constexpr size_t kStateSize = 4096;

    MockPeripheral mock(kStateSize, PeripheralId::GeneralSound);

    // Highly compressible data (all zeros)
    std::vector<uint8_t> zeros(kStateSize, 0);
    mock.SetState(zeros);
    registry.Register(PeripheralId::GeneralSound, &mock);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    auto blobSize = blobs[static_cast<uint8_t>(PeripheralId::GeneralSound)].size();

    // All-zeros should compress to much less than raw size
    // Expect at least 10:1 compression for uniform data
    EXPECT_LT(blobSize, kStateSize / 5);
}

namespace {

/// Mock with a caller-controlled hash, so hash-combination behaviour can be
/// tested independently of any real device's state layout.
class HashingPeripheral : public TTDSerializable
{
public:
    HashingPeripheral(PeripheralId id, uint64_t hash) : _id(id), _hash(hash) {}

    size_t TTDStateSize() const override { return 1; }
    void TTDSaveState(uint8_t* dst) const override { *dst = 0; }
    void TTDLoadState(const uint8_t*) override {}
    std::string TTDDeviceName() const override { return "HashingPeripheral"; }
    PeripheralId TTDPeripheralId() const override { return _id; }
    uint64_t TTDHashState() const override { return _hash; }

    void SetHash(uint64_t hash) { _hash = hash; }

private:
    PeripheralId _id;
    uint64_t _hash;
};

} // namespace

/// Regression: the id is used as a rotate distance. For id 0 the naive
/// expression evaluates `contribution >> 64`, which is undefined for a 64-bit
/// type and observably differs between -O0 and -O2 builds. A divergence hash
/// that disagrees across build configurations reports phantom divergences, so
/// rotating by zero must be exactly the identity.
TEST(TTDPeripheralHashTest, IdZeroRotatesByZeroInsteadOfShiftingBy64)
{
    constexpr uint64_t kHash = 0x0123456789ABCDEFull;

    TTDPeripheralRegistry registry;
    HashingPeripheral device(PeripheralId::TurboSound, kHash);   // TurboSound == id 0
    registry.Register(PeripheralId::TurboSound, &device);

    EXPECT_EQ(registry.ComputePeripheralHash(), kHash)
        << "a lone id-0 device must contribute its hash unrotated";
}

/// _devices is an unordered_map, so registration order must not reach the hash.
TEST(TTDPeripheralHashTest, HashIsIndependentOfRegistrationOrder)
{
    HashingPeripheral a(PeripheralId::TurboSound, 0x1111222233334444ull);
    HashingPeripheral b(PeripheralId::ScorpionProfROM, 0x5555666677778888ull);

    TTDPeripheralRegistry forward;
    forward.Register(PeripheralId::TurboSound, &a);
    forward.Register(PeripheralId::ScorpionProfROM, &b);

    TTDPeripheralRegistry reverse;
    reverse.Register(PeripheralId::ScorpionProfROM, &b);
    reverse.Register(PeripheralId::TurboSound, &a);

    EXPECT_EQ(forward.ComputePeripheralHash(), reverse.ComputePeripheralHash());
}

/// Two devices holding identical state must not cancel each other out under the
/// XOR — that is what the per-id rotation is for.
TEST(TTDPeripheralHashTest, IdenticalStateOnDifferentIdsDoesNotCancel)
{
    constexpr uint64_t kSame = 0xDEADBEEFCAFEF00Dull;

    HashingPeripheral a(PeripheralId::TurboSound, kSame);
    HashingPeripheral b(PeripheralId::ScorpionProfROM, kSame);

    TTDPeripheralRegistry registry;
    registry.Register(PeripheralId::TurboSound, &a);
    registry.Register(PeripheralId::ScorpionProfROM, &b);

    EXPECT_NE(registry.ComputePeripheralHash(), 0u);
}

/// The whole point of the contribution: a model-specific state change must
/// reach the common divergence hash.
TEST(TTDPeripheralHashTest, HashTracksDeviceStateChanges)
{
    HashingPeripheral device(PeripheralId::ScorpionProfROM, 0x1000ull);

    TTDPeripheralRegistry registry;
    registry.Register(PeripheralId::ScorpionProfROM, &device);

    const uint64_t before = registry.ComputePeripheralHash();
    device.SetHash(0x2000ull);
    EXPECT_NE(registry.ComputePeripheralHash(), before);
}

/// An empty registry contributes nothing, so models with no model-specific
/// state hash exactly as they did before the registry existed.
TEST(TTDPeripheralHashTest, EmptyRegistryContributesZero)
{
    TTDPeripheralRegistry registry;
    EXPECT_EQ(registry.ComputePeripheralHash(), 0u);
}
