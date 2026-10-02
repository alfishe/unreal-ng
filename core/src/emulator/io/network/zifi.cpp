#include "emulator/io/network/zifi.h"

#include <algorithm>
#include <cstring>

#include "emulator/emulatorcontext.h"
#include "emulator/io/serial/serialpeer.h"

namespace
{
/// The AVR's USART0: the TS 2016-04 rings (511 in, 255 out), 115200 at the
/// reset divisor, 8N2 out (USBS0) while the ESP sends 8N1, no flow control
Uart16550::Params LineUart()
{
    Uart16550::Params params = Uart16550::EvoAvrParams(Uart16550::AvrFirmware::Ts2016Apr);
    params.rxFrameBits = 10;
    params.avrClockHz = 0;   // no Z80 access reaches it directly: the #xxEF device charges the wait
    return params;
}

uint8_t Capped(uint32_t count)
{
    return static_cast<uint8_t>(std::min<uint32_t>(count, ZiFi::kDataLimit));
}
}  // namespace

ZiFi::ZiFi(EmulatorContext* context, Uart16550& rs, std::unique_ptr<ISerialPeer> peer,
           std::function<void()> raiseInterrupt)
    : _context(context),
      _rs(rs),
      _line(context, LineUart(), std::move(peer), [](uint16_t port) { return static_cast<int>(port & 0x07); }),
      _raiseInterrupt(std::move(raiseInterrupt))
{
    if (context && context->emulatorState.base_z80_frequency)
        _clockHz = context->emulatorState.base_z80_frequency;
    // No RTS pin on USART0: the ESP may always send
    _line.Uart().Write(Uart16550::kMcr, Uart16550::kMcrRts, _line.Now());
    _line.Uart().onRxByte = [this](uint64_t at) {
        _zfLastRx = at;
        Task(at);
    };
    _rs.onRxByte = [this](uint64_t at) {
        _rsLastRx = at;
        Task(at);
    };
}

ZiFi::~ZiFi()
{
    _line.Uart().onRxByte = nullptr;
    _rs.onRxByte = nullptr;
}

uint8_t ZiFi::TimeoutCount(uint64_t lastRx, uint64_t now) const
{
    if (lastRx == kNever || now < lastRx)
        return lastRx == kNever ? 0xFF : 0;
    const uint64_t ms = (now - lastRx) * 1000u / _clockHz;
    return static_cast<uint8_t>(std::min<uint64_t>(ms, 0xFF));
}

void ZiFi::Task(uint64_t now)
{
    // rs232_task: an armed condition moves its bit from IMR to ISR
    const uint16_t zfRx = _line.Uart().RxUsed();
    const uint16_t rsRx = _rs.RxUsed();
    auto fire = [this](uint8_t bit, bool condition) {
        if ((_imr & bit) && condition)
        {
            _imr = static_cast<uint8_t>(_imr & ~bit);
            _isr = static_cast<uint8_t>(_isr | bit);
        }
    };
    fire(kZfIbt, zfRx >= _zibtr);
    fire(kRsIbt, rsRx >= _ribtr);
    fire(kZfIto, zfRx && TimeoutCount(_zfLastRx, now) >= _zitor);
    fire(kRsIto, rsRx && TimeoutCount(_rsLastRx, now) >= _ritor);
    if (_isr && _raiseInterrupt)
        _raiseInterrupt();
}

uint8_t ZiFi::Read(uint8_t index)
{
    const uint64_t now = _line.Now();
    uint8_t value = 0xFF;
    if (index <= kDataLimit)
    {
        if (_selectZf)
        {
            if (_api == 1)
                value = _line.Uart().DataRead(now);
        }
        else if (_api)
            value = _rs.DataRead(now);
    }
    else if (_api)
    {
        _line.Uart().Advance(now);
        _rs.Advance(now);
        switch (index)
        {
            case kZifr:
                _selectZf = true;
                value = Capped(_line.Uart().RxUsed());
                break;
            case kZofr:
                _selectZf = true;
                value = Capped(_line.Uart().TxFree());
                break;
            case kRifr:
                _selectZf = false;
                value = Capped(_rs.RxUsed());
                break;
            case kRofr:
                _selectZf = false;
                value = Capped(_rs.TxFree());
                break;
            case kImrIsr:
                value = _isr;
                _isr = 0;
                break;
            case kZibtr: value = _zibtr; break;
            case kZitor: value = _zitor; break;
            case kCrEr: value = _err; break;
            case kRibtr: value = _ribtr; break;
            case kRitor: value = _ritor; break;
            default: break;   // #CA..#CF: #FF
        }
    }
    Task(now);
    return value;
}

void ZiFi::Write(uint8_t index, uint8_t value)
{
    const uint64_t now = _line.Now();
    if (index <= kDataLimit)
    {
        if (_selectZf)
        {
            if (_api == 1)
                _line.Uart().DataWrite(value, now);
        }
        else if (_api)
            _rs.DataWrite(value, now);
        Task(now);
        return;
    }
    switch (index)
    {
        case kCrEr:
            if ((value & 0xF8) == 0xF0)
            {
                // SETAPI: an unknown version turns the API off
                _api = static_cast<uint8_t>(value & 0x07);
                if (_api > kVersion)
                    _api = 0;
                _err = 0;
            }
            else if (_api)
            {
                if (value == 0xFF)
                    _err = kVersion;   // GETVER
                else if ((value & 0xFC) == 0x00)
                {
                    if (value & 0x01)
                        _line.Uart().ClearRx();
                    if (value & 0x02)
                        _line.Uart().ClearTx();
                }
                else if ((value & 0xFC) == 0x04)
                {
                    // LSR is not updated, unlike an FCR reset
                    if (value & 0x01)
                        _rs.ClearRx();
                    if (value & 0x02)
                        _rs.ClearTx();
                }
            }
            break;
        case kImrIsr: _imr = static_cast<uint8_t>(_imr | value); break;
        case kZibtr: _zibtr = value; break;
        case kZitor: _zitor = value; break;
        case kRibtr: _ribtr = value; break;
        case kRitor: _ritor = value; break;
        default: break;   // #C0..#C3, #CA..#CF: no write side
    }
    Task(now);
}

void ZiFi::OnFrame()
{
    _line.OnFrame();
    const uint64_t now = _line.Now();
    _rs.Advance(now);
    Task(now);
}

ZiFi::View ZiFi::GetView() const
{
    View v;
    v.api = _api;
    v.err = _err;
    v.imr = _imr;
    v.isr = _isr;
    v.zibtr = _zibtr;
    v.zitor = _zitor;
    v.ribtr = _ribtr;
    v.ritor = _ritor;
    v.selectZf = _selectZf;
    v.zfRx = _line.Uart().RxUsed();
    v.zfTx = static_cast<uint16_t>(_line.Uart().GetView().txCount);
    v.rsRx = _rs.RxUsed();
    v.rsTx = static_cast<uint16_t>(_rs.GetView().txCount);
    return v;
}

void ZiFi::SaveState(State& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.version = kStateVersion;
    out.api = _api;
    out.err = _err;
    out.selectZf = _selectZf ? 1 : 0;
    out.imr = _imr;
    out.isr = _isr;
    out.zibtr = _zibtr;
    out.zitor = _zitor;
    out.ribtr = _ribtr;
    out.ritor = _ritor;
    out.zfLastRx = _zfLastRx;
    out.rsLastRx = _rsLastRx;
}

void ZiFi::LoadState(const State& in)
{
    if (in.version != kStateVersion)
        return;
    _api = in.api;
    _err = in.err;
    _selectZf = in.selectZf != 0;
    _imr = in.imr;
    _isr = in.isr;
    _zibtr = in.zibtr;
    _zitor = in.zitor;
    _ribtr = in.ribtr;
    _ritor = in.ritor;
    _zfLastRx = in.zfLastRx;
    _rsLastRx = in.rsLastRx;
}
