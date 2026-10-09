/// @file timetravelengine_test.cpp
/// @brief TimeTravelEngine unit tests: sessions, capture, restore, parents and
/// accounting, on hand-made frame inputs (no emulator).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdperipheralregistry.h"

using namespace ttd;

namespace
{

TTDRegionDesc Region(TTDRegionId id, const char* name, uint32_t pieces)
{
    TTDRegionDesc r;
    r.id = id;
    r.name = name;
    r.pieces = pieces;
    r.bytes = pieces * kTTDPieceSize;
    return r;
}

std::vector<uint8_t> Piece(uint8_t fill)
{
    return std::vector<uint8_t>(kTTDPieceSize, fill);
}

TTDFrameInput Frame(uint64_t frame)
{
    TTDFrameInput in;
    in.position.frame = frame;
    in.start = frame * 71680;
    return in;
}

}  // namespace

TEST(TimeTravelEngine_Test, RestoresEachCheckpointsMemory)
{
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 4)}, err)) << err;

    const auto a = Piece(0x11), b = Piece(0x22), c = Piece(0x33), zero = Piece(0);
    TTDFrameInput f0 = Frame(100);
    f0.changed = {{0, 0, a.data()}, {0, 1, b.data()}, {0, 2, zero.data()}};
    ASSERT_TRUE(engine.CaptureFrame(f0, err)) << err;
    TTDFrameInput f1 = Frame(101);
    f1.changed = {{0, 1, c.data()}};
    ASSERT_TRUE(engine.CaptureFrame(f1, err)) << err;
    ASSERT_TRUE(engine.CaptureFrame(Frame(102), err)) << err;   // nothing changed

    std::vector<uint8_t> out(4 * kTTDPieceSize, 0xEE);
    std::vector<uint8_t> present;
    ASSERT_TRUE(engine.RestoreRegion(0, 0, out.data(), &present).Ok());
    EXPECT_EQ(present, (std::vector<uint8_t>{1, 1, 1, 0})) << "piece 3 was never seen";
    EXPECT_EQ(out[0], 0x11);
    EXPECT_EQ(out[kTTDPieceSize], 0x22);
    EXPECT_EQ(out[2 * kTTDPieceSize], 0x00);
    EXPECT_EQ(out[3 * kTTDPieceSize], 0xEE) << "an unseen piece is left untouched";

    ASSERT_TRUE(engine.RestoreRegion(2, 0, out.data(), &present).Ok());
    EXPECT_EQ(out[kTTDPieceSize], 0x33) << "the change of frame 101 holds in frame 102";
    ASSERT_TRUE(engine.RestoreRegion(0, 0, out.data(), &present).Ok());
    EXPECT_EQ(out[kTTDPieceSize], 0x22) << "going back restores the older content";
}

TEST(TimeTravelEngine_Test, CheckpointsLinkTheirParent_AndFindTheirPosition)
{
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1)}, err));
    for (uint64_t f = 7; f < 10; ++f)
        ASSERT_TRUE(engine.CaptureFrame(Frame(f), err)) << err;

    EXPECT_EQ(engine.Checkpoint(0)->parent, TTDEngineCheckpoint::kNoParent);
    EXPECT_EQ(engine.Checkpoint(2)->parent, 1u);
    EXPECT_EQ(engine.CheckpointIndexOf({0, 9, 0}), 2);
    EXPECT_EQ(engine.CheckpointIndexOf({0, 10, 0}), -1);
    EXPECT_EQ(engine.CheckpointIndexOf({1, 9, 0}), -1) << "no branch recorded yet";
}

TEST(TimeTravelEngine_Test, RefusesBadInput)
{
    TimeTravelEngine engine;
    std::string err;
    EXPECT_FALSE(engine.CaptureFrame(Frame(1), err)) << "no session yet";
    EXPECT_FALSE(engine.BeginSession({}, err));
    EXPECT_FALSE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1), Region(TTDRegionId::MachineRam, "x", 1)},
                                     err))
        << "a region id is listed once";
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 2)}, err));

    const auto a = Piece(1);
    TTDFrameInput bad = Frame(1);
    bad.changed = {{0, 2, a.data()}};
    EXPECT_FALSE(engine.CaptureFrame(bad, err)) << "piece beyond the region";
    bad.changed = {{1, 0, a.data()}};
    EXPECT_FALSE(engine.CaptureFrame(bad, err)) << "unknown region";

    ASSERT_TRUE(engine.CaptureFrame(Frame(5), err));
    EXPECT_FALSE(engine.CaptureFrame(Frame(5), err)) << "frames must increase";
    TTDFrameInput branch = Frame(6);
    branch.position.branch = 1;
    EXPECT_FALSE(engine.CaptureFrame(branch, err)) << "Phase 1 records the trunk only";
    EXPECT_EQ(engine.CheckpointCount(), 1u);
}

TEST(TimeTravelEngine_Test, UnchangedFramesShareTheReferenceTable)
{
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 256)}, err));
    const auto a = Piece(9);
    TTDFrameInput f0 = Frame(0);
    f0.changed = {{0, 0, a.data()}};
    ASSERT_TRUE(engine.CaptureFrame(f0, err));
    const size_t tablesAfterOne = engine.HeapBreakdown().referenceTables;
    for (uint64_t f = 1; f < 50; ++f)
        ASSERT_TRUE(engine.CaptureFrame(Frame(f), err));
    EXPECT_EQ(engine.HeapBreakdown().referenceTables, tablesAfterOne)
        << "frames that change nothing add no reference table";
    for (uint64_t f = 1; f < 50; ++f)
        EXPECT_EQ(engine.ChangeCount(f, 0), 0u) << "frame " << f << " records nothing";
}

TEST(TimeTravelEngine_Test, OptionalStreamsRunAtEveryCapturedFrameWhenOn)
{
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1)}, err));
    std::vector<uint64_t> seen;
    ASSERT_TRUE(engine.Streams().Register(0, "screenshot", [&](const TTDPosition& at) { seen.push_back(at.frame); }));
    ASSERT_TRUE(engine.CaptureFrame(Frame(1), err));
    ASSERT_TRUE(engine.Streams().SetEnabled(0, true));
    ASSERT_TRUE(engine.CaptureFrame(Frame(2), err));
    EXPECT_EQ(seen, (std::vector<uint64_t>{2}));
}

TEST(TimeTravelEngine_Test, ArbitraryContentRoundTrips_AndBadRequestsAreReported)
{
    // Content that is neither constant nor zero comes back byte for byte; a restore
    // of something that does not exist reports it instead of handing back wrong memory
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1)}, err));
    std::vector<uint8_t> p(kTTDPieceSize);
    for (size_t i = 0; i < p.size(); ++i)
        p[i] = static_cast<uint8_t>(i * 7);
    TTDFrameInput f0 = Frame(0);
    f0.changed = {{0, 0, p.data()}};
    ASSERT_TRUE(engine.CaptureFrame(f0, err));
    std::vector<uint8_t> out(kTTDPieceSize);
    EXPECT_TRUE(engine.RestoreRegion(0, 0, out.data()).Ok());
    EXPECT_EQ(out, p);
    EXPECT_EQ(engine.RestoreRegion(1, 0, out.data()).status, TTDRestoreStatus::Damaged) << "no such checkpoint";
}

TEST(TimeTravelEngine_Test, OneChangedPieceRecordsOneChange_TheRestIsShared)
{
    // ZX-Evo-sized RAM: 1,024 pieces = 32 blocks of 8 pages
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1024)}, err));
    std::vector<std::vector<uint8_t>> pieces;
    TTDFrameInput f0 = Frame(0);
    for (uint32_t p = 0; p < 1024; ++p)
    {
        pieces.push_back(Piece(static_cast<uint8_t>(p | 1)));
        f0.changed.push_back({0, p, pieces.back().data()});
    }
    ASSERT_TRUE(engine.CaptureFrame(f0, err));
    const size_t blocksAfterFirst = engine.HeapBreakdown().referenceTables;

    const auto changed = Piece(0x77);
    TTDFrameInput f1 = Frame(1);
    f1.changed = {{0, 100, changed.data()}};
    ASSERT_TRUE(engine.CaptureFrame(f1, err));
    for (uint32_t p = 0; p < 1024; ++p)
        if (p / 32 != 100 / 32)
            ASSERT_EQ(engine.VersionAt(0, 0, p), engine.VersionAt(1, 0, p));
    EXPECT_NE(engine.VersionAt(0, 0, 100), engine.VersionAt(1, 0, 100));

    // One change record, not a copy of the whole map
    EXPECT_EQ(engine.ChangeCount(1, 0), 1u);
    (void)blocksAfterFirst;
}

TEST(TimeTravelEngine_Test, ContentRewrittenUnchangedRecordsNothing)
{
    // A dirty piece whose content came out the same gets no new version, so no block is cloned
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 64)}, err));
    const auto a = Piece(5);
    TTDFrameInput f0 = Frame(0);
    f0.changed = {{0, 3, a.data()}};
    ASSERT_TRUE(engine.CaptureFrame(f0, err));
    TTDFrameInput f1 = Frame(1);
    f1.changed = {{0, 3, a.data()}};
    ASSERT_TRUE(engine.CaptureFrame(f1, err));
    EXPECT_EQ(engine.ChangeCount(1, 0), 0u);
    EXPECT_EQ(engine.VersionAt(1, 0, 3), engine.VersionAt(0, 0, 3));
}

TEST(TimeTravelEngine_Test, EndSessionReleasesEveryVersion_SharedStoreKeepsTheOtherSession)
{
    auto store = std::make_shared<TTDPieceStore>();
    TimeTravelEngine a(store);
    TimeTravelEngine b(store);
    std::string err;
    ASSERT_TRUE(a.BeginSession({Region(TTDRegionId::MachineRam, "ram", 8)}, err));
    ASSERT_TRUE(b.BeginSession({Region(TTDRegionId::MachineRam, "ram", 8)}, err));
    std::vector<std::vector<uint8_t>> data;
    for (uint64_t f = 0; f < 20; ++f)
    {
        data.push_back(Piece(static_cast<uint8_t>(f + 1)));
        data.back()[f] ^= 0x33;
        TTDFrameInput in = Frame(f);
        in.changed = {{0, static_cast<uint32_t>(f % 8), data.back().data()}};
        ASSERT_TRUE(a.CaptureFrame(in, err));
        ASSERT_TRUE(b.CaptureFrame(in, err));
    }
    const size_t both = store->LiveVersions();
    ASSERT_GT(both, 0u);

    a.EndSession();
    EXPECT_EQ(store->LiveVersions(), both / 2) << "a's versions are gone, b's stay";
    std::vector<uint8_t> out(8 * kTTDPieceSize);
    EXPECT_TRUE(b.RestoreRegion(19, 0, out.data()).Ok());
    EXPECT_EQ(out[size_t(19 % 8) * kTTDPieceSize], static_cast<uint8_t>(20));

    b.EndSession();
    EXPECT_EQ(store->LiveVersions(), 0u) << "no version leaks";
    EXPECT_EQ(store->PayloadBytes(), 0u);
}

TEST(TimeTravelEngine_Test, FullTablesShareUnchangedBlocks_AndRestoresCrossThem)
{
    // Full tables every 4 checkpoints; a restore between them starts from the last one
    TimeTravelEngine engine;
    engine.SetSnapshotInterval(4);
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1024)}, err));
    std::vector<std::vector<uint8_t>> frames;
    for (uint64_t f = 0; f < 13; ++f)
    {
        frames.push_back(Piece(static_cast<uint8_t>(f + 1)));
        TTDFrameInput in = Frame(f);
        in.changed = {{0, static_cast<uint32_t>(f * 37 % 1024), frames.back().data()}};
        ASSERT_TRUE(engine.CaptureFrame(in, err));
    }
    for (uint64_t f = 0; f < 13; ++f)
        EXPECT_EQ(engine.HasFullTable(f, 0), f % 4 == 0) << "frame " << f;

    std::vector<uint8_t> out(1024 * kTTDPieceSize);
    std::vector<uint8_t> present;
    for (uint64_t f = 0; f < 13; ++f)
    {
        ASSERT_TRUE(engine.RestoreRegion(f, 0, out.data(), &present).Ok());
        for (uint64_t g = 0; g <= f; ++g)
        {
            const size_t piece = g * 37 % 1024;
            ASSERT_EQ(present[piece], 1u);
            ASSERT_EQ(out[piece * kTTDPieceSize], static_cast<uint8_t>(g + 1)) << "frame " << f << ", change " << g;
        }
        EXPECT_EQ(static_cast<size_t>(std::count(present.begin(), present.end(), uint8_t(1))), f + 1);
    }
}

namespace
{
/// A 4-byte device for the restore-result tests
class FakeDevice : public TTDSerializable
{
public:
    explicit FakeDevice(PeripheralId id) : _id(id) {}
    size_t TTDStateSize() const override { return 4; }
    void TTDSaveState(uint8_t* dst) const override { std::memcpy(dst, state, 4); }
    void TTDLoadState(const uint8_t* src) override { std::memcpy(state, src, 4); }
    std::string TTDDeviceName() const override { return "Fake" + std::to_string(static_cast<int>(_id)); }
    PeripheralId TTDPeripheralId() const override { return _id; }
    TTDDeviceDescriptor TTDDescribe() const override
    {
        TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
        d.firmwareFingerprint = firmware;
        return d;
    }
    void TTDAfterRestore(const TTDRestoreContext&) override { ++afterRestore; }

    uint8_t state[4] = {1, 2, 3, 4};
    uint64_t firmware = 0;
    int afterRestore = 0;

private:
    PeripheralId _id;
};

std::vector<uint8_t> Blob(PeripheralId id, std::vector<uint8_t> state)
{
    return TTDPeripheralRegistry::EncodeBlob(static_cast<uint8_t>(id), state.data(), state.size());
}
}  // namespace

/// Phase 2, Step 3: every device problem of a restore is an issue naming the
/// device and what it holds now; the worst sets the result
TEST(TimeTravelEngine_RestoreDevices_Test, EachProblemIsReportedWithTheDevice)
{
    TimeTravelEngine engine;
    std::string err;
    std::vector<uint8_t> ram(kTTDPieceSize, 0);
    TTDRegionDesc r;
    r.name = "ram";
    r.memory = ram.data();
    r.pieces = 1;
    r.bytes = kTTDPieceSize;
    FakeDevice tape(PeripheralId::Tape);       // no state at the checkpoint
    FakeDevice covox(PeripheralId::Covox);     // no state
    FakeDevice mouse(PeripheralId::KempstonMouse);   // state of the wrong size
    FakeDevice beta(PeripheralId::BetaDisk);   // restored, firmware changed since
    beta.firmware = 0x1111;
    ASSERT_TRUE(engine.BeginSession({r},
                                    {{tape.TTDDescribe(), &tape, nullptr},
                                     {covox.TTDDescribe(), &covox, nullptr},
                                     {mouse.TTDDescribe(), &mouse, nullptr},
                                     {beta.TTDDescribe(), &beta, nullptr}},
                                    err))
        << err;
    beta.firmware = 0x2222;

    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    blobs[static_cast<uint8_t>(PeripheralId::KempstonMouse)] = Blob(PeripheralId::KempstonMouse, {9, 9});
    blobs[static_cast<uint8_t>(PeripheralId::BetaDisk)] = Blob(PeripheralId::BetaDisk, {7, 7, 7, 7});
    blobs[static_cast<uint8_t>(PeripheralId::NeoGS)] = Blob(PeripheralId::NeoGS, {1});   // no such device here
    TTDFrameInput in;
    in.position = {0, 1, 0};
    in.deviceBlobs = &blobs;
    in.changed.push_back({0, 0, ram.data()});
    ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;

    const TTDRestoreResult result = engine.RestoreDevices(0, TTDRestoreContext{1, 0, false});
    EXPECT_EQ(result.status, TTDRestoreStatus::Degraded);
    auto find = [&](TTDRestoreIssueKind kind, const std::string& instance) -> const TTDRestoreIssue* {
        for (const TTDRestoreIssue& i : result.issues)
            if (i.kind == kind && i.device.instance == instance)
                return &i;
        return nullptr;
    };
    const TTDRestoreIssue* t = find(TTDRestoreIssueKind::DeviceMissingState, "fake2");
    ASSERT_NE(t, nullptr) << result.message;
    EXPECT_EQ(t->action, TTDLiveStateAction::KeptLive);
    EXPECT_EQ(tape.state[0], 1) << "kept its live state";
    const TTDRestoreIssue* c = find(TTDRestoreIssueKind::DeviceMissingState, "fake3");
    ASSERT_NE(c, nullptr) << result.message;
    EXPECT_EQ(c->action, TTDLiveStateAction::KeptLive);
    EXPECT_EQ(covox.state[0], 1) << "kept its live state";
    ASSERT_NE(find(TTDRestoreIssueKind::SizeMismatch, "fake7"), nullptr) << result.message;
    EXPECT_EQ(mouse.state[0], 1) << "a state that does not fit is not loaded";
    const TTDRestoreIssue* f = find(TTDRestoreIssueKind::FirmwareDiffers, "fake1");
    ASSERT_NE(f, nullptr) << result.message;
    EXPECT_EQ(f->severity, TTDRestoreStatus::NotBitExact);
    EXPECT_EQ(beta.state[0], 7) << "restored all the same";
    bool notPresent = false;
    for (const TTDRestoreIssue& i : result.issues)
        notPresent |= i.kind == TTDRestoreIssueKind::DeviceNotPresent;
    EXPECT_TRUE(notPresent) << result.message;
    EXPECT_EQ(tape.afterRestore + covox.afterRestore + mouse.afterRestore + beta.afterRestore, 4)
        << "every device's after-restore call, once";
}

/// A region smaller than a piece (the SMUC EEPROM, 2 KB): restoring it writes
/// its real bytes only, never past the device's memory
TEST(TimeTravelEngine_Test, RestoringAPartialPieceStaysInsideTheRegion)
{
    std::vector<uint8_t> memory(2048 + 64, 0xEE);   // the region, then guard bytes
    TimeTravelEngine engine;
    std::string err;
    TTDRegionDesc r;
    r.name = "eeprom";
    r.memory = memory.data();
    r.pieces = 1;
    r.bytes = 2048;
    ASSERT_TRUE(engine.BeginSession({r}, err)) << err;

    std::vector<uint8_t> padded(kTTDPieceSize, 0);
    for (int f = 1; f <= 2; ++f)
    {
        std::fill(memory.begin(), memory.begin() + 2048, static_cast<uint8_t>(f));
        std::copy(memory.begin(), memory.begin() + 2048, padded.begin());
        TTDFrameInput in;
        in.position = {0, static_cast<uint64_t>(f), 0};
        in.changed.push_back({0, 0, padded.data()});
        ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
    }
    std::fill(memory.begin(), memory.begin() + 2048, 0x77);
    engine.ForgetMemory();
    ASSERT_TRUE(engine.RestoreToMemory(0).Ok());
    EXPECT_EQ(memory[0], 1);
    EXPECT_EQ(memory[2047], 1);
    for (size_t i = 2048; i < memory.size(); ++i)
        ASSERT_EQ(memory[i], 0xEE) << "byte " << i << " past the region was written";
}

/// Phase 2: time fields. A device declares the bytes that count time; the
/// engine stores each as its residual from a line through an anchor. Every
/// value comes back exactly at every checkpoint, whatever the field does; a
/// field that keeps its pace stops costing anything
TEST(TimeTravelEngine_TimeFields_Test, EveryValueComesBackExactly_SteadyFieldsCostNothing)
{
    // State: [0..7] steady +71680/frame (u64), [8..15] alternating +693633/+693634 (u64),
    // [16] u8 +228/frame (wraps), [17..20] u32 that jumps at frame 40 (a speed change),
    // [21..28] a "time field" that is really noise, [29..31] plain bytes
    struct Device : TTDSerializable
    {
        uint8_t state[32] = {};
        size_t TTDStateSize() const override { return sizeof(state); }
        void TTDSaveState(uint8_t* dst) const override { std::memcpy(dst, state, sizeof(state)); }
        void TTDLoadState(const uint8_t* src) override { std::memcpy(state, src, sizeof(state)); }
        std::string TTDDeviceName() const override { return "Clocks"; }
        PeripheralId TTDPeripheralId() const override { return PeripheralId::MoonSound; }
        TTDDeviceDescriptor TTDDescribe() const override
        {
            TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
            d.timeFields = {{0, 8}, {8, 8}, {16, 1}, {17, 4}, {21, 8}};
            return d;
        }
    } dev;
    auto put = [&](size_t off, uint8_t width, uint64_t v) {
        for (uint8_t i = 0; i < width; ++i)
            dev.state[off + i] = static_cast<uint8_t>(v >> (8 * i));
    };

    TimeTravelEngine engine;
    std::string err;
    std::vector<uint8_t> ram(kTTDPieceSize, 0);
    TTDRegionDesc r;
    r.name = "ram";
    r.memory = ram.data();
    r.pieces = 1;
    r.bytes = kTTDPieceSize;
    ASSERT_TRUE(engine.BeginSession({r}, {{dev.TTDDescribe(), &dev, nullptr}}, err)) << err;
    const auto id = static_cast<uint8_t>(PeripheralId::MoonSound);

    uint32_t noise = 12345;
    std::vector<std::vector<uint8_t>> expected;
    for (uint64_t f = 0; f < 80; ++f)
    {
        put(0, 8, 0x1234 + f * 71680);
        put(8, 8, 0x77 + f * 693633 + f / 2);
        put(16, 1, f * 228);
        put(17, 4, f < 40 ? 1000 + f * 300 : 5000000 + f * 600);
        noise = noise * 1664525u + 1013904223u;
        put(21, 8, noise);
        dev.state[29] = static_cast<uint8_t>(f == 60);
        expected.emplace_back(dev.state, dev.state + sizeof(dev.state));

        TTDFrameInput in;
        in.position = {0, f, 0};
        in.deviceStates.push_back({id, dev.state, sizeof(dev.state)});
        ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
    }
    for (size_t i = 0; i < expected.size(); ++i)
    {
        std::vector<uint8_t> got;
        ASSERT_TRUE(engine.DeviceState(i, id, got)) << "checkpoint " << i;
        ASSERT_TRUE(got == expected[i]) << "checkpoint " << i;
    }
}

TEST(TimeTravelEngine_TimeFields_Test, AFieldKeepingItsPaceAddsNoVersions)
{
    struct Device : TTDSerializable
    {
        uint8_t state[24] = {};
        size_t TTDStateSize() const override { return sizeof(state); }
        void TTDSaveState(uint8_t* dst) const override { std::memcpy(dst, state, sizeof(state)); }
        void TTDLoadState(const uint8_t* src) override { std::memcpy(state, src, sizeof(state)); }
        std::string TTDDeviceName() const override { return "Clock"; }
        PeripheralId TTDPeripheralId() const override { return PeripheralId::NeoGS; }
        TTDDeviceDescriptor TTDDescribe() const override
        {
            TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
            d.timeFields = {{0, 8}, {8, 4}};
            return d;
        }
    } dev;
    TimeTravelEngine engine;
    std::string err;
    std::vector<uint8_t> ram(kTTDPieceSize, 0);
    TTDRegionDesc r;
    r.name = "ram";
    r.memory = ram.data();
    r.pieces = 1;
    r.bytes = kTTDPieceSize;
    ASSERT_TRUE(engine.BeginSession({r}, {{dev.TTDDescribe(), &dev, nullptr}}, err)) << err;
    const auto id = static_cast<uint8_t>(PeripheralId::NeoGS);
    size_t versions = 0;
    for (uint64_t f = 0; f < 200; ++f)
    {
        const uint64_t clock = 2457600 * f + (f % 2) * 30;   // a card clock with its overshoot
        const uint32_t timer = static_cast<uint32_t>(491520 * f);
        std::memcpy(dev.state, &clock, 8);
        std::memcpy(dev.state + 8, &timer, 4);
        TTDFrameInput in;
        in.position = {0, f, 0};
        in.deviceStates.push_back({id, dev.state, sizeof(dev.state)});
        ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
        if (f == 10)
            versions = engine.PieceStore().LiveVersions();
    }
    // The overshoot alternates 0 / 30: the residual alternates too (two
    // contents of one piece), so new versions keep coming at that rate at most;
    // the timer, on its line, adds none
    EXPECT_LE(engine.PieceStore().LiveVersions() - versions, 190u);
    for (size_t i = 0; i < 200; ++i)
    {
        std::vector<uint8_t> got;
        ASSERT_TRUE(engine.DeviceState(i, id, got));
        uint64_t clock = 0;
        uint32_t timer = 0;
        std::memcpy(&clock, got.data(), 8);
        std::memcpy(&timer, got.data() + 8, 4);
        ASSERT_EQ(clock, 2457600 * i + (i % 2) * 30) << "checkpoint " << i;
        ASSERT_EQ(timer, static_cast<uint32_t>(491520 * i)) << "checkpoint " << i;
    }
}

TEST(TimeTravelEngine_TimeFields_Test, AnEvenPaceStoresNothingAfterTheLineIsSet)
{
    struct Device : TTDSerializable
    {
        uint8_t state[16] = {};
        size_t TTDStateSize() const override { return sizeof(state); }
        void TTDSaveState(uint8_t* dst) const override { std::memcpy(dst, state, sizeof(state)); }
        void TTDLoadState(const uint8_t* src) override { std::memcpy(state, src, sizeof(state)); }
        std::string TTDDeviceName() const override { return "Origin"; }
        PeripheralId TTDPeripheralId() const override { return PeripheralId::Tape; }
        TTDDeviceDescriptor TTDDescribe() const override
        {
            TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
            d.timeFields = {{0, 8}};
            return d;
        }
    } dev;
    TimeTravelEngine engine;
    std::string err;
    std::vector<uint8_t> ram(kTTDPieceSize, 0);
    TTDRegionDesc r;
    r.name = "ram";
    r.memory = ram.data();
    r.pieces = 1;
    r.bytes = kTTDPieceSize;
    ASSERT_TRUE(engine.BeginSession({r}, {{dev.TTDDescribe(), &dev, nullptr}}, err)) << err;
    const auto id = static_cast<uint8_t>(PeripheralId::Tape);
    size_t versionsAt5 = 0;
    for (uint64_t f = 0; f < 100; ++f)
    {
        const uint64_t origin = 71680 * f;
        std::memcpy(dev.state, &origin, 8);
        TTDFrameInput in;
        in.position = {0, f, 0};
        in.deviceStates.push_back({id, dev.state, sizeof(dev.state)});
        ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
        if (f == 5)
            versionsAt5 = engine.PieceStore().LiveVersions();
    }
    EXPECT_EQ(engine.PieceStore().LiveVersions(), versionsAt5) << "a steady clock adds no version once its line is set";
    std::vector<uint8_t> got;
    ASSERT_TRUE(engine.DeviceState(99, id, got));
    uint64_t origin = 0;
    std::memcpy(&origin, got.data(), 8);
    EXPECT_EQ(origin, 71680u * 99);
}

/// FR-19: a device that runs behind the CPU and is not at the frame boundary
/// is reported at the capture (the frame is still recorded) and, after a
/// restore leaves it there, as AfterRestoreFailed; a synced one is not
TEST(TimeTravelEngine_Sync_Test, ADeviceNotAtTheBoundaryIsReportedAtCaptureAndRestore)
{
    struct Card : TTDSerializable
    {
        uint8_t state[8] = {};
        bool synced = true;
        int64_t offset = 0;
        size_t TTDStateSize() const override { return sizeof(state); }
        void TTDSaveState(uint8_t* dst) const override { std::memcpy(dst, state, sizeof(state)); }
        void TTDLoadState(const uint8_t* src) override { std::memcpy(state, src, sizeof(state)); }
        std::string TTDDeviceName() const override { return "Card"; }
        PeripheralId TTDPeripheralId() const override { return PeripheralId::NeoGS; }
        TTDDeviceDescriptor TTDDescribe() const override
        {
            TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
            d.runsBehindCpu = true;
            return d;
        }
        bool TTDSyncedTime(int64_t& o) const override
        {
            o = offset;
            return synced;
        }
    } card;
    TimeTravelEngine engine;
    std::string err;
    std::vector<uint8_t> ram(kTTDPieceSize, 0);
    TTDRegionDesc r;
    r.name = "ram";
    r.memory = ram.data();
    r.pieces = 1;
    r.bytes = kTTDPieceSize;
    ASSERT_TRUE(engine.BeginSession({r}, {{card.TTDDescribe(), &card, nullptr}}, err)) << err;
    const auto id = static_cast<uint8_t>(PeripheralId::NeoGS);
    for (uint64_t f = 0; f < 4; ++f)
    {
        card.synced = f != 2;
        card.offset = f == 2 ? -700 : 0;
        TTDFrameInput in;
        in.position = {0, f, 0};
        in.deviceStates.push_back({id, card.state, sizeof(card.state)});
        ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
    }
    EXPECT_EQ(engine.CheckpointCount(), 4u) << "a miss is reported, the frame still recorded";
    ASSERT_EQ(engine.SyncMissCount(), 1u);
    EXPECT_EQ(engine.SyncMisses()[0].frame, 2u);
    EXPECT_EQ(engine.SyncMisses()[0].device.instance, "card");
    EXPECT_EQ(engine.SyncMisses()[0].offset, -700);

    card.synced = true;
    EXPECT_EQ(engine.RestoreDevices(1, {}).status, TTDRestoreStatus::Exact);
    card.synced = false;
    const TTDRestoreResult result = engine.RestoreDevices(1, {});
    ASSERT_EQ(result.issues.size(), 1u);
    EXPECT_EQ(result.issues[0].kind, TTDRestoreIssueKind::AfterRestoreFailed);
    EXPECT_EQ(result.issues[0].device.instance, "card");
    EXPECT_FALSE(result.Ok());
}

/// Damage (FR-7): a stored version that fails its checksum is reported as
/// DataDamaged with the frames it reaches - from the change that stored it,
/// through the differences built on it, to the next change that does not
/// depend on it; checkpoints outside that range restore exactly.
/// CheckSession finds the same range without restoring anything
TEST(TimeTravelEngine_Damage_Test, ADamagedPieceIsReportedWithTheFramesItReaches)
{
    TimeTravelEngine engine;
    std::string err;
    std::vector<uint8_t> ram(4 * kTTDPieceSize, 0);
    TTDRegionDesc r;
    r.name = "ram";
    r.memory = ram.data();
    r.pieces = 4;
    r.bytes = 4 * kTTDPieceSize;
    ASSERT_TRUE(engine.BeginSession({r}, {}, err)) << err;
    uint32_t noise = 77;
    for (uint64_t f = 0; f < 10; ++f)
    {
        uint8_t* piece = ram.data() + kTTDPieceSize;
        if (f == 3)
            for (size_t i = 0; i < kTTDPieceSize; ++i)
                piece[i] = static_cast<uint8_t>((noise = noise * 1664525u + 1013904223u) >> 24);
        if (f == 4)
            piece[100] ^= 1;   // a difference on frame 3's version
        if (f == 7)
            std::memset(piece, 0, kTTDPieceSize);   // depends on nothing
        TTDFrameInput in;
        in.position = {0, f, 0};
        for (uint32_t p = 0; p < 4; ++p)
            in.changed.push_back({0, p, ram.data() + size_t(p) * kTTDPieceSize});
        ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
    }
    ASSERT_TRUE(engine.DamageForTesting(3, 0, 1));

    std::vector<uint8_t> out(4 * kTTDPieceSize);
    for (size_t i : {size_t(0), size_t(2), size_t(7), size_t(9)})
        EXPECT_EQ(engine.RestoreRegion(i, 0, out.data()).status, TTDRestoreStatus::Exact) << "checkpoint " << i;
    const TTDRestoreResult damaged = engine.RestoreRegion(5, 0, out.data());
    EXPECT_EQ(damaged.status, TTDRestoreStatus::Damaged);
    ASSERT_EQ(damaged.issues.size(), 1u) << damaged.message;
    EXPECT_EQ(damaged.issues[0].kind, TTDRestoreIssueKind::DataDamaged);
    EXPECT_EQ(damaged.issues[0].firstFrame, 3u);
    EXPECT_EQ(damaged.issues[0].lastFrame, 6u);
    EXPECT_NE(damaged.issues[0].detail.find("piece 1"), std::string::npos) << damaged.issues[0].detail;

    const TTDRestoreResult check = engine.CheckSession();
    EXPECT_EQ(check.status, TTDRestoreStatus::Damaged);
    ASSERT_EQ(check.issues.size(), 1u) << check.message;
    EXPECT_EQ(check.issues[0].firstFrame, 3u);
    EXPECT_EQ(check.issues[0].lastFrame, 6u);
}

/// A device's damaged state is DataDamaged with the device named (not "no
/// state"); frames in which a device gave no state are listed by
/// CheckSession as DeviceMissingState with their frames
TEST(TimeTravelEngine_Damage_Test, DeviceDamageAndMissingStateAreNamedWithTheirFrames)
{
    struct Device : TTDSerializable
    {
        uint8_t state[16] = {};
        size_t TTDStateSize() const override { return sizeof(state); }
        void TTDSaveState(uint8_t* dst) const override { std::memcpy(dst, state, sizeof(state)); }
        void TTDLoadState(const uint8_t* src) override { std::memcpy(state, src, sizeof(state)); }
        std::string TTDDeviceName() const override { return "Probe"; }
        PeripheralId TTDPeripheralId() const override { return PeripheralId::Covox; }
    } dev;
    TimeTravelEngine engine;
    std::string err;
    std::vector<uint8_t> ram(kTTDPieceSize, 0);
    TTDRegionDesc r;
    r.name = "ram";
    r.memory = ram.data();
    r.pieces = 1;
    r.bytes = kTTDPieceSize;
    ASSERT_TRUE(engine.BeginSession({r}, {{dev.TTDDescribe(), &dev, nullptr}}, err)) << err;
    const auto id = static_cast<uint8_t>(PeripheralId::Covox);
    for (uint64_t f = 0; f < 12; ++f)
    {
        dev.state[0] = static_cast<uint8_t>(f < 6 ? 1 : 2);
        TTDFrameInput in;
        in.position = {0, f, 0};
        if (f < 8 || f > 9)   // frames 8 and 9: the device gives no state
            in.deviceStates.push_back({id, dev.state, sizeof(dev.state)});
        ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
    }
    uint32_t region = 0;
    for (uint32_t i = 0; i < engine.Regions().size(); ++i)
        if (engine.IsDeviceStateRegion(i))
            region = i;
    ASSERT_TRUE(engine.DamageForTesting(6, region, 0));

    const TTDRestoreResult restore = engine.RestoreDevices(7, {});
    ASSERT_EQ(restore.issues.size(), 1u) << restore.message;
    EXPECT_EQ(restore.issues[0].kind, TTDRestoreIssueKind::DataDamaged);
    EXPECT_EQ(restore.issues[0].device.instance, "probe");
    EXPECT_EQ(restore.issues[0].action, TTDLiveStateAction::KeptLive);
    EXPECT_EQ(restore.issues[0].firstFrame, 6u);
    EXPECT_EQ(restore.issues[0].lastFrame, 7u);
    EXPECT_EQ(engine.RestoreDevices(3, {}).status, TTDRestoreStatus::Exact);

    const TTDRestoreResult check = engine.CheckSession();
    ASSERT_EQ(check.issues.size(), 2u) << check.message;
    EXPECT_EQ(check.issues[0].kind, TTDRestoreIssueKind::DataDamaged);
    EXPECT_EQ(check.issues[0].firstFrame, 6u);
    EXPECT_EQ(check.issues[0].lastFrame, 7u);
    EXPECT_EQ(check.issues[1].kind, TTDRestoreIssueKind::DeviceMissingState);
    EXPECT_EQ(check.issues[1].device.instance, "probe");
    EXPECT_EQ(check.issues[1].firstFrame, 8u);
    EXPECT_EQ(check.issues[1].lastFrame, 9u);
}

/// Phase 3, Step 4: the session's settings. The first entry is the session's;
/// a change adds an entry and a ConfigChange cut at its frame; equal settings
/// add nothing. A checkpoint is checked against the settings it was recorded
/// with: for a replay every difference is NotBitExact, for a restore only the
/// model / RAM size count (Degraded)
TEST(TimeTravelEngine_Config_Test, SettingsAreKeptPerFrame_AndCheckedAgainstTheLiveMachine)
{
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1)}, err));
    TTDConfigFingerprint a;
    a.Add("machine.model", 3, true);
    a.Add("sound.decimator_high_fidelity", 0);
    TTDConfigFingerprint b = a;
    b.fields[1].value = 1;

    for (uint64_t f = 10; f < 16; ++f)
    {
        ASSERT_TRUE(engine.CaptureFrame(Frame(f), err)) << err;
        ASSERT_TRUE(engine.SetConfiguration(f, f < 13 ? a : b));
    }
    ASSERT_EQ(engine.Configurations().size(), 2u) << "equal settings add no entry";
    EXPECT_EQ(engine.Configurations()[1].frame, 13u);
    size_t cuts = 0;
    for (const TTDEvent& ev : engine.Events().Events())
        if (ev.kind == TTDEventKind::ConfigChange)
        {
            ++cuts;
            EXPECT_EQ(ev.machineTime, 13u * 71680) << "at the frame's start";
            uint32_t entry = 0;
            std::memcpy(&entry, ev.args, sizeof(entry));
            EXPECT_EQ(entry, 1u);
        }
    EXPECT_EQ(cuts, 1u);
    EXPECT_TRUE(*engine.ConfigurationAt(2) == a) << "frame 12";
    EXPECT_TRUE(*engine.ConfigurationAt(3) == b) << "frame 13";

    // A live machine set like a: frames 10-12 match, frames 13-15 differ in one setting
    EXPECT_EQ(engine.CheckConfiguration(1, a, true).status, TTDRestoreStatus::Exact);
    const TTDRestoreResult replay = engine.CheckConfiguration(4, a, true);
    EXPECT_EQ(replay.status, TTDRestoreStatus::NotBitExact);
    ASSERT_EQ(replay.issues.size(), 1u);
    EXPECT_EQ(replay.issues[0].kind, TTDRestoreIssueKind::ConfigurationDiffers);
    EXPECT_NE(replay.issues[0].detail.find("sound.decimator_high_fidelity"), std::string::npos);
    EXPECT_EQ(engine.CheckConfiguration(4, a, false).status, TTDRestoreStatus::Exact)
        << "a restore does not depend on the decimator";

    TTDConfigFingerprint otherModel = a;
    otherModel.fields[0].value = 4;
    EXPECT_EQ(engine.CheckConfiguration(1, otherModel, false).status, TTDRestoreStatus::Degraded);
}

/// Phase 3, Step 4: media versions. A version is kept with the checkpoint of
/// the frame it was noted in, only when it changed; a checkpoint's version is
/// the latest one at or before it
TEST(TimeTravelEngine_Config_Test, MediaVersionsAreKeptWhenTheyChange)
{
    TimeTravelEngine engine;
    std::string err;
    ASSERT_TRUE(engine.BeginSession({Region(TTDRegionId::MachineRam, "ram", 1)}, err));
    const TTDMediaVersion v0{0xAB, 0}, v1{0xAB, 1};
    for (uint64_t f = 0; f < 6; ++f)
    {
        engine.NoteMediaVersion("sd.zc", "img", false, f < 3 ? v0 : v1);
        ASSERT_TRUE(engine.CaptureFrame(Frame(f), err)) << err;
    }
    ASSERT_EQ(engine.MediaSlots().size(), 1u);
    const TTDMediaSlot& slot = engine.MediaSlots()[0];
    EXPECT_EQ(slot.slot, "sd.zc");
    EXPECT_FALSE(slot.hasVersions);
    ASSERT_EQ(slot.changes.size(), 2u) << "one entry per change, not per frame";
    TTDMediaVersion at;
    ASSERT_TRUE(engine.MediaVersionAt(2, 0, at));
    EXPECT_TRUE(at == v0);
    ASSERT_TRUE(engine.MediaVersionAt(3, 0, at));
    EXPECT_TRUE(at == v1);
    ASSERT_TRUE(engine.MediaVersionAt(5, 0, at));
    EXPECT_TRUE(at == v1);
    EXPECT_FALSE(engine.MediaVersionAt(0, 1, at)) << "no such slot";
}

namespace
{
/// A device that may hold up to 1 MB of state and usually holds a few KB (a
/// network card's queues)
class LargeStateDevice : public TTDSerializable
{
public:
    size_t TTDStateSize() const override { return size_t(1) << 20; }
    void TTDSaveState(uint8_t*) const override {}
    void TTDLoadState(const uint8_t*) override {}
    std::string TTDDeviceName() const override { return "LargeState"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Covox; }
};
}  // namespace

/// A device's declared maximum is not the work per frame: only the pieces its
/// state reaches now or reached last time are compared (a 16 MB network card
/// once cost a millisecond a frame). Every checkpoint gives back exactly the
/// state of its frame - grown, shrunk, absent, and after a resume from the past
TEST(TimeTravelEngine_DeviceState_Test, WorkFollowsTheStateNotItsMaximum)
{
    TimeTravelEngine engine;
    std::string err;
    std::vector<uint8_t> ram(kTTDPieceSize, 0);
    TTDRegionDesc r;
    r.name = "ram";
    r.memory = ram.data();
    r.pieces = 1;
    r.bytes = kTTDPieceSize;
    LargeStateDevice dev;
    ASSERT_TRUE(engine.BeginSession({r}, {{dev.TTDDescribe(), &dev, nullptr}}, err)) << err;
    const uint8_t id = static_cast<uint8_t>(PeripheralId::Covox);

    const std::vector<size_t> sizes = {100, 100, 9000, 300, 0 /* absent */, 300, 5000, 5000, 64, 64};
    std::vector<std::vector<uint8_t>> expected;
    std::vector<uint64_t> offered;
    for (uint64_t f = 0; f < sizes.size(); ++f)
    {
        std::vector<uint8_t> state(sizes[f]);
        for (size_t i = 0; i < state.size(); ++i)
            state[i] = static_cast<uint8_t>(i * 7 + f);
        TTDFrameInput in;
        in.position = {0, f, 0};
        in.start = f * 70000;
        if (sizes[f])
            in.deviceStates.push_back({id, state.data(), state.size()});
        ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
        expected.push_back(state);
        offered.push_back(engine.LastCaptureWork().devicePiecesOffered);
    }
    const uint32_t regionPieces = ((1u << 20) + 4 + kTTDPieceSize - 1) / kTTDPieceSize;
    EXPECT_EQ(offered[0], regionPieces) << "the first capture lays the region out whole";
    for (size_t f = 1; f < offered.size(); ++f)
        EXPECT_LE(offered[f], 4u) << "frame " << f << ": the state's pieces, not the 1 MB maximum";
    for (size_t i = 0; i < expected.size(); ++i)
    {
        std::vector<uint8_t> got;
        if (expected[i].empty())
        {
            EXPECT_FALSE(engine.DeviceState(i, id, got)) << "checkpoint " << i << " has no state";
            continue;
        }
        ASSERT_TRUE(engine.DeviceState(i, id, got)) << "checkpoint " << i;
        EXPECT_TRUE(got == expected[i]) << "checkpoint " << i << " (" << expected[i].size() << " bytes)";
    }

    // Resumed at the 9000-byte frame, then a short state: the long one's tail is gone
    ASSERT_TRUE(engine.TruncateAfter(2, {0, 2, 0}, err)) << err;
    std::vector<uint8_t> shortState(200, 0xAB);
    TTDFrameInput in;
    in.position = {0, 3, 0};
    in.start = 3 * 70000;
    in.deviceStates.push_back({id, shortState.data(), shortState.size()});
    ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
    std::vector<uint8_t> got;
    ASSERT_TRUE(engine.DeviceState(3, id, got));
    EXPECT_TRUE(got == shortState);
    ASSERT_TRUE(engine.DeviceState(2, id, got));
    EXPECT_TRUE(got == expected[2]) << "the kept checkpoint still holds its long state";
}

namespace
{
/// A variable-size device declaring 64 MiB (a network adapter's tail of queued bytes) and holding a few KB
class GrowingStateDevice : public LargeStateDevice
{
public:
    size_t TTDStateSize() const override { return size_t(64) << 20; }
    bool TTDVariableSize() const override { return true; }
};
}  // namespace

/// A variable-size state without time fields costs memory by the size it reaches, not by its declared maximum: the
/// capture scratch and the delta base grow with it (a 64 MiB network declaration held 136 MB of a session's memory).
/// Every checkpoint still gives back its frame's state - grown, shrunk, absent, and after a resume from the past
TEST(TimeTravelEngine_DeviceState_Test, AVariableSizeStateHoldsMemoryByItsSize)
{
    TimeTravelEngine engine;
    std::string err;
    std::vector<uint8_t> ram(kTTDPieceSize, 0);
    TTDRegionDesc r;
    r.name = "ram";
    r.memory = ram.data();
    r.pieces = 1;
    r.bytes = kTTDPieceSize;
    GrowingStateDevice dev;
    TTDHistoryPolicy policy;
    policy.segmentFrames = 3;   // baselines store every piece the region holds whole
    engine.SetHistoryPolicy(policy);
    ASSERT_TRUE(engine.BeginSession({r}, {{dev.TTDDescribe(), &dev, nullptr}}, err)) << err;
    const uint8_t id = static_cast<uint8_t>(PeripheralId::Covox);

    const std::vector<size_t> sizes = {100, 9000, 300, 0 /* absent */, 300, 20000, 64};
    std::vector<std::vector<uint8_t>> expected;
    for (uint64_t f = 0; f < sizes.size(); ++f)
    {
        std::vector<uint8_t> state(sizes[f]);
        for (size_t i = 0; i < state.size(); ++i)
            state[i] = static_cast<uint8_t>(i * 7 + f);
        TTDFrameInput in;
        in.position = {0, f, 0};
        in.start = f * 70000;
        if (sizes[f])
            in.deviceStates.push_back({id, state.data(), state.size()});
        ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
        EXPECT_LE(engine.LastCaptureWork().devicePiecesOffered, 6u) << "frame " << f << ": the state's pieces only";
        expected.push_back(state);
    }
    const TTDEngineHeapBreakdown heap = engine.HeapBreakdown();
    EXPECT_LT(heap.deltaBase, size_t(64) << 10) << "the delta base covers the 20000 bytes reached, not 64 MiB";
    EXPECT_LT(heap.bookkeeping, size_t(1) << 20) << "the capture scratch too";
    for (size_t i = 0; i < expected.size(); ++i)
    {
        std::vector<uint8_t> got;
        if (expected[i].empty())
        {
            EXPECT_FALSE(engine.DeviceState(i, id, got)) << "checkpoint " << i << " has no state";
            continue;
        }
        ASSERT_TRUE(engine.DeviceState(i, id, got)) << "checkpoint " << i;
        EXPECT_TRUE(got == expected[i]) << "checkpoint " << i << " (" << expected[i].size() << " bytes)";
    }

    // Resumed at the 9000-byte frame, then a state longer than any before it, then a short one
    ASSERT_TRUE(engine.TruncateAfter(1, {0, 1, 0}, err)) << err;
    std::vector<uint8_t> longState(40000);
    for (size_t i = 0; i < longState.size(); ++i)
        longState[i] = static_cast<uint8_t>(i * 13 + 5);
    const std::vector<uint8_t> shortState(200, 0xAB);
    for (uint64_t f = 2; f <= 3; ++f)
    {
        const std::vector<uint8_t>& state = f == 2 ? longState : shortState;
        TTDFrameInput in;
        in.position = {0, f, 0};
        in.start = f * 70000;
        in.deviceStates.push_back({id, state.data(), state.size()});
        ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
    }
    std::vector<uint8_t> got;
    ASSERT_TRUE(engine.DeviceState(1, id, got));
    EXPECT_TRUE(got == expected[1]) << "the kept checkpoint still holds its state";
    ASSERT_TRUE(engine.DeviceState(2, id, got));
    EXPECT_TRUE(got == longState) << "the grown state";
    ASSERT_TRUE(engine.DeviceState(3, id, got));
    EXPECT_TRUE(got == shortState) << "the long one's tail is gone";
}

/// A session loaded from a file has no delta base yet: continuing it from its last checkpoint gives the base every
/// piece the growing state holds there, so recapturing the same state stores nothing and the state comes back whole
TEST(TimeTravelEngine_DeviceState_Test, ALoadedSessionContinuesAVariableSizeState)
{
    std::vector<uint8_t> ram(kTTDPieceSize, 0);
    TTDRegionDesc r;
    r.name = "ram";
    r.memory = ram.data();
    r.pieces = 1;
    r.bytes = kTTDPieceSize;
    GrowingStateDevice dev;
    const uint8_t id = static_cast<uint8_t>(PeripheralId::Covox);
    std::vector<uint8_t> state(9000);
    for (size_t i = 0; i < state.size(); ++i)
        state[i] = static_cast<uint8_t>(i * 11 + 3);
    auto capture = [&](TimeTravelEngine& engine, uint64_t f) {
        TTDFrameInput in;
        in.position = {0, f, 0};
        in.start = f * 70000;
        in.deviceStates.push_back({id, state.data(), state.size()});
        std::string err;
        EXPECT_TRUE(engine.CaptureFrame(in, err)) << err;
    };

    TimeTravelEngine original;
    std::string err;
    ASSERT_TRUE(original.BeginSession({r}, {{dev.TTDDescribe(), &dev, nullptr}}, err)) << err;
    capture(original, 0);
    capture(original, 1);
    TTDMemorySink sink;
    TTDSessionSaveParams params;
    ASSERT_TRUE(TTDSessionFile::Save(original, sink, err, params)) << err;

    TimeTravelEngine loaded;
    TTDMemorySource source(sink.bytes);
    ASSERT_TRUE(TTDSessionFile::Load(loaded, source, err)) << err;
    ASSERT_TRUE(loaded.TruncateAfter(1, {0, 1, 0}, err)) << err;
    capture(loaded, 2);
    EXPECT_EQ(loaded.LastCaptureWork().versionsStored, 0u) << "the base holds all three pieces of the state";
    std::vector<uint8_t> got;
    ASSERT_TRUE(loaded.DeviceState(2, id, got));
    EXPECT_TRUE(got == state);
}

/// The delta base keeps a copy only of pieces that are not one repeated byte: a uniform piece (zeros, an erased
/// flash's #FF) is its value, read through a shared page; a piece changes kind both ways; never set reads as zeros
TEST(TTDDeltaBase_Test, UniformPiecesKeepOnlyTheirValue)
{
    TTDDeltaBase base;
    base.Reset(2);
    std::vector<uint8_t> mixed(kTTDPieceSize);
    for (size_t i = 0; i < mixed.size(); ++i)
        mixed[i] = static_cast<uint8_t>(i * 13 + 1);
    const std::vector<uint8_t> ff(kTTDPieceSize, 0xFF);
    const std::vector<uint8_t> zero(kTTDPieceSize, 0);

    EXPECT_TRUE(base.Equals(1, 300, zero.data())) << "never set: zeros";
    EXPECT_EQ(base.Set(0, 5, ff.data()), 0u);
    EXPECT_EQ(base.Set(1, 7, mixed.data()), kTTDPieceSize);
    EXPECT_EQ(base.StoredPieces(), 1u);
    EXPECT_TRUE(base.Equals(0, 5, ff.data()));
    EXPECT_TRUE(base.Equals(1, 7, mixed.data()));
    EXPECT_EQ(base.Set(1, 7, base.Get(1, 7)), kTTDPieceSize) << "its own bytes";
    EXPECT_TRUE(base.Equals(1, 7, mixed.data()));

    // mixed -> uniform -> mixed
    std::vector<uint8_t> almost(ff);
    almost[kTTDPieceSize - 1] = 0xFE;
    EXPECT_EQ(base.Set(1, 7, ff.data()), 0u);
    EXPECT_EQ(base.StoredPieces(), 0u);
    EXPECT_TRUE(base.Equals(1, 7, ff.data()));
    EXPECT_EQ(base.Set(1, 7, almost.data()), kTTDPieceSize) << "one byte off is not uniform";
    EXPECT_TRUE(base.Equals(1, 7, almost.data()));
    EXPECT_LT(base.HeapBytes(), size_t(4) * kTTDPieceSize);
}

/// A large region of mostly uniform memory (zeros and #FF, like NeoGS's RAM and flash) costs the delta base its
/// few pieces of real content, not its size; every checkpoint restores exactly, across baselines and after a resume
/// from the past (which rebuilds the base from the stored versions)
TEST(TimeTravelEngine_Test, AMostlyUniformRegionCostsTheDeltaBaseItsContentOnly)
{
    constexpr uint32_t kPieces = 512;   // 2 MB
    std::vector<uint8_t> mem(size_t(kPieces) * kTTDPieceSize, 0);
    auto fill = [&](uint32_t p, uint32_t seed) {
        for (size_t i = 0; i < kTTDPieceSize; ++i)
            mem[size_t(p) * kTTDPieceSize + i] = static_cast<uint8_t>((i * 31 + seed * 7) ^ (i >> 5));
    };
    for (uint32_t p = 0; p < 4; ++p)
        fill(p, p);
    std::memset(mem.data() + size_t(100) * kTTDPieceSize, 0xFF, size_t(100) * kTTDPieceSize);

    TimeTravelEngine engine;
    std::string err;
    TTDRegionDesc r;
    r.name = "big";
    r.memory = mem.data();
    r.pieces = kPieces;
    r.bytes = mem.size();
    TTDHistoryPolicy policy;
    policy.segmentFrames = 4;   // baselines store every piece whole, from the delta base
    engine.SetHistoryPolicy(policy);
    ASSERT_TRUE(engine.BeginSession({r}, {}, err)) << err;

    std::vector<std::vector<uint8_t>> expected;
    auto capture = [&](uint64_t f, const std::vector<uint32_t>& changed) {
        TTDFrameInput in;
        in.position = {0, f, 0};
        in.start = f * 70000;
        for (uint32_t p : changed)
            in.changed.push_back({0, p, mem.data() + size_t(p) * kTTDPieceSize});
        ASSERT_TRUE(engine.CaptureFrame(in, err)) << err;
        expected.push_back(mem);
    };
    std::vector<uint32_t> all(kPieces);
    for (uint32_t p = 0; p < kPieces; ++p)
        all[p] = p;
    capture(0, all);
    for (uint64_t f = 1; f < 10; ++f)
    {
        std::vector<uint32_t> changed = {static_cast<uint32_t>(10 + f)};
        fill(10 + static_cast<uint32_t>(f), static_cast<uint32_t>(f));
        if (f == 4)
        {
            std::memset(mem.data() + size_t(2) * kTTDPieceSize, 0, kTTDPieceSize);   // content -> zeros
            changed.push_back(2);
        }
        if (f == 5 || f == 7)
        {
            if (f == 5)
                fill(150, 99);   // #FF -> content -> #FF
            else
                std::memset(mem.data() + size_t(150) * kTTDPieceSize, 0xFF, kTTDPieceSize);
            changed.push_back(150);
        }
        capture(f, changed);
    }
    const TTDEngineHeapBreakdown heap = engine.HeapBreakdown();
    EXPECT_LT(heap.deltaBase, size_t(128) << 10) << "about 13 pieces of content, not 2 MB";

    auto expectRestores = [&](const char* when) {
        std::vector<uint8_t> out(mem.size());
        for (size_t i = 0; i < expected.size(); ++i)
        {
            ASSERT_TRUE(engine.RestoreRegion(i, 0, out.data()).Ok()) << when << " checkpoint " << i;
            ASSERT_TRUE(out == expected[i]) << when << " checkpoint " << i;
        }
    };
    expectRestores("recorded");

    // Resume from frame 5: the base is rebuilt from that checkpoint's versions, then capture goes on
    ASSERT_TRUE(engine.TruncateAfter(5, {0, 5, 0}, err)) << err;
    expected.resize(6);
    mem = expected[5];
    for (uint64_t f = 6; f < 12; ++f)
    {
        std::vector<uint32_t> changed = {150, 3};
        if (f % 2)
            std::memset(mem.data() + size_t(150) * kTTDPieceSize, 0xFF, kTTDPieceSize);
        else
            fill(150, static_cast<uint32_t>(f));
        fill(3, static_cast<uint32_t>(f + 40));
        capture(f, changed);
    }
    expectRestores("after the resume");
    EXPECT_LT(engine.HeapBreakdown().deltaBase, size_t(128) << 10);
}
