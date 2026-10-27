// Real-board acceptance of the Z80N library: runs the ZXSpectrumNextTests programs !Z80N.snx and !Z80Nc2.snx (their
// results were checked on real boards, photographs in the repository; source https://github.com/MrKWatkins/ZXSpectrumNextTests)
// on a bare host: 48K of RAM, a NextREG file behind ports #243B / #253B and the NEXTREG instructions, no keyboard, no video.
// The programs auto-start when NextREG #07 reads non-zero, run their "partial" tests and leave a result byte per instruction in
// a table; the test reads the table. The two images are pinned by CRC32 and their table addresses come from assembling the
// sources with sjasmplus (byte-identical to the release files).
//
// Provisioned: UNREAL_NEXT_TESTS names a clone (or copy) of ZXSpectrumNextTests that has release/!Z80N.snx and
// release/!Z80Nc2.snx; without it the tests skip. A run takes some seconds (the programs are designed for 28 MHz boards):
// this is an acceptance run, not a unit test.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <3rdparty/unreal-next-z80/z80ncpu.h>

namespace
{
constexpr uint8_t kResultErr = 0;
constexpr uint8_t kResultNone = 3;

uint32_t Crc32(const std::vector<uint8_t>& data)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t b : data)
    {
        crc ^= b;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/// A bare 48K host with the NextREG file
struct Host
{
    Host()
    {
        cpu = Z80nCpuCreate();
        Z80nCpuSetMemoryBus(cpu, &Read, this, &Write, this);
        Z80nCpuSetPortBus(cpu, &In, this, &Out, this);
        Z80nCpuSetNextRegFn(cpu, &NextReg, this);
        std::memset(mem, 0, sizeof(mem));
        std::memset(nr, 0, sizeof(nr));
        nr[0x07] = 0x03;  // "turbo is on": the programs auto-start
    }
    ~Host() { Z80nCpuDestroy(cpu); }

    static uint8_t Read(Z80nCPU*, uint16_t addr, Z80nCpuAccessKind, void* u) { return static_cast<Host*>(u)->mem[addr]; }
    static void Write(Z80nCPU*, uint16_t addr, uint8_t v, void* u)
    {
        if (addr >= 0x4000)
            static_cast<Host*>(u)->mem[addr] = v;  // ROM area is not writable
    }
    static uint8_t In(Z80nCPU*, uint16_t port, void* u)
    {
        Host& h = *static_cast<Host*>(u);
        if (port == 0x243B)
            return h.select;
        if (port == 0x253B)
            return h.nr[h.select];
        return 0xFF;  // no key pressed, nothing else answers
    }
    static void Out(Z80nCPU*, uint16_t port, uint8_t v, void* u)
    {
        Host& h = *static_cast<Host*>(u);
        if (port == 0x243B)
            h.select = v;
        else if (port == 0x253B)
            h.nr[h.select] = v;
    }
    static void NextReg(Z80nCPU*, uint8_t reg, uint8_t v, void* u) { static_cast<Host*>(u)->nr[reg] = v; }

    /// A 48K .sna / .snx: header of 27 bytes, 48K of RAM from #4000; PC is on the stack
    bool LoadSna(const std::vector<uint8_t>& sna)
    {
        if (sna.size() != 49179)
            return false;
        std::memcpy(mem + 0x4000, sna.data() + 27, 0xC000);
        Z80nCpuRegisters r{};
        auto w = [&](int o) { return static_cast<uint16_t>(sna[o] | sna[o + 1] << 8); };
        r.i = sna[0];
        r.hlAlt = w(1);
        r.deAlt = w(3);
        r.bcAlt = w(5);
        r.afAlt = w(7);
        r.hl = w(9);
        r.de = w(11);
        r.bc = w(13);
        r.iy = w(15);
        r.ix = w(17);
        r.iff1 = r.iff2 = (sna[19] >> 2) & 1;
        r.r = sna[20];
        r.af = w(21);
        r.sp = w(23);
        r.im = sna[25];
        const uint16_t pc = static_cast<uint16_t>(mem[r.sp] | mem[r.sp + 1] << 8);
        r.sp = static_cast<uint16_t>(r.sp + 2);
        r.pc = pc;
        Z80nCpuSetRegisters(cpu, &r);
        Z80nCpuSetTstates(cpu, 0);
        return true;
    }

    Z80nCPU* cpu = nullptr;
    uint8_t mem[0x10000];
    uint8_t nr[256];
    uint8_t select = 0;
};

struct Program
{
    const char* file;
    uint32_t crc;
    uint16_t detailsAddr;  // InstructionsData_Details (4 bytes per instruction, the result is byte 1)
    int count;
};

std::string ProvisionedDir()
{
    const char* dir = std::getenv("UNREAL_NEXT_TESTS");
    return dir ? dir : "";
}

void RunProgram(const Program& p)
{
    const std::string dir = ProvisionedDir();
    if (dir.empty())
        GTEST_SKIP() << "UNREAL_NEXT_TESTS names no ZXSpectrumNextTests folder";
    const std::vector<uint8_t> sna = ReadFile(dir + "/release/" + p.file);
    if (sna.empty())
        GTEST_SKIP() << dir << "/release/" << p.file << " not found";
    ASSERT_EQ(Crc32(sna), p.crc) << p.file << " is another build: the table address in this test does not apply";

    Host host;
    ASSERT_TRUE(host.LoadSna(sna));

    uint64_t steps = 0;
    bool finished = false;
    while (!finished && steps < 800000000ull)
    {
        for (int i = 0; i < 100000; i++)
            Z80nCpuStep(host.cpu);
        steps += 100000;
        finished = true;
        for (int k = 0; k < p.count; k++)
            if (host.mem[p.detailsAddr + 4 * k + 1] == kResultNone)
                finished = false;
    }
    ASSERT_TRUE(finished) << p.file << ": not finished after " << steps << " instructions";

    std::string report;
    int errors = 0;
    for (int k = 0; k < p.count; k++)
    {
        const uint8_t r = host.mem[p.detailsAddr + 4 * k + 1];
        if (r == kResultErr)
        {
            errors++;
            report += " #" + std::to_string(k);
        }
    }
    EXPECT_EQ(errors, 0) << p.file << ": ERR for the instructions with index" << report << " (the program's error log is in its screen";
    printf("[   NOTE   ] %s finished after %llu instructions, T = %u\n", p.file, static_cast<unsigned long long>(steps),
           Z80nCpuTstates(host.cpu));
}
}  // namespace

// The 23 instructions of the Z80N program (core 3.x). The programs' own judgement: no "ERR" in the result table.
TEST(Z80nRealBoard_Test, Z80nProgramShowsNoErrors)
{
    RunProgram({"!Z80N.snx", 0x3a35226a, 0x88B1, 23});
}

// The six instructions added in core 2.00.22+ (barrel shifts and JP (C))
TEST(Z80nRealBoard_Test, Z80nc2ProgramShowsNoErrors)
{
    RunProgram({"!Z80Nc2.snx", 0x71240419, 0x8779, 6});
}
