#pragma once

// A flat 64 KB test bus for the z84c15 library's core (z84cpu.h): memory, a
// port stub that reads the high byte of the port (FUSE's convention), a log of
// every bus event with its T-state, and an optional external /WAIT per memory
// cycle. Shared by the library tests in this folder.

#include <3rdparty/z84c15/z84cpu.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace Z84Test
{
struct Event
{
    char type;  ///< 'M' opcode fetch, 'P' operand fetch, 'R' data read, 'W' write, 'I' port in, 'O' port out, 'N' internal T
    uint16_t addr;
    uint8_t value;
    uint32_t t;
};

class TestBus
{
public:
    explicit TestBus(Z84CPU* cpu) : _cpu(cpu)
    {
        std::memset(memory, 0, sizeof(memory));
        Z84CpuSetMemoryBus(cpu, &Read, this, &Write, this);
        Z84CpuSetPortBus(cpu, &In, this, &Out, this);
    }

    /// Record the internal (no-MREQ) T-states as 'N' events (installs the contention hook)
    void TraceInternal()
    {
        Z84CpuSetContendFn(_cpu, &Contend, this);
    }

    void Load(uint16_t addr, const std::vector<uint8_t>& bytes)
    {
        for (uint8_t b : bytes)
            memory[addr++] = b;
    }

    uint8_t memory[0x10000];
    std::vector<Event> events;
    uint32_t externalWaitPerMemoryCycle = 0;  ///< the board's /WAIT on every memory cycle

private:
    static uint8_t Read(Z84CPU* cpu, uint16_t addr, Z84CpuAccessKind kind, void* user)
    {
        TestBus& bus = *static_cast<TestBus*>(user);
        const char type = kind == Z84CpuAccessM1 ? 'M' : (kind == Z84CpuAccessOperand ? 'P' : 'R');
        bus.events.push_back({type, addr, bus.memory[addr], Z84CpuTstates(cpu)});
        if (bus.externalWaitPerMemoryCycle)
            Z84CpuAddWaitStates(cpu, bus.externalWaitPerMemoryCycle);
        return bus.memory[addr];
    }

    static void Write(Z84CPU* cpu, uint16_t addr, uint8_t value, void* user)
    {
        TestBus& bus = *static_cast<TestBus*>(user);
        bus.events.push_back({'W', addr, value, Z84CpuTstates(cpu)});
        bus.memory[addr] = value;
        if (bus.externalWaitPerMemoryCycle)
            Z84CpuAddWaitStates(cpu, bus.externalWaitPerMemoryCycle);
    }

    static uint8_t In(Z84CPU* cpu, uint16_t port, void* user)
    {
        TestBus& bus = *static_cast<TestBus*>(user);
        const uint8_t value = static_cast<uint8_t>(port >> 8);
        bus.events.push_back({'I', port, value, Z84CpuTstates(cpu)});
        return value;
    }

    static void Out(Z84CPU* cpu, uint16_t port, uint8_t value, void* user)
    {
        static_cast<TestBus*>(user)->events.push_back({'O', port, value, Z84CpuTstates(cpu)});
    }

    static int Contend(Z84CPU* cpu, uint16_t addr, Z84CpuAccessKind kind, void* user)
    {
        if (kind == Z84CpuAccessInternal)
            static_cast<TestBus*>(user)->events.push_back({'N', addr, 0, Z84CpuTstates(cpu)});
        return 0;
    }

    Z84CPU* _cpu;
};
}  // namespace Z84Test
