#include "neogsspi.h"

#include "emulator/io/spi/spidevice.h"

NeoGSSpi::NeoGSSpi()
{
    reset();
}

void NeoGSSpi::reset()
{
    const uint8_t old = _sctrl;
    _sctrl = SCTRL_RESET;
    applySelects(old);
}

int NeoGSSpi::byteClocks(Master master) const
{
    int divider = 2;
    switch (master)
    {
        case SD:
            divider = 2;
            break;
        case MC:
        {
            // MCSPD = {bit5, bit3}: 00 /2, 01 /4, 10 /8, 11 /16
            const int speed = ((_sctrl & SCTRL_MCSPD1) ? 2 : 0) | ((_sctrl & SCTRL_MCSPD0) ? 1 : 0);
            divider = 2 << speed;
            break;
        }
        case MD:
            divider = (_sctrl & SCTRL_MDHLF) ? 4 : 2;
            break;
    }
    return divider == 2 ? 16 : 8 * divider + 2;
}

void NeoGSSpi::deliver(Master master)
{
    MasterState& m = _m[master];
    if (!m.pending)
        return;
    m.pending = false;
    SpiDevice* device = _device[master];
    m.rx = device ? device->exchange(m.tx) : 0xFF;
}

void NeoGSSpi::sync(int64_t now)
{
    for (int i = 0; i < 3; i++)
    {
        if (_m[i].pending && _m[i].end <= now)
            deliver(static_cast<Master>(i));
    }
}

void NeoGSSpi::start(Master master, uint8_t tx, int64_t now, int64_t unitsPerCycle)
{
    sync(now);
    MasterState& m = _m[master];
    if (m.pending)
    {
        // Restart: the byte in flight is cut off on the wire
        m.pending = false;
        if (_device[master])
            _device[master]->truncatedByte();
    }
    m.tx = tx;
    m.pending = true;
    m.end = now + static_cast<int64_t>(byteClocks(master)) * unitsPerCycle;
}

void NeoGSSpi::onClockChange(int64_t now, int64_t oldUnitsPerCycle, int64_t newUnitsPerCycle)
{
    if (oldUnitsPerCycle <= 0)
        return;
    for (MasterState& m : _m)
    {
        if (!m.pending || m.end <= now)
            continue;
        const int64_t clocks = (m.end - now + oldUnitsPerCycle - 1) / oldUnitsPerCycle;
        m.end = now + clocks * newUnitsPerCycle;
    }
}

void NeoGSSpi::applySelects(uint8_t oldSctrl)
{
    const uint8_t changed = static_cast<uint8_t>(oldSctrl ^ _sctrl);
    if ((changed & SCTRL_SD_NCS) && _device[SD])
        _device[SD]->select((_sctrl & SCTRL_SD_NCS) == 0);
    if ((changed & SCTRL_MC_NCS) && _device[MC])
        _device[MC]->select((_sctrl & SCTRL_MC_NCS) == 0);
}

void NeoGSSpi::writeSctrl(uint8_t value, int64_t now)
{
    sync(now);
    const uint8_t old = _sctrl;
    const uint8_t mask = static_cast<uint8_t>(value & 0x3F);
    if (value & 0x80)
        _sctrl = static_cast<uint8_t>(_sctrl | mask);
    else
        _sctrl = static_cast<uint8_t>(_sctrl & ~mask);
    applySelects(old);
}
