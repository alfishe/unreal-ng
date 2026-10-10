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
    _transferring = _waiting = _finishPending = false;
    _atLeastOne = _endOfBlock = false;
    _counter = 0;
    _readSeq = 0;
    _nextByteAt = 0;
    _z80Compat = false;
}

void NextDma::Write(uint8_t value, bool z80Compatible)
{
    _z80Compat = z80Compatible;
    if (_seq != Seq::Idle)
    {
        switch (_seq)
        {
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
        _seq = (value & 0x04) ? Seq::R4b0 : (value & 0x08) ? Seq::R4b1 : Seq::Idle;
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
            break;
        case 0xC7: _portATiming = 1; break;
        case 0xCB: _portBTiming = 1; break;
        case 0xCF: Load(); break;
        case 0xD3:  // continue: the counter restarts, the addresses go on
            _endOfBlock = false;
            _counter = _z80Compat ? 0xFFFF : 0;
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
        default: break;  // interrupt control, force ready: nothing here
    }
}

/// The end of a block (dma.vhd FINISH_DMA): the end-of-block flag is set; an auto-restart reloads the addresses and the counter WITHOUT
/// clearing the flag (only LOAD, CONTINUE, 0x8B and reset do), else the DMA stops
void NextDma::FinishBlock()
{
    _endOfBlock = true;
    if (_autoRestart)
    {
        if (_aToB)
        {
            _src = _portAAddress;
            _dst = _portBAddress;
        }
        else
        {
            _src = _portBAddress;
            _dst = _portAAddress;
        }
        _counter = _z80Compat ? 0xFFFF : 0;
    }
    else
        _transferring = false;
}

void NextDma::Load()
{
    _endOfBlock = false;
    if (_aToB)
    {
        _src = _portAAddress;
        _dst = _portBAddress;
    }
    else
    {
        _src = _portBAddress;
        _dst = _portAAddress;
    }
    _counter = _z80Compat ? 0xFFFF : 0;
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
        if (_bus.advance)
            _bus.advance(Cycles(_aToB ? _portATiming : _portBTiming));
        if (srcIo)
        {
            if (_bus.readIo)
                data = _bus.readIo(_src);
        }
        else if (_bus.readMemory)
            data = _bus.readMemory(_src);
        if (_bus.advance)
            _bus.advance(Cycles(_aToB ? _portBTiming : _portATiming));
        if (dstIo)
        {
            if (_bus.writeIo)
                _bus.writeIo(_dst, data);
        }
        else if (_bus.writeMemory)
            _bus.writeMemory(_dst, data);
        _counter++;
        moved++;
        _atLeastOne = true;
        if (srcMode == 1)
            _src++;
        else if (srcMode == 0)
            _src--;
        if (dstMode == 1)
            _dst++;
        else if (dstMode == 0)
            _dst--;
        if (_prescaler)
        {
            // the wait after a byte: prescaler * 32 clocks of 28 MHz (875 kHz steps), also after the LAST byte of a block - the
            // end-of-block flag, the stop and the auto-restart come after it
            _waiting = true;
            _nextByteAt = now28 + static_cast<uint64_t>(_prescaler) * 32;
            _finishPending = _counter >= _blockLength;
            break;
        }
        if (_counter >= _blockLength)
        {
            FinishBlock();
            break;
        }
        if (_mode == 2)
            break;  // burst: one byte per run
    }
    return moved;
}
