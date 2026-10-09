#pragma once

#include <cstdint>

#include "3rdparty/unreal-next-z80/z80ncpu.h"
#include "emulator/cpu/z80.h"

class EmulatorContext;
class Memory;

/// The machine's side of the NEXTREG instructions: `NEXTREG n,A` and `NEXTREG n,nn` write NextREG n without a port cycle and
/// without touching the select latch (design-core.md section 3). Called in the middle of the instruction; the machine may
/// rebuild its slot table or change the CPU speed there, and the engine takes the clock back afterwards
/// Where the stackless NMI keeps the return address (NR #C2 / #C3)
class INmiReturnStore
{
public:
    virtual ~INmiReturnStore() = default;
    virtual void StoreNmiReturn(uint8_t low, uint8_t high) = 0;
    virtual uint16_t LoadNmiReturn() const = 0;
};

class INextRegHost
{
public:
    virtual ~INextRegHost() = default;
    virtual void WriteNextReg(uint8_t reg, uint8_t value) = 0;
    /// After every instruction, on the CPU's thread between two instructions: where a NEXTREG-requested reset runs
    virtual void AfterInstruction() {}
    /// A RETN completed (ED 45 and its aliases): z80_retn_seen of zxnext.vhd - the DivMMC leaves its automap
    virtual void OnRetn() {}
};

/// @file z80nengine.h
/// @brief The ZX Spectrum Next's Z80N as a machine's CPU engine (ICpuEngine): the vendored unreal-next-z80 library executing on
/// this machine's Z80 registers and time, every bus cycle through the machine's normal paths
/// (docs/inprogress/2026-10-07-zx-next/design-integration.md section 4).
///
/// Built after the Z84C15 adapter (io/z84c15): zero-copy register file (the layout is pinned by static_asserts), the library's T
/// is Z80::t set before each step and acknowledge and taken back after every callback (a decoder's AddWaitStates or a mid-frame
/// speed switch), opcode fetches are Z80::m1_cycle's host half (machineM1Hook before and after, MemIf->MemoryReadM1), operands and
/// data go through MemIf->MemoryRead / MemoryWrite so the bus overlays act as on the native core, ports through Z80::in / Z80::out
/// (TTD journal, interceptor, the model decoder, the port trace), the instruction boundary is synchronized around every step.
/// What it adds: the NEXTREG instructions call INextRegHost; the maskable interrupt source stays the host's (the frame interrupt
/// until the Next's IM2 chain exists), RETI is forwarded to it.
///
/// Machine neutral by interface, Next by content: PortDecoderNext owns and installs it.
///
/// CPU-LIBRARY-MIGRATION(engine-template): Z84C15Engine and this class share the register contract, time mapping and bus
/// callbacks; the design plans one header-only template for both once the second user is stable.
class Z80NEngine : public ICpuEngine
{
public:
    Z80NEngine(EmulatorContext* context, Z80* cpu);
    ~Z80NEngine() override;
    Z80NEngine(const Z80NEngine&) = delete;
    Z80NEngine& operator=(const Z80NEngine&) = delete;

    /// Make this the CPU's engine
    void Install();
    /// Give the CPU back to the native interpreter (only if this is installed)
    void Uninstall();
    bool IsInstalled() const;
    /// The host's registers were replaced from outside (a TTD restore): its boundary is pushed to the library at the next step
    void InvalidateBoundary() { _boundarySeen = 0xFF; }

    /// NR #C0 bit 3: the NMI acknowledge writes no stack, the return address goes to `store`; RETN reads it back from there
    void SetStacklessNmi(bool on, INmiReturnStore* store);
    /// The machine that handles NEXTREG instructions (null: they are dropped)
    void SetNextRegHost(INextRegHost* host) { _nextRegHost = host; }

    /// The library, for the machine's configuration calls (variant switches of the CPU model)
    Z80nCPU* Cpu() { return _cpu; }

    /// region <ICpuEngine>
    void ExecuteStep() override;
    void AcknowledgeInterrupt(uint8_t vector) override;
    void AcknowledgeNmi() override;
    /// endregion </ICpuEngine>

private:
    /// Host -> library before a step or an acknowledge: T and, when the host changed it, the boundary
    void Enter();
    /// Library -> host after it: tt, the boundary, the decoded opcode, the HALT entry
    void Leave(bool wasHalted);
    void Publish(Z80nCPU* cpu);
    void Absorb(Z80nCPU* cpu);

    static uint8_t MemRead(Z80nCPU* cpu, uint16_t addr, Z80nCpuAccessKind kind, void* user);
    static void MemWrite(Z80nCPU* cpu, uint16_t addr, uint8_t value, void* user);
    static uint8_t PortIn(Z80nCPU* cpu, uint16_t port, void* user);
    static void PortOut(Z80nCPU* cpu, uint16_t port, uint8_t value, void* user);
    static uint8_t IntVector(Z80nCPU* cpu, void* user);
    static void Reti(Z80nCPU* cpu, void* user);
    static void Retn(Z80nCPU* cpu, void* user);
    static void NmiStore(Z80nCPU* cpu, uint8_t low, uint8_t high, void* user);
    static uint16_t NmiLoad(Z80nCPU* cpu, void* user);
    static void NextReg(Z80nCPU* cpu, uint8_t reg, uint8_t value, void* user);

    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    Z80nCPU* _cpu = nullptr;
    INextRegHost* _nextRegHost = nullptr;
    INmiReturnStore* _nmiStore = nullptr;
    uint8_t _boundarySeen = Z80_BOUNDARY_NONE;  ///< the boundary the host last got from the library
    uint8_t _vector = 0xFF;                     ///< the data bus byte of the acknowledge in progress
};
