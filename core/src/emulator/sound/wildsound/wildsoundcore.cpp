#include "wildsoundcore.h"

#include <cstring>

namespace wildsound
{

namespace
{

constexpr uint8_t kDevSig[2] = {0x55, 0xAA};  ///< #E0: 0xAA55 little-endian, as TS-Labs' AYX-32
/// #EC: 10000 + YY * 100 + MM, 10000 + DD * 100 + hh, 10000 + mm * 100 + ss of the shipped build
constexpr uint16_t kVersionWords[3] = {12104, 12316, 15931};

void Put16(uint8_t*& p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p += 2;
}

void Put32(uint8_t*& p, uint32_t v)
{
    Put16(p, static_cast<uint16_t>(v));
    Put16(p, static_cast<uint16_t>(v >> 16));
}

void Put64(uint8_t*& p, uint64_t v)
{
    Put32(p, static_cast<uint32_t>(v));
    Put32(p, static_cast<uint32_t>(v >> 32));
}

uint16_t Get16(const uint8_t*& p)
{
    const uint16_t v = static_cast<uint16_t>(p[0] | (p[1] << 8));
    p += 2;
    return v;
}

uint32_t Get32(const uint8_t*& p)
{
    const uint32_t lo = Get16(p);
    return lo | (static_cast<uint32_t>(Get16(p)) << 16);
}

uint64_t Get64(const uint8_t*& p)
{
    const uint64_t lo = Get32(p);
    return lo | (static_cast<uint64_t>(Get32(p)) << 32);
}

}  // namespace

WildSoundCore::WildSoundCore()
{
    Reset(0);
    _events.clear();
}

void WildSoundCore::Reset(uint64_t t)
{
    for (Chip& c : _chips)
    {
        c.r.fill(0);
        c.addr = 0;
        // Tone duty of A / B / C: 50 % (ChipAY_WildSound.c:193-195)
        c.r[0x18] = c.r[0x19] = c.r[0x1A] = 0x80;
    }
    _chip = 0;
    _ws.fill(0);
    _latch = 0xFF;
    _lockBit = 0xFF;
    _workMode = 0xFF;
    _workCmd = 0xFF;
    _workPos = 0;
    _workStatus = 0x80;  // S_BOOT, set by chMain()
    _workError = 0;
    _workFwSize = 0;
    _work.fill(0);
    StreamClear();
    _size = 0;
    _pos = 0;
    _started = false;
    _storage.Clear();
    _lastError.clear();
    if (_mode == WildSoundMode::Xm)
        PushEvent({t, WildSoundEvent::StopXm, 0, 0, 0});
    _mode = WildSoundMode::Ay;
    PushEvent({t, WildSoundEvent::ChipReset, 0, 0, 0});
}

void WildSoundCore::PushEvent(const WildSoundEvent& e)
{
    // Nobody drains the queue without the audio side (a card with no machine): keep the newest half
    if (_events.size() >= kMaxEvents)
        _events.erase(_events.begin(), _events.begin() + kMaxEvents / 2);
    _events.push_back(e);
}

/// region <Bus>

void WildSoundCore::WriteAddress(uint8_t value, uint64_t t)
{
    (void)t;
    uint8_t look = value;
    if (value == 0xFE || value == 0xFF)
    {
        // Chip select: the other chip's own register latch is kept; this event's next byte comes from the newly
        // selected chip's previous register, and that chip's latch then holds #FE / #FF (ChipAY_Interrupt.c:675-682)
        _chip = value == 0xFE ? 1 : 0;
        look = _chips[_chip].addr;
    }
    else if (value & 0x80)
    {
        SpecialAddress(value);
    }
    _chips[_chip].addr = value;
    _latch = Lookup(look);
}

void WildSoundCore::WriteData(uint8_t value, uint64_t t)
{
    const uint8_t reg = _chips[_chip].addr;
    _ws[reg] = value;  // every data write lands in the shared area first (ChipAY_Interrupt.c:875)
    if (reg & 0x80)
    {
        SpecialData(reg, value, t);
    }
    else if (reg < 0x20)
    {
        _chips[_chip].r[reg] = value;
        PushEvent({t, WildSoundEvent::AyWrite, static_cast<uint8_t>(_chip), reg, value});
    }
    _latch = Lookup(reg);
}

uint8_t WildSoundCore::Read(uint64_t t)
{
    (void)t;
    const uint8_t result = _latch;
    if (_chips[_chip].addr & 0x80)
        SpecialRead(_chips[_chip].addr);
    _latch = Lookup(_chips[_chip].addr);  // #F1 switches the register to #F3 in its read
    return result;
}

uint8_t WildSoundCore::Lookup(uint8_t reg) const
{
    return reg < 0x20 ? _chips[_chip].r[reg] : _ws[reg];
}

/// endregion </Bus>

/// region <Special registers>

uint8_t WildSoundCore::ReadStopString(const char* s)
{
    const uint8_t b = static_cast<uint8_t>(s[_workPos]);
    if (b)
        ++_workPos;
    return b;
}

void WildSoundCore::SpecialAddress(uint8_t reg)
{
    switch (reg)
    {
        case 0xE0:  // device signature
            _workPos = 0;
            _ws[reg] = kDevSig[(_workPos++) & 1];
            break;
        case 0xE1:  // command / status
            _ws[reg] = _workStatus;
            break;
        case 0xE2:  // error
            _ws[reg] = _workError;
            break;
        case 0xE3:  // parameter
            _workPos = 0;
            break;
        case 0xE4:  // firmware data
            _workPos = 0;
            _workError = 3;  // E_BUSY
            break;
        case 0xEB:  // uptime, ms
        {
            _workPos = 0;
            uint8_t* p = _work.data();
            Put32(p, _uptimeMs);
            _ws[reg] = _work[(_workPos++) & (kWorkSize - 1)];
            break;
        }
        case 0xEC:  // version words
        {
            _workPos = 0;
            uint8_t* p = _work.data();
            for (uint16_t w : kVersionWords)
                Put16(p, w);
            _ws[reg] = _work[(_workPos++) & (kWorkSize - 1)];
            break;
        }
        case 0xED:
            _workPos = 0;
            _ws[reg] = ReadStopString(kCopyrightString);
            break;
        case 0xEE:
            _workPos = 0;
            _ws[reg] = ReadStopString(kBuildString);
            break;
        case 0xEF:  // core clock
        {
            _workPos = 0;
            uint8_t* p = _work.data();
            Put32(p, kCoreClockHz);
            _ws[reg] = _work[(_workPos++) & (kWorkSize - 1)];
            break;
        }
        case 0xF0:  // lock and start the unlock sequence
            _workMode = 0xFF;
            _workCmd = 0;
            _lockBit = 0xFF;
            _workPos = 0;
            break;
        case 0xF1:  // stream command: the status frame's byte 0 without advancing
            _workPos = 0;
            SnapshotStatus();
            _ws[reg] = _work[0];
            break;
        case 0xF2:  // stream data: a packet starts with its length
            _workPos = 0;
            break;
        case 0xF3:  // stream status
            _workPos = 0;
            SnapshotStatus();
            _ws[reg] = NextStatusByte();
            break;
        case 0xF4:  // UART byte: none
            _ws[reg] = 3;
            break;
        case 0xF5:
            _workMode = 0xFF;
            break;
        case 0xF6:  // UART receive count
            _ws[reg] = _workMode = 0;
            break;
        case 0xF7:  // UART transmit room: none (no UART on this card model)
            _ws[reg] = 0;
            break;
        default:
            break;
    }
}

void WildSoundCore::SpecialData(uint8_t reg, uint8_t value, uint64_t t)
{
    switch (reg)
    {
        case 0xE1:  // TS-Labs AYX-32 commands: kept, not acted on (firmware update, reset)
            _workCmd = value;
            _ws[reg] = _workStatus;
            break;
        case 0xE3:
            _work[(_workPos++) & (kWorkSize - 1)] = value;
            break;
        case 0xF0:
        {
            const char c = kLockString[_workPos];
            if (c)
            {
                _lockBit = 0xFF;
                if (static_cast<uint8_t>(c) == value)
                    ++_workPos;
                else
                    _workPos = 0;
            }
            else if (_lockBit == 0xFF)
            {
                _workCmd = value;  // the mode byte (nothing uses it in this firmware)
                _lockBit = 0xFE;
            }
            else if (value == 0)
            {
                _lockBit = 0;
                _ws[reg] = 0xDF;
                _workPos = 0;
            }
            else
            {
                _lockBit = 0xFF;
            }
            break;
        }
        case 0xF1:
            _work[(_workPos++) & (kWorkSize - 1)] = value;
            if (_workPos == 2)
            {
                StreamClear();
                _cmd = static_cast<uint16_t>(_work[0] | (_work[1] << 8));
                _started = false;
            }
            if (_workPos == 6)
            {
                _size = static_cast<uint32_t>(_work[2]) | (static_cast<uint32_t>(_work[3]) << 8) |
                        (static_cast<uint32_t>(_work[4]) << 16) | (static_cast<uint32_t>(_work[5]) << 24);
                _pos = 0;
            }
            break;
        case 0xF2:
            if (_busy)
                break;
            if (_workPos == 0)
            {
                _workFwSize = value;
                ++_workPos;
            }
            else if (_workFwSize)
            {
                --_workFwSize;
                if (_fifoCount < kFifoCapacity)
                    _fifo[_fifoCount++] = value;
            }
            if (_workFwSize == 0)
            {
                _busy = true;
                _workPos = 0;
                PacketDone(t);
            }
            break;
        default:
            break;
    }
}

void WildSoundCore::SpecialRead(uint8_t reg)
{
    switch (reg)
    {
        case 0xE0:
            _ws[reg] = kDevSig[(_workPos++) & 1];
            break;
        case 0xE1:
            _ws[reg] = _workStatus;
            break;
        case 0xEB:
        case 0xEC:
        case 0xEF:
            _ws[reg] = _work[(_workPos++) & (kWorkSize - 1)];
            break;
        case 0xED:
            _ws[reg] = ReadStopString(kCopyrightString);
            break;
        case 0xEE:
            _ws[reg] = ReadStopString(kBuildString);
            break;
        case 0xF0:
            if (_lockBit == 0)
                _ws[reg] = ReadStopString(kVersionString);
            break;
        case 0xF1:
            // Falls through to #F3 after consuming a frame byte (ChipAY_Interrupt.c:526-531)
            _chips[_chip].addr = 0xF3;
            NextStatusByte();
            _ws[0xF3] = NextStatusByte();
            break;
        case 0xF3:
            _ws[reg] = NextStatusByte();
            break;
        case 0xF4:
            _ws[reg] = 3;
            break;
        case 0xF6:
        case 0xF7:
            _ws[reg] = 0;
            break;
        default:
            break;
    }
}

/// endregion </Special registers>

/// region <Status frame>

uint8_t WildSoundCore::CrcStep(uint8_t b, uint8_t r, uint8_t p)
{
    switch (p & 3)
    {
        case 0:
            r ^= static_cast<uint8_t>(b + p);
            break;
        case 1:
            r ^= static_cast<uint8_t>(b - p);
            break;
        case 2:
            r = static_cast<uint8_t>(r - static_cast<uint8_t>(b + p));
            break;
        default:
            r = static_cast<uint8_t>(r + static_cast<uint8_t>(b - p));
            break;
    }
    return r;
}

void WildSoundCore::SnapshotStatus()
{
    uint8_t* p = _work.data();
    Put16(p, static_cast<uint16_t>((_cmd & 0x7FFF) | (_busy ? 0x8000 : 0)));
    Put32(p, _size);
    Put32(p, _pos);
    *p++ = static_cast<uint8_t>(kFifoCapacity - _fifoCount);
    *p++ = 0;  // nothing ever fills the card's IN FIFO
    _work[kWorkSize - 3] = 0xFA;
    _work[kWorkSize - 2] = 0x3D;
}

uint8_t WildSoundCore::NextStatusByte()
{
    const uint8_t last = kWorkSize - 1;
    const uint8_t r = static_cast<uint8_t>(_workPos >> 8);
    const uint8_t p = static_cast<uint8_t>(_workPos);
    if (p != last)
    {
        const uint8_t b = _work[p & last];
        _workPos = static_cast<uint16_t>((CrcStep(b, r, p) << 8) | static_cast<uint8_t>(p + 1));
        return b;
    }
    _work[last] = r;
    SnapshotStatus();
    _workPos = 0;
    return r;
}

/// endregion </Status frame>

/// region <Streams>

void WildSoundCore::StreamClear()
{
    _fifoCount = 0;
    _busy = false;
    _cmd = kCmdNop;
}

void WildSoundCore::Fail(const std::string& why)
{
    _lastError = why;
}

void WildSoundCore::SetMode(WildSoundMode mode, uint64_t t)
{
    if (mode == WildSoundMode::Xm)
        PushEvent({t, WildSoundEvent::PlayXm, 0, 0, 0});
    else if (_mode == WildSoundMode::Xm)
        PushEvent({t, WildSoundEvent::StopXm, 0, 0, 0});
    _mode = mode;
}

void WildSoundCore::PacketDone(uint64_t t)
{
    RunStep(t);
    _fifoCount = 0;
    _busy = false;
    ++_commandsRun;
}

void WildSoundCore::RunStep(uint64_t t)
{
    const bool empty = _fifoCount == 0;
    switch (_cmd)
    {
        case kCmdNop:
        case kCmdXmAData:
            break;
        case kCmdClear:
            SetMode(WildSoundMode::Ay, t);
            StreamClear();
            break;
        case kCmdErase:
            SetMode(WildSoundMode::Ay, t);
            _storage.Clear();
            _lastError.clear();
            break;
        case kCmdXmAStart:
            if (!_started)
            {
                _started = true;
                if (!_storage.BeginTrack(_size))
                    Fail("XMAST: " + _storage.Error());
            }
            if (!empty)
            {
                const uint32_t before = _storage.TrackReceived();
                _storage.AppendTrack(_fifo.data(), _fifoCount);
                _pos += _storage.TrackReceived() - before;
            }
            else if (_storage.TrackSize() && !_storage.EndTrack())
            {
                Fail("XMAST: " + _storage.Error());
            }
            break;
        case kCmdXmBStart:
            if (!_started)
            {
                _started = true;
                if (!_storage.BeginInstruments(_size))
                    Fail("XMBST: " + _storage.Error());
            }
            if (!empty)
            {
                _storage.AppendInstruments(_fifo.data(), _fifoCount);
                _pos += static_cast<uint32_t>(_fifoCount);
            }
            else if (!_storage.EndInstruments())
            {
                Fail("XMBST: " + _storage.Error());
            }
            break;
        case kCmdXmBData:
            for (size_t i = 0; i < _fifoCount; ++i)
                _storage.PutSampleByte(_fifo[i]);
            _pos += static_cast<uint32_t>(_fifoCount);
            break;
        case kCmdDigPlay:
            if (_storage.Playable())
                SetMode(WildSoundMode::Xm, t);
            else
                Fail("DIGPLAY without a valid track and instrument table" +
                     (_storage.Error().empty() ? std::string() : ": " + _storage.Error()));
            break;
        default:
            break;
    }
}

/// endregion </Streams>

/// region <State>

void WildSoundCore::SaveState(uint8_t* dst) const
{
    std::memset(dst, 0, kStateSize);
    uint8_t* p = dst;
    for (const Chip& c : _chips)
    {
        std::memcpy(p, c.r.data(), c.r.size());
        p += c.r.size();
        *p++ = c.addr;
    }
    *p++ = static_cast<uint8_t>(_chip);
    std::memcpy(p, _ws.data(), _ws.size());
    p += _ws.size();
    *p++ = _latch;
    *p++ = _lockBit;
    *p++ = _workMode;
    *p++ = _workCmd;
    Put16(p, _workPos);
    *p++ = _workStatus;
    *p++ = _workError;
    Put16(p, _workFwSize);
    std::memcpy(p, _work.data(), _work.size());
    p += _work.size();
    Put16(p, _cmd);
    Put32(p, _size);
    Put32(p, _pos);
    *p++ = static_cast<uint8_t>((_busy ? 1 : 0) | (_started ? 2 : 0));
    std::memcpy(p, _fifo.data(), _fifo.size());
    p += _fifo.size();
    *p++ = static_cast<uint8_t>(_fifoCount);
    *p++ = static_cast<uint8_t>(_mode);
    _storage.SaveState(p);
    p += WildSoundStorage::kStateSize;
    Put64(p, _commandsRun);
}

void WildSoundCore::LoadState(const uint8_t* src)
{
    const uint8_t* p = src;
    for (Chip& c : _chips)
    {
        std::memcpy(c.r.data(), p, c.r.size());
        p += c.r.size();
        c.addr = *p++;
    }
    _chip = *p++ & 1;
    std::memcpy(_ws.data(), p, _ws.size());
    p += _ws.size();
    _latch = *p++;
    _lockBit = *p++;
    _workMode = *p++;
    _workCmd = *p++;
    _workPos = Get16(p);
    _workStatus = *p++;
    _workError = *p++;
    _workFwSize = Get16(p);
    std::memcpy(_work.data(), p, _work.size());
    p += _work.size();
    _cmd = Get16(p);
    _size = Get32(p);
    _pos = Get32(p);
    const uint8_t flags = *p++;
    _busy = (flags & 1) != 0;
    _started = (flags & 2) != 0;
    std::memcpy(_fifo.data(), p, _fifo.size());
    p += _fifo.size();
    _fifoCount = *p++ % (kFifoCapacity + 1);
    _mode = (*p++ & 1) ? WildSoundMode::Xm : WildSoundMode::Ay;
    _storage.LoadState(p);
    p += WildSoundStorage::kStateSize;
    _commandsRun = Get64(p);
    _events.clear();
    _lastError.clear();
}

/// endregion </State>

}  // namespace wildsound
