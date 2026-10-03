// Sprinter Sp2000 BIOS 3.04 against the MAME reference captures
// (testdata/machines/sprinter/reference/, Sprinter roadmap §6 "S1 against MAME").
//
// What is pinned here, from MAME's `sprinter` driver (0.289, BIOS 3.04, no media):
//   - the first 10 000 port accesses after power-on (ports.csv): direction, port,
//     value, PC and the port table's internal code, in order, and their timing;
//   - the BIOS logo's palette in video RAM, frame by frame (palette.csv);
//   - the INT position of the three FN_SYNC modes (int.csv) and the INT
//     acknowledge 6-11 T after it.
//
// Time base. MAME ends the PLD load after 4 096 writes (its shortcut) and starts
// the BIOS 58 225 T after power-on; unreal-ng's fast start starts it at T 0. So
// the trace is compared relative to the BIOS's first port access (PC #0156), and
// a frame-synchronous event (the palette, the INT) is compared by frame number,
// where the start offset (0.81 frame) can move a frame-synced step by one frame.
//
// All tests boot the real ROM: seconds of emulated time; the turbo mode is on
// because no assertion looks at rendered pixels (the palette is read from video RAM).

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include <emulator/ports/models/portdecoder_sprinter.h>
#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "pch.h"
#include "sprinterfixture.h"
#include "stdafx.h"

namespace
{
/// One row of ports.csv (MAME's trace)
struct MameAccess
{
    double timeUs = 0;
    bool isRead = false;
    uint16_t port = 0;
    uint8_t value = 0;
    uint16_t pc = 0;
    std::string phase;  ///< loader, closed, open
    std::string code;   ///< "" (loader), "z84", or two hex digits
};

std::string ReferencePath(const char* name)
{
    return (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "sprinter" / "reference" / name).string();
}

std::vector<std::string> SplitCsv(const std::string& line)
{
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ','))
        fields.push_back(field);
    if (!line.empty() && line.back() == ',')
        fields.emplace_back();
    return fields;
}

std::vector<MameAccess> LoadMamePorts()
{
    std::vector<MameAccess> rows;
    std::ifstream in(ReferencePath("ports.csv"));
    std::string line;
    std::getline(in, line);  // header: n,frame,time_us,dir,port,value,pc,phase,index,code
    while (std::getline(in, line))
    {
        const std::vector<std::string> f = SplitCsv(line);
        if (f.size() < 10)
            continue;
        MameAccess row;
        row.timeUs = std::stod(f[2]);
        row.isRead = f[3] == "R";
        row.port = static_cast<uint16_t>(std::stoul(f[4], nullptr, 16));
        row.value = static_cast<uint8_t>(std::stoul(f[5], nullptr, 16));
        row.pc = static_cast<uint16_t>(std::stoul(f[6], nullptr, 16));
        row.phase = f[7];
        row.code = f[9];
        rows.push_back(row);
    }
    return rows;
}

/// palette.csv: frame -> sum, only the frames where the sum changed
std::map<int, uint32_t> LoadMamePalette()
{
    std::map<int, uint32_t> rows;
    std::ifstream in(ReferencePath("palette.csv"));
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line))
    {
        const std::vector<std::string> f = SplitCsv(line);
        if (f.size() >= 2)
            rows[std::stoi(f[0])] = static_cast<uint32_t>(std::stoul(f[1]));
    }
    return rows;
}
}  // namespace

class SprinterReference_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;
    Z80* _z80 = nullptr;

    void SetUp() override
    {
        if (!SprinterFixture::Rom304Available())
            GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";

        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);

        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-reference", "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _z80 = _context->pCore->GetZ80();
        _decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC
        ASSERT_TRUE(SprinterFixture::SelectBios(_context, "sp2k-3.04.rom"));  // pinned to 3.04 (shipped default: 3.07 BETA 1)

        _context->config.sprinter.fast_start = 1;
        _emulator->Reset();
        // No assertion looks at rendered pixels
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        _emulator.reset();
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
    }

    /// The palette bytes of video RAM summed: the last 32 bytes of every 1 KB line
    /// (MAME vram_w: laddr >= #3E0), as mame-capture.lua "palette" sums them
    uint32_t PaletteSum() const
    {
        const uint8_t* vram = _decoder->GetVideoRam().Data();
        uint32_t sum = 0;
        for (uint32_t line = 0; line < 256; line++)
            for (uint32_t offset = 0x3E0; offset < 0x400; offset++)
                sum += vram[line * 1024 + offset];
        return sum;
    }

    /// Run whole frames to the end of frame `frame` (1-based, MAME's numbering: frame n ends at n x 20.48 ms)
    void RunToEndOfFrame(uint64_t frame)
    {
        while (_context->emulatorState.frame_counter < frame)
            EmulatorTestHelper::RunFramesFast(_emulator.get(), 1);
    }

    /// Step the CPU from the start of the next frame to the interrupt routine's first
    /// instruction; returns its frame position in 3.5 MHz T-states, or -1 if not reached
    double MeasureIntAcknowledge(uint16_t isr)
    {
        const uint32_t multiplier = _context->emulatorState.current_z80_frequency_multiplier
                                        ? _context->emulatorState.current_z80_frequency_multiplier
                                        : 1;
        const uint32_t limit = 71000u * multiplier;
        for (int guard = 0; guard < 400000 && _z80->t < limit; guard++)
        {
            _z80->StepInstruction();  // with the INT sampling (Z80Step is the bare instruction)
            if (_z80->pc == isr)
                return static_cast<double>(_z80->t) / multiplier;
        }
        return -1;
    }

    /// Call BIOS function FN_SYNC (#F2) with A = `mode` the way mame-capture.lua does:
    /// a stub below the stack saves the registers, LD A,n : LD C,#F2 : RST #18, sets
    /// a done flag, restores the registers and returns to the interrupted code
    bool CallFnSync(uint8_t mode)
    {
        Memory* memory = _context->pMemory;
        const uint16_t sp = _z80->sp;
        const uint16_t stub = static_cast<uint16_t>(sp - 0x400);
        const uint16_t flag = static_cast<uint16_t>(stub + 0x40);
        const std::vector<uint8_t> code = {0xF5, 0xC5, 0xD5, 0xE5, 0xDD, 0xE5, 0xFD, 0xE5,  // PUSH AF..IY
                                           0x3E, mode, 0x0E, 0xF2, 0xDF,                    // LD A,n : LD C,#F2 : RST #18
                                           0x3E, 0xA5, 0x32, static_cast<uint8_t>(flag), static_cast<uint8_t>(flag >> 8),
                                           0xFD, 0xE1, 0xDD, 0xE1, 0xE1, 0xD1, 0xC1, 0xF1, 0xC9};  // POP IY..AF : RET
        for (size_t i = 0; i < code.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(stub + i), code[i]);
        memory->DirectWriteToZ80Memory(flag, 0x00);
        // A halted CPU leaves the HALT first, as an interrupt would: the call returns past it. Left
        // halted, the Z84C15 core keeps running HALT M1s at the stub's address until the next INT
        // and then resumes one byte into the stub (the native core ran the stub at once, but its INT
        // acknowledge then skipped a byte of whatever code was running)
        uint16_t ret = _z80->pc;
        if (_z80->halted & 1)
            ret = static_cast<uint16_t>(ret + 1);
        _z80->halted = 0;
        _z80->sp = static_cast<uint16_t>(sp - 2);
        memory->DirectWriteToZ80Memory(_z80->sp, static_cast<uint8_t>(ret));
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(_z80->sp + 1), static_cast<uint8_t>(ret >> 8));
        _z80->pc = stub;

        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return memory->DirectReadFromZ80Memory(flag) == 0xA5; }, 20, 1);
        return memory->DirectReadFromZ80Memory(flag) == 0xA5;
    }
};

// ports.csv: the first 10 000 port accesses equal MAME's - the same accesses in the
// same order with the same internal codes, at the same time (plus the Z84C15's own
// waits before the BIOS turns them off, which MAME does not model).
// Boot-bound (~40 frames of BIOS POST, partly at 21 MHz): the turbo mode is on
TEST_F(SprinterReference_Test, Bios304_PortTraceMatchesMame)
{
    std::vector<MameAccess> mame = LoadMamePorts();
    ASSERT_EQ(mame.size(), 10000u) << ReferencePath("ports.csv");
    // The loader's 11 accesses are not in a fast start (FastStart() sets their result)
    std::vector<MameAccess> bios;
    for (const MameAccess& row : mame)
        if (row.phase != "loader")
            bios.push_back(row);
    ASSERT_EQ(bios.size(), 9989u);

    ASSERT_TRUE(_emulator->GetFeatureManager()->setFeature(Features::kPortTrace, true));
    PortDiagnosticRecorder* recorder = _decoder->getPortTraceRecorder();
    ASSERT_NE(recorder, nullptr);
    recorder->setCapacity(bios.size());
    recorder->setOverflowMode(PortTraceOverflowMode::StopWhenFull);
    recorder->start();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return recorder->eventCount() >= bios.size(); }, 60, 1);
    const std::vector<PortTraceEvent> events = recorder->getAll();
    ASSERT_EQ(events.size(), bios.size());

    // The base-clock (3.5 MHz) time of each access: the turbo switch (code #C6 / #CE,
    // bit 1 set, bit 0 = turbo) rescales the CPU clocks of the frame by 6 from that write on
    std::vector<double> unrealT(events.size());
    uint32_t multiplier = 1;
    size_t turboRow = 0;
    for (size_t i = 0; i < events.size(); i++)
    {
        const PortTraceEvent& e = events[i];
        if (e.isOut() && (e.internalCode == 0xC6 || e.internalCode == 0xCE) && (e.value & 0x02))
        {
            multiplier = (e.value & 0x01) ? 6 : 1;
            if (multiplier == 6 && turboRow == 0)
                turboRow = i;
        }
        const uint64_t tInFrame = e.timestamp - static_cast<uint64_t>(e.frameNumber) * _context->config.frame;
        unrealT[i] = static_cast<double>(e.frameNumber) * _context->config.frame + static_cast<double>(tInFrame) / multiplier;
    }
    ASSERT_GT(turboRow, 0u) << "the BIOS never switched the turbo on";

    // The Z84C15's wait generator (research-cpu-z84c15.md section 4.1, the CPU library's design
    // section 6): the fast start leaves WCR = #04 as the loader does, so the BIOS runs with one
    // memory wait per cycle (M1s included) until InitCpuPorts writes WCR = 0 - access 7,
    // OUT (#EF),A at #016C. MAME stores WCR and never applies it. Between the BIOS's first port
    // access and that write the BIOS makes 22 memory cycles (e.g. access 3 -> 4: XOR A, then
    // OUT (#1D),A's M1 and operand: 3 waits), so every later access is 22 T further from the
    // first one than in MAME; before it the distance grows by the waits of each step
    size_t wcrRow = 0;
    for (size_t i = 1; i < bios.size() && !wcrRow; i++)
        if (!bios[i].isRead && (bios[i].port & 0xFF) == 0xEF && bios[i - 1].port == 0xFFEE && bios[i - 1].value == 0x00)
            wcrRow = i;
    ASSERT_EQ(wcrRow, 6u) << "InitCpuPorts' WCR write";
    ASSERT_EQ(bios[wcrRow].value, 0x00);
    constexpr double kWcrShift = 22.0;

    const double mame0 = bios[0].timeUs * 3.5;
    const double unreal0 = unrealT[0];
    size_t mismatches = 0;
    double lastShift = 0;
    std::string report;
    for (size_t i = 0; i < events.size(); i++)
    {
        const PortTraceEvent& e = events[i];
        const MameAccess& m = bios[i];
        std::string code;
        if (e.internalCode != PortTraceCode::kNone)
            code = e.internalCode >= PortDecoder_Sprinter::kTraceZ84Base ? "z84" : StringHelper::Format("%02X", e.internalCode);
        // MAME prints the table byte for every access; unreal-ng names a code only once the decoder is open
        const bool codeChecked = m.phase == "open" || m.code == "z84";
        const bool same = e.isOut() == !m.isRead && e.rawPort == m.port && e.value == m.value && e.pc == m.pc &&
                          (!codeChecked || code == m.code);
        if (!same && mismatches++ < 10)
            report += StringHelper::Format("\n  #%zu MAME %s %04X=%02X PC %04X code %s, unreal-ng %s %04X=%02X PC %04X code %s", i + 1,
                                           m.isRead ? "R" : "W", m.port, m.value, m.pc, m.code.c_str(), e.isOut() ? "W" : "R",
                                           e.rawPort, e.value, e.pc, code.c_str());

        // At 3.5 MHz (until the turbo switch) the time from the BIOS start is MAME's to the T-state plus
        // the chip's waits (above). MAME's timestamps are exact there; tolerance 1 T for the
        // microsecond rounding of the CSV
        const double shift = (unrealT[i] - unreal0) - (m.timeUs * 3.5 - mame0);
        if (i < turboRow && i > wcrRow)
            EXPECT_NEAR(shift, kWcrShift, 1.0) << "access #" << i + 1 << " PC " << std::hex << m.pc;
        else if (i <= wcrRow)
        {
            EXPECT_GE(shift, lastShift - 1.0) << "access #" << i + 1 << ": the waits only add";
            EXPECT_LE(shift, kWcrShift + 1.0) << "access #" << i + 1;
            lastShift = shift;
        }
    }
    EXPECT_EQ(mismatches, 0u) << report;

    // "DCP opened": the first IN after the configuration, page 8 #0CD8, 2 312 334 T
    // (0.660 667 s) after the BIOS's first access, frame 33 in MAME (0.677 332 s from power-on)
    EXPECT_EQ(_decoder->DcpOpenedPc(), 0x0CD8);

    // At 21 MHz: MAME's timestamp jumps back 4.77 ms at the turbo switch (set_clock_scale
    // in the middle of a time slice re-reads the cycles already run at the new clock), so
    // the durations from the access after the switch to the last one are compared: the
    // two differ only by the PLD wait unreal-ng adds to Z84C15 port writes (MAME adds
    // none, 16 writes x 6 clocks = 4.6 us) and the clock rounding of MAME's timestamps.
    // Before the port wait was taken at the start of the I/O cycle the difference was 1 245 us
    const size_t from = turboRow + 1;
    const size_t last = events.size() - 1;
    const double mameUs = bios[last].timeUs - bios[from].timeUs;
    const double unrealUs = (unrealT[last] - unrealT[from]) / 3.5;
    EXPECT_NEAR(unrealUs, mameUs, 20.0) << "21 MHz part: MAME " << mameUs << " us, unreal-ng " << unrealUs << " us";
}

// palette.csv: the BIOS logo's palette reaches its full value at frame 58 and fades one
// step per frame (to frame 186), frame by frame equal to MAME (the logo.png frame 60 =
// sum 302 548). The build-up (frames 55-57) depends on the start offset, see the top.
// Checked to frame 100: 43 fade steps; the rest repeats the same loop.
// Boot-bound (100 frames of BIOS logo): the turbo mode is on, the palette is read from video RAM
TEST_F(SprinterReference_Test, Bios304_LogoPaletteMatchesMame)
{
    const std::map<int, uint32_t> mame = LoadMamePalette();
    ASSERT_FALSE(mame.empty()) << ReferencePath("palette.csv");
    ASSERT_EQ(mame.count(58), 1u);
    ASSERT_EQ(mame.count(100), 1u);

    uint32_t expected = 0;
    for (int frame = 1; frame <= 100; frame++)
    {
        RunToEndOfFrame(static_cast<uint64_t>(frame));
        const auto it = mame.find(frame);
        if (it != mame.end())
            expected = it->second;
        if (frame >= 58)
            ASSERT_EQ(PaletteSum(), expected) << "frame " << frame;
    }
    EXPECT_EQ(mame.at(60), 302548u);

    // The INT the BIOS sets up for the logo: the Scorpion position (BIOS 3.04 cold start), int.csv "booted"
    const std::vector<uint32_t>& positions = _decoder->GetIntSource().Positions();
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0], 60896u);
}

// int.csv: FN_SYNC (#F2) puts the INT at MAME's line 271 / 287 / 295, x 768 for Scorpion /
// Pentagon / Spectrum (60 896 / 64 480 / 66 272 T), and the interrupt routine starts
// 6-11 T after it in MAME (the instruction in progress at 21 MHz plus the acknowledge).
// Boot-bound (~60 frames to the logo, where the BIOS runs in IM 2): the turbo mode is on
TEST_F(SprinterReference_Test, Bios304_FnSyncIntPositionsMatchMame)
{
    RunToEndOfFrame(62);
    ASSERT_EQ(_z80->im, 2) << "the BIOS logo runs in IM 2";
    const uint16_t vector = static_cast<uint16_t>(_z80->i << 8 | 0xFF);
    const uint16_t isr = static_cast<uint16_t>(_context->pMemory->DirectReadFromZ80Memory(vector) |
                                               _context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(vector + 1)) << 8);

    struct Mode
    {
        uint8_t a;
        uint32_t position;
    };
    for (const Mode mode : {Mode{1, 60896}, Mode{2, 64480}, Mode{3, 66272}, Mode{0, 66272}})
    {
        ASSERT_TRUE(CallFnSync(mode.a)) << "FN_SYNC A=" << int(mode.a) << " did not return";
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 1);

        const std::vector<uint32_t>& positions = _decoder->GetIntSource().Positions();
        ASSERT_EQ(positions.size(), 1u) << "A=" << int(mode.a);
        EXPECT_EQ(positions[0], mode.position) << "A=" << int(mode.a);

        // The acknowledge: MAME measured the routine's first fetch 6.5-10.5 T after the edge over
        // 25 frames (it depends on the instruction in progress); unreal-ng gives 7-8 T
        const double ack = MeasureIntAcknowledge(isr);
        ASSERT_GE(ack, 0) << "A=" << int(mode.a) << ": no interrupt in the frame";
        EXPECT_GE(ack - mode.position, 6.0) << "A=" << int(mode.a);
        EXPECT_LE(ack - mode.position, 11.0) << "A=" << int(mode.a);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 1);
    }
}
