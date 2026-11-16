#include "stdafx.h"

#include "nextdma.h"

void NextDma::Reset()
{
    // dma.vhd 182-216: the programmed addresses, the block length, the direction and the port types are not reset
    _seq = Seq::Idle;
    _portATiming = _portBTiming = 1;
    _prescaler = 0;
    _mode = 1;
    _ceWait = false;
    _autoRestart = false;
    _readMask = 0x7F;
    _transferring = _waiting = _finishPending = _busHeld = false;
    _atLeastOne = _endOfBlock = false;
    _counter = 0;
    _readSeq = 0;
    _nextByteAt = 0;
    _z80Compat = false;
    ClearDdma();
}

/// Reset (hard, soft or #C3) clears the zxnDDMA state; the programmed addresses, direction, port types, modes, block length stay
void NextDma::ClearDdma()
{
    _extA = _extB = 0;
    _phys = _2d = _transparent = _fill = _l2xy = _hidden = false;
    _l2x = 0;
    _l2y = 0;
    _rowLen = _rows = _strideA = _strideB = 0;
    _transValue = _fillValue = 0;
    _row = _col = 0;
}

void NextDma::Write(uint8_t value, bool z80Compatible)
{
    _z80Compat = z80Compatible;
    if (_seq != Seq::Idle)
    {
        switch (_seq)
        {
            case Seq::Stuck:  // nothing leaves this state but a reset (dma.vhd "when others => null")
                break;
            case Seq::R0b0:
                _portAAddress = static_cast<uint16_t>((_portAAddress & 0xFF00) | value);
                _seq = (_regTemp & 0x10) ? Seq::R0b1 : (_regTemp & 0x20) ? Seq::R0b2 : (_regTemp & 0x40) ? Seq::R0b3 : Seq::Idle;
                break;
            case Seq::R0b1:
                _portAAddress = static_cast<uint16_t>((_portAAddress & 0x00FF) | (value << 8));
                _seq = (_regTemp & 0x20) ? Seq::R0b2 : (_regTemp & 0x40) ? Seq::R0b3 : Seq::Idle;
                break;
            case Seq::R0b2:
                _blockLength = static_cast<uint16_t>((_blockLength & 0xFF00) | value);
                _seq = (_regTemp & 0x40) ? Seq::R0b3 : Seq::Idle;
                break;
            case Seq::R0b3:
                _blockLength = static_cast<uint16_t>((_blockLength & 0x00FF) | (value << 8));
                _seq = Seq::Idle;
                break;
            case Seq::R1b0:
                _portATiming = value & 3;
                _seq = (value & 0x20) ? Seq::R1b1 : Seq::Idle;
                break;
            case Seq::R2b0:
                _portBTiming = value & 3;
                _seq = (value & 0x20) ? Seq::R2b1 : Seq::Idle;
                break;
            case Seq::R2b1:
                _prescaler = value;
                _seq = Seq::Idle;
                break;
            case Seq::R3b0:
                _seq = (_regTemp & 0x10) ? Seq::R3b1 : Seq::Idle;
                break;
            case Seq::R4b0:
                _portBAddress = static_cast<uint16_t>((_portBAddress & 0xFF00) | value);
                _seq = (_regTemp & 0x08) ? Seq::R4b1 : Seq::Idle;
                break;
            case Seq::R4b1:
                _portBAddress = static_cast<uint16_t>((_portBAddress & 0x00FF) | (value << 8));
                _seq = Seq::Idle;
                break;
            case Seq::ExtA: _extA = value & 0x3F; _seq = Seq::Idle; break;
            case Seq::ExtB: _extB = value & 0x3F; _seq = Seq::Idle; break;
            case Seq::DdmaMode:
                _phys = value & 0x01;
                _2d = value & 0x02;
                _transparent = value & 0x04;
                _fill = value & 0x08;
                _l2xy = (value & 0x10) && _phys;
                _hidden = (value & 0x20) && _l2xy;
                if (_l2xy)
                    _strideB = 256;  // the Layer 2 destination moves a row (256 bytes) at a time, until #EF or #C3
                _seq = (value & 0x10) ? Seq::DdmaX0 : Seq::Idle;
                break;
            case Seq::DdmaX0: _l2x = static_cast<uint16_t>((_l2x & 0xFF00) | value); _seq = Seq::DdmaX1; break;
            case Seq::DdmaX1: _l2x = static_cast<uint16_t>((_l2x & 0x00FF) | (value << 8)); _seq = Seq::DdmaY; break;
            case Seq::DdmaY: _l2y = value; _seq = Seq::Idle; break;
            case Seq::RowLen0: _rowLen = static_cast<uint16_t>((_rowLen & 0xFF00) | value); _seq = Seq::RowLen1; break;
            case Seq::RowLen1: _rowLen = static_cast<uint16_t>((_rowLen & 0x00FF) | (value << 8)); _seq = Seq::Idle; break;
            case Seq::Rows0: _rows = static_cast<uint16_t>((_rows & 0xFF00) | value); _seq = Seq::Rows1; break;
            case Seq::Rows1: _rows = static_cast<uint16_t>((_rows & 0x00FF) | (value << 8)); _seq = Seq::Idle; break;
            case Seq::StrideA0: _strideA = static_cast<uint16_t>((_strideA & 0xFF00) | value); _seq = Seq::StrideA1; break;
            case Seq::StrideA1: _strideA = static_cast<uint16_t>((_strideA & 0x00FF) | (value << 8)); _seq = Seq::Idle; break;
            case Seq::StrideB0: _strideB = static_cast<uint16_t>((_strideB & 0xFF00) | value); _seq = Seq::StrideB1; break;
            case Seq::StrideB1: _strideB = static_cast<uint16_t>((_strideB & 0x00FF) | (value << 8)); _seq = Seq::Idle; break;
            case Seq::Transparent: _transValue = value; _seq = Seq::Idle; break;
            case Seq::FillValue: _fillValue = value; _seq = Seq::Idle; break;
            case Seq::R6Mask:
                _readMask = value;
                AdvanceReadSequence(-1);
                _seq = Seq::Idle;
                break;
            default:  // R1b1, R3b1: a byte that is taken and ignored
                _seq = Seq::Idle;
                break;
        }
        return;
    }
    if ((value & 0x83) == 0x83)
        Command(value);
    else if ((value & 0xC7) == 0x82)  // WR5
    {
        _regTemp = value;
        _ceWait = (value >> 4) & 1;
        _autoRestart = (value >> 5) & 1;
    }
    else if ((value & 0x83) == 0x81)  // WR4
    {
        _regTemp = value;
        _mode = (value >> 5) & 3;
        // dma.vhd: bit 2 -> low byte, bit 3 -> high byte, bit 4 -> R4_BYTE_2, a state the register sequencer has no branch for: the
        // register interface of the DMA is dead until the next reset
        _seq = (value & 0x04) ? Seq::R4b0 : (value & 0x08) ? Seq::R4b1 : (value & 0x10) ? Seq::Stuck : Seq::Idle;
    }
    else if ((value & 0x83) == 0x80)  // WR3
    {
        _regTemp = value;
        if ((value >> 6) & 1)  // DMA enable
        {
            _transferring = true;
            _waiting = false;
            _atLeastOne = false;
        }
        _seq = (value & 0x08) ? Seq::R3b0 : (value & 0x10) ? Seq::R3b1 : Seq::Idle;
    }
    else if ((value & 0x87) == 0x00)  // WR2: port B
    {
        _regTemp = value;
        _portBIsIo = (value >> 3) & 1;
        _portBMode = (value >> 4) & 3;
        _seq = (value & 0x40) ? Seq::R2b0 : Seq::Idle;
    }
    else if ((value & 0x87) == 0x04)  // WR1: port A
    {
        _regTemp = value;
        _portAIsIo = (value >> 3) & 1;
        _portAMode = (value >> 4) & 3;
        _seq = (value & 0x40) ? Seq::R1b0 : Seq::Idle;
    }
    else if ((value & 0x80) == 0 && (value & 3) != 0)  // WR0
    {
        _regTemp = value;
        _aToB = (value >> 2) & 1;
        _seq = (value & 0x08) ? Seq::R0b0 : (value & 0x10) ? Seq::R0b1 : (value & 0x20) ? Seq::R0b2 : (value & 0x40) ? Seq::R0b3 : Seq::Idle;
    }
}

/// Clocks of one port access by its timing byte (bits 1:0): 4, 3, 2, 4
unsigned NextDma::Cycles(uint8_t timing)
{
    static const unsigned kCycles[4] = {4, 3, 2, 4};
    return kCycles[timing & 3];
}

void NextDma::Command(uint8_t value)
{
    _seq = Seq::Idle;
    switch (value)
    {
        case 0xC3:  // reset
            _transferring = false;
            _waiting = false;
            _endOfBlock = false;
            _atLeastOne = false;
            _portATiming = 1;
            _portBTiming = 1;
            _prescaler = 0;
            _ceWait = false;
            _autoRestart = false;
            ClearDdma();
            break;
        case 0xC7: _portATiming = 1; break;
        case 0xCB: _portBTiming = 1; break;
        case 0xCF: Load(); break;
        case 0xD3:  // continue: the counter restarts, the addresses go on
            _endOfBlock = false;
            _counter = _z80Compat ? 0xFFFF : 0;
            _row = _col = 0;
            _rowSrc = _src;
            _rowDst = _dst;
            break;
        case 0xBF: _readSeq = 0; break;  // read the status byte
        case 0x8B:
            _endOfBlock = false;
            _atLeastOne = false;
            break;
        case 0xA7: AdvanceReadSequence(-1); break;  // initialise the read sequence
        case 0x87:  // enable
            _transferring = true;
            _waiting = false;
            _atLeastOne = false;
            break;
        case 0x83:  // disable
            _transferring = false;
            _waiting = false;
            break;
        case 0xBB: _seq = Seq::R6Mask; break;
        default:
            // zxnDDMA: port #6B only; a core without it (or port #0B) ignores the byte and takes its follow bytes as register writes
            if (!_extended || _z80Compat)
                break;
            switch (value)
            {
                case 0xD7: _seq = Seq::ExtA; break;
                case 0xDB: _seq = Seq::ExtB; break;
                case 0xDF: _seq = Seq::DdmaMode; break;
                case 0xE3: _seq = Seq::RowLen0; break;
                case 0xE7: _seq = Seq::Rows0; break;
                case 0xEB: _seq = Seq::StrideA0; break;
                case 0xEF: _seq = Seq::StrideB0; break;
                case 0xF3: _seq = Seq::Transparent; break;
                case 0xF7: _seq = Seq::FillValue; break;
                default: break;  // #FB (obsolete) and the rest: nothing
            }
            break;
    }
}

/// The end of a block (dma.vhd FINISH_DMA): the end-of-block flag is set; an auto-restart reloads the addresses and the counter WITHOUT
/// clearing the flag (only LOAD, CONTINUE, 0x8B and reset do), else the DMA stops
void NextDma::FinishBlock()
{
    if (_onBlock) [[unlikely]]
        _onBlock(*this);
    _endOfBlock = true;
    if (_autoRestart)
    {
        InitAddresses();
        _counter = _z80Compat ? 0xFFFF : 0;
    }
    else
    {
        _transferring = false;
        ReleaseBus(kFinishReleaseT);
    }
}

void NextDma::ReleaseBus(unsigned clocks)
{
    if (!_busHeld)
        return;
    _busHeld = false;
    HoldBus(clocks);
}

void NextDma::HoldBus(unsigned clocks)
{
    for (unsigned i = 0; i < clocks; i++)
    {
        if (_bus.contend)
            _bus.contend(_src, true);
        if (_bus.advance)
            _bus.advance(1);
    }
}

void NextDma::Load()
{
    _endOfBlock = false;
    InitAddresses();
    _counter = _z80Compat ? 0xFFFF : 0;
}

void NextDma::InitAddresses()
{
    uint32_t a = (static_cast<uint32_t>(_extA) << 16) | _portAAddress;
    uint32_t b = (static_cast<uint32_t>(_extB) << 16) | _portBAddress;
    if (_l2xy && _bus.layer2)
    {
        // Layer 2 destination (port B): the bank is NR #12 (the shadow NR #13 when hidden), 16K at SRAM #040000 + bank x #4000; 256x192 is
        // row-major (Y x 256 + X, X one byte), the 320x256 and 640x256 modes are column-major (X x 256 + Y)
        uint8_t bank = 0, resolution = 0;
        _bus.layer2(_hidden, bank, resolution);
        const uint32_t base = 0x40000u + static_cast<uint32_t>(bank & 0x7F) * 0x4000u;
        const uint32_t where = (resolution & 3) == 0 ? (static_cast<uint32_t>(_l2y) << 8) + (_l2x & 0xFF)
                                                      : (static_cast<uint32_t>(_l2x & 0x3FF) << 8) + _l2y;
        b = (base + where) & 0x3FFFFF;
    }
    _src = _aToB ? a : b;
    _dst = _aToB ? b : a;
    _row = _col = 0;
    _rowSrc = _src;
    _rowDst = _dst;
}

/// One address step: physical mode carries and borrows through the extended byte, else the 16-bit address wraps in 64K
uint32_t NextDma::Step(uint32_t address, uint8_t mode) const
{
    if (mode > 1)
        return address;
    const uint32_t delta = mode == 1 ? 1u : 0xFFFFFFFFu;
    if (_phys)
        return (address + delta) & 0x3FFFFF;
    return (address & 0xFF0000u) | ((address + delta) & 0xFFFFu);
}

uint8_t NextDma::Read()
{
    uint8_t result = 0;
    switch (_readSeq)
    {
        case 0: result = StatusByte(); break;  // bit 0: a byte was moved while the DMA is not idle (a burst / prescaler wait; the board reads 1A / 3A after a block)
        case 1: result = _counter & 0xFF; break;
        case 2: result = _counter >> 8; break;
        case 3: result = (_aToB ? _src : _dst) & 0xFF; break;
        case 4: result = (_aToB ? _src : _dst) >> 8; break;
        case 5: result = (_aToB ? _dst : _src) & 0xFF; break;
        case 6: result = (_aToB ? _dst : _src) >> 8; break;
        default: break;
    }
    AdvanceReadSequence(_readSeq);
    return result;
}

void NextDma::AdvanceReadSequence(int after)
{
    for (int i = 1; i <= 7; i++)
    {
        const int bit = (after + i) % 7;
        if (_readMask & (1 << bit))
        {
            _readSeq = static_cast<uint8_t>(bit);
            return;
        }
    }
    _readSeq = 0;
}

unsigned NextDma::Run(unsigned maxBytes, uint64_t now28)
{
    if (!_transferring)
        return 0;
    if (_waiting)
    {
        if (now28 < _nextByteAt)
            return 0;
        _waiting = false;
        if (_finishPending)  // the last byte's prescaler period is over: now the block ends (dma.vhd WRITE_4 checks the gate first)
        {
            _finishPending = false;
            FinishBlock();
            return 0;
        }
    }
    const bool srcIo = _aToB ? _portAIsIo : _portBIsIo;
    const bool dstIo = _aToB ? _portBIsIo : _portAIsIo;
    const uint8_t srcMode = _aToB ? _portAMode : _portBMode;
    const uint8_t dstMode = _aToB ? _portBMode : _portAMode;
    unsigned moved = 0;
    while (moved < maxBytes && _transferring)
    {
        uint8_t data = 0xFF;
        if (!_busHeld)
        {
            // WAITING_ACK -> TRANSFERING_READ_1: the read starts the T after the CPU's BUSAK
            _busHeld = true;
            HoldBus(kGrantT);
        }
        const bool srcPhys = _phys && !srcIo, dstPhys = _phys && !dstIo;
        const uint16_t srcAddr = static_cast<uint16_t>(_src), dstAddr = static_cast<uint16_t>(_dst);
        if (_fill)
            data = _fillValue;  // no source read: the byte costs only the write cycle (twice as fast as a fixed source)
        else
        {
            if (_bus.contend && !srcPhys)
                _bus.contend(srcAddr, srcIo);
            if (_bus.advance)
                _bus.advance(Cycles(_aToB ? _portATiming : _portBTiming));
            if (srcIo)
            {
                if (_bus.readIo)
                    data = _bus.readIo(srcAddr);
            }
            else if (srcPhys)
            {
                if (_bus.readPhysical)
                    data = _bus.readPhysical(_src & 0x3FFFFF);
            }
            else if (_bus.readMemory)
                data = _bus.readMemory(srcAddr);
        }
        if (_bus.contend && !dstPhys)
            _bus.contend(dstAddr, dstIo);
        if (_bus.advance)
            _bus.advance(Cycles(_aToB ? _portBTiming : _portATiming));
        // a source byte equal to the transparent value is not written; the addresses still advance
        const bool skipWrite = _transparent && !_fill && data == _transValue;
        if (skipWrite) [[unlikely]]
            _atLeastOne = true;
        else if (dstIo)
        {
            if (_bus.writeIo)
                _bus.writeIo(dstAddr, data);
        }
        else if (dstPhys)
        {
            if (_bus.writePhysical)
                _bus.writePhysical(_dst & 0x3FFFFF, data);
        }
        else if (_bus.writeMemory)
            _bus.writeMemory(dstAddr, data);
        _counter++;
        moved++;
        _atLeastOne = true;
        _src = Step(_src, srcMode);
        _dst = Step(_dst, dstMode);
        bool blockDone;
        if (_2d)
        {
            // the block is rows x row length (the WR0 block length is ignored); a row ends at its length, the next starts one stride on
            const uint16_t rowLen = _rowLen ? _rowLen : 1, rows = _rows ? _rows : 1;
            if (++_col >= rowLen)
            {
                _col = 0;
                _row++;
                const uint16_t srcStride = _aToB ? _strideA : _strideB, dstStride = _aToB ? _strideB : _strideA;
                const uint32_t mask = _phys ? 0x3FFFFFu : 0xFFFFu;
                _rowSrc = (_rowSrc & ~mask) | ((_rowSrc + srcStride) & mask);
                _rowDst = (_rowDst & ~mask) | ((_rowDst + dstStride) & mask);
                _src = _rowSrc;
                _dst = _rowDst;
            }
            blockDone = _row >= rows;
        }
        else
            blockDone = _counter >= _blockLength;
        if (_prescaler)
        {
            // the wait after a byte: prescaler * 32 clocks of 28 MHz (875 kHz steps), also after the LAST byte of a block - the
            // end-of-block flag, the stop and the auto-restart come after it
            _waiting = true;
            _nextByteAt = now28 + static_cast<uint64_t>(_prescaler) * 32;
            _finishPending = blockDone;
            if (_mode == 2)
                ReleaseBus(kBurstReleaseT);  // WAITING_CYCLES gives the bus back one state before FINISH_DMA / IDLE would
            break;
        }
        if (blockDone)
        {
            FinishBlock();
            break;
        }
        if (_mode == 2)
            break;  // burst: one byte per run
    }
    return moved;
}
