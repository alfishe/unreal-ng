// Ym2203Pair (core/src/emulator/sound/chips/tsfm/ym2203pair.h): the YM2203 pair shared by the TSFM board and the
// ZX-MultiSound card (docs/inprogress/2026-10-03-zx-multisound/architecture.md §2, §4.1, MS-1). The TSFM's own
// behavior through the pair is pinned by the TSFM suite and TsfmGolden_Test; these tests cover what the pair adds:
// the master-clock ratio, the per-chip / per-channel outputs, the SSG I/O port listener and the pair's TTD blob.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <string>
#include <tuple>

#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/memory/memory.h"
#include "debugger/analyzers/audiocapture/audiocaptureanalyzer.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/sound/soundmanager.h"
#ifdef UNREALNG_HAVE_SAM2695
#include "emulator/slots/cards/multisound/multisoundcard.h"
#include "emulator/slots/cards/multisound/multisoundslotcard.h"
#include "emulator/slots/cards/multisound/multisoundstagedmachine.h"
#endif
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
            pair.beginChannelRender(kBlock);
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
            pair.beginChannelRender(frames);
            ASSERT_EQ(pair.renderChannels(frames, block, true), frames);
            if (frame < 2)
                continue;  // attack and decimator warm-up
            const auto [lo, hi] = std::minmax_element(fm.begin(), fm.begin() + static_cast<std::ptrdiff_t>(frames));
            // One carrier at TL 0: the DAC word swings +-8168 (the TSFM output tests' reference note)
            EXPECT_NEAR(*hi - *lo, 2.0f * 8168.0f / 32768.0f, 0.005f) << "host " << host << " frame " << frame;
        }
    }
}

TEST_F(Ym2203Pair_Test, PerChannelOutputsFollowTheChipsAfterAResetOnAContinuousAxis)
{
    // The MultiSound's host axis is continuous (never rebased). A reset there - a snapshot load, a reset of a running
    // machine - leaves the cursor far from the chips, and the re-anchor must put it where the next block ENDS the
    // render lag behind them: anchored at the chips instead, every block rendered a block ahead of them, consumed a
    // whole frame of words at its first half-tick and held the last one (MS-7 owner report 2026-10-05: clicks for
    // FM). The FM stream then carries the note's frequency (zero crossings, not a level), on both host rates
    for (const uint32_t host : {kMasterHz, kHost128Hz})
    {
        Ym2203PairConfig config = RatioConfig();
        config.hostTickRate = host;
        Ym2203Pair pair(_context, config);
        pair.configureChannelOutputs(44100);
        pair.syncTo(0);

        const uint64_t frameTicks = host == kMasterHz ? 71680 : 70908;
        std::vector<float> frameOut(2000);
        std::vector<float> fm;
        uint64_t samplesAcc = 0;
        auto frame = [&](bool keep)
        {
            pair.syncTo(pair.syncedT() + frameTicks);
            samplesAcc += frameTicks * 44100;
            const size_t frames = static_cast<size_t>(samplesAcc / host);
            samplesAcc %= host;
            Ym2203ChannelBlock block;
            block.fm[0] = frameOut.data();
            pair.beginChannelRender(frames);
            ASSERT_EQ(pair.renderChannels(frames, block, true), frames);
            if (keep)
                fm.insert(fm.end(), frameOut.begin(), frameOut.begin() + static_cast<std::ptrdiff_t>(frames));
        };

        // Ten frames of a running machine, then a reset mid-frame
        for (int i = 0; i < 10; i++)
            frame(false);
        pair.syncTo(pair.syncedT() + 12345);
        pair.reset();
        pair.syncTo(pair.syncedT());
        ProgramFmNote(pair, 0);
        for (int i = 0; i < 2; i++)
            frame(false);  // attack and decimator warm-up
        for (int i = 0; i < 8; i++)
            frame(true);

        size_t crossings = 0;
        for (size_t i = 1; i < fm.size(); i++)
            crossings += (fm[i - 1] < 0.0f) != (fm[i] < 0.0f) ? 1 : 0;
        const double hz = static_cast<double>(crossings) / 2.0 / (static_cast<double>(fm.size()) / 44100.0);
        const double note = 256.0 * 64.0 * (kMasterHz / 72.0) / 1048576.0;   // F-number #100, block 7: 759.5 Hz
        EXPECT_NEAR(hz, note, note * 0.01) << "host " << host;
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

#ifdef UNREALNG_HAVE_SAM2695
/// region <Render-cursor invariant on both boards>
//
// The render cursor and the chips' time are kept together by one rule (Ym2203Pair::anchorRender). The suite below
// drives the two boards that own a pair - the TurboSound FM in the AY socket (frame-relative axis, renders as the CPU
// runs) and the ZX-MultiSound in a ZX-bus slot (continuous axis, syncs a frame and then renders it) - through every
// event that moves time or the cursor, at several T-states inside a frame, and checks what the owner hears: an FM note
// and an SSG tone at their frequencies (zero crossings, not a level), with regular periods, the cursor's lag in
// bounds and no re-anchor in the steady state.

namespace pairboards
{

enum class Board
{
    Tsfm,
    MultiSound
};

enum class Event
{
    None,          ///< frame boundaries only
    MachineReset,  ///< Emulator::Reset (the bus /RESET: the TSFM's reset, the card's BusReset)
    SnapshotLoad,  ///< a .sna load (machine reset + state load)
    CoreRate,      ///< the core sample rate to 48 kHz (decimators redesigned at a frame boundary)
    HostSpeed,     ///< host speed x2 for four frames, back to x1
    FmMute         ///< FM muted by the board's control byte for a frame, unmuted
};

const char* EventName(Event e)
{
    switch (e)
    {
        case Event::None: return "None";
        case Event::MachineReset: return "MachineReset";
        case Event::SnapshotLoad: return "SnapshotLoad";
        case Event::CoreRate: return "CoreRate";
        case Event::HostSpeed: return "HostSpeed";
        case Event::FmMute: return "FmMute";
    }
    return "?";
}

constexpr uint16_t kCode = 0x8000;
constexpr uint32_t kFrameT = 71680;              // Pentagon
const double kFmNoteHz = 256.0 * 64.0 * (3500000.0 / 72.0) / 1048576.0;   // F-number #100, block 7: 759.5 Hz
const double kSsgToneHz = 1750000.0 / 16.0 / 256.0;                        // tone period #100: 427.2 Hz

/// The tones as a Z80 program (a TTD replay re-runs it): both chips quiet (fast release, key off, SSG off), then an FM
/// carrier on channel 2 of `fmChip` and SSG tone A of the other chip, the control byte left on `fmChip` with FM on
std::vector<uint8_t> ToneProgram(int fmChip)
{
    std::vector<uint8_t> code{0xF3};   // DI
    auto out = [&](uint16_t port, uint8_t value)
    {
        code.insert(code.end(), {0x01, uint8_t(port), uint8_t(port >> 8), 0x3E, value, 0xED, 0x79});
    };
    auto reg = [&](uint8_t r, uint8_t v)
    {
        out(0xFFFD, r);
        out(0xBFFD, v);
    };
    for (int chip = 0; chip < 2; chip++)
    {
        out(0xFFFD, uint8_t(0xF8 | chip));   // chip select, status read, FM on (TSFM and the card alike)
        for (uint8_t r = 0x80; r <= 0x8E; r++)
            reg(r, 0xFF);                    // release fastest
        for (uint8_t ch = 0; ch < 3; ch++)
            reg(0x28, ch);                   // key off
        for (uint8_t r = 0x40; r <= 0x4E; r++)
            reg(r, 0x7F);                    // every operator silent
        reg(0x07, 0x3F);
        reg(0x08, 0x00);
    }
    const int ssgChip = 1 - fmChip;
    out(0xFFFD, uint8_t(0xF8 | ssgChip));
    reg(0x00, 0x00);
    reg(0x01, 0x01);
    reg(0x07, 0x3E);                         // tone A only
    reg(0x08, 0x0F);
    out(0xFFFD, uint8_t(0xF8 | fmChip));
    for (const auto& [r, v] : std::initializer_list<std::pair<uint8_t, uint8_t>>{
             {0x4E, 0x00}, {0x5E, 0x1F}, {0x3E, 0x01}, {0xA6, 0x39}, {0xA2, 0x00}, {0x28, 0xF2}})
        reg(r, v);
    code.insert(code.end(), {0xF3, 0x76});   // DI; HALT
    return code;
}

/// One machine with a pair: a Pentagon with the TSFM in its AY socket, or with the AY in the socket and the
/// MultiSound in ZX-bus slot 1; the audio capture analyzer on
class PairMachine
{
public:
    explicit PairMachine(Board board)
        : _board(board),
          _m("pentagon128k", board == Board::Tsfm ? "ay-socket = tsfm" : "ay-socket = ay\nzxbus.1 = multisound")
    {
    }

    bool Init()
    {
        if (!_m.Ok())
            return false;
        AnalyzerManager* analyzers = _m.Context()->pDebugManager->GetAnalyzerManager();
        analyzers->activate("audiocapture");
        _capture = analyzers->getAnalyzer<AudioCaptureAnalyzer>("audiocapture");
        return _capture != nullptr && Pair() != nullptr;
    }

    Emulator& Machine() { return _m.Machine(); }
    EmulatorContext* Context() const { return _m.Context(); }
    Board board() const { return _board; }

    Ym2203Pair* Pair() const
    {
        if (_board == Board::MultiSound)
            return _m.Card() != nullptr ? &_m.Card()->Card().Ym() : nullptr;
        auto* tsfm = dynamic_cast<SoundChip_TurboSoundFM*>(Context()->pSoundManager->getTurboSound());
        return tsfm != nullptr ? &tsfm->pair() : nullptr;
    }

    AudioSourceType FmSource(int chip) const
    {
        if (_board == Board::MultiSound)
            return AudioSourceType::MultiSoundFm;
        return chip == 0 ? AudioSourceType::FM1 : AudioSourceType::FM2;
    }
    AudioSourceType SsgSource(int chip) const
    {
        if (_board == Board::MultiSound)
            return AudioSourceType::MultiSoundSsg;
        return chip == 0 ? AudioSourceType::AY1_All : AudioSourceType::AY2_All;
    }

    /// Runs the tone program from the current position (the CPU ends parked in DI; HALT)
    void PlayTones(int fmChip)
    {
        const std::vector<uint8_t> code = ToneProgram(fmChip);
        Z80* z80 = Context()->pCore->GetZ80();
        for (size_t i = 0; i < code.size(); i++)
            z80->DirectWrite(static_cast<uint16_t>(kCode + i), code[i]);
        z80->halted = 0;
        z80->pc = kCode;
    }

    void Out(uint16_t port, uint8_t value) { multisoundtest::Out(_m, port, value); }

    void Frames(int n)
    {
        for (int i = 0; i < n; i++)
            Machine().RunFrame(true);
    }

    /// `seconds` of one source's left channel from the next frame on, mean removed
    std::vector<double> Capture(AudioSourceType source, double seconds)
    {
        const size_t rate = Context()->pSoundManager->getCoreRate();
        _capture->startCapture(static_cast<size_t>(seconds * double(rate)) * 2, source);
        for (int guard = 0; guard < 400 && !_capture->isCaptureComplete(); guard++)
            Machine().RunFrame(true);
        std::vector<double> left(_capture->getCapturedSamples() / 2);
        double mean = 0.0;
        for (size_t i = 0; i < left.size(); i++)
            mean += left[i] = _capture->getBuffer()[i * 2];
        mean /= double(left.empty() ? 1 : left.size());
        for (double& v : left)
            v -= mean;
        _capture->stopCapture();
        return left;
    }

    double Rate() const { return double(Context()->pSoundManager->getCoreRate()); }

private:
    Board _board;
    multisoundtest::StagedMachine _m;
    AudioCaptureAnalyzer* _capture = nullptr;
};

/// The tone in a capture: frequency from the rising zero crossings (interpolated) and the largest deviation of one
/// period from their mean (a cursor jump, a held word or a dropped stretch shows up as a broken period)
struct Tone
{
    double hz = 0.0;
    double worstPeriod = 1.0;   // max |period - mean| / mean
    size_t periods = 0;
};

Tone Measure(const std::vector<double>& x, double rate)
{
    std::vector<double> at;
    for (size_t i = 1; i < x.size(); i++)
        if (x[i - 1] < 0.0 && x[i] >= 0.0)
            at.push_back(double(i - 1) + x[i - 1] / (x[i - 1] - x[i]));
    Tone tone;
    if (at.size() < 3)
        return tone;
    tone.periods = at.size() - 1;
    const double mean = (at.back() - at.front()) / double(tone.periods);
    tone.hz = rate / mean;
    tone.worstPeriod = 0.0;
    for (size_t i = 1; i < at.size(); i++)
        tone.worstPeriod = std::max(tone.worstPeriod, std::abs((at[i] - at[i - 1]) - mean) / mean);
    return tone;
}

/// Words queued per chip stay bounded (nothing piles up) and, for the card (which renders right after syncing),
/// the cursor ends every frame within one SSG tick and the decimators' phase of kRenderLag behind the chips
void ExpectCursorInBounds(PairMachine& m, const char* where)
{
    Ym2203Pair& pair = *m.Pair();
    for (int c = 0; c < 2; c++)
        EXPECT_LT(pair.chip(c)->words.size(), 2u * 996u + 64u) << where << ": chip " << c << " words pile up";
    if (m.board() == Board::MultiSound)
    {
        // The frame's sample count saws (903 / 904 at 44.1 kHz on a Pentagon) and the decimator's phase carries
        // over: the end of a frame's render sits within two output samples of kRenderLag behind the chips
        const int64_t slack = 2 * int64_t(3500000.0 / m.Rate()) + 16;
        const int64_t lag = pair.chipT() - pair.renderT();
        EXPECT_GE(lag, Ym2203Pair::kRenderLag - slack) << where;
        EXPECT_LE(lag, Ym2203Pair::kRenderLag + slack) << where;
    }
}

} // namespace pairboards

using pairboards::Board;
using pairboards::Event;

class Ym2203PairBoards_Test : public ::testing::TestWithParam<std::tuple<Board, Event, uint32_t>>
{
};

/// One board, one event at one T-state inside a frame (~150-300 ms: a machine with its sound devices, about 40
/// emulated frames and two captures; the card renders all five of its paths each frame)
TEST_P(Ym2203PairBoards_Test, ToneSurvivesEventAtAnyTState)
{
    using namespace pairboards;
    const auto [board, event, offset] = GetParam();
    // Both chips are covered across the offsets: the FM note on chip 0 at offset 0 and the frame end, on chip 1 mid
    const int fmChip = offset == kFrameT / 2 ? 1 : 0;
    const int ssgChip = 1 - fmChip;
    const std::string where = std::string(board == Board::Tsfm ? "TSFM" : "MultiSound") + " " + EventName(event) +
                              " at T " + std::to_string(offset) + ", FM chip " + std::to_string(fmChip);

    PairMachine m(board);
    ASSERT_TRUE(m.Init()) << where;
    m.PlayTones(fmChip);
    m.Frames(3);

    // The event at `offset` T-states into a frame
    if (offset > 0)
        m.Machine().RunTStates(offset);
    switch (event)
    {
        case Event::None:
            break;
        case Event::MachineReset:
            m.Machine().Reset();
            m.PlayTones(fmChip);
            break;
        case Event::SnapshotLoad:
        {
            const auto sna = TestPathHelper::FindProjectRoot() / "testdata/sound/tsfm/tech_support.sna";
            ASSERT_TRUE(m.Machine().LoadSnapshot(sna.string()));
            m.PlayTones(fmChip);
            break;
        }
        case Event::CoreRate:
            m.Context()->pSoundManager->requestCoreRate(48000);
            break;
        case Event::HostSpeed:
            ASSERT_TRUE(m.Machine().SetSpeedMultiplier(2));
            m.Frames(4);
            ASSERT_TRUE(m.Machine().SetSpeedMultiplier(1));
            break;
        case Event::FmMute:
            m.Out(0xFFFD, uint8_t(0xFC | fmChip));
            m.Frames(1);
            m.Out(0xFFFD, uint8_t(0xF8 | fmChip));
            break;
    }
    m.Frames(4);   // the note's attack, the decimators and the coupling settle
    if (event == Event::CoreRate)
        ASSERT_EQ(m.Rate(), 48000.0) << where;

    Ym2203Pair& pair = *m.Pair();
    const uint64_t anchors = pair.renderAnchors();

    const Tone fm = Measure(m.Capture(m.FmSource(fmChip), 0.3), m.Rate());
    EXPECT_NEAR(fm.hz, kFmNoteHz, kFmNoteHz * 0.01) << where << ": FM note";
    EXPECT_LT(fm.worstPeriod, 0.05) << where << ": an FM period broken (" << fm.periods << " periods)";
    const Tone ssg = Measure(m.Capture(m.SsgSource(ssgChip), 0.3), m.Rate());
    EXPECT_NEAR(ssg.hz, kSsgToneHz, kSsgToneHz * 0.01) << where << ": SSG tone";
    EXPECT_LT(ssg.worstPeriod, 0.05) << where << ": an SSG period broken (" << ssg.periods << " periods)";

    for (int i = 0; i < 20; i++)
    {
        m.Frames(1);
        ExpectCursorInBounds(m, where.c_str());
    }
    EXPECT_EQ(pair.renderAnchors(), anchors) << where << ": the cursor was re-anchored in the steady state";
}

INSTANTIATE_TEST_SUITE_P(
    BoardsEventsOffsets, Ym2203PairBoards_Test,
    ::testing::Combine(::testing::Values(Board::Tsfm, Board::MultiSound),
                       ::testing::Values(Event::None, Event::MachineReset, Event::SnapshotLoad, Event::CoreRate,
                                         Event::HostSpeed, Event::FmMute),
                       ::testing::Values(0u, pairboards::kFrameT / 2, pairboards::kFrameT - 1)),
    [](const ::testing::TestParamInfo<Ym2203PairBoards_Test::ParamType>& info)
    {
        return std::string(std::get<0>(info.param) == Board::Tsfm ? "Tsfm" : "MultiSound") + "_" +
               pairboards::EventName(std::get<1>(info.param)) + "_T" + std::to_string(std::get<2>(info.param));
    });

/// The steady state over a long run, both boards (~3.5 s in all, on purpose: drift shows only over many frames):
/// 1000 frames (20 s of emulated time; PAIR_LONG_RUN_FRAMES overrides, 10 000 checked by hand 2026-10-05) with the
/// note and the tone playing, the cursor in bounds every frame, the queues bounded, not one re-anchor, and the note
/// still at its frequency with regular periods at the end
TEST(Ym2203PairBoardsLongRun_Test, NoDriftOverAThousandFrames)
{
    using namespace pairboards;
    const char* framesOverride = std::getenv("PAIR_LONG_RUN_FRAMES");
    const int frames = framesOverride != nullptr ? std::atoi(framesOverride) : 1000;
    for (const Board board : {Board::Tsfm, Board::MultiSound})
    {
        const char* name = board == Board::Tsfm ? "TSFM" : "MultiSound";
        PairMachine m(board);
        ASSERT_TRUE(m.Init()) << name;
        m.PlayTones(0);
        m.Frames(3);
        Ym2203Pair& pair = *m.Pair();
        const uint64_t anchors = pair.renderAnchors();
        for (int i = 0; i < frames; i++)
        {
            m.Frames(1);
            ExpectCursorInBounds(m, name);
            if (HasFailure())
                break;
        }
        EXPECT_EQ(pair.renderAnchors(), anchors) << name;
        const Tone fm = Measure(m.Capture(m.FmSource(0), 0.3), m.Rate());
        EXPECT_NEAR(fm.hz, kFmNoteHz, kFmNoteHz * 0.01) << name;
        EXPECT_LT(fm.worstPeriod, 0.05) << name;
    }
}

/// TTD: a seek at any T-state inside a frame and the replay from there bring the cursor back exactly where it was
/// live (the same lag behind the chips at every later frame end) and the same audio: the FM capture taken at the same
/// frames live and replayed agrees within 1 % of its peak once the flushed output layers (coupling, decimator
/// history) have settled. Both boards, three offsets (~1 s in all: two machines, TTD recording, 50 frames x 4 runs)
TEST(Ym2203PairBoardsTtd_Test, SeekAndReplayKeepTheCursorAndTheAudio)
{
    using namespace pairboards;
    for (const Board board : {Board::Tsfm, Board::MultiSound})
    {
        const char* name = board == Board::Tsfm ? "TSFM" : "MultiSound";
        PairMachine m(board);
        ASSERT_TRUE(m.Init()) << name;
        FeatureManager* features = m.Machine().GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        m.Context()->pMemory->UpdateFeatureCache();
        ttd::TimeTravelManager* ttd = m.Context()->pTimeTravelManager;
        ASSERT_NE(ttd, nullptr);
        Ym2203Pair& pair = *m.Pair();
        const uint64_t& frame = m.Context()->emulatorState.frame_counter;

        ASSERT_TRUE(ttd->StartRecording()) << name;
        const uint64_t start = frame;
        m.PlayTones(0);
        std::vector<int64_t> liveLag;
        std::vector<uint32_t> liveCpuT;   // the CPU's T-state after each frame (the next frame's overshoot)
        for (int i = 0; i < 36; i++)
        {
            m.Frames(1);
            liveLag.push_back(pair.chipT() - pair.renderT());
            liveCpuT.push_back(m.Context()->pCore->GetZ80()->t);
        }
        const uint64_t captureFrame = frame;
        const std::vector<double> live = m.Capture(m.FmSource(0), 0.2);
        ttd->StopRecording();
        ASSERT_GT(live.size(), 8000u) << name;

        for (const uint32_t offset : {0u, kFrameT / 2, kFrameT - 1})
        {
            const std::string where = std::string(name) + " seek at T " + std::to_string(offset);
            ASSERT_TRUE(ttd->SeekTo({10, offset})) << where;
            // To the end of the sought frame (RunFrame from inside a frame runs a whole frame's length and stops
            // inside the next one), then frame by frame as live
            Z80& z80 = *m.Context()->pCore->GetZ80();
            if (frame < start + 11)
                m.Machine().RunTStates(kFrameT - z80.t);
            ASSERT_EQ(frame, start + 11) << where;
            for (uint64_t f = 11; f <= liveLag.size(); f++)
            {
                if (f > 11)
                    m.Frames(1);
                ASSERT_EQ(z80.t, liveCpuT[f - 1]) << where << ", frame " << f << ": the CPU is where it was live";
                // The live lag at the end of the same frame (liveLag[i] is the end of frame start + 1 + i)
                EXPECT_EQ(pair.chipT() - pair.renderT(), liveLag[f - 1]) << where << ", frame " << f;
            }
            ASSERT_EQ(frame, captureFrame) << where;
            const std::vector<double> replay = m.Capture(m.FmSource(0), 0.2);
            ASSERT_EQ(replay.size(), live.size()) << where;
            double peak = 0.0;
            double worst = 0.0;
            for (size_t i = 0; i < live.size(); i++)
            {
                peak = std::max(peak, std::abs(live[i]));
                worst = std::max(worst, std::abs(live[i] - replay[i]));
            }
            EXPECT_GT(peak, 1000.0) << where;
            EXPECT_LT(worst, peak * 0.01) << where << ": replayed FM differs from live";
        }
    }
}

/// endregion </Render-cursor invariant on both boards>
#endif // UNREALNG_HAVE_SAM2695
