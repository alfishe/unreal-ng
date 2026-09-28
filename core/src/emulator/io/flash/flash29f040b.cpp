#include "flash29f040b.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "common/statebytes.h"

namespace
{
constexpr uint32_t kUnlockAddr1 = 0x555;
constexpr uint32_t kUnlockAddr2 = 0x2AA;
constexpr uint32_t kUnlockMask = 0x7FF; // A10..A0

constexpr double kProgramSeconds = 10e-6;
constexpr double kSectorEraseSeconds = 1.0;
constexpr double kChipEraseSeconds = 8.0;
constexpr double kEraseWindowSeconds = 50e-6;
} // namespace

using statebytes::get64;
using statebytes::put64;

Flash29F040B::Flash29F040B(double unitsPerSecond, Vendor vendor)
    : _data(SIZE, 0xFF)
    , _unitsPerSecond(unitsPerSecond)
    , _vendor(vendor)
{
}

void Flash29F040B::load(const uint8_t* data, size_t size)
{
    std::fill(_data.begin(), _data.end(), 0xFF);
    if (data && size)
        memcpy(_data.data(), data, std::min(size, SIZE));
    _modified = false;
    reset();
}

void Flash29F040B::reset()
{
    _mode = Mode::Read;
    _erasing = false;
    _toggle = 0;
    _statusData = 0;
    _eraseSectors = 0;
    _windowEnd = 0;
    _busyEnd = 0;
    _programOffset = 0;
    _programValue = 0;
}

int64_t Flash29F040B::busyUntil() const
{
    if (_mode == Mode::Busy)
        return _busyEnd;
    if (_mode == Mode::EraseWindow)
        return _windowEnd;
    return std::numeric_limits<int64_t>::max();
}

void Flash29F040B::finishIfDone(int64_t now)
{
    if (_mode == Mode::EraseWindow && now >= _windowEnd)
    {
        // The command window closed: the erase itself starts now
        int sectors = 0;
        for (size_t s = 0; s < SECTORS; s++)
            sectors += (_eraseSectors >> s) & 1;
        _mode = Mode::Busy;
        _busyEnd = _windowEnd + static_cast<int64_t>(kSectorEraseSeconds * sectors * _unitsPerSecond);
    }

    if (_mode == Mode::Busy && now >= _busyEnd)
    {
        if (_erasing)
        {
            for (size_t s = 0; s < SECTORS; s++)
            {
                if ((_eraseSectors >> s) & 1)
                    std::fill_n(_data.begin() + static_cast<std::ptrdiff_t>(s * SECTOR_SIZE), SECTOR_SIZE, 0xFF);
            }
            _modified = true;
            _erasing = false;
            _eraseSectors = 0;
            _mode = Mode::Read;
        }
        else
        {
            // Programming can only clear bits: a 1 requested over a 0 fails
            const uint8_t old = _data[_programOffset];
            _data[_programOffset] = static_cast<uint8_t>(old & _programValue);
            _modified = true;
            _mode = ((old & _programValue) != _programValue) ? Mode::Failed : Mode::Read;
        }
    }
}

void Flash29F040B::update(int64_t now)
{
    finishIfDone(now);
}

uint8_t Flash29F040B::status()
{
    _toggle ^= 0x40;
    uint8_t value = _toggle;
    if (_mode == Mode::Failed)
        value |= 0x20;
    if (_erasing)
    {
        // DQ7 = 0 during erase; DQ3 = 1 once the command window has closed
        if (_mode == Mode::Busy)
            value |= 0x08;
    }
    else
    {
        value |= static_cast<uint8_t>(~_statusData & 0x80);
    }
    return value;
}

uint8_t Flash29F040B::read(uint32_t offset, int64_t now)
{
    offset &= SIZE - 1;
    finishIfDone(now);

    switch (_mode)
    {
        case Mode::Autoselect:
            switch (offset & 0xFF)
            {
                case 0: return _vendor == Vendor::ST ? 0x20 : 0x01;
                case 1: return _vendor == Vendor::ST ? 0xE2 : 0xA4;
                case 2: return 0x00; // sector not protected
                default: return 0x00;
            }
        case Mode::EraseWindow:
        case Mode::Busy:
        case Mode::Failed:
            return status();
        default:
            // Read array, also during an unfinished command sequence
            return _data[offset];
    }
}

void Flash29F040B::startProgram(uint32_t offset, uint8_t value, int64_t now)
{
    _programOffset = offset;
    _programValue = value;
    _statusData = value;
    _erasing = false;
    _mode = Mode::Busy;
    _busyEnd = now + static_cast<int64_t>(kProgramSeconds * _unitsPerSecond);
}

void Flash29F040B::write(uint32_t offset, uint8_t value, int64_t now)
{
    offset &= SIZE - 1;
    finishIfDone(now);
    if (!_writable)
        return;

    const uint32_t low = offset & kUnlockMask;

    switch (_mode)
    {
        case Mode::Busy:
            return; // commands are ignored while the chip works

        case Mode::Failed:
        case Mode::Autoselect:
            // Only a reset leaves (plain F0, or the unlocked F0 sequence)
            if (value == 0xF0)
                _mode = Mode::Read;
            else if (value == 0xAA && low == kUnlockAddr1)
                _mode = Mode::Unlock1; // AA 55 F0 resets; AA 55 90 re-enters autoselect
            return;

        case Mode::Read:
            if (value == 0xF0)
                return;
            if (value == 0xAA && low == kUnlockAddr1)
                _mode = Mode::Unlock1;
            return;

        case Mode::Unlock1:
            _mode = (value == 0x55 && low == kUnlockAddr2) ? Mode::Unlock2 : Mode::Read;
            return;

        case Mode::Unlock2:
            if (low != kUnlockAddr1)
            {
                _mode = Mode::Read;
                return;
            }
            switch (value)
            {
                case 0x90: _mode = Mode::Autoselect; return;
                case 0xA0: _mode = Mode::ProgramSetup; return;
                case 0x80: _mode = Mode::EraseSetup; return;
                default: _mode = Mode::Read; return; // F0 and unknown commands
            }

        case Mode::ProgramSetup:
            startProgram(offset, value, now);
            return;

        case Mode::EraseSetup:
            _mode = (value == 0xAA && low == kUnlockAddr1) ? Mode::EraseUnlock1 : Mode::Read;
            return;

        case Mode::EraseUnlock1:
            _mode = (value == 0x55 && low == kUnlockAddr2) ? Mode::EraseUnlock2 : Mode::Read;
            return;

        case Mode::EraseUnlock2:
            if (value == 0x10 && low == kUnlockAddr1)
            {
                _eraseSectors = 0xFF;
                _erasing = true;
                _statusData = 0;
                _mode = Mode::Busy;
                _busyEnd = now + static_cast<int64_t>(kChipEraseSeconds * _unitsPerSecond);
                return;
            }
            if (value == 0x30)
            {
                _eraseSectors = static_cast<uint8_t>(1u << (offset / SECTOR_SIZE));
                _erasing = true;
                _statusData = 0;
                _mode = Mode::EraseWindow;
                _windowEnd = now + static_cast<int64_t>(kEraseWindowSeconds * _unitsPerSecond);
                return;
            }
            _mode = Mode::Read;
            return;

        case Mode::EraseWindow:
            // Further sector commands restart the window; anything else is
            // ignored here (erase suspend is not modelled - nothing uses it)
            if (value == 0x30)
            {
                _eraseSectors |= static_cast<uint8_t>(1u << (offset / SECTOR_SIZE));
                _windowEnd = now + static_cast<int64_t>(kEraseWindowSeconds * _unitsPerSecond);
            }
            return;
    }
}

void Flash29F040B::saveState(uint8_t* dst) const
{
    memset(dst, 0, STATE_SIZE);
    dst[0] = static_cast<uint8_t>(_mode);
    dst[2] = static_cast<uint8_t>((_erasing ? 1 : 0) | (_writable ? 2 : 0) | (_modified ? 4 : 0) |
                                  (_vendor == Vendor::AMD ? 8 : 0));
    dst[3] = _toggle;
    dst[4] = _statusData;
    dst[5] = _eraseSectors;
    dst[6] = _programValue;
    put64(dst + 7, _windowEnd);
    put64(dst + 15, _busyEnd);
    dst[23] = static_cast<uint8_t>(_programOffset);
    dst[24] = static_cast<uint8_t>(_programOffset >> 8);
    dst[25] = static_cast<uint8_t>(_programOffset >> 16);
}

void Flash29F040B::loadState(const uint8_t* src)
{
    _mode = static_cast<Mode>(src[0]);
    _erasing = (src[2] & 1) != 0;
    _writable = (src[2] & 2) != 0;
    _modified = (src[2] & 4) != 0;
    _vendor = (src[2] & 8) ? Vendor::AMD : Vendor::ST;
    _toggle = src[3];
    _statusData = src[4];
    _eraseSectors = src[5];
    _programValue = src[6];
    _windowEnd = get64(src + 7);
    _busyEnd = get64(src + 15);
    _programOffset = static_cast<uint32_t>(src[23] | (src[24] << 8) | (src[25] << 16));
}
