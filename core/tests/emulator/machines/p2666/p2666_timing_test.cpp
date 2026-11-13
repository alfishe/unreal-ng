// Pentagon 2.666 LE CPU timing against the board's own VHDL (tools/poc/051-p2666-hdl-timing-oracle).
//
// The oracle ran PaE.vhd's clock, I/O and INT logic with the T80 core over test programs at 3.5, 14 and 28 MHz and
// logged every M1 with its mainclk (114 MHz) edge. The emulator runs the same programs and must start every
// instruction on the same edge: the turbo clocks, the I/O machine cycles stretched to the 3.5 MHz grid, the clock
// switches at the end of an I/O cycle, the INT pulse of 32 CPU clock rising edges and its acceptance.
//
// Golden files (testdata/machines/p2666/timing, made by golden.py of the POC): `<name>.bin` the program, mapped as
// the testbench maps it (0x0000..0x3FFF = the Gluk ROM page the board boots in, 0x4000.. = RAM pages 5, 2, 0);
// `<name>.trace` the oracle's M1 / INTA / IOR / IOW / INT lines inside the `WIN` windows.
//
// Edges: the emulator's CPU clock t at multiplier u (8 = 28 MHz, 4 = 14, 1 = 3.5) rises on mainclk edge
// t * 32 / u (+ 2 at 28 MHz) of the emulator frame, whose origin is kFrameOriginShift edges before the board's.
// One allowed difference: the M1 right after an I/O cycle at 28 MHz. The board's T1 rises on the slow grid (edge
// 0 mod 32) and lasts 6 edges; the emulator keeps the 28 MHz grid and starts that T1 2 edges later, so every later
// edge is the board's again (PortDecoderP2666::IoCycleTiming).

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/ports/models/portdecoder_p2666.h"
#include "pch.h"
#include "stdafx.h"

namespace
{
struct OracleTrace
{
    unsigned frames = 0;
    std::vector<std::pair<uint64_t, uint64_t>> windows;
    std::set<std::pair<uint64_t, uint16_t>> m1;  // (edge, address)
    std::vector<uint64_t> inta;
};

bool LoadTrace(const std::string& path, OracleTrace& trace)
{
    std::ifstream in(path);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream s(line);
        std::string kind;
        s >> kind;
        if (kind == "FRAMES")
            s >> trace.frames;
        else if (kind == "WIN")
        {
            uint64_t a = 0;
            uint64_t b = 0;
            s >> a >> b;
            trace.windows.emplace_back(a, b);
        }
        else if (kind == "M1")
        {
            uint64_t edge = 0;
            std::string addr;
            s >> edge >> addr;
            trace.m1.emplace(edge, static_cast<uint16_t>(std::stoul(addr, nullptr, 16)));
        }
        else if (kind == "INTA")
        {
            uint64_t edge = 0;
            s >> edge;
            trace.inta.push_back(edge);
        }
    }
    return trace.frames > 0 && !trace.m1.empty();
}

struct Start
{
    uint64_t edge;
    uint16_t pc;
    uint8_t multiplier;
};
}  // namespace

class P2666Timing_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void RunAgainstOracle(const std::string& name)
    {
        OracleTrace trace;
        const std::string base = TestPathHelper::GetTestDataPath("machines/p2666/timing/" + name);
        ASSERT_TRUE(LoadTrace(base + ".trace", trace)) << base << ".trace";
        std::ifstream binFile(base + ".bin", std::ios::binary);
        ASSERT_TRUE(binFile) << base << ".bin";
        const std::vector<uint8_t> program((std::istreambuf_iterator<char>(binFile)), std::istreambuf_iterator<char>());

        _emulator = EmulatorTestHelper::CreateStandardEmulator("P2666", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        EmulatorContext* context = _emulator->GetContext();
        EmulatorState& state = context->emulatorState;
        Z80* z80 = context->pCore->GetZ80();
        Memory* memory = context->pMemory;
        ASSERT_EQ(context->config.mem_model, MM_P2666);
        ASSERT_EQ(state.current_z80_frequency_multiplier, 8) << "the board powers on at 28 MHz";
        ASSERT_TRUE(state.flags & CF_TRDOS) << "the board starts in Gluk with DOS on";

        // The testbench's memory: the program over zeros, the ROM window writable (the programs never write it)
        uint8_t* pages[4] = {memory->ROMPageHostAddress(0), memory->RAMPageAddress(5), memory->RAMPageAddress(2),
                             memory->RAMPageAddress(0)};
        for (uint8_t* page : pages)
        {
            ASSERT_NE(page, nullptr);
            std::fill(page, page + 0x4000, 0);
        }
        for (size_t a = 0; a < program.size() && a < 0x10000; a++)
            pages[a >> 14][a & 0x3FFF] = program[a];

        // The oracle's CPU leaves reset with its first T1 rising on board edge 70 (28 MHz)
        const uint64_t firstEdge = 70;
        const uint32_t originShift = PortDecoderP2666::kFrameOriginShift;
        z80->pc = 0;
        z80->t = static_cast<uint32_t>((firstEdge + originShift - 2) / 4);
        const uint32_t startFrame = state.frame_counter;

        std::vector<Start> starts;
        starts.reserve(1 << 20);
        uint16_t lastPc = 0xFFFF;
        z80->m1TraceHook = [&](uint16_t pc) {
            if (pc == lastPc)
                return;  // a HALT's idle fetches: the trace keeps the first
            lastPc = pc;
            const uint8_t u = state.current_z80_frequency_multiplier;
            const uint64_t edge = static_cast<uint64_t>(state.frame_counter - startFrame) * PortDecoderP2666::kFrameEdges +
                                  static_cast<uint64_t>(z80->t) * (32u / u) + (u == 8 ? 2u : 0u) - originShift;
            starts.push_back({edge, pc, u});
        };

        for (unsigned f = 0; f <= trace.frames; f++)
            _emulator->RunFrame();
        z80->m1TraceHook = nullptr;

        auto inWindow = [&](uint64_t edge) {
            for (const auto& w : trace.windows)
                if (edge >= w.first && edge <= w.second)
                    return true;
            return false;
        };

        const uint64_t traceEnd = static_cast<uint64_t>(trace.frames + 1) * PortDecoderP2666::kFrameEdges - 2000;
        size_t compared = 0;
        size_t shifted = 0;
        size_t intAccepted = 0;
        int failures = 0;
        uint16_t prevPc = 0xFFFF;
        for (const Start& s : starts)
        {
            if (s.pc == 0x0038 && prevPc != 0x0037)
                intAccepted++;
            prevPc = s.pc;
            if (!inWindow(s.edge) || s.edge >= traceEnd)
                continue;
            compared++;
            if (trace.m1.count({s.edge, s.pc}))
                continue;
            // The first M1 after an I/O cycle at 28 MHz: the board's T1 rose 2 edges earlier, on the slow grid
            if (s.multiplier == 8 && s.edge >= 2 && (s.edge - 2) % 32 == 0 && trace.m1.count({s.edge - 2, s.pc}))
            {
                shifted++;
                continue;
            }
            if (++failures <= 10)
            {
                auto near = trace.m1.lower_bound({s.edge > 200 ? s.edge - 200 : 0, 0});
                std::string around;
                for (int i = 0; i < 8 && near != trace.m1.end(); i++, ++near)
                {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), " %llu:%04X", static_cast<unsigned long long>(near->first), near->second);
                    around += buf;
                }
                ADD_FAILURE() << name << ": instruction at " << std::hex << s.pc << std::dec << " starts on edge " << s.edge
                              << " (x" << int(s.multiplier) << "), not in the oracle; oracle near:" << around;
            }
        }
        EXPECT_EQ(failures, 0) << name << ": " << failures << " of " << compared << " instruction starts off the oracle";
        EXPECT_GT(compared, 200u) << name;
        EXPECT_EQ(intAccepted, trace.inta.size()) << name << ": interrupts taken";
        RecordProperty("compared", static_cast<int>(compared));
        RecordProperty("shifted", static_cast<int>(shifted));
    }
};

/// Every speed and every switch between them, an I/O mix (IN / OUT n, (C), INI / OUTI / INIR / OTIR), HALT and IM 1
TEST_F(P2666Timing_Test, SpeedsAndSwitchesIm1MatchTheHdl)
{
    RunAgainstOracle("speeds_im1");
}

/// The same with IM 2 (the vector byte is #FF)
TEST_F(P2666Timing_Test, SpeedsAndSwitchesIm2MatchTheHdl)
{
    RunAgainstOracle("speeds_im2");
}

/// The INT pulse while the CPU runs: I/O loops at 28 / 14 / 3.5 MHz (slow cycles inside the pulse lengthen it) and
/// NOP loops
TEST_F(P2666Timing_Test, IntDuringIoAndNopLoopsMatchesTheHdl)
{
    RunAgainstOracle("busy_im1");
}
