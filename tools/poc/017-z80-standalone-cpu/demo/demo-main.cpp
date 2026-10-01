// demo-main.cpp - standalone execution-loop demo for the extracted Z80 core.
//
// Builds a tiny hand-assembled guest program that prints a countdown and a
// message through RST 0x10 (captured by the HostMachine's soft-ROM), then runs
// it twice to demonstrate both bus modes of the library:
//   1. flat-memory fast path (Z80CpuAttachMemory)
//   2. callback bus (Z80CpuSetMemoryBus), the z80ex-style integration path
//
// Build & run:  cmake -S . -B build -G Ninja && ninja -C build && ./build/z80demo

#include <cstdio>
#include <cstring>

#include "hostmachine.h"

namespace
{
// Guest program at 0x8000. Assembled by hand:
//   8000: 3E 39     LD A,'9'
//   8002: D7        RST 10h          ; print A (soft-ROM captures it)
//   8003: 3D        DEC A
//   8004: FE 30     CP '0'
//   8006: 30 FA     JR NC,8002       ; print '9'..'0'
//   8008: 21 14 80  LD HL,msg(8014)
//   800B: 7E        LD A,(HL)
//   800C: B7        OR A
//   800D: 28 03     JR Z,8012
//   800F: D7        RST 10h
//   8010: 23        INC HL
//   8011: 18 F8     JR 800B
//   8012: 76        HALT
//   8014:           " STANDALONE Z80",00
const uint8_t kProgram[] = {
    0x3E, 0x39,                         // LD A,'9'
    0xD7,                               // RST 10h
    0x3D,                               // DEC A
    0xFE, 0x30,                         // CP '0'
    0x30, 0xFA,                         // JR NC,-6
    0x21, 0x14, 0x80,                   // LD HL,msg
    0x7E,                               // LD A,(HL)
    0xB7,                               // OR A
    0x28, 0x03,                         // JR Z,done
    0xD7,                               // RST 10h
    0x23,                               // INC HL
    0x18, 0xF8,                         // JR,-8
    0x76,                               // HALT
};
const char kMessage[] = " STANDALONE Z80";

const uint16_t kLoadAddress = 0x8000;

void LoadGuestProgram(HostMachine& host)
{
    host.LoadCode(kLoadAddress, kProgram, sizeof(kProgram));
    host.LoadCode(static_cast<uint16_t>(kLoadAddress + sizeof(kProgram)),
                  reinterpret_cast<const uint8_t*>(kMessage), sizeof(kMessage));
}

// --- callback-bus shims (demonstrate the z80ex-style integration path) ---

HostMachine* gHost = nullptr;

uint8_t CallbackMemRead(Z80CPU* /*cpu*/, uint16_t addr, int /*m1State*/, void* /*userData*/)
{
    return gHost->mem[addr];  // a real host would decode banking here
}

void CallbackMemWrite(Z80CPU* /*cpu*/, uint16_t addr, uint8_t value, void* /*userData*/)
{
    gHost->mem[addr] = value;
}
}  // namespace

int main()
{
    printf("PoC 017 standalone Z80 demo - %s\n\n", Z80CpuVersion());

    HostMachine host;
    gHost = &host;

    // --- Run 1: flat-memory fast path -------------------------------------
    host.Reset();
    LoadGuestProgram(host);
    Z80CpuSetReg(host.cpu, Z80CpuRegPc, kLoadAddress);
    Z80CpuSetReg(host.cpu, Z80CpuRegSp, 0xFFF0);

    const uint32_t t0 = Z80CpuTstates(host.cpu);
    while (!Z80CpuHalted(host.cpu))
        host.StepCapture();
    const uint32_t flatT = Z80CpuTstates(host.cpu) - t0;
    const std::string flatOutput = host.output;

    printf("[flat memory]   output: \"%s\"\n", flatOutput.c_str());
    printf("                %llu instructions, %u T-states (%.1f us at 3.5 MHz)\n\n",
           static_cast<unsigned long long>(host.instructions), flatT, flatT / 3.5);

    // --- Run 2: callback bus (z80ex-style) --------------------------------
    Z80CpuAttachMemory(host.cpu, nullptr);  // detach flat memory, use callbacks
    Z80CpuSetMemoryBus(host.cpu, CallbackMemRead, nullptr, CallbackMemWrite, nullptr);

    host.Reset();
    LoadGuestProgram(host);
    Z80CpuSetReg(host.cpu, Z80CpuRegPc, kLoadAddress);
    Z80CpuSetReg(host.cpu, Z80CpuRegSp, 0xFFF0);

    const uint32_t t1 = Z80CpuTstates(host.cpu);
    while (!Z80CpuHalted(host.cpu))
        host.StepCapture();
    const uint32_t cbT = Z80CpuTstates(host.cpu) - t1;

    printf("[callback bus]  output: \"%s\"\n", host.output.c_str());
    printf("                %llu instructions, %u T-states\n\n",
           static_cast<unsigned long long>(host.instructions), cbT);

    const bool identical = (flatOutput == host.output) && flatT == cbT;
    printf("bus modes produce %s results (output %s, T-states %s)\n",
           identical ? "identical" : "DIFFERENT",
           flatOutput == host.output ? "match" : "MISMATCH",
           flatT == cbT ? "match" : "MISMATCH");

    // --- Register introspection -------------------------------------------
    printf("final PC=%04Xh SP=%04Xh AF=%04Xh HALT=%d\n",
           Z80CpuGetReg(host.cpu, Z80CpuRegPc), Z80CpuGetReg(host.cpu, Z80CpuRegSp),
           Z80CpuGetReg(host.cpu, Z80CpuRegAf), Z80CpuHalted(host.cpu));

    return identical ? 0 : 1;
}
