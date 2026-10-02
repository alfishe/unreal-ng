#pragma once

#include <cstdint>

#include "3rdparty/z84c15/z84c15.h"
#include "emulator/cpu/z80.h"

class EmulatorContext;
class Memory;

/// Board logic between the CPU and the machine's memory paths that needs to
/// know the kind of each bus cycle (a block-transfer accelerator that snoops
/// opcodes and repeats data accesses: the Sprinter's, tdd-accel-sound-input §1).
/// Machine neutral; the engine calls it only while one is set.
///
/// Worked example: the Sprinter program runs `LD C,C : LD (HL),A`. The agent
/// sees the opcode fetch of #49 (fill armed), then BeforeWrite(HL, A) returns
/// A, the store goes out through the machine's normal write path, and
/// AfterWrite(HL, A) stores A at HL+1 ... HL+length-1 and adds the time.
class IZ84BusAgent
{
public:
    virtual ~IZ84BusAgent() = default;

    /// An opcode fetch (M1, also a halted CPU's) read `opcode`
    virtual void OnOpcodeFetch(uint16_t addr, uint8_t opcode) = 0;
    /// An operand or data read returned `value`; the result is what the CPU gets
    virtual uint8_t OnRead(uint16_t addr, uint8_t value) = 0;
    /// A write is about to go out with `value`; the result is the byte the bus carries
    virtual uint8_t BeforeWrite(uint16_t addr, uint8_t value) = 0;
    /// The write of `value` (the byte BeforeWrite returned) went out
    virtual void AfterWrite(uint16_t addr, uint8_t value) = 0;
    /// The CPU accepted an INT: its acknowledge cycle (M1 with /IORQ), before the pushes
    virtual void OnInterruptAcknowledge() = 0;

    /// false: the engine skips OnRead / BeforeWrite / AfterWrite (an idle agent costs one test per access)
    bool watchData = false;
};

/// @file z84c15engine.h
/// @brief The Zilog Z84C15 as a machine's CPU engine (ICpuEngine): the
/// vendored z84c15 library (core/src/3rdparty/z84c15) executing on this
/// machine's Z80 registers and time, every bus cycle through the machine's
/// normal paths (docs/inprogress/2026-10-01-z84c15-cpu-library/design.md §4).
///
/// What the adapter does around the library:
///   - registers: zero copy, the library executes on Z80Registers from pc to
///     nmi_in_progress (Z84CpuAttachRegisterFile; the layout is pinned by
///     static_asserts in the .cpp);
///   - time: the library's T is Z80::t, set before each step and acknowledge;
///     at every callback tt = T << 8 | t_l, and after it the library takes tt
///     back (a decoder's AddWaitStates - the board's /WAIT - or a mid-frame
///     clock switch);
///   - memory: an opcode fetch (M1, also a halted CPU's) is Z80::m1_cycle's
///     host half - machineM1Hook before and after, MemIf->MemoryReadM1;
///     operands and data go through MemIf->MemoryRead / MemoryWrite, so the
///     bus overlays (wait states, write intercepts) act as on the native core;
///   - ports: Z80::in / Z80::out (TTD journal, RZX, interceptor, the model
///     decoder, the port trace);
///   - boundary: Z80State::boundary and the library's boundary register are
///     synchronized around every step (a TTD restore or a reset writes the
///     host side);
///   - interrupts: as the CPU's IInterruptSource it puts the chip's daisy
///     chain in front of the board's own /INT (`external`); RETI reaches the
///     chain inside the library and the board through `external->OnReti`.
///
/// Machine neutral: the Sprinter is its first user (its port decoder, which
/// owns the chip - its on-chip ports and registers exist before the CPU is
/// wired - and installs this adapter).
///
/// Worked example: the Sprinter BIOS runs `OUT (#EE),A : OUT (#EF),A` with
/// A = 0 (WCR = 0). Both go through Z80::out into the Sprinter decoder, which
/// hands the on-chip ports to Chip().Write; the WCR write turns the chip's
/// programmed waits off for every later bus cycle.
class Z84C15Engine : public ICpuEngine
{
public:
    /// Wire `chip` (owned by the machine, outliving this adapter) to `cpu`
    Z84C15Engine(EmulatorContext* context, Z80* cpu, Z84Lib::Z84C15& chip);
    ~Z84C15Engine() override;
    Z84C15Engine(const Z84C15Engine&) = delete;
    Z84C15Engine& operator=(const Z84C15Engine&) = delete;

    /// Make this the CPU's engine and interrupt source; `external` is the
    /// board's own /INT logic (behind the on-chip daisy chain), may be null
    void Install(IInterruptSource* external);
    /// Give the CPU back to the native interpreter (only if this is installed)
    void Uninstall();
    bool IsInstalled() const;
    /// The host's registers were replaced from outside (a TTD restore): its boundary is pushed to the
    /// library at the next step even when it equals the one the library last reported
    void InvalidateBoundary() { _boundarySeen = 0xFF; }

    /// The board's bus agent (null = none): opcode fetches, data accesses, INT acknowledges
    void SetBusAgent(IZ84BusAgent* agent) { _agent = agent; }
    IZ84BusAgent* GetBusAgent() const { return _agent; }

    Z84Lib::Z84C15& Chip() { return _chip; }
    const Z84Lib::Z84C15& Chip() const { return _chip; }

    /// region <ICpuEngine>
    void ExecuteStep() override;
    void AcknowledgeInterrupt(uint8_t vector) override;
    void AcknowledgeNmi() override;
    /// endregion </ICpuEngine>

private:
    /// The CPU's interrupt source: the on-chip daisy chain, then the board's /INT
    class ChainSource : public IInterruptSource
    {
    public:
        explicit ChainSource(Z84C15Engine& engine) : _engine(engine) {}
        bool IsIntAsserted(uint32_t t) override;
        uint8_t AcknowledgeInterrupt(uint32_t t) override;
        void OnReti() override;

    private:
        Z84C15Engine& _engine;
    };

    /// Host -> library before a step or an acknowledge: T and, when the host changed it, the boundary
    void Enter();
    /// Library -> host after it: tt, the boundary, the decoded opcode, the HALT entry
    void Leave(bool wasHalted);
    /// tt from the library's T inside a callback, and back after it
    void Publish(Z84CPU* cpu);
    void Absorb(Z84CPU* cpu);

    static uint8_t MemRead(Z84CPU* cpu, uint16_t addr, Z84CpuAccessKind kind, void* user);
    static void MemWrite(Z84CPU* cpu, uint16_t addr, uint8_t value, void* user);
    static uint8_t PortIn(Z84CPU* cpu, uint16_t port, void* user);
    static void PortOut(Z84CPU* cpu, uint16_t port, uint8_t value, void* user);
    static uint8_t IntVector(Z84CPU* cpu, void* user);
    static void Reti(Z84CPU* cpu, void* user);

    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    Z84Lib::Z84C15& _chip;
    ChainSource _source{*this};
    IInterruptSource* _external = nullptr;
    IZ84BusAgent* _agent = nullptr;
    uint8_t _boundarySeen = Z80_BOUNDARY_NONE;  ///< the boundary the host last got from the library
    uint8_t _vector = 0xFF;                     ///< the data bus byte of the acknowledge in progress
};
