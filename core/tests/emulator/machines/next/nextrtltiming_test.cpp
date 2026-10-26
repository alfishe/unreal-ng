// ZX Spectrum Next CPU timing against the RTL (POC 036, tools/poc/036-next-cpu-timing-rtl). PinnedCasesTakeTheRtlsTime runs the test
// programs and holds their instruction durations to the RTL's (nextrtltiming_pins.h, from the GHDL simulation of the core): contention
// of memory, ports and internal T-states on the 48K, 128K and +3 timings, NEXTREG in contended memory, the DMA's bus hand-over, the
// NR #08 switch. TraceOfATestRom runs a test ROM of gen.py the way the simulation runs it, and writes the instruction starts and port
// cycles for compare.py. Driven by the environment, skipped without it:
//   UNREAL_NEXT_RTL_ROM    the 16K image (gen.py <name>.bin)
//   UNREAL_NEXT_RTL_TRACE  the output
//   UNREAL_NEXT_RTL_NR03   the NR #03 value of the run (#91 48K, #A2 128K, #B3 +3)
//   UNREAL_NEXT_RTL_START  "<body address>:<pc>:<T after the INT>": the body's run address, and the instruction the run starts at with
//                          its time, as the RTL trace has it (the prologue's last HALT: the INT acceptance is compared too)
//   UNREAL_NEXT_RTL_FRAMES frames to run from there (default 2)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "nextrtltiming_pins.h"

namespace
{
    /// A machine on the case's timing, the boot ROM off: NR #03 out of config mode, as the testbench's injected write, takes effect at the
    /// frame end
    Emulator* MachineOn(uint8_t nr03)
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        if (!emulator)
            return nullptr;
        auto* ports = dynamic_cast<PortDecoder_Next*>(emulator->GetContext()->pPortDecoder);
        ports->DecodePortOut(0x243B, 0x03, 0);
        ports->DecodePortOut(0x253B, nr03, 0);
        for (int i = 0; i < 2; i++)
            emulator->RunFrame(true);
        return emulator;
    }
}  // namespace

TEST(NextRtlTiming_Test, PinnedCasesTakeTheRtlsTime)
{
    std::vector<uint8_t> machines;
    for (const NextRtlCase& c : NextRtlCases())
        if (std::find(machines.begin(), machines.end(), c.nr03) == machines.end())
            machines.push_back(c.nr03);
    for (uint8_t nr03 : machines)
    {
        Emulator* emulator = MachineOn(nr03);
        ASSERT_NE(emulator, nullptr);
        EmulatorContext* context = emulator->GetContext();
        Z80* z80 = context->pCore->GetZ80();
        auto* memory = dynamic_cast<NextMemory*>(context->pMemory);
        ASSERT_FALSE(memory->BootRomEnabled());
        // every case starts from the same RAM (banks 0-7) and I: a body writes bank 5, mixir loads I
        constexpr uint16_t kPages = 16;
        std::vector<uint8_t> ram(kPages * NextMemory::kSlotSize);
        for (uint16_t p = 0; p < kPages; p++)
            std::copy_n(memory->RAMPageAddress(p), NextMemory::kSlotSize, ram.begin() + p * NextMemory::kSlotSize);
        const uint8_t i = z80->i;
        for (const NextRtlCase& c : NextRtlCases())
        {
            if (c.nr03 != nr03)
                continue;
            for (uint16_t p = 0; p < kPages; p++)
                std::copy_n(ram.begin() + p * NextMemory::kSlotSize, NextMemory::kSlotSize, memory->RAMPageAddress(p));
            z80->i = i;
            // the body as the prologue leaves it: SP in bank 2, IM 1, interrupts on (no INT comes before the windows end)
            for (size_t i = 0; i < c.body.size(); i++)
                memory->PokeSlot(static_cast<uint16_t>(c.runAt + i), c.body[i]);
            z80->pc = c.runAt;
            z80->sp = 0xBFF0;
            z80->im = 1;
            z80->iff1 = z80->iff2 = 1;
            z80->halted = 0;
            z80->t = context->config.intstart + c.startT;
            // an instruction's start: its opcode fetch (a fetch's wait comes before M1 on the 48K / 128K, inside it on the +3: the fetch
            // is where both agree)
            std::vector<uint32_t> fetches;
            int fetchPc = -1;
            z80->m1TraceHook = [&](uint16_t pc) { fetchPc = pc; };
            z80->busTraceHook = [&](char type, uint16_t addr, uint8_t) {
                if (type == 'R' && addr == fetchPc)
                {
                    fetches.push_back(z80->t);
                    fetchPc = -1;
                }
            };
            uint32_t needed = 0;
            for (const NextRtlWindow& w : c.windows)
                needed = std::max<uint32_t>(needed, w.first + static_cast<uint32_t>(w.durations.size()) + 1);
            while (fetches.size() < needed && z80->t < context->config.frame)
                z80->EngineStep();
            z80->m1TraceHook = nullptr;
            z80->busTraceHook = nullptr;
            ASSERT_GE(fetches.size(), needed) << c.name;
            for (const NextRtlWindow& w : c.windows)
            {
                unsigned differing = 0;
                for (size_t k = 0; k < w.durations.size(); k++)
                {
                    const uint32_t ours = fetches[w.first + k + 1] - fetches[w.first + k];
                    if (ours != w.durations[k] && differing++ < 3)
                        ADD_FAILURE() << c.name << ": instruction " << w.first + k << " takes " << ours << " T, the RTL " << w.durations[k];
                }
                EXPECT_EQ(differing, 0u) << c.name << ": the window from instruction " << w.first;
            }
        }
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

TEST(NextRtlTiming_Test, TraceOfATestRom)
{
    const char* romPath = std::getenv("UNREAL_NEXT_RTL_ROM");
    const char* tracePath = std::getenv("UNREAL_NEXT_RTL_TRACE");
    const char* nr03 = std::getenv("UNREAL_NEXT_RTL_NR03");
    const char* start = std::getenv("UNREAL_NEXT_RTL_START");
    if (!romPath || !tracePath || !nr03 || !start)
        GTEST_SKIP() << "POC 036: set UNREAL_NEXT_RTL_ROM / _TRACE / _NR03 / _START";
    const char* framesEnv = std::getenv("UNREAL_NEXT_RTL_FRAMES");
    const int frames = framesEnv ? std::atoi(framesEnv) : 2;

    std::vector<uint8_t> rom(0x4000);
    FILE* f = std::fopen(romPath, "rb");
    ASSERT_NE(f, nullptr) << romPath;
    ASSERT_EQ(std::fread(rom.data(), 1, rom.size(), f), rom.size());
    std::fclose(f);
    unsigned runAt = 0, startPc = 0, startT = 0;
    ASSERT_EQ(std::sscanf(start, "%x:%x:%u", &runAt, &startPc, &startT), 3);

    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    Z80* z80 = context->pCore->GetZ80();
    auto* memory = dynamic_cast<NextMemory*>(context->pMemory);
    auto* ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    ASSERT_NE(memory, nullptr);
    ASSERT_NE(ports, nullptr);

    // NR #03 out of config mode, as the testbench's injected write; the timing takes effect at the frame end
    ports->DecodePortOut(0x243B, 0x03, 0);
    ports->DecodePortOut(0x253B, static_cast<uint8_t>(std::strtoul(nr03, nullptr, 16)), 0);
    for (int i = 0; i < 2; i++)
        emulator->RunFrame(true);
    ASSERT_FALSE(memory->BootRomEnabled());
    for (uint8_t r = 0; r < 4; r++)
        std::copy(rom.begin(), rom.end(), memory->ROMPageHostAddress(r));
    // the body as the prologue leaves it: copied to its address, SP in bank 2, IM 1, interrupts on (the handler's EI)
    constexpr unsigned kBodySource = 0x0100;
    if (runAt != kBodySource)
        for (unsigned i = 0; kBodySource + i < rom.size(); i++)
            memory->PokeSlot(static_cast<uint16_t>(runAt + i), rom[kBodySource + i]);
    z80->pc = static_cast<uint16_t>(startPc);
    z80->sp = 0xBFF0;
    z80->im = 1;
    z80->iff1 = z80->iff2 = 1;
    z80->halted = 0;
    const uint32_t intStart = context->config.intstart;
    z80->t = intStart + startT;

    FILE* out = std::fopen(tracePath, "w");
    ASSERT_NE(out, nullptr) << tracePath;
    uint64_t frameBase = 0;
    const uint32_t frameT = context->config.frame;
    // the instruction start, and its opcode fetch ("F"): the RTL holds the clock before M1 falls, ours waits inside the instruction
    int fetchPc = -1;
    z80->m1TraceHook = [&](uint16_t pc) {
        std::fprintf(out, "M %llu %04X\n", static_cast<unsigned long long>(frameBase + z80->t - intStart), pc);
        fetchPc = pc;
    };
    z80->busTraceHook = [&](char type, uint16_t addr, uint8_t value) {
        if (type == 'R' && addr == fetchPc)
        {
            std::fprintf(out, "F %llu %04X\n", static_cast<unsigned long long>(frameBase + z80->t - intStart), addr);
            fetchPc = -1;
        }
        if (type == 'I' || type == 'O' || (std::getenv("UNREAL_NEXT_RTL_MEM") && (type == 'R' || type == 'W')))
            std::fprintf(out, "%c %llu %04X %02X\n", type, static_cast<unsigned long long>(frameBase + z80->t - intStart), addr, value);
    };
    for (int i = 0; i < frames; i++)
    {
        emulator->RunFrame(true);
        frameBase += frameT;
        std::fprintf(out, "INT %llu\n", static_cast<unsigned long long>(frameBase));
    }
    z80->m1TraceHook = nullptr;
    z80->busTraceHook = nullptr;
    std::fclose(out);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
