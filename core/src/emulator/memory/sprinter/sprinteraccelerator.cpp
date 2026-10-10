#include "stdafx.h"

#include "sprinteraccelerator.h"

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/ports/models/sprinter/sprinterpldstate.h"

namespace
{
/// The opcodes a fetch may change the accelerator with even when no latch is set: a same-register LD r,r and
/// HALT (ACC_MODE), the ALU group (FN_ACC), the prefixes (the prefix and ED latches)
struct FetchMattersTable
{
    bool entries[256] = {};
    constexpr FetchMattersTable()
    {
        for (int op = 0; op < 256; op++)
        {
            const bool sameRegister = (op & 0xC0) == 0x40 && ((op >> 3) & 0x07) == (op & 0x07);
            const bool alu = (op & 0xC0) == 0x80;
            const bool prefix = op == 0xCB || op == 0xDD || op == 0xED || op == 0xFD;
            entries[op] = sameRegister || alu || prefix;
        }
    }
};
constexpr FetchMattersTable kFetchMatters;
}  // namespace

SprinterAccelerator::SprinterAccelerator(EmulatorContext* context, SprinterPldState& pld) : _context(context), _pld(pld)
{
    fetchMatters = kFetchMatters.entries;
}

void SprinterAccelerator::Reset()
{
    // ACC_MODE, ALT_ACC clear on /RESET, ACC_BLK presets to "enabled"; the buffer RAM, RGACC and FN_ACC keep their contents
    _state.mode = 0;
    _state.dir = 0;
    _state.prefix = 0;
    _state.edSeen = 0;
    _state.reti = 0;
    _state.blocked = 0;
    _state.alt = 0;
    _state.xcnt = 0;
    _state.xagr = 0;
    _state.aagr = 0;
    _writePending = false;
    RefreshWatch();
    RefreshFetchQuiet();
}

bool SprinterAccelerator::IsEnabled() const
{
    return (_pld.allMode & 0x01) != 0;
}

bool SprinterAccelerator::CheckEnabled()
{
    if (IsEnabled())
        return true;
    _state.mode = 0;
    _state.dir = 0;
    RefreshWatch();
    RefreshFetchQuiet();
    return false;
}

void SprinterAccelerator::OnScaleWrite(uint16_t port, uint8_t value)
{
    // ACCELER.TDF WR_C7: AAGR = (AI9, AI8, DI), XCNT = (0, 0, AI15..AI10), XAGR = 0; ALT_ACC set until reset
    _state.alt = 1;
    _state.aagr = static_cast<uint16_t>((port & 0x0300) | value);
    _state.xcnt = static_cast<uint8_t>((port >> 10) & 0x3F);
    _state.xagr = 0;
}

/// region <Control: the opcode fetches>

void SprinterAccelerator::OnOpcodeFetch([[maybe_unused]] uint16_t addr, uint8_t opcode)
{
    // ACC_BLK at the end of this M1 (/IORQ high): it stays enabled, or the RETI latched by the previous
    // fetch enables it again. The latches below take this fetch's byte after that (one M1 later)
    if (_state.blocked && _state.reti)
        _state.blocked = 0;

    const bool prefixed = _state.prefix != 0;

    if (CheckEnabled() && !prefixed)
    {
        // Same-register LD r,r and HALT: 01 rrr rrr with both fields equal (ACCELER.TDF ACC_MODE.ena)
        if ((opcode & 0xC0) == 0x40 && ((opcode >> 3) & 0x07) == (opcode & 0x07))
        {
            _state.mode = opcode & 0x07;
            _state.dir = kDir[_state.mode];
            RefreshWatch();
        }
    }

    // FN_ACC: every fetch; an unprefixed 10 xxx yyy (ALU) opcode gives ~xxx, anything else plain
    _state.fn = (!prefixed && (opcode & 0xC0) == 0x80) ? static_cast<uint8_t>(~(opcode >> 3) & 0x07) : 0;

    _state.reti = (_state.edSeen && opcode == 0x4D) ? 1 : 0;
    _state.edSeen = opcode == 0xED ? 1 : 0;
    _state.prefix = (opcode == 0xCB || opcode == 0xDD || opcode == 0xED || opcode == 0xFD) ? 1 : 0;
    RefreshFetchQuiet();
}

bool SprinterAccelerator::RepeatFetchIsInert([[maybe_unused]] uint16_t addr, uint8_t opcode) const
{
    // OnOpcodeFetch step by step, comparing instead of storing
    if (_state.blocked && _state.reti)
        return false;
    const bool prefixed = _state.prefix != 0;
    if (!IsEnabled())
    {
        if (_state.mode != 0 || _state.dir != 0)
            return false;  // CheckEnabled clears them
    }
    else if (!prefixed && (opcode & 0xC0) == 0x40 && ((opcode >> 3) & 0x07) == (opcode & 0x07))
    {
        const uint8_t mode = opcode & 0x07;
        if (_state.mode != mode || _state.dir != kDir[mode])
            return false;
    }
    const uint8_t fn = (!prefixed && (opcode & 0xC0) == 0x80) ? static_cast<uint8_t>(~(opcode >> 3) & 0x07) : 0;
    const uint8_t reti = (_state.edSeen && opcode == 0x4D) ? 1 : 0;
    const uint8_t edSeen = opcode == 0xED ? 1 : 0;
    const uint8_t prefix = (opcode == 0xCB || opcode == 0xDD || opcode == 0xED || opcode == 0xFD) ? 1 : 0;
    return _state.fn == fn && _state.reti == reti && _state.edSeen == edSeen && _state.prefix == prefix;
}

void SprinterAccelerator::OnInterruptAcknowledge()
{
    // ACC_BLK at the end of the acknowledge M1 (/IORQ low): blocked, unless a RETI was latched by the
    // last fetch (an INT taken right after RETI leaves the accelerator on). Off: MAME's behavior
    if (!_context->config.sprinter.accel_int_suspend)
        return;
    _state.blocked = (!_state.blocked || !_state.reti) ? 1 : 0;
}

/// endregion </Control>

/// region <Data: the repeated accesses>

uint8_t& SprinterAccelerator::BufferCell(uint8_t counter)
{
    if (!_state.alt)
        return _state.buffer[counter];

    // XCNT:XAGR += AAGR per access (MAME accel_buffer)
    const uint8_t index = _state.xcnt;
    const uint16_t sum = static_cast<uint16_t>(((_state.xcnt << 8) | _state.xagr) + _state.aagr);
    _state.xcnt = static_cast<uint8_t>(sum >> 8);
    _state.xagr = static_cast<uint8_t>(sum & 0xFF);
    return _state.buffer[index];
}

uint8_t SprinterAccelerator::Combine(uint8_t fn, uint8_t buffer, uint8_t data)
{
    switch (fn & 0x03)
    {
        case 3: return static_cast<uint8_t>(data & buffer);  // #A6 AND (HL)
        case 2: return static_cast<uint8_t>(data ^ buffer);  // #AE XOR (HL)
        case 1: return static_cast<uint8_t>(data | buffer);  // #B6 OR (HL)
        default: return data;                                // #BE CP (HL): plain
    }
}

void SprinterAccelerator::Advance(uint16_t& addr)
{
    if (_state.dir & kDirVertical)
        _pld.portY++;
    else
        addr++;
}

uint32_t SprinterAccelerator::ExtraClocks(uint32_t accesses, uint32_t ratio)
{
    if (accesses <= 1)
        return 0;
    // 42 MHz = 12 x 3.5 MHz: a CPU clock is 12 / ratio clocks of 42 MHz; the CPU waits for the next whole clock
    const uint32_t ticks42 = (accesses - 1) * kTicks42PerAccess;
    const uint32_t perClock = 12u / (ratio ? ratio : 1u);
    return (ticks42 + perClock - 1) / perClock;
}

void SprinterAccelerator::Charge(uint32_t accesses)
{
    _state.operations++;
    const uint32_t ratio = _context->emulatorState.hw_turbo_ratio;
    _state.lastExtraClocks = ExtraClocks(accesses, ratio > 1 ? ratio : 1u);
    if (_state.lastExtraClocks && _context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->AddWaitStates(_state.lastExtraClocks);
}

uint8_t SprinterAccelerator::OnRead(uint16_t addr, uint8_t value)
{
    if (!CheckEnabled())
        return value;

    if (_state.dir & kDirLength)
    {
        _state.length = value;  // RGACC: not gated by the INT block
        return value;
    }
    if (!CanStart() || !(_state.dir & kDirBlock) || !_memory)
        return value;

    // The CPU's read is the first access; the PLD repeats it with the down-counter from RGACC
    const uint32_t accesses = Accesses(_state.length);
    const bool toBuffer = (_state.dir & kDirBuffer) != 0;
    uint8_t counter = _state.length;
    uint8_t data = value;
    for (uint32_t i = 0;; i++)
    {
        if (i > 0 && _memory->AcceleratorReaches(addr))
            data = _memory->AcceleratorRead(addr);
        if (toBuffer)
        {
            uint8_t& cell = BufferCell(counter);
            cell = Combine(_state.fn, cell, data);
        }
        Advance(addr);
        if (i + 1 == accesses)
            break;
        counter--;
    }

    Charge(accesses);
    return data;  // the CPU gets the last byte read
}

uint8_t SprinterAccelerator::BeforeWrite(uint16_t addr, uint8_t value)
{
    _writePending = false;
    if (!CheckEnabled())
        return value;

    if (_state.dir & kDirLength)
    {
        _state.length = value;
        return value;
    }
    if (!CanStart())
        return value;

    if (_state.dir & kDirBlock)
    {
        _writePending = true;
        if (_state.dir & kDirBuffer)
            return BufferCell(_state.length);  // copy: the first store is the buffer's
        return value;
    }
    if (_state.dir & kDirDouble)
        _writePending = true;
    (void)addr;
    return value;
}

void SprinterAccelerator::AfterWrite(uint16_t addr, uint8_t value)
{
    if (!_writePending || !_memory)
        return;
    _writePending = false;

    if (!(_state.dir & kDirBlock))
    {
        // LD H,H: the PLD writes both byte lanes in one cycle (DOUBLE_CAS); the other lane gets the IDE high
        // byte latch HDDR there - MAME stores the CPU's byte at addr ^ 1, as here (no IDE latch yet)
        const uint16_t other = static_cast<uint16_t>(addr ^ 1);
        if (_memory->AcceleratorReaches(other))
            _memory->AcceleratorWrite(other, value);
        _state.operations++;
        _state.lastExtraClocks = 0;
        return;
    }

    const uint32_t accesses = Accesses(_state.length);
    const bool fromBuffer = (_state.dir & kDirBuffer) != 0;
    uint8_t counter = _state.length;
    Advance(addr);  // after the CPU's own store
    for (uint32_t i = 1; i < accesses; i++)
    {
        counter--;
        const uint8_t data = fromBuffer ? BufferCell(counter) : value;
        if (_memory->AcceleratorReaches(addr))
            _memory->AcceleratorWrite(addr, data);
        Advance(addr);
    }

    Charge(accesses);
}

/// endregion </Data>

const char* SprinterAccelerator::ModeName(uint8_t mode)
{
    static const char* const kNames[8] = {"off", "fill", "length", "vertical-fill", "double", "copy", "off-halt", "vertical-copy"};
    return kNames[mode & 7];
}

const char* SprinterAccelerator::FunctionName(uint8_t fn)
{
    static const char* const kNames[4] = {"plain", "or", "xor", "and"};
    return kNames[fn & 3];
}
