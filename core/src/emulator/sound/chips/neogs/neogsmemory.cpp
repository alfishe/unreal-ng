#include "neogsmemory.h"

#include <algorithm>

NeoGSMemory::NeoGSMemory(size_t ramKB, Flash29F040B* flash)
    : _ram(ramKB >= 4096 ? 4096 * 1024 : 2048 * 1024, 0x00)
    , _flash(flash)
    , _ramPageMask(ramKB >= 4096 ? 0xFF : 0x7F)
{
    rebuild();
}

void NeoGSMemory::resetRegisters()
{
    _pg[0] = 0;
    _pg[1] = 3;
    _cfg = 0x30;
    rebuild();
}

void NeoGSMemory::powerOn()
{
    std::fill(_ram.begin(), _ram.end(), 0x00);
    _pg[2] = 0;
    _pg[3] = 2;
    _mpag = 0;
    resetRegisters();
}

void NeoGSMemory::writeMpag(uint8_t value)
{
    _mpag = value;
    if (!(_cfg & CFG_EXPAG))
    {
        _pg[2] = static_cast<uint8_t>((value << 1) & 0xFE);
        _pg[3] = static_cast<uint8_t>(((value << 1) & 0xFE) | 1);
    }
    else
    {
        // Extended paging: rotate left, PG3 untouched (MPAGEX sets it)
        _pg[2] = static_cast<uint8_t>((value << 1) | (value >> 7));
    }
    rebuild();
}

void NeoGSMemory::writeMpagEx(uint8_t value)
{
    if (!(_cfg & CFG_EXPAG))
        return;
    _pg[3] = static_cast<uint8_t>((value << 1) | (value >> 7));
    rebuild();
}

void NeoGSMemory::writePage(int window, uint8_t value)
{
    _pg[window & 3] = value;
    rebuild();
}

void NeoGSMemory::setConfig(uint8_t gscfg0)
{
    _cfg = gscfg0;
    rebuild();
}

void NeoGSMemory::setPagesRaw(const uint8_t pages[4], uint8_t mpag, uint8_t cfg)
{
    for (int i = 0; i < 4; i++)
        _pg[i] = pages[i];
    _mpag = mpag;
    _cfg = cfg;
    rebuild();
}

void NeoGSMemory::rebuild()
{
    const bool romMode = !(_cfg & CFG_NOROM);
    const bool writeProtect = (_cfg & CFG_RAMRO) && (_cfg & CFG_NOROM);
    const bool flashArray = _flash && _flash->arrayMode();

    for (int w = 0; w < 4; w++)
    {
        if (romMode && w != 1)
        {
            _windowFlash[w] = true;
            _writePtr[w] = nullptr; // every write goes to the chip's command decoder
            _readPtr[w] = (flashArray) ? _flash->data() + static_cast<size_t>(_pg[w] & 0x1F) * PAGE_SIZE : nullptr;
        }
        else
        {
            _windowFlash[w] = false;
            uint8_t* base = _ram.data() + ramPageIndex(_pg[w]) * PAGE_SIZE;
            _readPtr[w] = base;
            _writePtr[w] = (writeProtect && (_pg[w] & 0x7E) == 0) ? nullptr : base;
        }
    }
}

uint32_t NeoGSMemory::physical(uint16_t addr) const
{
    const int w = addr >> 14;
    const uint32_t offset = addr & (PAGE_SIZE - 1);
    if (_windowFlash[w])
        return static_cast<uint32_t>((_pg[w] & 0x1F) * PAGE_SIZE + offset);
    return static_cast<uint32_t>(ramPageIndex(_pg[w]) * PAGE_SIZE + offset);
}

uint8_t NeoGSMemory::readFlash(int window, uint16_t addr, int64_t now)
{
    if (!_flash)
        return 0xFF;
    const uint8_t value = _flash->read(physical(addr), now);
    // A finished operation returns the chip to read-array: restore fast reads
    if (_flash->arrayMode())
        rebuild();
    (void)window;
    return value;
}

void NeoGSMemory::writeFlash(int window, uint16_t addr, uint8_t value, int64_t now)
{
    if (!_flash)
        return;
    const bool wasArray = _flash->arrayMode();
    _flash->write(physical(addr), value, now);
    if (_flash->arrayMode() != wasArray)
        rebuild();
    (void)window;
}

uint8_t NeoGSMemory::peek(uint16_t addr) const
{
    const int w = addr >> 14;
    if (_windowFlash[w])
        return _flash ? _flash->data()[physical(addr)] : 0xFF;
    return _ram[physical(addr)];
}

void NeoGSMemory::poke(uint16_t addr, uint8_t value)
{
    const int w = addr >> 14;
    if (_windowFlash[w])
    {
        if (_flash)
            _flash->data()[physical(addr)] = value;
        return;
    }
    _ram[physical(addr)] = value;
}
