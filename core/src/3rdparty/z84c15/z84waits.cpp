// z84waits.cpp - the Z84C15's wait-state generator, applied by the bus
// primitives (PS0182 p. 318-320; research-cpu-z84c15.md section 4.1;
// docs/inprogress/2026-10-01-z84c15-cpu-library/design.md section 6).
//
// WCR bits: 7-6 daisy-chain waits in INTA (0/2/4/6) and the RETI extension,
// 5 one vector wait in INTA, 4 one more wait in every M1, 3-2 memory waits
// (0-3) inside the MWBR range, 1-0 I/O waits (0/2/4/6), never on the on-chip
// ports. MWBR: memory waits where MWBR[7:4] >= A15-A12 >= MWBR[3:0].
//
// Worked example: WCR = #04 (the PLD loader), MWBR = #F0: LD (DE),A is
// 7 + 2 = 9 T (its M1 and its write wait once each).

#include "z84cpu-internal.h"

namespace Z84Lib
{

namespace
{
constexpr uint8_t kPairWaits[4] = {0, 2, 4, 6};  // the 2-bit fields: INTA daisy chain, I/O
constexpr uint8_t kRetiWaits[4] = {0, 0, 2, 4};  // ED 4D, by WCR[7:6]

int MemoryWaits(const Z84CPU* cpu, uint16_t addr)
{
    const uint8_t block = static_cast<uint8_t>(addr >> 12);
    const uint8_t mwbr = cpu->wait.mwbr;
    if (block > (mwbr >> 4) || block < (mwbr & 0x0F))
        return 0;
    return (cpu->wait.wcr >> 2) & 0x03;
}
}  // namespace

bool Z84OnChipPort(uint16_t port)
{
    const uint8_t low = static_cast<uint8_t>(port);
    return (low >= 0x10 && low <= 0x13) || (low >= 0x18 && low <= 0x1F) || low == 0xEE || low == 0xEF ||
           low == 0xF0 || low == 0xF1 || low == 0xF4;
}

int Z84WaitMemory(Z84CPU* cpu, uint16_t addr)
{
    return MemoryWaits(cpu, addr);
}

int Z84WaitM1(Z84CPU* cpu, uint16_t addr, uint8_t opcode)
{
    Z84CPU::Z84WaitGen& w = cpu->wait;
    int waits = MemoryWaits(cpu, addr) + ((w.wcr >> 4) & 1);

    // RETI extension: the M1 right after an ED fetch. ED 4D gets 0/0/2/4,
    // another byte after ED gets 1 with WCR[7:6] = 2 or 3
    const uint8_t chain = static_cast<uint8_t>(w.wcr >> 6);
    if (w.afterEd)
    {
        waits += (opcode == 0x4D) ? kRetiWaits[chain] : (chain >= 2 ? 1 : 0);
        w.afterEd = 0;
    }
    else if (opcode == 0xED && chain != 0)
    {
        w.afterEd = 1;
    }

    // Power-on window: WCR acts as #FF for the first 15 M1 cycles unless written
    if (w.powerOnM1Left && --w.powerOnM1Left == 0)
    {
        w.wcr = w.wcrProgrammed;
        w.active = w.wcr != 0;
        w.afterEd = 0;
    }
    return waits;
}

int Z84WaitIo(Z84CPU* cpu, uint16_t port)
{
    if (Z84OnChipPort(port))
        return 0;
    return kPairWaits[cpu->wait.wcr & 0x03];
}

int Z84WaitInta(const Z84CPU* cpu)
{
    if (!cpu->wait.active)
        return 0;
    const uint8_t wcr = cpu->wait.wcr;
    return kPairWaits[wcr >> 6] + ((wcr >> 5) & 1);
}

}  // namespace Z84Lib
