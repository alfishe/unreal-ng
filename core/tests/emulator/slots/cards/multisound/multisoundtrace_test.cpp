#ifdef UNREALNG_HAVE_SAM2695
// ZX-MultiSound real-program bus traces (CL-2, docs/inprogress/2026-10-03-zx-multisound/tdd-card-logic.md §8 and
// cl2-real-program-traces.md). Each stored trace (testdata/sound/multisound/traces/<name>.msc.zst) is a real program's
// card bus traffic from the machine's power-on, captured in the emulator (multisoundtracecapture_test.cpp, script
// <name>.script next to it). Three checks per trace:
//   1. RTL: every line played into MultiSoundLogic gives the records the card's CPLD gives in Verilator (the RTL
//      records are frozen as a hash chain in <name>.rtl), and every read the program made returned what the model and
//      the RTL drive (status bytes, GS flags, the GS reply, #FF on undecoded GS ports; for YM2203 reads: who drives).
//   2. The card: the host lines replayed into a fresh MultiSoundCard at their times reproduce the trace line for line -
//      every read the same value (so the program's polling loops end where they ended), and the card's GS, running the
//      real firmware from its power-on, does the same port cycles and DAC fetches.
//   3. The audio of that replay: golden digests of the rows the program plays, plus per-program content checks.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/multisoundscenario.h"
#include "_helpers/testpathhelper.h"
#include "debugger/ttd/ttdcompression.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/cards/multisound/multisoundcard.h"
#include "multisoundstagedmachine.h"
#include "sam2695/sam2695.h"

namespace
{

std::string ReadFileText(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

/// testdata/sound/multisound/traces/<name>.msc.zst, decompressed
bool LoadTraceText(const std::string& name, std::string& text)
{
    const std::string packed = ReadFileText(TestPathHelper::GetTestDataPath("sound/multisound/traces/" + name + ".msc.zst"));
    if (packed.empty())
        return false;
    const std::vector<uint8_t> bytes(packed.begin(), packed.end());
    const uint64_t size = ttd::codec::DeclaredContentSize(bytes);
    if (size == 0 || size > (64u << 20))
        return false;
    text.assign(static_cast<size_t>(size), '\0');
    return ttd::codec::Decompress(bytes, static_cast<size_t>(size), reinterpret_cast<uint8_t*>(text.data()));
}

/// The cycle lines of a trace text (no comments, no header directives, no blank lines)
std::vector<std::string> BodyLines(const std::string& text)
{
    static const char* const directives[] = { "mask ", "ram ", "dip ", "cpu ", "frame ", "rate ", "length " };
    std::vector<std::string> lines;
    std::stringstream stream(text);
    std::string line;
    bool header = true;
    while (std::getline(stream, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        if (header && std::any_of(std::begin(directives), std::end(directives),
                                  [&](const char* d) { return line.rfind(d, 0) == 0; }))
            continue;
        header = false;
        lines.push_back(line);
    }
    return lines;
}

/// A stored trace, its RTL summary and the result of playing it into MultiSoundLogic
struct LoadedTrace
{
    std::string name;
    std::string text;
    MultiSoundScenario scenario;
    MultiSoundTraceRtl rtl;
};

::testing::AssertionResult Load(const std::string& name, LoadedTrace& trace)
{
    trace.name = name;
    if (!LoadTraceText(name, trace.text))
        return ::testing::AssertionFailure() << "cannot read " << name << ".msc.zst";
    std::string error;
    if (!ParseMultiSoundScenario(trace.text, trace.scenario, error))
        return ::testing::AssertionFailure() << name << ": " << error;
    if (!ParseMultiSoundTraceRtl(ReadFileText(TestPathHelper::GetTestDataPath("sound/multisound/traces/" + name + ".rtl")), trace.rtl))
        return ::testing::AssertionFailure() << "cannot read " << name << ".rtl (tools/verification/multisound/regenerate.sh)";
    if (trace.scenario.frameTicks == 0 || trace.scenario.tickRate == 0 || trace.scenario.lengthTicks == 0)
        return ::testing::AssertionFailure() << name << ": the header lacks frame / rate / length";
    return ::testing::AssertionSuccess();
}

/// Check 1: the logic against the RTL's frozen records and against every read the program made. With `variant` (the
/// trace replayed on another card configuration, `<name>-<variant>.rtl`) only the records: the reads belong to the
/// captured configuration. `observe` sees every line's record
::testing::AssertionResult AgreesWithTheRtl(
    const LoadedTrace& trace, const std::string& variant = {}, const MultiSoundOptions* options = nullptr,
    const std::function<void(const MultiSoundCycle&, const MultiSoundCycleRecord&)>& observe = {})
{
    MultiSoundTraceRtl rtl = trace.rtl;
    MultiSoundScenario header = trace.scenario;
    if (!variant.empty())
    {
        const std::string file = "sound/multisound/traces/" + trace.name + "-" + variant + ".rtl";
        if (!ParseMultiSoundTraceRtl(ReadFileText(TestPathHelper::GetTestDataPath(file)), rtl))
            return ::testing::AssertionFailure() << "cannot read " << file;
        header.options = *options;
    }
    MultiSoundLogicBus logic(header);
    uint64_t hash = MultiSoundHashSeed;
    size_t checkpoint = 0;
    uint64_t reads = 0, values = 0;
    const std::vector<MultiSoundCycle>& cycles = trace.scenario.cycles;
    for (size_t i = 0; i < cycles.size(); i++)
    {
        const MultiSoundCycle& cycle = cycles[i];
        const bool ymRead = cycle.op == MultiSoundCycle::Op::In && MultiSoundTraceReadIsYm(logic.Logic(), cycle.address);
        const MultiSoundCycleRecord record = logic.Execute(cycle);
        hash = HashMultiSoundRecord(hash, record);
        if (observe)
            observe(cycle, record);
        if (cycle.observed && variant.empty())
        {
            reads++;
            std::string why;
            if (!MultiSoundTraceReadMatches(cycle, record, ymRead, why))
                return ::testing::AssertionFailure() << trace.name << ":" << cycle.sourceLine << " '"
                                                     << FormatMultiSoundCycle(cycle) << "': " << why;
            values += (!ymRead && cycle.observedDriven) ? 1 : 0;
        }
        if (checkpoint < rtl.checkpoints.size() && rtl.checkpoints[checkpoint].first == i + 1)
        {
            if (rtl.checkpoints[checkpoint].second != hash)
            {
                const size_t from = checkpoint == 0 ? 0 : rtl.checkpoints[checkpoint - 1].first;
                return ::testing::AssertionFailure() << trace.name << ": the logic's records differ from the RTL's in cycles "
                                                     << from + 1 << "-" << i + 1 << " (mscosim trace names the line)";
            }
            checkpoint++;
        }
    }
    if (cycles.size() != rtl.cycles || hash != rtl.hash)
        return ::testing::AssertionFailure() << trace.name << ": " << cycles.size() << " cycles, hash " << std::hex << hash
                                             << "; the RTL: " << std::dec << rtl.cycles << " cycles, hash " << std::hex
                                             << rtl.hash << " (regenerate.sh if the trace changed)";
    if (reads != rtl.reads || values != rtl.values)
        return ::testing::AssertionFailure() << trace.name << ": " << reads << " reads, " << values << " values; the RTL run "
                                             << rtl.reads << " / " << rtl.values;
    return ::testing::AssertionSuccess();
}

/// The program's polling loops as the trace shows them: a loop is a run of reads at one instruction that kept getting
/// the same value. It ends when a read at that instruction gets another value (the condition it waited for) or the
/// program reads elsewhere (it gave up waiting, or it was not a wait); a run still going when the trace ends is a
/// program that may hang
struct PollLoops
{
    uint64_t loops = 0;         // runs of two or more identical reads at one instruction
    uint64_t endedByValue = 0;  // of them, those a different value at the same instruction ended
    bool openAtEnd = false;     // the last run of the trace was still going
    uint64_t reads = 0;         // the reads of all loops
    uint32_t longest = 0;       // the most reads of one loop
};

PollLoops FindPollLoops(const MultiSoundScenario& scenario, uint16_t portMask, uint16_t portMatch)
{
    PollLoops out;
    using Op = MultiSoundCycle::Op;
    uint16_t m1 = 0;
    bool open = false;
    uint16_t runM1 = 0;
    uint8_t runValue = 0;
    uint32_t runReads = 0;
    auto close = [&](bool ended)
    {
        if (open && runReads >= 2)
        {
            out.loops++;
            out.endedByValue += ended ? 1 : 0;
            out.reads += runReads;
            out.longest = std::max(out.longest, runReads);
        }
        open = false;
    };
    for (const MultiSoundCycle& cycle : scenario.cycles)
    {
        if (cycle.op == Op::M1)
            m1 = cycle.address;
        if (cycle.op != Op::In || !cycle.observed || (cycle.address & portMask) != portMatch)
            continue;
        if (open && runM1 == m1 && runValue == cycle.observedValue)
        {
            runReads += cycle.repeat;
            continue;
        }
        close(open && runM1 == m1);
        open = true;
        runM1 = m1;
        runValue = cycle.observedValue;
        runReads = cycle.repeat;
    }
    out.openAtEnd = open && runReads >= 2;
    close(false);
    return out;
}

/// Check 2 and 3: the host lines into a fresh card at their times, the card's own trace and its rows
class CardReplay
{
public:
    explicit CardReplay(const MultiSoundScenario& scenario, const std::string& midiBankPath = {})
        : _scenario(scenario)
    {
        _context = std::make_unique<EmulatorContext>(LoggerLevel::LogError);
        _context->config.sound.gs_vol = 8000;
        _context->config.frame = static_cast<uint32_t>(scenario.frameTicks);
        _context->config.frame_duration_us = 20000;
        _context->emulatorState.current_z80_frequency_multiplier = 1;
        _context->emulatorState.hw_turbo_ratio_applied = 1;
        MultiSoundCardConfig config;
        config.options = scenario.options;
        config.hostTickRate = scenario.tickRate;
        config.outputRate = 44100;
        config.midiBankPath = midiBankPath;
        _card = std::make_unique<MultiSoundCard>(_context.get(), config);
    }
    ~CardReplay() { _card->SetBusTrace(nullptr); }

    /// Plays the trace; false with the first problem in `why`
    bool Run(std::string& why)
    {
        // The frame calls where the live card got them; a trace whose first mark is a frame end began inside a
        // frame the machine had already started (the machine's start-up)
        const uint64_t frame = _scenario.frameTicks;
        uint64_t nextBase = 0;
        uint64_t frameEnd = frame;
        auto frames = [&](const std::string& marks)
        {
            for (char mark : marks)
            {
                if (mark == 'E')
                {
                    Collect(_card->FrameEnd(frameEnd));
                    nextBase = frameEnd;
                }
                else
                {
                    _card->FrameStart(nextBase, frame);
                    frameEnd = nextBase + frame;
                }
            }
        };
        for (const MultiSoundCycle& cycle : _scenario.cycles)
        {
            const size_t e = cycle.framesBefore.find_first_of("ES");
            if (e != std::string::npos)
            {
                if (cycle.framesBefore[e] == 'E')
                    _card->FrameStart(0, frame);
                break;
            }
        }
        _card->SetBusTrace(&_writer);   // from here on as the live card was traced
        using Op = MultiSoundCycle::Op;
        for (const MultiSoundCycle& cycle : _scenario.cycles)
        {
            frames(cycle.framesBefore);
            const bool host = cycle.op == Op::Out || cycle.op == Op::In || cycle.op == Op::Reset;
            if (host && (!cycle.timed || cycle.time + frame < frameEnd))
            {
                why = "line " + std::to_string(cycle.sourceLine) + " has no time or lies before its frame";
                return false;
            }
            switch (cycle.op)
            {
                case Op::M1:
                    _card->M1(cycle.address);
                    break;
                case Op::Out:
                    _card->Out(cycle.address, cycle.value, cycle.time);
                    break;
                case Op::In:
                    for (uint32_t r = 0; r < cycle.repeat; r++)
                    {
                        bool drives = false;
                        const uint8_t value = _card->In(cycle.address, cycle.time, drives);
                        if (cycle.observed && (drives != cycle.observedDriven || (drives && value != cycle.observedValue)) &&
                            _readMismatches++ == 0)
                        {
                            char text[160];
                            std::snprintf(text, sizeof(text), "line %d '%s': the program read %s, the card answers %s",
                                          cycle.sourceLine, FormatMultiSoundCycle(cycle).c_str(),
                                          cycle.observedDriven ? std::to_string(cycle.observedValue).c_str() : "--",
                                          drives ? std::to_string(value).c_str() : "--");
                            _firstReadMismatch = text;
                        }
                    }
                    break;
                case Op::Reset:
                    _card->BusReset(cycle.time);
                    break;
                default:
                    break;  // the GS lines: the card's GS produces them itself
            }
        }
        frames(_scenario.trailingFrames);
        if (nextBase != _scenario.lengthTicks)
        {
            why = "the trace's frames end at " + std::to_string(nextBase) + ", its header says " +
                  std::to_string(_scenario.lengthTicks);
            return false;
        }
        if (_readMismatches)
        {
            why = std::to_string(_readMismatches) + " reads differ, first: " + _firstReadMismatch;
            return false;
        }
        return true;
    }

    /// The card's own trace, cycle lines only
    std::vector<std::string> ReplayLines() { return BodyLines(_writer.Text("")); }

    MultiSoundCard& Card() { return *_card; }
    const std::vector<int16_t>& Row(MultiSoundRow row) const { return _rows[static_cast<size_t>(row)]; }

    /// FNV-1a over a row's samples (the YM golden digests' form)
    uint64_t Digest(MultiSoundRow row) const
    {
        uint64_t h = 0xcbf29ce484222325ull;
        for (int16_t v : Row(row))
        {
            for (int i = 0; i < 2; i++)
            {
                h ^= static_cast<uint8_t>(static_cast<uint16_t>(v) >> (8 * i));
                h *= 0x100000001b3ull;
            }
        }
        return h;
    }

    /// RMS level of a row in dB per block of `block` stereo frames: the fingerprint of a float synthesizer, whose
    /// samples differ in the last bits between libm / compiler (glibc vs Apple libm, FP contraction), so no digest
    std::vector<double> LevelsDb(MultiSoundRow row, size_t block) const
    {
        const std::vector<int16_t>& v = Row(row);
        std::vector<double> levels;
        for (size_t b = 0; (b + 1) * block * 2 <= v.size(); b++)
        {
            double sum = 0.0;
            for (size_t i = b * block * 2; i < (b + 1) * block * 2; i++)
                sum += static_cast<double>(v[i]) * v[i];
            levels.push_back(10.0 * std::log10(sum / static_cast<double>(block * 2) + 1e-20));
        }
        return levels;
    }

    /// RMS of one side of a row
    double Rms(MultiSoundRow row, int side) const
    {
        const std::vector<int16_t>& v = Row(row);
        double sum = 0.0;
        size_t n = 0;
        for (size_t i = static_cast<size_t>(side); i < v.size(); i += 2, n++)
            sum += static_cast<double>(v[i]) * v[i];
        return n ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
    }

private:
    void Collect(size_t frames)
    {
        for (size_t r = 0; r < static_cast<size_t>(MultiSoundRow::Count); r++)
        {
            const int16_t* row = _card->Row(static_cast<MultiSoundRow>(r));
            _rows[r].insert(_rows[r].end(), row, row + frames * 2);
        }
    }

    const MultiSoundScenario& _scenario;
    std::unique_ptr<EmulatorContext> _context;
    std::unique_ptr<MultiSoundCard> _card;
    MultiSoundTraceWriter _writer;
    std::array<std::vector<int16_t>, static_cast<size_t>(MultiSoundRow::Count)> _rows;
    uint64_t _readMismatches = 0;
    std::string _firstReadMismatch;
};

/// The replayed card's own trace against the stored one: the first differing line
::testing::AssertionResult ReproducesTheTrace(CardReplay& replay, const LoadedTrace& trace)
{
    const std::vector<std::string> expected = BodyLines(trace.text);
    const std::vector<std::string> actual = replay.ReplayLines();
    const size_t n = std::min(expected.size(), actual.size());
    for (size_t i = 0; i < n; i++)
    {
        if (expected[i] != actual[i])
            return ::testing::AssertionFailure() << trace.name << ": cycle line " << i + 1 << " differs: stored '" << expected[i]
                                                 << "', replayed '" << actual[i] << "'";
    }
    if (expected.size() != actual.size())
        return ::testing::AssertionFailure() << trace.name << ": " << expected.size() << " stored lines, " << actual.size()
                                             << " replayed (first extra: '"
                                             << (expected.size() > n ? expected[n] : actual[n]) << "')";
    return ::testing::AssertionSuccess();
}

} // namespace

/// Development aid (not a check): prints the digests and levels of a stored trace's replay
TEST(MultiSoundTrace_Test, DISABLED_PrintReplay)
{
    const char* name = std::getenv("MS_TRACE");
    if (name == nullptr)
        GTEST_SKIP() << "MS_TRACE not set";
    LoadedTrace trace;
    ASSERT_TRUE(Load(name, trace));
    EXPECT_TRUE(AgreesWithTheRtl(trace));
    CardReplay replay(trace.scenario, multisoundtest::WriteTestBank());
    std::string why;
    EXPECT_TRUE(replay.Run(why)) << why;
    EXPECT_TRUE(ReproducesTheTrace(replay, trace));
    static const char* const names[] = { "Ssg1", "Ssg2", "Fm1", "Fm2", "Saa", "Pcm", "Midi" };
    for (size_t r = 0; r < static_cast<size_t>(MultiSoundRow::Count); r++)
    {
        const auto row = static_cast<MultiSoundRow>(r);
        std::printf("%-5s digest 0x%016llXull rms L %.1f R %.1f frames %zu\n", names[r],
                    static_cast<unsigned long long>(replay.Digest(row)), replay.Rms(row, 0), replay.Rms(row, 1),
                    replay.Row(row).size() / 2);
    }
}

namespace
{

/// The rows a program does not use stay digital silence
void ExpectSilent(const CardReplay& replay, std::initializer_list<MultiSoundRow> rows)
{
    for (MultiSoundRow row : rows)
        EXPECT_EQ(replay.Rms(row, 0) + replay.Rms(row, 1), 0.0) << "row " << static_cast<int>(row);
}

/// Pearson correlation of the left and right side of a row
double SideCorrelation(const std::vector<int16_t>& row)
{
    double sl = 0, sr = 0, sll = 0, srr = 0, slr = 0;
    const size_t n = row.size() / 2;
    for (size_t i = 0; i < n; i++)
    {
        const double l = row[i * 2], r = row[i * 2 + 1];
        sl += l; sr += r; sll += l * l; srr += r * r; slr += l * r;
    }
    const double cov = slr / n - (sl / n) * (sr / n);
    const double vl = sll / n - (sl / n) * (sl / n), vr = srr / n - (sr / n) * (sr / n);
    return (vl > 0 && vr > 0) ? cov / std::sqrt(vl * vr) : 0.0;
}

/// The MIDI bytes on chip 0's (U4) port A bit 2 as the logic routes the trace's writes: the pin follows R14 bit 2 while
/// R7 bit 6 makes port A an output and floats high (the pull-up) otherwise; decoded as 8N1 at `bitTicks` (sampled
/// mid-bit), a byte whose stop bit is low counts as a framing error
struct MidiLineDecode
{
    std::vector<uint8_t> bytes;
    int framingErrors = 0;
};

MidiLineDecode DecodeU4MidiLine(const LoadedTrace& trace, double bitTicks)
{
    std::vector<std::pair<uint64_t, bool>> edges;   // (time, level) after each R7 / R14 write
    uint8_t address[2] = {};
    uint8_t r7 = 0, r14 = 0;
    uint64_t lastTime = 0;
    MultiSoundLogicBus logic(trace.scenario);
    for (const MultiSoundCycle& cycle : trace.scenario.cycles)
    {
        if (cycle.timed)
            lastTime = cycle.time;
        const MultiSoundCycleRecord r = logic.Execute(cycle);
        for (int chip = 0; chip < 2; chip++)
        {
            if (!(r.ymWrite & (1 << chip)))
                continue;
            if (r.ymA0 == 0)
                address[chip] = r.ymValue;
            else if (chip == 0 && (address[0] == 7 || address[0] == 14))
            {
                (address[0] == 7 ? r7 : r14) = r.ymValue;
                edges.emplace_back(lastTime, !(r7 & 0x40) || (r14 & 0x04) != 0);
            }
        }
    }
    MidiLineDecode out;
    auto levelAt = [&](uint64_t t)
    {
        bool level = true;
        for (const auto& [time, high] : edges)
        {
            if (time > t)
                break;
            level = high;
        }
        return level;
    };
    size_t i = 0;
    bool previous = true;
    while (i < edges.size())
    {
        // the next falling edge is a start bit
        while (i < edges.size() && !(previous && !edges[i].second))
            previous = edges[i++].second;
        if (i >= edges.size())
            break;
        const uint64_t start = edges[i].first;
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++)
            byte |= static_cast<uint8_t>(levelAt(start + static_cast<uint64_t>((1.5 + b) * bitTicks)) ? 1u << b : 0u);
        const uint64_t stop = start + static_cast<uint64_t>(9.5 * bitTicks);
        if (levelAt(stop))
            out.bytes.push_back(byte);
        else
            out.framingErrors++;
        while (i < edges.size() && edges[i].first <= stop)
            previous = edges[i++].second;
    }
    return out;
}

#define EXPECT_DIGEST(replay, row, golden)                                                                             \
    EXPECT_EQ((replay).Digest(row), golden##ull) << #row << " digest 0x" << std::hex << (replay).Digest(row)

/// Loads a trace and replays it on a fresh card with the one-preset test bank (a sine: the MIDI row's level and
/// timing, independent of the shipped bank); the replay must reproduce the trace line for line
#define REPLAY_TRACE(name, trace, replay)                                                                              \
    LoadedTrace trace;                                                                                                 \
    ASSERT_TRUE(Load(name, trace));                                                                                    \
    CardReplay replay(trace.scenario, multisoundtest::WriteTestBank());                                               \
    {                                                                                                                  \
        std::string why;                                                                                               \
        ASSERT_TRUE(replay.Run(why)) << why;                                                                           \
        ASSERT_TRUE(ReproducesTheTrace(replay, trace));                                                                \
    }

} // namespace

/// region <TFM Music Compiler player (uzhos.scl): the YM2203 busy flag>

/// The player reads #FFFD (status, bit 7 = busy) before every register write and loops while it is set: 2657 reads,
/// 760 loops of 5-8 reads (the busy time after a write, 110-176 T). The logic and the RTL agree on every cycle, and
/// every read is driven by the selected chip (~5 ms)
TEST(MultiSoundTrace_Test, TfmCompilerPlayerPollsTheBusyFlag)
{
    LoadedTrace trace;
    ASSERT_TRUE(Load("tfmc-uzhos", trace));
    EXPECT_TRUE(AgreesWithTheRtl(trace));
    const PollLoops loops = FindPollLoops(trace.scenario, 0xC00F, 0xC00D);
    EXPECT_GE(loops.loops, 700u);
    EXPECT_EQ(loops.endedByValue, loops.loops) << "every busy loop ends with the flag clear";
    EXPECT_FALSE(loops.openAtEnd);
    EXPECT_LE(loops.longest, 8u);
}

/// The trace replayed on a fresh card (~300 ms, over the 50 ms budget: 260 frames from the power-on, the card's GS
/// firmware boots on its 16 MHz CPU meanwhile): every status read answers as in the live run - busy while the
/// program saw busy, free when it went on - and the tune's FM on both chips matches its digests; no SSG, SAA, DAC
/// or MIDI
TEST(MultiSoundTrace_Test, TfmCompilerPlayerReplaysOnTheCard)
{
    REPLAY_TRACE("tfmc-uzhos", trace, replay);
    EXPECT_GT(replay.Rms(MultiSoundRow::Fm1, 0), 300.0);
    EXPECT_GT(replay.Rms(MultiSoundRow::Fm2, 0), 300.0);
    ExpectSilent(replay, {MultiSoundRow::Ssg1, MultiSoundRow::Ssg2, MultiSoundRow::Saa, MultiSoundRow::Pcm, MultiSoundRow::Midi});
    EXPECT_DIGEST(replay, MultiSoundRow::Fm1, 0xB03FCE4440371AB5);
    EXPECT_DIGEST(replay, MultiSoundRow::Fm2, 0x68093A5906417A05);
}

/// endregion </TFM Music Compiler player>

/// region <Mod Player v2.5: the General Sound mailbox>

/// Host and GS side of the mailbox in one trace: the player's #BB polls (waiting for the command flag to clear, for
/// the data flag), the module upload through #B3, the per-frame reply bytes; the GS firmware's port 4 polls, port 2
/// and 1 reads, port 3 replies and port 5 acknowledges; 4096 DAC fetches. Every status, reply and GS port value the
/// emulator's General Sound produced is what the CPLD drives (~25 ms: 74 k cycles)
TEST(MultiSoundTrace_Test, GsModPlayerMailboxAgreesWithTheRtl)
{
    LoadedTrace trace;
    ASSERT_TRUE(Load("gs-modplayer", trace));
    EXPECT_TRUE(AgreesWithTheRtl(trace));
    const PollLoops loops = FindPollLoops(trace.scenario, 0x00FF, 0x00BB);
    EXPECT_GE(loops.loops, 800u);
    EXPECT_GE(loops.endedByValue, 700u) << "the waits end when the GS answers (the rest are repeated reads at #7564)";
    EXPECT_FALSE(loops.openAtEnd);
}

/// The trace replayed on a fresh card (~700 ms, over the 50 ms budget: 640 frames from the power-on with the real
/// GS 1.05b firmware on a 16 MHz Z80 - its POST alone is ~200 frames): the GS answers every poll and reply as in the
/// live run and does the same port cycles and DAC fetches; the MOD plays on the DACs hard left and right
TEST(MultiSoundTrace_Test, GsModPlayerReplaysOnTheCard)
{
    REPLAY_TRACE("gs-modplayer", trace, replay);
    EXPECT_GT(replay.Rms(MultiSoundRow::Pcm, 0), 500.0);
    EXPECT_GT(replay.Rms(MultiSoundRow::Pcm, 1), 500.0);
    EXPECT_LT(std::fabs(SideCorrelation(replay.Row(MultiSoundRow::Pcm))), 0.5) << "two different channel pairs";
    ExpectSilent(replay, {MultiSoundRow::Ssg1, MultiSoundRow::Ssg2, MultiSoundRow::Fm1, MultiSoundRow::Fm2,
                          MultiSoundRow::Saa, MultiSoundRow::Midi});
    EXPECT_DIGEST(replay, MultiSoundRow::Pcm, 0x8FABEDA80BB78C82);
    EXPECT_EQ(replay.Card().Dacs().LateEvents(), 0u);
}

/// endregion </Mod Player>

/// region <Ball Quest: #F0-#F7 as register numbers (issue #11)>

/// The game writes #F2 and #F0 to #FFFD as register numbers. With the card's firmware (ctrlMask = pro) they are
/// control bytes: FM unmuted, chip 0. With the issue #11 patch (classic, SAA off) they stay register numbers. Both
/// configurations agree with their RTL (~40 ms: 115 k cycles twice)
TEST(MultiSoundTrace_Test, BallQuestControlBytesProAndClassic)
{
    LoadedTrace trace;
    ASSERT_TRUE(Load("ballquest", trace));
    int unmutes = 0;
    bool muted = true;
    auto count = [&](const MultiSoundCycle&, const MultiSoundCycleRecord& r)
    {
        unmutes += (muted && !r.fmMuted) ? 1 : 0;
        muted = r.fmMuted != 0;
    };
    EXPECT_TRUE(AgreesWithTheRtl(trace, {}, nullptr, count));
    EXPECT_EQ(unmutes, 4) << "#F2 and three #F0 unmute the FM";

    MultiSoundOptions classic = trace.scenario.options;
    classic.ctrlMask = MultiSoundCtrlMask::Classic;
    classic.saa = false;
    unmutes = 0;
    muted = true;
    EXPECT_TRUE(AgreesWithTheRtl(trace, "classic", &classic, count));
    EXPECT_EQ(unmutes, 0) << "the patched firmware leaves FM muted";
}

/// The trace replayed on a fresh card (~1.4 s, over the 50 ms budget: 1595 frames - the ZX-Evo's start, the title
/// and the in-game demo up to the issue #11 writes 30 s in): the AY music on both SSG parts; the FM unmutes are
/// silent (no FM voice is programmed: the click of the real card is not modeled, owner decision 2026-10-05)
TEST(MultiSoundTrace_Test, BallQuestReplaysOnTheCard)
{
    REPLAY_TRACE("ballquest", trace, replay);
    EXPECT_GT(replay.Rms(MultiSoundRow::Ssg1, 0), 200.0);
    EXPECT_GT(replay.Rms(MultiSoundRow::Ssg2, 0), 200.0);
    ExpectSilent(replay, {MultiSoundRow::Fm1, MultiSoundRow::Fm2, MultiSoundRow::Saa, MultiSoundRow::Pcm, MultiSoundRow::Midi});
    EXPECT_DIGEST(replay, MultiSoundRow::Ssg1, 0x4AB5FD7D7CBCCC54);
    EXPECT_DIGEST(replay, MultiSoundRow::Ssg2, 0x311D9568838B0376);
}

/// endregion </Ball Quest>

/// region <Wild Commander plugins on TS-Conf: VGMPLAY.WMF, GSPLAYER.WMF MIDI>

/// VGMPLAY.WMF writes without reading (no busy polling: it paces its writes, 110-123 T apart at 14 MHz): a YM2203
/// VGM on chip 0 (control byte #FA: FM on, register read mode), then an SAA VGM (#01FF / #00FF, the SAA clock started
/// by a control byte) (~5 ms)
TEST(MultiSoundTrace_Test, VgmPlayAgreesWithTheRtl)
{
    LoadedTrace trace;
    ASSERT_TRUE(Load("wc-vgmplay", trace));
    bool saaClock = false;
    EXPECT_TRUE(AgreesWithTheRtl(trace, {}, nullptr, [&](const MultiSoundCycle&, const MultiSoundCycleRecord& r) {
        saaClock = saaClock || r.saaClock;
    }));
    EXPECT_TRUE(saaClock) << "the plugin starts the SAA clock";
    EXPECT_EQ(FindPollLoops(trace.scenario, 0xC00F, 0xC00D).loops, 0u);
}

/// The trace replayed on a fresh card (~1 s, over the 50 ms budget: 1152 frames - Wild Commander's start from the SD
/// card, two tunes of about 3 s): FM and SSG of chip 0, then the SAA; chip 1 untouched
TEST(MultiSoundTrace_Test, VgmPlayReplaysOnTheCard)
{
    REPLAY_TRACE("wc-vgmplay", trace, replay);
    EXPECT_GT(replay.Rms(MultiSoundRow::Fm1, 0), 100.0);
    EXPECT_GT(replay.Rms(MultiSoundRow::Ssg1, 0), 50.0);
    EXPECT_GT(replay.Rms(MultiSoundRow::Saa, 0), 200.0);
    EXPECT_GT(replay.Rms(MultiSoundRow::Saa, 1), 200.0);
    ExpectSilent(replay, {MultiSoundRow::Fm2, MultiSoundRow::Ssg2, MultiSoundRow::Pcm, MultiSoundRow::Midi});
    EXPECT_DIGEST(replay, MultiSoundRow::Fm1, 0x81FC4AD816711B81);
    EXPECT_DIGEST(replay, MultiSoundRow::Ssg1, 0xD27924B370A4B770);
    EXPECT_DIGEST(replay, MultiSoundRow::Saa, 0x837BD4BB4BC2AA40);
}

/// GSPLAYER.WMF in MIDI mode bit-bangs scale.mid on chip 0's (U4) port A bit 2: R7 makes port A an output, then R14
/// once per bit, 107-114 T apart (31 250 baud = 112 T); the line, decoded from the logic's routing, carries the file
/// (~3 ms)
TEST(MultiSoundTrace_Test, WcMidiPlayerAgreesWithTheRtl)
{
    LoadedTrace trace;
    ASSERT_TRUE(Load("wc-midi", trace));
    int u4DataWrites = 0;
    EXPECT_TRUE(AgreesWithTheRtl(trace, {}, nullptr, [&](const MultiSoundCycle&, const MultiSoundCycleRecord& r) {
        u4DataWrites += (r.ymWrite == 1 && r.ymA0 == 1) ? 1 : 0;
    }));
    EXPECT_GE(u4DataWrites, 900) << "the bits go to U4";

    // scale.mid (generated, music/mid/ on the SD card): GM System On (SysEx F0 7E 7F 09 01 F7), program 0, volume
    // 100, then C4-C5 and back, each note on (velocity 100) and off (velocity 64). The plugin sends the SysEx's F0
    // only (it skips the body: plugin behavior); the line's one framing error is the break of the start-up, R7 makes
    // port A an output while R14 bit 2 is still 0
    std::vector<uint8_t> expected = { 0xF0, 0xC0, 0x00, 0xB0, 0x07, 0x64 };
    const uint8_t keys[] = { 60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60 };
    for (uint8_t key : keys)
        expected.insert(expected.end(), { 0x90, key, 0x64, 0x80, key, 0x40 });
    const MidiLineDecode line = DecodeU4MidiLine(trace, 112.0);
    EXPECT_EQ(line.bytes, expected);
    EXPECT_EQ(line.framingErrors, 1);
}

/// The trace replayed on a fresh card (~700 ms, over the 50 ms budget: 927 frames - Wild Commander's start, then
/// 4 s of the file): the synthesizer receives the line's 96 bytes, its only framing error the start-up break, and
/// plays them
TEST(MultiSoundTrace_Test, WcMidiPlayerReplaysOnTheCard)
{
    REPLAY_TRACE("wc-midi", trace, replay);
    sam2695::SynthReport synth;
    replay.Card().DescribeSynth(synth);
    EXPECT_EQ(synth.bytesReceived, 96u) << "the bytes the line carries (WcMidiPlayerAgreesWithTheRtl)";
    EXPECT_EQ(synth.framingErrors, 1u) << "the start-up break";
    EXPECT_GT(replay.Rms(MultiSoundRow::Midi, 0), 200.0);
    ExpectSilent(replay, {MultiSoundRow::Fm1, MultiSoundRow::Fm2, MultiSoundRow::Ssg1, MultiSoundRow::Ssg2,
                          MultiSoundRow::Saa, MultiSoundRow::Pcm});
    // no digest: the synthesizer is float, its samples differ in the last bits between libm / compilers (the macOS
    // golden failed under gcc); the level per 32768-frame block agrees to 0.01 dB on both
    const std::vector<double> levels = replay.LevelsDb(MultiSoundRow::Midi, 32768);
    const std::vector<double> golden = { -200.0, -200.0, -200.0, -200.0, -200.0, -200.0, -200.0, -200.0, -200.0, -200.0,
                                         -200.0, -200.0, -200.0, -200.0, -200.0, -200.0, -200.0, -200.0, -200.0, -200.0,
                                         60.34, 61.87, 60.76, 60.80, 61.88 };
    ASSERT_EQ(levels.size(), golden.size());
    for (size_t i = 0; i < golden.size(); i++)
        EXPECT_NEAR(levels[i], golden[i], 0.1) << "block " << i;
}

/// endregion </Wild Commander plugins>

#endif // UNREALNG_HAVE_SAM2695
