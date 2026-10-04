// Ym2203Pair (core/src/emulator/sound/chips/tsfm/ym2203pair.h): the YM2203 pair shared by the TSFM board and the
// ZX-MultiSound card (docs/inprogress/2026-10-03-zx-multisound/architecture.md §2, §4.1, MS-1). The TSFM's own
// behavior through the pair is pinned by the TSFM suite and TsfmGolden_Test; these tests cover what the pair adds:
// the master-clock ratio, the per-chip / per-channel outputs, the SSG I/O port listener and the pair's TTD blob.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/ayioport.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/chips/tsfm/ym2203pair.h"

namespace
{

/// The MultiSound's own oscillator on a 128K-family host
constexpr uint32_t kMasterHz = 3'500'000;
constexpr uint32_t kHost128Hz = 3'546'900;

Ym2203PairConfig RatioConfig()
{
    Ym2203PairConfig config;
    config.masterClockHz = kMasterHz;
    config.hostTickRate = kHost128Hz;
    return config;
}

struct PinEvent
{
    uint64_t t;
    int port;
    uint8_t pins;

    bool operator==(const PinEvent& o) const { return t == o.t && port == o.port && pins == o.pins; }
};

void PrintTo(const PinEvent& e, std::ostream* os)
{
    *os << "{t=" << e.t << " port=" << e.port << " pins=#" << std::hex << int(e.pins) << std::dec << "}";
}

class RecordingListener : public IAyIoPortListener
{
public:
    void OnIoPortPins(uint64_t t, int port, uint8_t pins) override { events.push_back({t, port, pins}); }
    std::vector<PinEvent> events;
};

void Reg(Ym2203Pair& pair, int chip, uint8_t reg, uint8_t value)
{
    pair.writeAddress(chip, reg);
    pair.writeData(chip, value);
}

/// One FM carrier at TL 0 on channel 2 (the TSFM output tests' reference note: word +-8168, 759.5 Hz at /6)
void ProgramFmNote(Ym2203Pair& pair, int chip)
{
    const uint8_t setup[][2] = {
        {0x42, 0x7F}, {0x46, 0x7F}, {0x4A, 0x7F}, {0x4E, 0x00},  // modulators silent, carrier TL 0
        {0x52, 0x1F}, {0x56, 0x1F}, {0x5A, 0x1F}, {0x5E, 0x1F},  // AR: fastest
        {0x3E, 0x01},                                            // carrier MUL 1
        {0xA6, 0x39}, {0xA2, 0x00},                              // fnum 0x100, block 7
        {0x28, 0xF2},                                            // key on, channel 2
    };
    for (const auto& [reg, data] : setup)
        Reg(pair, chip, reg, data);
}

size_t DrainWords(Ym2203Chip& c)
{
    const size_t n = c.words.size();
    c.words.clear();
    return n;
}

float Peak(const std::vector<float>& v)
{
    float peak = 0.0f;
    for (float x : v)
        peak = std::max(peak, std::fabs(x));
    return peak;
}

}  // namespace

class Ym2203Pair_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
    }

    void TearDown() override
    {
        delete _context;
        _context = nullptr;
    }

    EmulatorContext* _context = nullptr;
};

TEST_F(Ym2203Pair_Test, UnityRatioIsTheHostAxis)
{
    // masterClockHz == hostTickRate (the TSFM): one master clock per host tick, the master-clock axis is the
    // frame-relative host axis and is rebased with it
    Ym2203Pair pair(_context);
    ASSERT_TRUE(pair.unityRatio());
    pair.syncTo(1000);  // adopt
    EXPECT_EQ(pair.chipT(), 1000);
    pair.syncTo(71680 + 33);
    EXPECT_EQ(pair.chipT(), 71680 + 33);
    EXPECT_EQ(pair.ratioPhase(), 0u);

    const int64_t cursor = pair.renderT();
    pair.rebaseFrame(33);  // the host subtracted one frame
    EXPECT_EQ(pair.syncedT(), 33u);
    EXPECT_EQ(pair.chipT(), 33);
    EXPECT_EQ(pair.renderT(), cursor - 71680);
}

TEST_F(Ym2203Pair_Test, RatioAccumulatorAt3500kHzOn128kHost)
{
    // 3.5 MHz master on a 3.5469 MHz host: chips advance masterClockHz / hostTickRate clocks per host tick, the
    // remainder carried in the phase. One host second is exactly 3 500 000 master clocks, phase 0
    Ym2203Pair pair(_context, RatioConfig());
    ASSERT_FALSE(pair.unityRatio());
    pair.syncTo(0);  // adopt

    // One 128K frame (70908 T): 70908 x 3.5e6 / 3546900 = 69973.7... clocks
    pair.syncTo(70908);
    const uint64_t product = 70908ull * kMasterHz;
    EXPECT_EQ(pair.chipT(), int64_t(product / kHost128Hz));
    EXPECT_EQ(pair.ratioPhase(), product % kHost128Hz);

    // In odd steps up to one host second: no clock lost or gained to rounding
    uint64_t t = 70908;
    while (t < kHost128Hz)
    {
        t = std::min<uint64_t>(t + 12345, kHost128Hz);
        pair.syncTo(t);
    }
    EXPECT_EQ(pair.chipT(), int64_t(kMasterHz));
    EXPECT_EQ(pair.ratioPhase(), 0u);

    // A host frame rebase leaves the continuous master-clock axis alone
    pair.rebaseFrame(t - 70908);
    EXPECT_EQ(pair.chipT(), int64_t(kMasterHz));
}

TEST_F(Ym2203Pair_Test, RatioLowersFmPitchAgainstTheHost)
{
    // The same FM note over the same host time: the pair on its own 3.5 MHz clock produces 3.5 / 3.5469 of the
    // words a 1 : 1 pair produces (the 1.3 % lower pitch of a MultiSound against a TSFM on a 128K host)
    Ym2203Pair ratio(_context, RatioConfig());
    Ym2203PairConfig unityConfig;
    unityConfig.masterClockHz = unityConfig.hostTickRate = kHost128Hz;
    Ym2203Pair unity(_context, unityConfig);

    size_t words[2] = {0, 0};
    Ym2203Pair* pairs[2] = {&ratio, &unity};
    for (int p = 0; p < 2; p++)
    {
        Ym2203Pair& pair = *pairs[p];
        pair.syncTo(0);
        ProgramFmNote(pair, 0);
        // A quarter host second in 128K frames; the queue holds 4096 words, so drain per frame
        for (uint64_t t = 70908; t <= kHost128Hz / 4; t += 70908)
        {
            pair.syncTo(t);
            words[p] += DrainWords(*pair.chip(0));
        }
    }

    // Words at /6: one per 72 master clocks
    const uint64_t hostTicks = (kHost128Hz / 4 / 70908) * 70908;
    EXPECT_EQ(words[0], size_t(hostTicks * kMasterHz / kHost128Hz / 72));
    EXPECT_EQ(words[1], size_t(hostTicks / 72));
    const double measured = double(words[0]) / double(words[1]);
    EXPECT_NEAR(measured, double(kMasterHz) / double(kHost128Hz), 1e-3);
}

TEST_F(Ym2203Pair_Test, PerChipAndChannelOutputs)
{
    // Chip 0 plays an FM note, chip 1 an SSG tone on channel B only: each stream carries its own source and
    // nothing else; the board's FM mute silences the FM streams only
    Ym2203Pair pair(_context, RatioConfig());
    pair.configureChannelOutputs(44100);
    pair.syncTo(0);

    ProgramFmNote(pair, 0);
    Reg(pair, 1, 2, 0x40);  // tone B period 0x040
    Reg(pair, 1, 3, 0x00);
    Reg(pair, 1, 7, 0xFD);  // tone B on, everything else off
    Reg(pair, 1, 9, 0x0F);  // volume B 15

    constexpr size_t kBlock = 100;
    std::vector<float> fm[2], ssg[2][3];
    auto run = [&](bool fmEnabled, int blocks)
    {
        for (auto& v : fm)
            v.assign(kBlock * blocks, 0.0f);
        for (auto& chip : ssg)
            for (auto& v : chip)
                v.assign(kBlock * blocks, 0.0f);
        for (int b = 0; b < blocks; b++)
        {
            // 100 output samples of host time (3546900 / 44100 x 100 = 8042.9 ticks)
            pair.syncTo(pair.syncedT() + 8043);
            Ym2203ChannelBlock block;
            for (int c = 0; c < 2; c++)
            {
                block.fm[c] = fm[c].data() + b * kBlock;
                for (int ch = 0; ch < 3; ch++)
                    block.ssg[c][ch] = ssg[c][ch].data() + b * kBlock;
            }
            ASSERT_EQ(pair.renderChannels(kBlock, block, fmEnabled), kBlock);
        }
    };

    run(true, 12);
    EXPECT_GT(Peak(fm[0]), 0.2f) << "chip 0 FM (one carrier at TL 0: word 8168 / 32768 = 0.249)";
    EXPECT_LT(Peak(fm[0]), 0.3f);
    EXPECT_EQ(Peak(fm[1]), 0.0f) << "chip 1 has no FM note";
    EXPECT_GT(Peak(ssg[1][1]), 0.5f) << "chip 1 channel B at volume 15";
    EXPECT_LT(Peak(ssg[1][0]), 1e-6f);
    EXPECT_LT(Peak(ssg[1][2]), 1e-6f);
    for (int ch = 0; ch < 3; ch++)
        EXPECT_LT(Peak(ssg[0][ch]), 1e-6f) << "chip 0 SSG channel " << ch;

    // FM muted by the board: the FM streams fall silent (after the decimator's few samples of history), the SSG
    // stream does not
    run(false, 4);
    const std::vector<float> fmTail(fm[0].begin() + kBlock, fm[0].end());
    EXPECT_LT(Peak(fmTail), 1e-6f);
    EXPECT_GT(Peak(ssg[1][1]), 0.5f);
}

TEST_F(Ym2203Pair_Test, PerChannelOutputsRenderAWholeSyncedFrameAtOnce)
{
    // A board that syncs a whole frame and then renders it (the MultiSound's FrameEnd) leaves the cursor a frame
    // behind the chips with that frame's words queued: the render must take every word, not re-anchor to the end.
    // A steady carrier then has the same swing in every frame, on both host rates
    for (const uint32_t host : {kMasterHz, kHost128Hz})
    {
        Ym2203PairConfig config = RatioConfig();
        config.hostTickRate = host;
        Ym2203Pair pair(_context, config);
        pair.configureChannelOutputs(44100);
        pair.syncTo(0);
        ProgramFmNote(pair, 0);

        const uint64_t frameTicks = host == kMasterHz ? 71680 : 70908;
        std::vector<float> fm(2000);
        uint64_t samplesAcc = 0;
        for (int frame = 0; frame < 8; frame++)
        {
            pair.syncTo(pair.syncedT() + frameTicks);
            samplesAcc += frameTicks * 44100;
            const size_t frames = static_cast<size_t>(samplesAcc / host);
            samplesAcc %= host;
            Ym2203ChannelBlock block;
            block.fm[0] = fm.data();
            ASSERT_EQ(pair.renderChannels(frames, block, true), frames);
            if (frame < 2)
                continue;  // attack and decimator warm-up
            const auto [lo, hi] = std::minmax_element(fm.begin(), fm.begin() + static_cast<std::ptrdiff_t>(frames));
            // One carrier at TL 0: the DAC word swings +-8168 (the TSFM output tests' reference note)
            EXPECT_NEAR(*hi - *lo, 2.0f * 8168.0f / 32768.0f, 0.005f) << "host " << host << " frame " << frame;
        }
    }
}

TEST_F(Ym2203Pair_Test, SsgIoPortListenerOnChip1)
{
    // The MultiSound's MIDI line hangs on YM chip 1 IOA2: the pair's SSG reports pin changes of chip 1 with the
    // host tick of the write; chip 0 has no listener. Reads go through the bus semantics (an input port reads its
    // pins, an output port its latch)
    Ym2203Pair pair(_context, RatioConfig());
    RecordingListener listener;
    pair.setIoPortListener(1, &listener);
    pair.syncTo(0);

    // Reset clears every register: both ports are inputs, the pins read #FF through the pull-ups
    pair.syncTo(100);
    Reg(pair, 1, 14, 0x04);  // latch only: an input port's pins do not follow it
    pair.writeAddress(1, 14);
    EXPECT_EQ(pair.readData(1), 0xFF) << "an input port reads its pins";

    pair.syncTo(150);
    Reg(pair, 1, 7, 0x40);   // port A to output: IOA2 goes high with the latch
    pair.writeAddress(1, 14);
    EXPECT_EQ(pair.readData(1), 0x04) << "an output port reads its latch";

    pair.syncTo(250);
    Reg(pair, 0, 7, 0x40);   // chip 0: not listened to
    Reg(pair, 0, 14, 0x55);
    Reg(pair, 1, 14, 0x04);  // no change on the pins: not reported

    pair.syncTo(400);
    Reg(pair, 1, 14, 0x00);  // IOA2 low (a MIDI start bit)

    pair.syncTo(500);
    Reg(pair, 1, 7, 0x00);   // port A back to input: pulled up
    pair.writeAddress(1, 14);
    EXPECT_EQ(pair.readData(1), 0xFF);
    EXPECT_EQ(pair.chip(1)->ssg.readRegister(14), 0x00) << "the latch is kept";

    const std::vector<PinEvent> expected = {
        {150, AyIoPort::PortA, 0x04}, {400, AyIoPort::PortA, 0x00}, {500, AyIoPort::PortA, 0xFF}};
    EXPECT_EQ(listener.events, expected);

    // Detached: silent
    pair.setIoPortListener(1, nullptr);
    Reg(pair, 1, 7, 0x40);
    EXPECT_EQ(listener.events.size(), expected.size());
}

TEST_F(Ym2203Pair_Test, TtdRoundTripKeepsRatioPhase)
{
    // The ratio phase is pair state: a pair restored mid-tune at a non-zero phase advances its chips over exactly
    // the clocks the original does - same words, same blob afterwards
    Ym2203Pair a(_context, RatioConfig());
    a.syncTo(0);
    ProgramFmNote(a, 0);
    Reg(a, 1, 0x24, 0x80);   // chip 1 timer A running: timer state in the blob
    Reg(a, 1, 0x27, 0x05);
    Reg(a, 1, 8, 0x0A);      // a pending SSG write in the timeline
    a.syncTo(12345);
    ASSERT_NE(a.ratioPhase(), 0u);

    std::vector<uint8_t> blob(a.TTDStateSize());
    a.TTDSaveState(blob.data());
    DrainWords(*a.chip(0));

    Ym2203Pair b(_context, RatioConfig());
    b.TTDLoadState(blob.data(), a.syncedT());
    EXPECT_EQ(b.ratioPhase(), a.ratioPhase());
    EXPECT_EQ(b.chip(1)->ssgWrites.size(), a.chip(1)->ssgWrites.size());

    for (uint64_t t = 12345 + 777; t < 12345 + 30000; t += 777)
    {
        a.syncTo(t);
        b.syncTo(t);
        ASSERT_EQ(a.ratioPhase(), b.ratioPhase()) << "t " << t;
        ASSERT_EQ(a.chip(0)->words.size(), b.chip(0)->words.size()) << "t " << t;
        for (size_t i = 0; i < a.chip(0)->words.size(); i++)
            ASSERT_EQ(a.chip(0)->words.at(i).word, b.chip(0)->words.at(i).word) << "t " << t << " word " << i;
        DrainWords(*a.chip(0));
        DrainWords(*b.chip(0));
        EXPECT_EQ(a.readStatus(1), b.readStatus(1)) << "t " << t;
    }

    std::vector<uint8_t> blobA(a.TTDStateSize()), blobB(b.TTDStateSize());
    a.TTDSaveState(blobA.data());
    b.TTDSaveState(blobB.data());
    EXPECT_EQ(blobA, blobB);
}

/// region <Time-travel engine descriptor>

namespace
{
uint32_t ReadU32(const std::vector<uint8_t>& blob, size_t offset)
{
    uint32_t v = 0;
    std::memcpy(&v, blob.data() + offset, 4);
    return v;
}

/// The pair's own blob as a device of the engine's table (how a board carrying the pair whole registers it, MS-5)
struct PairDevice : ttd::TTDSerializable
{
    Ym2203Pair& pair;
    explicit PairDevice(Ym2203Pair& p) : pair(p) {}
    size_t TTDStateSize() const override { return pair.TTDStateSize(); }
    void TTDSaveState(uint8_t* dst) const override { pair.TTDSaveState(dst); }
    void TTDLoadState(const uint8_t* src) override { pair.TTDLoadState(src, pair.syncedT()); }
    std::string TTDDeviceName() const override { return "Ym2203Pair"; }
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::TSFM; }
    ttd::TTDDeviceDescriptor TTDDescribe() const override
    {
        ttd::TTDDeviceDescriptor d = ttd::TTDSerializable::TTDDescribe();
        d.runsBehindCpu = true;
        Ym2203Pair::TTDTimeFields(d.timeFields, Ym2203Pair::kStateChipsOffset);
        return d;
    }
    bool TTDSyncedTime(int64_t& offset) const override { return pair.TTDSyncedTime(pair.syncedT(), offset); }
};
}  // namespace

TEST_F(Ym2203Pair_Test, TsfmDescriptorTimeFieldsAreMastersThroughThePair)
{
    // The TSFM's engine descriptor keeps master's offsets (50-byte header, 586 bytes per chip, ymfm payload at +19,
    // envelope counter +5 u32, clock count +15 u8) now that the pair supplies them
    SoundChip_TurboSoundFM tsfm(_context);
    const ttd::TTDDeviceDescriptor d = tsfm.TTDDescribe();
    ASSERT_EQ(d.timeFields.size(), 4u);
    const uint16_t offsets[] = {74, 84, 660, 670};
    const uint8_t widths[] = {4, 1, 4, 1};
    for (size_t i = 0; i < 4; i++)
    {
        EXPECT_EQ(d.timeFields[i].offset, offsets[i]) << i;
        EXPECT_EQ(d.timeFields[i].width, widths[i]) << i;
    }
    EXPECT_TRUE(d.runsBehindCpu);
    EXPECT_EQ(d.stateSize, 2008u);

    ttd::TTDPeripheralRegistry registry;
    registry.Register(ttd::PeripheralId::TSFM, &tsfm);
    std::string error;
    EXPECT_TRUE(registry.CheckDeviceTable(error)) << error;
}

TEST_F(Ym2203Pair_Test, TsfmBlobCarriesThePairChipPayloadsAfterItsHeader)
{
    // The chips in the TSFM blob are the pair's chip payloads byte for byte, at the header offset the descriptor
    // names; in the pair's own blob they sit at kStateChipsOffset. The ymfm payload size (u16) precedes the payload
    SoundChip_TurboSoundFM tsfm(_context);
    Ym2203Pair& pair = tsfm.pair();
    pair.syncTo(0);
    ProgramFmNote(pair, 0);
    ProgramFmNote(pair, 1);
    pair.syncTo(20000);

    std::vector<uint8_t> tsfmBlob(tsfm.TTDStateSize()), pairBlob(pair.TTDStateSize());
    tsfm.TTDSaveState(tsfmBlob.data());
    pair.TTDSaveState(pairBlob.data());
    const size_t chips = 2 * Ym2203Pair::kChipStateSize;
    EXPECT_TRUE(std::equal(tsfmBlob.begin() + SoundChip_TurboSoundFM::kTsfmStateHeaderSize,
                           tsfmBlob.begin() + SoundChip_TurboSoundFM::kTsfmStateHeaderSize + chips,
                           pairBlob.begin() + Ym2203Pair::kStateChipsOffset));
    for (int chip = 0; chip < 2; chip++)
    {
        const size_t size = Ym2203Pair::kStateChipsOffset + chip * Ym2203Pair::kChipStateSize +
                            Ym2203Pair::kChipYmfmOffset - 2;
        EXPECT_EQ(pairBlob[size] | (pairBlob[size + 1] << 8), int(Ym2203Pair::kYmfmStateSize)) << chip;
    }
}

TEST_F(Ym2203Pair_Test, PairBlobMatchesItsDescriptorAndItsTimeFieldsAdvance)
{
    // The pair's own blob (the MultiSound's, ratio phase included) satisfies the engine's descriptor contract, and
    // the time fields are the counters that run with the FM clock: the envelope counters move on with the chips
    Ym2203Pair pair(_context, RatioConfig());
    PairDevice device(pair);
    ttd::TTDPeripheralRegistry registry;
    registry.Register(ttd::PeripheralId::TSFM, &device);
    std::string error;
    EXPECT_TRUE(registry.CheckDeviceTable(error)) << error;

    const ttd::TTDDeviceDescriptor d = device.TTDDescribe();
    ASSERT_EQ(d.timeFields.size(), 4u);
    EXPECT_EQ(d.stateSize, Ym2203Pair::kStateSize);

    pair.syncTo(0);
    ProgramFmNote(pair, 0);
    std::vector<uint8_t> before(pair.TTDStateSize()), after(pair.TTDStateSize());
    pair.TTDSaveState(before.data());
    pair.syncTo(30000);
    pair.TTDSaveState(after.data());
    for (int chip = 0; chip < 2; chip++)
    {
        const size_t env = d.timeFields[size_t(chip) * 2].offset;
        EXPECT_GT(ReadU32(after, env), ReadU32(before, env)) << "chip " << chip << ": envelope counter";
    }
}

TEST_F(Ym2203Pair_Test, SyncedTimeAdoptsAfterResetAndFollowsTheHost)
{
    Ym2203Pair pair(_context);
    int64_t offset = -1;
    EXPECT_TRUE(pair.TTDSyncedTime(5000, offset)) << "after reset the next sync adopts the host's position";
    pair.syncTo(1000);
    EXPECT_TRUE(pair.TTDSyncedTime(1000, offset));
    EXPECT_EQ(offset, 1000);
    EXPECT_FALSE(pair.TTDSyncedTime(1001, offset)) << "behind the host";
    pair.reset();
    EXPECT_TRUE(pair.TTDSyncedTime(1001, offset));
}

/// endregion </Time-travel engine descriptor>
