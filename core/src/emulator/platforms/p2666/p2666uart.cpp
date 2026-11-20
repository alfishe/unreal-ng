#include "stdafx.h"

#include "p2666uart.h"

#include <algorithm>

#include "emulator/io/serial/serialpeer.h"

void P2666Uart::SetPeer(ISerialPeer* peer)
{
    if (_peer)
        _peer->onReceive = nullptr;
    _peer = peer;
    if (_peer)
        _peer->onReceive = [this]() { Sync(); };
    _ctsOld = Cts();
}

void P2666Uart::Reset()
{
    // uart.vhd reset branches (reset = cpu_res)
    _dll = 1;
    _dlm = _ier = _fcr = _lcr = _mcr = _spr = 0;
    _txHead = _txFilled = _rxHead = _rxFilled = 0;
    _txEmptyInt = false;
    _transmitterEmpty = true;
    _rxAvailable = _overrun = _msInt = _rtsFifo = false;
    _ctsOld = Cts();
    _txBusy = _rxBusy = false;
    _last = Now();
    _rxLineFree = _rxSince = _last;
    UpdateInt();
}

bool P2666Uart::Cts() const
{
    // The pin is low (clear to send) while the peer allows; nothing on the line: the input floats low [inferred]
    return _peer ? !_peer->Cts() : false;
}

bool P2666Uart::InterruptPending() const
{
    const bool rls = _overrun;  // parity, framing and break never happen with the emulator's byte peers
    return (_rxAvailable && (_ier & 0x01)) || (_txEmptyInt && (_ier & 0x02)) || (rls && (_ier & 0x04)) ||
           (_msInt && (_ier & 0x08));
}

void P2666Uart::UpdateInt()
{
    const bool now = InterruptPending();
    if (now != _intState)
    {
        _intState = now;
        if (_onInt)
            _onInt(now);
    }
}

void P2666Uart::PushRx(uint8_t byte)
{
    if (_rxFilled == kFifo)
    {
        _overrun = true;
        lost++;
        return;
    }
    // A short word arrives in the high bits (the receiver shifts right into bit 7)
    const int bits = DataBits();
    _rx[static_cast<size_t>((_rxHead + _rxFilled) % kFifo)] = static_cast<uint8_t>(byte << (8 - bits));
    static constexpr int kTrigger[4] = {0, 3, 7, 13};
    if (_rxFilled == kTrigger[_fcr >> 6])
        _rxAvailable = true;
    if (_rxFilled == kRtsThreshold - 1)
        _rtsFifo = true;
    _rxFilled++;
    bytesIn++;
}

void P2666Uart::Advance(uint64_t edge)
{
    if (edge < _last)
        edge = _last;  // a TTD rewind or snapshot: the line does not run backwards
    for (;;)
    {
        // The FIFO reset bits hold their FIFO empty until the next register write (D-16)
        if (_fcr & 0x04)
            _txHead = _txFilled = 0;
        if (_fcr & 0x02)
        {
            _rxHead = _rxFilled = 0;
            _rxAvailable = false;
        }

        // The modem status: a CTS change raises DCTS
        const bool cts = Cts();
        if (cts != _ctsOld)
            _msInt = true;

        // The transmitter takes the next byte while CTS is low; the FIFO going empty raises THRE (tx_reg_empty_int)
        if (!_txBusy && _txFilled > 0 && !cts)
        {
            _txByte = _tx[static_cast<size_t>(_txHead)];
            _txHead = (_txHead + 1) % kFifo;
            if (_txFilled == 1)
                _txEmptyInt = true;
            _txFilled--;
            _txBusy = true;
            _txEnd = _last + TxFrameEdges();
        }

        // The receiver: the peer starts a byte while the RTS pin is low (or it ignores RTS)
        if (!_rxBusy && _peer && _peer->HasByte() && (!_peer->HonorsRts() || !RtsStop()))
        {
            _rxBusy = true;
            _rxEnd = std::max(_rxLineFree, _last) + RxFrameEdges();
        }

        // The next event at or before `edge`
        uint64_t next = edge + 1;
        if (_txBusy)
            next = std::min(next, _txEnd);
        if (_rxBusy)
            next = std::min(next, _rxEnd);
        if (next > edge)
            break;
        _last = next;
        if (_txBusy && _txEnd == next)
        {
            _txBusy = false;
            bytesOut++;
            if (_peer)
                _peer->Transmit(static_cast<uint8_t>(_txByte & ((1u << DataBits()) - 1u)));
            if (_txFilled == 0)
                _transmitterEmpty = true;
        }
        if (_rxBusy && _rxEnd == next)
        {
            _rxBusy = false;
            // The other half of the stop bit (and a second one) before the next start bit
            _rxLineFree = next + kRxBitEdges * Divisor() / 2u;
            PushRx(_peer && _peer->HasByte() ? _peer->TakeByte() : 0xFF);
            _rxSince = next;
        }
    }
    _last = edge;
    UpdateInt();
}

void P2666Uart::Sync() { Advance(Now()); }

uint8_t P2666Uart::Read(uint8_t reg)
{
    Advance(Now());
    const bool dlab = (_lcr & 0x80) != 0;
    switch (reg & 7)
    {
        case 0:
            if (dlab)
            {
                _out = _dll;
                break;
            }
            // The level strobe pops every clock of the I/O cycle: the FIFO empties, the bus keeps the last byte
            // (D-15). The timeout counter restarts
            //
            // P2666-FIX D-15 (disabled, hardware-reference.md §11): a strobe on the first clock of the cycle only
            // (reg_stb_n <= iorq_after_bus OR reg_stb_old) pops one byte:
            //   if (_rxFilled > 0) { pop one as below; } break;
            while (_rxFilled > 0)
            {
                static constexpr int kClear[4] = {1, 4, 8, 14};
                _out = _rx[static_cast<size_t>(_rxHead)];
                _rxHead = (_rxHead + 1) % kFifo;
                if (_rxFilled == kClear[_fcr >> 6])
                    _rxAvailable = false;
                if (_rxFilled == kRtsThreshold)
                    _rtsFifo = false;
                _rxFilled--;
            }
            _rxSince = _last;
            break;
        case 1: _out = dlab ? _dlm : _ier; break;
        case 2:
        {
            // uart_isr; reading it clears THRE (D-17)
            const bool timeout = _rxFilled > 0 && _last - _rxSince >= TimeoutEdges();
            uint8_t id = 0x02;  // none
            if (_overrun && (_ier & 0x04))
                id = 0x0C;
            else if ((_rxAvailable || timeout) && (_ier & 0x01))
                id = 0x08;
            else if (_txEmptyInt && (_ier & 0x02))
                id = 0x04;
            else if (_msInt && (_ier & 0x08))
                id = 0x00;
            _out = static_cast<uint8_t>(((_fcr & 1) ? 0xC0 : 0x00) | id | (InterruptPending() ? 0x00 : 0x01));
            _txEmptyInt = false;
            break;
        }
        case 3: _out = _lcr; break;
        case 4: _out = _mcr; break;
        case 5:
            // error bit 7, TEMT, THRE (D-17), break, framing, parity, overrun, data ready; the read clears overrun
            _out = static_cast<uint8_t>((_transmitterEmpty ? 0x40 : 0) | (_txEmptyInt ? 0x20 : 0) | (_overrun ? 0x02 : 0) |
                                        (_rxFilled > 0 ? 0x01 : 0));
            _overrun = false;
            break;
        case 6:
            _out = static_cast<uint8_t>(0xA0 | (Cts() ? 0x10 : 0) | (_msInt ? 0x01 : 0));
            _ctsOld = Cts();
            _msInt = false;
            break;
        default: _out = _spr; break;
    }
    UpdateInt();
    return _out;
}

void P2666Uart::Write(uint8_t reg, uint8_t value)
{
    Advance(Now());
    // A write to any register ends the FCR reset bits (D-16), unless it writes them again
    _fcr = static_cast<uint8_t>(_fcr & ~0x06);
    const bool dlab = (_lcr & 0x80) != 0;
    switch (reg & 7)
    {
        case 0:
            if (dlab)
            {
                _dll = value;
                break;
            }
            // The level strobe pushes every clock of the I/O cycle: the byte fills the TX FIFO (D-15)
            //
            // P2666-FIX D-15 (disabled): one push per OUT:
            //   if (_txFilled < kFifo) { _tx[(_txHead + _txFilled) % kFifo] = value; _txFilled++; }
            while (_txFilled < kFifo)
            {
                _tx[static_cast<size_t>((_txHead + _txFilled) % kFifo)] = value;
                _txFilled++;
            }
            _txEmptyInt = false;
            _transmitterEmpty = false;
            break;
        case 1:
            if (dlab)
                _dlm = value;
            else
                _ier = value;
            break;
        case 2: _fcr = value; break;
        case 3: _lcr = value; break;
        case 4: _mcr = value; break;
        case 7: _spr = value; break;
        default: break;
    }
    if (_peer && (reg & 7) == 4)
        _peer->OnModemLines(!RtsStop(), (_mcr & 0x01) != 0);
    Advance(Now());  // the FIFO reset bits act at once
}
