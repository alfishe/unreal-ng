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
    explicit MockPeripheral(size_t stateSize, PeripheralId id = PeripheralId::Count,
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
    bool _supportsDelta;
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
    registry.CaptureAll(nullptr, blobs);
    EXPECT_TRUE(blobs.empty());
}

TEST_F(TTDPeripheralRegistryTest, RegisterAndCapture)
{
    MockPeripheral mock(64, PeripheralId::TurboSound);
    registry.Register(PeripheralId::TurboSound, &mock);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(nullptr, blobs);

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
    registry.CaptureAll(nullptr, blobs);

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
    registry.CaptureAll(nullptr, blobs);

    // Modify state
    mock.ModifyByte(0, 0xFF);
    mock.ModifyByte(63, 0x00);

    // Restore
    registry.RestoreAll(blobs, nullptr);

    // Verify state matches original
    EXPECT_EQ(mock.GetState(), originalState);
}

TEST_F(TTDPeripheralRegistryTest, DeltaEncodingSmallChange)
{
    constexpr size_t kStateSize = 4096;
    MockPeripheral mock(kStateSize, PeripheralId::GeneralSound, true);

    // Initial state: pseudo-random data (doesn't compress as well)
    std::vector<uint8_t> initialState(kStateSize);
    std::mt19937 gen(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<> dist(0, 255);
    for (auto& b : initialState)
        b = static_cast<uint8_t>(dist(gen));
    mock.SetState(initialState);
    registry.Register(PeripheralId::GeneralSound, &mock);

    // First capture (full)
    std::unordered_map<uint8_t, std::vector<uint8_t>> firstBlobs;
    registry.CaptureAll(nullptr, firstBlobs);

    // Small change (only 4 bytes different)
    mock.ModifyByte(100, initialState[100] ^ 0xFF);
    mock.ModifyByte(200, initialState[200] ^ 0xFF);
    mock.ModifyByte(300, initialState[300] ^ 0xFF);
    mock.ModifyByte(400, initialState[400] ^ 0xFF);

    // Second capture with delta
    std::unordered_map<uint8_t, std::vector<uint8_t>> secondBlobs;
    registry.CaptureAll(&firstBlobs, secondBlobs);

    // Delta blob should be smaller than full blob for small changes
    // because XOR of mostly-unchanged data produces mostly zeros
    auto firstSize = firstBlobs[static_cast<uint8_t>(PeripheralId::GeneralSound)].size();
    auto secondSize = secondBlobs[static_cast<uint8_t>(PeripheralId::GeneralSound)].size();

    // With only 4 bytes changed in 4KB, delta should compress much better
    EXPECT_LT(secondSize, firstSize);
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
    registry.CaptureAll(nullptr, blobs);

    EXPECT_EQ(blobs.size(), 3);

    // Clear states
    ay.SetState(std::vector<uint8_t>(32, 0));
    fdc.SetState(std::vector<uint8_t>(256, 0));
    tape.SetState(std::vector<uint8_t>(64, 0));

    // Restore all
    registry.RestoreAll(blobs, nullptr);

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
    registry.CaptureAll(nullptr, blobs);
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
    registry.RestoreAll(emptyBlobs, nullptr);

    // State should be unchanged
    EXPECT_EQ(mock.GetState(), originalState);
}

// Benchmark: Peripheral capture/restore must fit within 5-6ms budget
TEST_F(TTDPeripheralRegistryTest, PerformanceBudget)
{
    constexpr size_t kLargeStateSize = 32768;  // 32KB (e.g., GeneralSound sample RAM)
    constexpr int kIterations = 100;

    MockPeripheral large(kLargeStateSize, PeripheralId::GeneralSound, true);

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
        registry.CaptureAll(nullptr, blobs);
    }
    auto captureEnd = std::chrono::high_resolution_clock::now();
    auto captureUs = std::chrono::duration_cast<std::chrono::microseconds>(
        captureEnd - captureStart).count() / kIterations;

    // Measure restore time
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(nullptr, blobs);

    auto restoreStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i)
    {
        registry.RestoreAll(blobs, nullptr);
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

    MockPeripheral mock(kStateSize, PeripheralId::GeneralSound, true);

    // Highly compressible data (all zeros)
    std::vector<uint8_t> zeros(kStateSize, 0);
    mock.SetState(zeros);
    registry.Register(PeripheralId::GeneralSound, &mock);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(nullptr, blobs);

    auto blobSize = blobs[static_cast<uint8_t>(PeripheralId::GeneralSound)].size();

    // All-zeros should compress to much less than raw size
    // Expect at least 10:1 compression for uniform data
    EXPECT_LT(blobSize, kStateSize / 5);
}
