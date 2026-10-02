#pragma once

#include <cstdint>

#include "emulator/io/z84c15/z84c15engine.h"

class EmulatorContext;
class SprinterMemory;
struct SprinterPldState;

/// The accelerator's state (Sprinter tdd-accel-sound-input §1.1). Plain
/// fixed-width fields, no padding: the TTD blob of phase S7 is the struct.
/// Names follow the PLD source (ACCELER.TDF) and MAME (sprinter.cpp).
struct SprinterAccelState
{
    uint8_t buffer[256];  ///< the PLD's line buffer (LPM_RAM_DP), indexed by the down-counter (or XCNT)

    uint8_t mode;         ///< ACC_MODE[2..0]: the low 3 bits of the last same-register LD (0 = off)
    uint8_t dir;          ///< ACC_DIR, from `mode` (kDir): bit 0 on, 1 buffer, 2 block, 3 length, 4 vertical, 5 horizontal, 6 double
    uint8_t fn;           ///< FN_ACC[2..0], set by every opcode fetch: 3 AND, 2 XOR, 1 OR, 0 plain
    uint8_t length;       ///< RGACC: accesses per operation, 0 = 256
    uint8_t prefix;       ///< PRF_CMD: the last opcode fetch was CB / DD / ED / FD
    uint8_t edSeen;       ///< ED_CMD: the last opcode fetch was ED
    uint8_t reti;         ///< RETI: the last two opcode fetches were ED 4D
    uint8_t blocked;      ///< ACC_BLK = 0: an INT acknowledge stopped new operations until the M1 after RETI
    uint8_t alt;          ///< ALT_ACC: a write to code #C7 / #CF switched the buffer index to XCNT (until reset)
    uint8_t xcnt;         ///< XCNT: buffer index in the alternate addressing
    uint8_t xagr;         ///< XAGR: its fraction
    uint8_t reserved0;
    uint16_t aagr;        ///< AAGR[9..0]: the step added to XCNT:XAGR per access
    uint16_t reserved1;
    uint32_t operations;  ///< block operations started (statistics for the debugger and automation)
    uint32_t lastExtraClocks;  ///< CPU clocks the last block operation added
};

static_assert(sizeof(SprinterAccelState) == 256 + 16 + 8, "SprinterAccelState must stay padding-free (TTD blob)");

/// The Sprinter's block accelerator, the Standard PLD configuration's
/// (Sprinter tdd-accel-sound-input §1; hardware-reference §7; MAN §6; PLD
/// ACCELER.TDF; MAME sprinter.cpp accel_control_r / acc_tick / check_accel).
///
/// Control: every opcode fetch (not after a CB/DD/ED/FD prefix, ALL_MODE bit 0
/// set) that is a same-register `LD r,r` sets the mode:
///   #40 LD B,B off        #49 LD C,C fill         #52 LD D,D set length
///   #5B LD E,E vert. fill #64 LD H,H double byte  #6D LD L,L copy
///   #7F LD A,A vert. copy #76 HALT     off
/// and every opcode fetch sets the logic function from its byte: 10xxx110-style
/// ALU opcodes give FN = ~xxx (#A6 AND, #AE XOR, #B6 OR, #BE plain; #86 / #8E
/// / #96 alias AND / XOR / OR as in the PLD), any other opcode plain.
///
/// Data: the next operand or data access (not M1) while a mode is armed is
/// repeated `length` times (0 = 256): address +1 per access (vertical: the
/// address stays, PORT_Y +1 per access). Reads load the buffer (copy modes:
/// buffer = fn(buffer, byte)) and the CPU gets the last byte read; writes store
/// the CPU's byte (fill) or the buffer (copy). Every store goes through
/// SprinterMemory's normal write path (graphics pages, VRAM shadow); accesses
/// to a window that is not main RAM are skipped (MAME accel_mem_r / _w).
/// The CPU's own access is the first of the `length`; the other length - 1
/// take 6 clocks of 42 MHz each (7 MB/s, MAN §6), added to the instruction as
/// wait states: 3 CPU clocks per access at 21 MHz, 1/2 at 3.5 MHz (rounded up
/// per operation).
///
/// INT suspend ([SPRINTER] AccelIntSuspend=1, default; the PLD's ACC_BLK): an
/// INT acknowledge blocks new operations; the first opcode fetch after RETI
/// (ED 4D) unblocks; the mode is kept; RETN does not unblock; NMI does not block.
///
/// Worked example: `LD D,D : LD A,(HL)` with (HL) = 4 sets the length 4;
/// `LD C,C : LD (IX+0),A` with A = #AA stores #AA at IX, IX+1, IX+2, IX+3 and
/// adds 9 CPU clocks at 21 MHz (3 extra accesses x 6 / 2).
class SprinterAccelerator : public IZ84BusAgent
{
public:
    static constexpr uint8_t kDirOn = 0x01;
    static constexpr uint8_t kDirBuffer = 0x02;
    static constexpr uint8_t kDirBlock = 0x04;
    static constexpr uint8_t kDirLength = 0x08;
    static constexpr uint8_t kDirVertical = 0x10;
    static constexpr uint8_t kDirDouble = 0x40;

    /// ACC_DIR per mode (ACCELER.TDF, MAME accel_control_r)
    static constexpr uint8_t kDir[8] = {0x00, 0x25, 0x09, 0x15, 0x41, 0x27, 0x00, 0x17};

    /// 42 MHz clocks per accelerator access
    static constexpr uint32_t kTicks42PerAccess = 6;

    SprinterAccelerator(EmulatorContext* context, SprinterPldState& pld);

    void AttachMemory(SprinterMemory* memory) { _memory = memory; }

    /// The PLD's /RESET: mode off, unblocked, alternate addressing off; the buffer and the length stay
    void Reset();

    /// A write to code #C7 / #CF: the alternate buffer addressing (AAGR = A9 A8 D7..D0, XCNT = A15..A10)
    void OnScaleWrite(uint16_t port, uint8_t value);

    SprinterAccelState& State() { return _state; }
    const SprinterAccelState& State() const { return _state; }

    /// Whether ALL_MODE bit 0 enables the accelerator
    bool IsEnabled() const;

    /// Accesses per operation for a length register value (0 = 256)
    static uint32_t Accesses(uint8_t length) { return length ? length : 256u; }
    /// CPU clocks the accesses after the CPU's own take, at the CPU clock ratio (1 = 3.5 MHz, 6 = 21 MHz)
    static uint32_t ExtraClocks(uint32_t accesses, uint32_t ratio);
    /// Names for the debugger and automation
    static const char* ModeName(uint8_t mode);
    static const char* FunctionName(uint8_t fn);

    /// region <IZ84BusAgent>
    void OnOpcodeFetch(uint16_t addr, uint8_t opcode) override;
    uint8_t OnRead(uint16_t addr, uint8_t value) override;
    uint8_t BeforeWrite(uint16_t addr, uint8_t value) override;
    void AfterWrite(uint16_t addr, uint8_t value) override;
    void OnInterruptAcknowledge() override;
    /// endregion

private:
    /// Mode off when ALL_MODE bit 0 is clear (ACC_MODE.clrn = ACC_ENA); returns whether it is on
    bool CheckEnabled();
    /// Whether a new operation may start (START_ACC: a mode on, not blocked)
    bool CanStart() const { return (_state.dir & kDirOn) && !_state.blocked; }
    void RefreshWatch() { watchData = _state.dir != 0; }
    /// The buffer cell of this access (the down-counter, or XCNT in the alternate addressing, which advances)
    uint8_t& BufferCell(uint8_t counter);
    static uint8_t Combine(uint8_t fn, uint8_t buffer, uint8_t data);
    /// Next access: address +1, or PORT_Y +1 in the vertical modes
    void Advance(uint16_t& addr);
    void Charge(uint32_t accesses);

    EmulatorContext* _context = nullptr;
    SprinterPldState& _pld;
    SprinterMemory* _memory = nullptr;
    SprinterAccelState _state{};

    /// A block write in progress between BeforeWrite and AfterWrite: the down-counter after the CPU's own store
    bool _writePending = false;
};
