#ifdef UNREALNG_HAVE_SAM2695
// MidiLine (core/src/emulator/sound/midi/midiline.h): tdd-midi-line.md §3 / ML-2. A program bit-bangs
// MIDI through AY register 14 bit 2 (the 128K convention, the MultiSound's YM chip 1 IOA2); the chip's
// pins reach MidiLine, MidiLine feeds sam2695::Synth::WriteLine, and the synthesizer's UART assembles
// (or rejects) the bytes from that timing. Time axis: Z80 T-states at 3.5 MHz, 31 250 baud = 112 T
// per bit.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/ayioport.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/midi/midiline.h"
#include "sam2695/sam2695.h"

namespace
{
constexpr uint64_t kHostRate = 3500000;
constexpr uint64_t kBitT = 112;  // 3 500 000 / 31 250

/// One preset (bank 0, program 0): a looped 64-frame square, so a Note On holds a voice for as long
/// as the test runs and Describe() shows it on its channel
class TinyBank : public sam2695::ISoundBank
{
public:
    TinyBank()
    {
        using namespace sam2695;
        _model.name = "midiline-test";

        SampleInfo s;
        s.name = "square";
        s.start = 0;
        s.end = 64;
        s.loopStart = 0;
        s.loopEnd = 64;
        s.sampleRate = kInternalRate;
        s.originalPitch = 60;
        _model.samples.push_back(s);
        for (int i = 0; i < 64; i++)
            _model.data16.push_back(static_cast<int16_t>(i < 32 ? 8000 : -8000));
        _model.data16.resize(64 + 46, 0);  // SF2 zero tail after the sample

        Zone presetZone;
        presetZone.link = 0;  // instrument 0
        Zone instZone;
        instZone.link = 0;    // sample 0
        instZone.gens[static_cast<int>(Gen::SampleModes)] = 1;  // loop continuously
        instZone.setMask |= 1ull << static_cast<int>(Gen::SampleModes);
        _model.zones.push_back(presetZone);
        _model.zones.push_back(instZone);

        Instrument inst;
        inst.name = "square";
        inst.zoneFirst = 1;
        inst.zoneCount = 1;
        _model.instruments.push_back(inst);

        Preset p;
        p.name = "square";
        p.program = 0;
        p.bank = 0;
        p.zoneFirst = 0;
        p.zoneCount = 1;
        _model.presets.push_back(p);

        _digest.fill(0);
        _digest[0] = 0x4D;  // any fixed identity: LoadState compares it
    }

    const sam2695::BankModel& Model() const override { return _model; }
    const sam2695::BankDigest& Digest() const override { return _digest; }

private:
    sam2695::BankModel _model;
    sam2695::BankDigest _digest{};
};

std::shared_ptr<const sam2695::ISoundBank> SharedBank()
{
    static std::shared_ptr<const sam2695::ISoundBank> bank = std::make_shared<TinyBank>();
    return bank;
}

void ConfigureSynth(sam2695::Synth& synth)
{
    sam2695::SynthConfig cfg;
    cfg.hostTickRate = kHostRate;
    cfg.resetDelay = false;  // the 50 ms boot window is the synthesizer's business, not the line's
    cfg.effects = false;
    ASSERT_TRUE(synth.Configure(cfg));
    ASSERT_TRUE(synth.LoadBank(SharedBank()));
}

/// One AY register write at a T-state
struct Write
{
    uint64_t t;
    uint8_t reg;
    uint8_t value;
};

/// What a MIDI send routine does (the 128K ROM style): idle the line, make port A an output, then
/// per byte a start bit, 8 data bits LSB first and a stop bit, one R14 write per bit `bitT` apart.
/// Only bit 2 of R14 is the line; the other bits stay high
std::vector<Write> MidiOutScript(const std::vector<uint8_t>& bytes, uint64_t bitT, uint64_t t0)
{
    std::vector<Write> w;
    w.push_back({t0, AY_PORTA, 0xFF});          // latch idle-high while still an input
    w.push_back({t0 + 4, AY_MIXER_CONTROL, 0x7F});  // port A output: the pins show #FF
    uint64_t t = t0 + 10 * bitT;                 // a little idle time
    auto level = [](bool high) { return static_cast<uint8_t>(high ? 0xFF : 0xFB); };
    for (uint8_t byte : bytes)
    {
        w.push_back({t, AY_PORTA, level(false)});  // start bit
        t += bitT;
        for (int b = 0; b < 8; b++)
        {
            w.push_back({t, AY_PORTA, level((byte >> b) & 1)});
            t += bitT;
        }
        w.push_back({t, AY_PORTA, level(true)});  // stop bit
        t += bitT;
    }
    return w;
}

uint64_t ScriptEnd(const std::vector<Write>& w, uint64_t bitT)
{
    return w.back().t + 2 * bitT;
}
}  // namespace

class MidiLine_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _chip = new SoundChip_AY8910(_context);
        ConfigureSynth(_synth);
        _line.Connect(&_synth);
        _chip->setIoPortListener(&_line);
    }

    void TearDown() override
    {
        delete _chip;
        _chip = nullptr;
        delete _context;
        _context = nullptr;
    }

    void Play(const std::vector<Write>& script, size_t from = 0, size_t to = SIZE_MAX)
    {
        for (size_t i = from; i < script.size() && i < to; i++)
            _chip->writeRegister(script[i].reg, script[i].value, script[i].t);
    }

    sam2695::SynthReport RunTo(uint64_t t)
    {
        _synth.Run(t);
        _synth.DiscardPendingAudio();
        sam2695::SynthReport report;
        _synth.Describe(report);
        return report;
    }

    EmulatorContext* _context = nullptr;
    SoundChip_AY8910* _chip = nullptr;
    sam2695::Synth _synth;
    MidiLine _line;  // MultiSound wiring: port A, bit 2
};

TEST_F(MidiLine_Test, BitTimelineToSynth)
{
    // Note On, channel 1, middle C, velocity 100
    const std::vector<Write> script = MidiOutScript({0x90, 0x3C, 0x64}, kBitT, 1000);
    Play(script);

    // Only bit 2 changes reach the line: start bit + data / stop transitions of three bytes
    MidiLineReport line;
    _line.Describe(line);
    EXPECT_TRUE(line.connected);
    EXPECT_TRUE(line.level);
    EXPECT_EQ(line.lastChangeTime, script.back().t);  // the last stop bit raised the line

    const sam2695::SynthReport report = RunTo(ScriptEnd(script, kBitT) + 2 * kHostRate / 1000);
    EXPECT_EQ(report.bytesReceived, 3u);
    EXPECT_EQ(report.framingErrors, 0u);
    // The parser took #90 #3C #64 as a Note On on channel 1: one voice sounds there and nowhere else
    EXPECT_EQ(report.channels[0].activeVoices, 1u);
    EXPECT_EQ(report.activeVoices, 1u);
}

TEST_F(MidiLine_Test, FramingErrorFromBadTiming)
{
    // A send loop timed for a CPU twice as fast, run at 3.5 MHz (bit time 224 T, e.g. a routine tuned
    // for a 7 MHz turbo): the UART reads each bit twice and finds low stop bits - framing errors,
    // the bytes are dropped, no Note On reaches the parser
    const std::vector<Write> slow = MidiOutScript({0x90, 0x3C, 0x64}, kBitT * 2, 1000);
    Play(slow);
    const sam2695::SynthReport report = RunTo(ScriptEnd(slow, kBitT * 2) + 20 * kBitT);
    EXPECT_GE(report.framingErrors, 1u);
    EXPECT_LT(report.bytesReceived, 3u);
    EXPECT_EQ(report.activeVoices, 0u);
}

TEST_F(MidiLine_Test, DoubleSpeedGivesWrongBytes)
{
    // The opposite mistake (bit time 56 T) need not trip the stop-bit check: the stop samples can land
    // on high bits. The UART then assembles other bytes than the program sent, and no Note On on
    // channel 1 sounds - the same cause and effect as on the real card
    const std::vector<Write> fast = MidiOutScript({0x90, 0x3C, 0x64}, kBitT / 2, 1000);
    Play(fast);
    const sam2695::SynthReport report = RunTo(ScriptEnd(fast, kBitT) + 20 * kBitT);
    EXPECT_NE(report.bytesReceived, 3u);
    EXPECT_EQ(report.channels[0].activeVoices, 0u);
}

TEST_F(MidiLine_Test, TtdRoundTrip)
{
    const std::vector<Write> script = MidiOutScript({0x90, 0x3C, 0x64}, kBitT, 1000);
    // Cut in the middle of the second byte (index: 2 setup writes + 10 of byte 1 + 4 of byte 2)
    const size_t cut = 2 + 10 + 4;
    const uint64_t end = ScriptEnd(script, kBitT) + 2 * kHostRate / 1000;

    // Reference run, saving the state at the cut
    Play(script, 0, cut);
    _synth.Run(script[cut].t);  // what the host has rendered by the checkpoint
    _synth.DiscardPendingAudio();
    std::vector<uint8_t> chipBlob(_chip->TTDStateSize());
    std::vector<uint8_t> lineBlob(_line.TTDStateSize());
    std::vector<uint8_t> synthBlob(_synth.StateSize());
    _chip->TTDSaveState(chipBlob.data());
    _line.TTDSaveState(lineBlob.data());
    _synth.SaveState(synthBlob.data());
    const uint8_t pinsAtCut = _chip->ioPortPins(AyIoPort::PortA);
    EXPECT_GT(_line.Edges(), 0u);  // mid-byte: the line has moved

    Play(script, cut);
    const sam2695::SynthReport reference = RunTo(end);
    std::vector<uint8_t> referenceFinal(_synth.StateSize());
    _synth.SaveState(referenceFinal.data());
    ASSERT_EQ(reference.bytesReceived, 3u);

    // Restore into fresh objects and finish the byte stream
    SoundChip_AY8910 chip(_context);
    MidiLine line;
    sam2695::Synth synth;
    ConfigureSynth(synth);
    chip.TTDLoadState(chipBlob.data());
    line.TTDLoadState(lineBlob.data());
    ASSERT_TRUE(synth.LoadState(synthBlob.data(), synthBlob.size()));
    line.Connect(&synth);
    chip.setIoPortListener(&line);

    // The chip's pins come back from its registers, the line level from its own blob: they agree
    EXPECT_EQ(chip.ioPortPins(AyIoPort::PortA), pinsAtCut);
    MidiLineReport restored;
    line.Describe(restored);
    EXPECT_EQ(restored.level, ((pinsAtCut >> 2) & 1) != 0);

    for (size_t i = cut; i < script.size(); i++)
        chip.writeRegister(script[i].reg, script[i].value, script[i].t);
    synth.Run(end);
    synth.DiscardPendingAudio();
    sam2695::SynthReport report;
    synth.Describe(report);

    EXPECT_EQ(report.bytesReceived, reference.bytesReceived);
    EXPECT_EQ(report.framingErrors, reference.framingErrors);
    EXPECT_EQ(report.channels[0].activeVoices, reference.channels[0].activeVoices);
    std::vector<uint8_t> restoredFinal(synth.StateSize());
    synth.SaveState(restoredFinal.data());
    EXPECT_EQ(restoredFinal, referenceFinal);
    EXPECT_EQ(line.Edges(), _line.Edges());
    EXPECT_EQ(line.LastChangeTime(), _line.LastChangeTime());
}

#endif  // UNREALNG_HAVE_SAM2695
