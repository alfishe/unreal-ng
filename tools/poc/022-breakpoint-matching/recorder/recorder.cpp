// Records access traces of real programs for the matcher experiments: a 128K Spectrum (ROM 0/1, eight RAM
// pages, #7FFD paging, the frame INT) on the vendored unreal-z80 core, started from a .sna snapshot. Every
// instruction start, opcode / operand fetch, data read, write, port IN / OUT and slot remap becomes one event.
//
// Usage: recorder <rom128> <snapshot.sna> <out.trace> [instructions]
// Keys are never pressed (port #FE reads #FF): games sit in their menu / attract loop, demos play.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../../../core/src/3rdparty/unreal-z80/z80cpu.h"
#include "../common/trace.h"

namespace
{
struct Machine
{
    uint8_t rom[2][0x4000];
    uint8_t ram[8][0x4000];
    uint8_t port7ffd = 0;
    bool locked = false;
    poc::PhysPage slots[4];
    uint8_t* readPtr[4];
    uint8_t* writePtr[4];
    uint8_t trash[0x4000];
    poc::Trace trace;
    bool recording = true;

    void Map()
    {
        const uint8_t romPage = (port7ffd >> 4) & 1;
        const poc::PhysPage want[4] = {poc::MakePhys(poc::kRom, romPage), poc::MakePhys(poc::kRam, 5),
                                       poc::MakePhys(poc::kRam, 2), poc::MakePhys(poc::kRam, port7ffd & 7)};
        for (int s = 0; s < 4; s++)
        {
            if (want[s] != slots[s] && recording)
                trace.events.push_back({static_cast<uint16_t>(s), want[s], poc::KRemap});
            slots[s] = want[s];
        }
        readPtr[0] = rom[romPage];
        writePtr[0] = trash;
        readPtr[1] = writePtr[1] = ram[5];
        readPtr[2] = writePtr[2] = ram[2];
        readPtr[3] = writePtr[3] = ram[port7ffd & 7];
    }

    void Push(uint16_t addr, uint16_t value, poc::Kind kind)
    {
        if (recording)
            trace.events.push_back({addr, value, static_cast<uint8_t>(kind)});
    }
};

uint8_t Read(Z80CPU*, uint16_t addr, int m1State, void* user)
{
    auto* m = static_cast<Machine*>(user);
    const uint8_t v = m->readPtr[addr >> 14][addr & 0x3FFF];
    m->Push(addr, v, m1State ? poc::KFetch : poc::KRead);
    return v;
}

void Write(Z80CPU*, uint16_t addr, uint8_t value, void* user)
{
    auto* m = static_cast<Machine*>(user);
    m->writePtr[addr >> 14][addr & 0x3FFF] = value;
    m->Push(addr, value, poc::KWrite);
}

uint8_t In(Z80CPU*, uint16_t port, void* user)
{
    auto* m = static_cast<Machine*>(user);
    m->Push(port, 0xFF, poc::KIn);
    return 0xFF;
}

void Out(Z80CPU*, uint16_t port, uint8_t value, void* user)
{
    auto* m = static_cast<Machine*>(user);
    m->Push(port, value, poc::KOut);
    // 128K decode: A15 = 0, A1 = 0
    if (!(port & 0x8002) && !m->locked)
    {
        m->port7ffd = value;
        m->locked = value & 0x20;
        m->Map();
    }
}

bool ReadFile(const char* path, std::vector<uint8_t>& data)
{
    FILE* f = std::fopen(path, "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    data.resize(static_cast<size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    const bool ok = std::fread(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <rom128> <snapshot.sna> <out.trace> [instructions]\n", argv[0]);
        return 2;
    }
    const uint64_t instructions = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 2000000;

    std::vector<uint8_t> romData, sna;
    if (!ReadFile(argv[1], romData) || romData.size() < 0x8000 || !ReadFile(argv[2], sna))
    {
        std::fprintf(stderr, "cannot read the ROM (32K) or the snapshot\n");
        return 1;
    }
    static Machine m;
    std::memcpy(m.rom[0], romData.data(), 0x4000);
    std::memcpy(m.rom[1], romData.data() + 0x4000, 0x4000);

    // .sna: 27-byte header, 48K of RAM (pages 5, 2, current); 128K adds PC, #7FFD, TR-DOS flag and the rest
    if (sna.size() != 49179 && sna.size() != 131103 && sna.size() != 147487)
    {
        std::fprintf(stderr, "not a 48K / 128K .sna (%zu bytes)\n", sna.size());
        return 1;
    }
    const uint8_t* h = sna.data();
    const bool is128 = sna.size() != 49179;
    m.port7ffd = is128 ? sna[49181] : 0x30;  // a 48K snapshot runs with the 48 BASIC ROM, paging locked
    m.locked = !is128 || (m.port7ffd & 0x20);
    std::memcpy(m.ram[5], &sna[27], 0x4000);
    std::memcpy(m.ram[2], &sna[27 + 0x4000], 0x4000);
    std::memcpy(m.ram[m.port7ffd & 7], &sna[27 + 0x8000], 0x4000);
    if (is128)
    {
        size_t at = 49183;
        for (int p = 0; p < 8; p++)
        {
            if (p == 5 || p == 2 || p == (m.port7ffd & 7))
                continue;
            if (at + 0x4000 <= sna.size())
                std::memcpy(m.ram[p], &sna[at], 0x4000);
            at += 0x4000;
        }
    }
    m.recording = false;
    for (int s = 0; s < 4; s++)
        m.slots[s] = 0xFFFF;
    m.Map();
    m.recording = true;
    for (int s = 0; s < 4; s++)
        m.trace.initialSlots[s] = m.slots[s];

    Z80CPU* cpu = Z80CpuCreate();
    Z80CpuSetMemoryBus(cpu, Read, &m, Write, &m);
    Z80CpuSetPortBus(cpu, In, &m, Out, &m);
    Z80CpuRegisters r{};
    r.i = h[0];
    r.hlAlt = static_cast<uint16_t>(h[1] | (h[2] << 8));
    r.deAlt = static_cast<uint16_t>(h[3] | (h[4] << 8));
    r.bcAlt = static_cast<uint16_t>(h[5] | (h[6] << 8));
    r.afAlt = static_cast<uint16_t>(h[7] | (h[8] << 8));
    r.hl = static_cast<uint16_t>(h[9] | (h[10] << 8));
    r.de = static_cast<uint16_t>(h[11] | (h[12] << 8));
    r.bc = static_cast<uint16_t>(h[13] | (h[14] << 8));
    r.iy = static_cast<uint16_t>(h[15] | (h[16] << 8));
    r.ix = static_cast<uint16_t>(h[17] | (h[18] << 8));
    r.iff1 = r.iff2 = (h[19] & 4) ? 1 : 0;
    r.r = h[20];
    r.af = static_cast<uint16_t>(h[21] | (h[22] << 8));
    r.sp = static_cast<uint16_t>(h[23] | (h[24] << 8));
    r.im = h[25] & 3;
    if (is128)
    {
        r.pc = static_cast<uint16_t>(sna[49179] | (sna[49180] << 8));
    }
    else
    {
        // 48K: PC is on the stack
        const uint16_t sp = r.sp;
        r.pc = static_cast<uint16_t>(m.readPtr[sp >> 14][sp & 0x3FFF] | (m.readPtr[(sp + 1) >> 14 & 3][(sp + 1) & 0x3FFF] << 8));
        r.sp = static_cast<uint16_t>(sp + 2);
    }
    Z80CpuSetRegisters(cpu, &r);

    m.trace.events.reserve(instructions * 4);
    constexpr uint32_t kFrame = 70908;  // 128K frame
    uint32_t frameT = 0;
    for (uint64_t i = 0; i < instructions; i++)
    {
        m.Push(Z80CpuGetReg(cpu, Z80CpuRegPc), 0, poc::KExec);
        frameT += static_cast<uint32_t>(Z80CpuStep(cpu));
        if (frameT >= kFrame)
        {
            frameT -= kFrame;
            frameT += static_cast<uint32_t>(Z80CpuInt(cpu));
        }
    }
    Z80CpuDestroy(cpu);

    if (!m.trace.Save(argv[3]))
    {
        std::fprintf(stderr, "cannot write %s\n", argv[3]);
        return 1;
    }
    size_t counts[poc::KCount] = {};
    for (const poc::Event& e : m.trace.events)
        counts[e.kind]++;
    std::printf("%s: %zu events (exec %zu, fetch %zu, read %zu, write %zu, in %zu, out %zu, remap %zu)\n", argv[3],
                m.trace.events.size(), counts[poc::KExec], counts[poc::KFetch], counts[poc::KRead], counts[poc::KWrite],
                counts[poc::KIn], counts[poc::KOut], counts[poc::KRemap]);
    return 0;
}
