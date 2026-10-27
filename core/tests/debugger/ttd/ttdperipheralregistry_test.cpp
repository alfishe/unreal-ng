#include <gtest/gtest.h>

#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdserializable.h"
#include "debugger/ttd/engine/ttdregiontracker.h"

#include <cstring>
#include <numeric>
#include <random>

using namespace ttd;

namespace {

/// Mock peripheral for testing with configurable state size.
class MockPeripheral : public TTDSerializable
{
public:
    explicit MockPeripheral(size_t stateSize, PeripheralId id = PeripheralId::None)
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
    registry.Register(&mock);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);

    EXPECT_EQ(blobs.size(), 1);
    EXPECT_TRUE(blobs.find(static_cast<uint8_t>(PeripheralId::TurboSound)) != blobs.end());
}

TEST_F(TTDPeripheralRegistryTest, UnregisteredPeripheralNoBytes)
{
    MockPeripheral mock1(64, PeripheralId::TurboSound);
    MockPeripheral mock2(128, PeripheralId::GeneralSound);

    registry.Register(&mock1);
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

    registry.Register(&mock);

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
    registry.Register(&mock);

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
    registry.Register(&mock);

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

    registry.Register(&ay);
    registry.Register(&tape);

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
    registry.Register(&ay);
    registry.Register(&tape);

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
    registry.Register(&ay);

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

    registry.Register(&ay);
    registry.Register(&fdc);
    registry.Register(&tape);

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
    registry.Register(&mock);

    EXPECT_TRUE(registry.IsRegistered(TTDDeviceType::Covox));

    registry.Unregister(mock.TTDDescribe().Key());

    EXPECT_FALSE(registry.IsRegistered(TTDDeviceType::Covox));

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    EXPECT_TRUE(blobs.empty());
}

TEST_F(TTDPeripheralRegistryTest, TotalStateSizeAccurate)
{
    MockPeripheral small(32, PeripheralId::TurboSound);
    MockPeripheral medium(256, PeripheralId::BetaDisk);
    MockPeripheral large(4096, PeripheralId::GeneralSound);

    registry.Register(&small);
    registry.Register(&medium);
    registry.Register(&large);

    EXPECT_EQ(registry.TotalStateSize(), 32 + 256 + 4096);
}

TEST_F(TTDPeripheralRegistryTest, EmptyBlobNotRestored)
{
    MockPeripheral mock(64, PeripheralId::TSFM);
    std::vector<uint8_t> originalState(64, 0x42);
    mock.SetState(originalState);

    registry.Register(&mock);

    // Empty blob map
    std::unordered_map<uint8_t, std::vector<uint8_t>> emptyBlobs;
    registry.RestoreAll(emptyBlobs);

    // State should be unchanged
    EXPECT_EQ(mock.GetState(), originalState);
}

// The worst case for capture/restore: 32 KB of *incompressible* data (zstd does maximum work and cannot early-out).
// What it costs in time is a speed of the host and of the build type (~20 us Release, ~1000 us Debug): the benchmark
// core/benchmarks/debugger/ttd/ttd_peripheralregistry_benchmark.cpp measures it, no test here reads a clock. The test
// pins what must hold on any host: the state survives capture, a change and restore bit for bit, and the blob of
// random data stays near the raw size (a duplicated pass or a copy would show as a blown-up blob or a wrong state)
TEST_F(TTDPeripheralRegistryTest, LargeIncompressibleStateRoundTrips)
{
    constexpr size_t kLargeStateSize = 32768;  // 32KB (e.g., GeneralSound sample RAM)

    MockPeripheral large(kLargeStateSize, PeripheralId::GeneralSound);

    // Random state data: a fixed seed, the same bytes on every run
    std::mt19937 gen(0x5EED);
    std::uniform_int_distribution<> dist(0, 255);
    std::vector<uint8_t> randomState(kLargeStateSize);
    for (auto& b : randomState)
        b = static_cast<uint8_t>(dist(gen));
    large.SetState(randomState);

    registry.Register(&large);

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    const auto& blob = blobs[static_cast<uint8_t>(PeripheralId::GeneralSound)];
    EXPECT_FALSE(blob.empty());
    EXPECT_LT(blob.size(), kLargeStateSize + kLargeStateSize / 16) << "random data must not grow past the raw size by much";

    large.SetState(std::vector<uint8_t>(kLargeStateSize, 0xAA));
    ASSERT_NE(large.GetState(), randomState);
    registry.RestoreAll(blobs);
    EXPECT_EQ(large.GetState(), randomState);
}

TEST_F(TTDPeripheralRegistryTest, CompressionEfficiency)
{
    constexpr size_t kStateSize = 4096;

    MockPeripheral mock(kStateSize, PeripheralId::GeneralSound);

    // Highly compressible data (all zeros)
    std::vector<uint8_t> zeros(kStateSize, 0);
    mock.SetState(zeros);
    registry.Register(&mock);

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
    registry.Register(&device);

    EXPECT_EQ(registry.ComputePeripheralHash(), kHash)
        << "a lone id-0 device must contribute its hash unrotated";
}

/// Registration order must not reach the hash.
TEST(TTDPeripheralHashTest, HashIsIndependentOfRegistrationOrder)
{
    HashingPeripheral a(PeripheralId::TurboSound, 0x1111222233334444ull);
    HashingPeripheral b(PeripheralId::ScorpionProfROM, 0x5555666677778888ull);

    TTDPeripheralRegistry forward;
    forward.Register(&a);
    forward.Register(&b);

    TTDPeripheralRegistry reverse;
    reverse.Register(&b);
    reverse.Register(&a);

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
    registry.Register(&a);
    registry.Register(&b);

    EXPECT_NE(registry.ComputePeripheralHash(), 0u);
}

/// The whole point of the contribution: a model-specific state change must
/// reach the common divergence hash.
TEST(TTDPeripheralHashTest, HashTracksDeviceStateChanges)
{
    HashingPeripheral device(PeripheralId::ScorpionProfROM, 0x1000ull);

    TTDPeripheralRegistry registry;
    registry.Register(&device);

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

// region <D23: devices are keys (d23-device-keys.md)>

namespace
{
/// A device added after the v1 ids froze: a type past them, no v1 id, its instance given
class KeyedPeripheral : public TTDSerializable
{
public:
    KeyedPeripheral(TTDDeviceType type, std::string instance, size_t size, uint8_t fill)
        : _state(size, fill), _type(type), _instance(std::move(instance))
    {
    }
    size_t TTDStateSize() const override { return _state.size(); }
    void TTDSaveState(uint8_t* dst) const override { std::memcpy(dst, _state.data(), _state.size()); }
    void TTDLoadState(const uint8_t* src) override { std::memcpy(_state.data(), src, _state.size()); }
    std::string TTDDeviceName() const override { return _instance; }
    TTDDeviceType TTDType() const override { return _type; }

    std::vector<uint8_t> _state;

private:
    TTDDeviceType _type;
    std::string _instance;
};

constexpr auto kNewType = static_cast<TTDDeviceType>(66);   // the first type without a v1 id

/// A device whose state carries memory it offers as engine regions: the registry pairs the two by object
class RegionPeripheral : public MockPeripheral, public ITTDRegionSource
{
public:
    RegionPeripheral() : MockPeripheral(16, PeripheralId::GeneralSound) {}
    void TTDRegions(std::vector<TTDDeviceRegion>&) override {}
    void TTDArmRegions(bool) override {}
    bool TTDStateWithoutRegions(std::vector<uint8_t>& state) const override
    {
        state.assign(2, 0x5A);
        return true;
    }
};
}  // namespace

/// The first device past the v1 ids records through the engine's capture: its state is kept per entry, v1 cannot
/// take it and says which device
TEST_F(TTDPeripheralRegistryTest, DeviceWithoutV1IdIsCapturedAndRefusedByV1)
{
    MockPeripheral ay(32, PeripheralId::TurboSound);
    KeyedPeripheral next(kNewType, "next.uart0", 40, 0x77);
    ASSERT_TRUE(registry.Register(&ay));
    ASSERT_TRUE(registry.Register(&next));

    std::string error;
    EXPECT_TRUE(registry.CheckDeviceTable(error)) << error;
    const std::vector<TTDDeviceEntry> entries = registry.DeviceEntries();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[1].descriptor.type, kNewType);
    EXPECT_EQ(entries[1].descriptor.legacyId, PeripheralId::None);

    registry.CaptureStates();
    EXPECT_EQ(registry.LastCaptureState(1), next._state);
    EXPECT_EQ(registry.LastCaptureStateBytes(), 32u + 40u);

    std::string why;
    EXPECT_FALSE(registry.V1Recordable(why));
    EXPECT_NE(why.find("next.uart0"), std::string::npos) << why;

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    EXPECT_EQ(blobs.size(), 1u) << "v1's blobs hold only the devices with a v1 id";
}

/// Two instances of one type sit side by side under their instance names; one key twice is refused
TEST_F(TTDPeripheralRegistryTest, TwoInstancesOfOneTypeAreTwoDevices)
{
    KeyedPeripheral a(TTDDeviceType::SerialPort, "next.uart0", 8, 0x11);
    KeyedPeripheral b(TTDDeviceType::SerialPort, "next.uart1", 8, 0x22);
    KeyedPeripheral again(TTDDeviceType::SerialPort, "next.uart0", 8, 0x33);
    ASSERT_TRUE(registry.Register(&a));
    ASSERT_TRUE(registry.Register(&b));
    EXPECT_FALSE(registry.Register(&again)) << "a key is held once";

    EXPECT_EQ(registry.Find({TTDDeviceType::SerialPort, "next.uart1"}), &b);
    EXPECT_EQ(registry.FindType(TTDDeviceType::SerialPort), &a);
    std::string error;
    EXPECT_FALSE(registry.CheckDeviceTable(error));
    EXPECT_NE(error.find("next.uart0"), std::string::npos) << error;

    registry.Clear();
    ASSERT_TRUE(registry.Register(&a));
    ASSERT_TRUE(registry.Register(&b));
    EXPECT_TRUE(registry.CheckDeviceTable(error)) << error;
    registry.CaptureStates();
    EXPECT_EQ(registry.LastCaptureState(0), a._state);
    EXPECT_EQ(registry.LastCaptureState(1), b._state);
}

/// The instance given at registration names the device (a slot card's device by its slot)
TEST_F(TTDPeripheralRegistryTest, InstanceGivenAtRegistrationNamesTheDevice)
{
    MockPeripheral gs(16, PeripheralId::NeoGS);
    ASSERT_TRUE(registry.Register(&gs, "zxbus.1.neogs"));
    EXPECT_EQ(registry.InstanceOf(TTDDeviceType::NeoGS), "zxbus.1.neogs");
    EXPECT_EQ(registry.DeviceEntries().at(0).descriptor.instance, "zxbus.1.neogs");
    EXPECT_EQ(registry.FindLegacy(static_cast<uint8_t>(PeripheralId::NeoGS)), &gs);
}

/// Not recorded and the type switch (the General Sound personality)
TEST_F(TTDPeripheralRegistryTest, NotRecordedAndUnregisterType)
{
    MockPeripheral gs(16, PeripheralId::GeneralSound);
    ASSERT_TRUE(registry.Register(&gs));
    registry.UnregisterType(TTDDeviceType::GeneralSound);
    EXPECT_FALSE(registry.IsRegistered(TTDDeviceType::GeneralSound));
    registry.MarkNotRecorded({TTDDeviceType::GeneralSoundLightweight, "gs-lw"});
    ASSERT_EQ(registry.NotRecorded().size(), 1u);
    EXPECT_EQ(registry.NotRecorded()[0].type, TTDDeviceType::GeneralSoundLightweight);
    registry.UnregisterType(TTDDeviceType::GeneralSoundLightweight);
    EXPECT_TRUE(registry.NotRecorded().empty());
}

/// A live snapshot carries every device, those without a v1 id too
TEST_F(TTDPeripheralRegistryTest, LiveSnapshotCarriesDevicesWithoutV1Id)
{
    MockPeripheral tape(8, PeripheralId::Tape);
    KeyedPeripheral next(kNewType, "next.thing", 8, 0x42);
    ASSERT_TRUE(registry.Register(&tape));
    ASSERT_TRUE(registry.Register(&next));
    std::vector<std::vector<uint8_t>> snapshot;
    registry.SaveLive(snapshot);
    tape.SetState(std::vector<uint8_t>(8, 0x99));
    next._state.assign(8, 0);
    registry.LoadLive(snapshot);
    EXPECT_EQ(tape.GetState(), std::vector<uint8_t>(8, 0));
    EXPECT_EQ(next._state, std::vector<uint8_t>(8, 0x42));
}

/// A region source that is the registered device itself gives the engine its state without the region memory
TEST_F(TTDPeripheralRegistryTest, RegionSourcePairsWithItsDevice)
{
    RegionPeripheral gs;
    ASSERT_TRUE(registry.Register(&gs));
    registry.RegisterRegionSource(&gs);
    const std::vector<TTDDeviceEntry> entries = registry.DeviceEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].withoutRegions, &gs);
    EXPECT_EQ(entries[0].descriptor.stateSize, 2u);
    registry.CaptureStates();
    EXPECT_EQ(registry.LastCaptureState(0), std::vector<uint8_t>(2, 0x5A));
}

/// v1 blobs as device states: under the live device's key
TEST_F(TTDPeripheralRegistryTest, V1BlobsBecomeStatesByLiveKey)
{
    MockPeripheral gs(16, PeripheralId::NeoGS);
    ASSERT_TRUE(registry.Register(&gs, "zxbus.2.neogs"));
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    const std::vector<uint8_t> tape(4, 1);
    blobs[static_cast<uint8_t>(PeripheralId::Tape)] =
        TTDPeripheralRegistry::EncodeBlob(static_cast<uint8_t>(PeripheralId::Tape), tape.data(), tape.size());
    const TTDDeviceStates states = registry.StatesOfV1Blobs(blobs);
    ASSERT_EQ(states.size(), 2u);
    EXPECT_EQ(states.count({TTDDeviceType::NeoGS, "zxbus.2.neogs"}), 1u);
    EXPECT_EQ(states.at({TTDDeviceType::Tape, ""}), tape);
    PeripheralMask mask = PeripheralMask::FromLow(0x801);
    mask.Set(static_cast<uint8_t>(PeripheralId::JoystickSerialPeer));   // an id past 63: the mask's second word
    EXPECT_EQ(TTDPeripheralRegistry::V1MaskOf(TTDPeripheralRegistry::KeysOfV1Mask(mask)), mask);
}

// endregion </D23>
