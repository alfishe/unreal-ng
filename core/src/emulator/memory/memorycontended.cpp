#include "stdafx.h"

#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/video/ulacontention.h"

/// Video memory contention on the memory access path (design: docs/inprogress/2026-09-28-m1-contention).
///
/// The contended interfaces are the plain ones (Fast / Debug) with one difference, held here: an access to
/// a contended slot while the video logic fetches the screen waits for it first. Every MREQ cycle goes
/// through the same wrapper - the opcode fetch (M1), the operand bytes at PC and the data accesses - so
/// nothing is exempt by kind. Core::SelectMemoryInterface picks these interfaces only while the machine's
/// contention is in effect; the machines without contention (Pentagon and the other clones) run the plain
/// interfaces and never reach this code.
///
/// Timing: Z80::rd / wd charge the 3 T of the memory cycle before calling the interface, so the wait is
/// computed for the T-state the access started at (Z80::AccessStartT) and inserted before the plain access:
/// the byte is read or written at start + wait + 3, exactly where the former check in Z80::rd / wd put it.
///
/// Statistics (UlaContention::CountAccess) are compiled into the Debug instantiation only.

template <MemoryReadCallback Plain, bool Stats>
uint8_t Memory::MemoryReadContended(uint16_t addr, bool isExecution)
{
    if (!_contentionUla->IsSlotContended(static_cast<uint8_t>(addr >> 14)))
        return (this->*Plain)(addr, isExecution);

    const uint8_t wait = _contentionUla->DelayAt(_contentionCpu->AccessStartT());
    _contentionCpu->InsertWaitStates(wait);
    if constexpr (Stats)
        _contentionUla->CountAccess(isExecution ? CONTENTION_FETCH : CONTENTION_READ, wait);

    const uint8_t value = (this->*Plain)(addr, isExecution);
    _contentionUla->LatchContendedByte(value);  // +2A/+3 floating bus: the last contended byte
    return value;
}

template <MemoryWriteCallback Plain, bool Stats>
void Memory::MemoryWriteContended(uint16_t addr, uint8_t value)
{
    if (_contentionUla->IsSlotContended(static_cast<uint8_t>(addr >> 14)))
    {
        const uint8_t wait = _contentionUla->DelayAt(_contentionCpu->AccessStartT());
        _contentionCpu->InsertWaitStates(wait);
        if constexpr (Stats)
            _contentionUla->CountAccess(CONTENTION_WRITE, wait);
        _contentionUla->LatchContendedByte(value);
    }
    (this->*Plain)(addr, value);
}

MemoryInterface* Memory::GetFastContendedMemoryInterface()
{
    return new MemoryInterface(&Memory::MemoryReadContended<&Memory::MemoryReadFast, false>,
                               &Memory::MemoryWriteContended<&Memory::MemoryWriteFast, false>);
}

MemoryInterface* Memory::GetDebugContendedMemoryInterface()
{
    return new MemoryInterface(&Memory::MemoryReadContended<&Memory::MemoryReadDebug, true>,
                               &Memory::MemoryWriteContended<&Memory::MemoryWriteDebug, true>);
}

// Explicit instantiations: the host bus overlay interfaces (memory.cpp) wrap
// these same four as their inner access
template uint8_t Memory::MemoryReadContended<&Memory::MemoryReadFast, false>(uint16_t, bool);
template uint8_t Memory::MemoryReadContended<&Memory::MemoryReadDebug, true>(uint16_t, bool);
template void Memory::MemoryWriteContended<&Memory::MemoryWriteFast, false>(uint16_t, uint8_t);
template void Memory::MemoryWriteContended<&Memory::MemoryWriteDebug, true>(uint16_t, uint8_t);
