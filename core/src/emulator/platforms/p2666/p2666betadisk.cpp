#include "stdafx.h"

#include "p2666betadisk.h"

#include <algorithm>
#include <cstring>

/// region <Power-on and reset>

void P2666BetaDisk::PowerOn(uint64_t edge)
{
    // hardware_reset (the MCU's command 1 while it uploads): the register reset, which starts a restore; the upload
    // outlasts its 0.56 s, so the board comes out of it with the restore just finished
    ResetRegisters();
    _indexEpoch = edge;
    Complete(edge);
}

void P2666BetaDisk::ResetRegisters()
{
    // betadisk.vhd:184-207: the register reset (hardware_reset or system bit 2 = 0) loads a restore
    _command = 0x03;
    _sector = 1;
    _track = 0;
    _position = 0;
    _restore = true;
    _intrq = false;
    _stepIn = true;
    _nrd = true;
    _seek = _step = _readAddr = _read = _write = _sectorCmd = _trackCmd = false;
    _drq = false;
    _waitCpu = false;
    _byteEdge = 0;
    _delayEnd = 0;
}

/// endregion

/// region <State (reports)>

const char* P2666BetaDisk::Phase() const
{
    if (_restore)
        return "restore";
    if (_seek)
        return "seek";
    if (_step)
        return "step";
    if (_readAddr)
        return "read address";
    if (_read)
        return _trackCmd ? "read track" : "read sector";
    if (_write)
        return _trackCmd ? "write track" : "write sector";
    return "idle";
}

/// endregion

/// region <The controller's time>

bool P2666BetaDisk::IndexPulse(uint64_t edge) const
{
    const uint64_t phase = (edge - std::min(edge, _indexEpoch)) % kIndexPeriod;
    return phase >= kIndexOn && phase < kIndexOff;
}

uint8_t P2666BetaDisk::Status(uint64_t edge) const
{
    const bool active = _readAddr || _read || _write;
    const bool idle = !(_restore || _seek || _step || _readAddr || _read || _write || _sectorCmd || _trackCmd);
    const bool typeI = !active && !idle;
    uint8_t status = 0;
    if (typeI)
        status |= 0x20 | (_position == 0 ? 0x04 : 0x00);
    if (active ? _drq : IndexPulse(edge))
        status |= 0x02;
    if (_nrd)
        status |= 0x01;
    // P2666-FIX D-14 (disabled, hardware-reference.md §11): bit 2 drops to 0 as soon as the type I command ends, so
    // a program that tests track 0 after a restore sees "not on track 0". A WD1793 keeps it:
    //   if (idle && _position == 0) status |= 0x04;
    return status;
}

void P2666BetaDisk::Complete(uint64_t edge)
{
    (void)edge;
    // betadisk.vhd:208-236 (vg_delay_res or an intrq flag of a data command): INTRQ, not busy, read address over, a
    // restore puts the head on track 0; the read / write / sector / track flags stay
    _intrq = true;
    _nrd = false;
    _readAddr = false;
    _drq = false;
    _waitCpu = false;
    _byteEdge = 0;
    if (_restore)
    {
        _restore = false;
        _track = 0;
        _position = 0;
    }
    _seek = false;
    _step = false;
    _delayEnd = 0;
}

void P2666BetaDisk::Terminate()
{
    _read = _write = _readAddr = _restore = _seek = _step = _sectorCmd = _trackCmd = false;
    _drq = false;
    _waitCpu = false;
    _byteEdge = 0;
}

uint8_t* P2666BetaDisk::ImageByte(uint8_t cylinder, uint8_t side, uint8_t sector, uint8_t byte)
{
    // vg93_ram_addr: '1' & (#36 - pos(6 downto 1)) & pos(0) & side & (sector - 1)(3 downto 0) & byte
    if (!_resolver)
        return nullptr;
    const auto page = static_cast<uint8_t>(0x40 | ((0x36 - ((cylinder >> 1) & 0x3F)) & 0x3F));
    uint8_t* base = _resolver(_owner, page);
    if (!base)
        return nullptr;
    const unsigned offset = ((cylinder & 1u) << 13) | ((side & 1u) << 12) | (((sector - 1u) & 0x0Fu) << 8) | byte;
    return base + offset;
}

uint8_t* P2666BetaDisk::RamByte(uint8_t sector, uint16_t byte)
{
    // side = NOT system bit 4 (bit 4 = 1: the lower side, side 0)
    return ImageByte(_position, (_system & 0x10) ? 0 : 1, sector, static_cast<uint8_t>(byte));
}

void P2666BetaDisk::Advance(uint64_t edge)
{
    for (;;)
    {
        // A type I command's (or a force interrupt's) delay
        if (_delayEnd != 0 && _delayEnd <= edge && (_byteEdge == 0 || _waitCpu || _delayEnd <= _byteEdge))
        {
            Complete(_delayEnd);
            continue;
        }
        if (_byteEdge == 0 || _waitCpu || _byteEdge > edge)
            return;
        const uint64_t at = _byteEdge;
        _byteEdge = 0;
        if (_readAddr)
        {
            // radr_state 6..1: track position, side (system bit 4 as it is), sector register, length #FF, CRC #F0 #E0
            static constexpr uint8_t kFixed[3] = {0xFF, 0xF0, 0xE0};
            switch (_radrState)
            {
                case 6: _radrData = _position; break;
                case 5: _radrData = static_cast<uint8_t>((_system >> 4) & 1); break;
                case 4: _radrData = _sector; break;
                default: _radrData = kFixed[3 - std::min<uint8_t>(_radrState, 3)]; break;
            }
            _drq = true;
            _waitCpu = true;
            continue;
        }
        if (_byte < 256)
        {
            if (_read)
            {
                // The RAM slot (vg_tormoz) fetches the byte, then DRQ
                const uint8_t* src = RamByte(_ioSector, _byte);
                _ramData = src ? *src : 0xFF;
            }
            _drq = true;
            _waitCpu = true;
            continue;
        }
        // The sector's last byte went 127 edges ago
        const bool next = _read ? (_multi || _trackCmd) : _multi;
        if (next && _ioSector < 16)
        {
            _ioSector++;
            _byte = 0;
            _byteEdge = at + kByteEdges;
            continue;
        }
        Complete(at);
    }
}

/// endregion

/// region <Ports>

uint8_t P2666BetaDisk::ReadRegister(uint8_t reg, uint64_t edge, bool strobe)
{
    Advance(edge);
    switch (reg & 3)
    {
        case 0:
        {
            const uint8_t status = Status(edge);
            if (strobe && DriveA())
                _intrq = false;  // a status read clears INTRQ (drive A only: the read is in the drive's branch)
            return status;
        }
        case 1: return _track;
        case 2: return _sector;
        default: break;
    }
    // vg93_data_for_cpu, then the data read takes DRQ down (any drive: the processes test vg93_cs only)
    const uint8_t value = _readAddr ? _radrData : _ramData;
    if (strobe && _waitCpu && (_readAddr || _read))
    {
        _drq = false;
        _waitCpu = false;
        if (_readAddr)
        {
            if (--_radrState > 0)
                _byteEdge = edge + kByteEdges - 1;  // the next byte's DRQ (radr_delay reloads 127, DRQ at 1)
            else
                _byteEdge = 0, Complete(edge + 1);  // radr_delay = 126: INTRQ
        }
        else
        {
            _byte++;
            _byteEdge = edge + kByteEdges;
        }
    }
    return value;
}

void P2666BetaDisk::WriteRegister(uint8_t reg, uint8_t value, uint64_t edge)
{
    Advance(edge);
    reg &= 3;
    if (reg == 3 && _write && _waitCpu)
    {
        // The data write takes DRQ down (any drive), the RAM slot stores the data register as it is then
        if (DriveA())
            _data = value;
        if (uint8_t* dst = RamByte(_ioSector, _byte))
            *dst = _data;
        _drq = false;
        _waitCpu = false;
        _byte++;
        _byteEdge = edge + kByteEdges;
        return;
    }
    if (!DriveA())
        return;  // betadisk.vhd:240: commands and register writes for drive A only
    if (!(_system & 0x04))
        return;  // held in reset (system bit 2 = 0): the reset branch overrides every write
    switch (reg)
    {
        case 1: _track = value; return;
        case 2: _sector = value; return;
        case 3: _data = value; return;
        default: break;
    }

    _command = value;
    _intrq = false;
    // vg_delay is level-set by a type I command and cleared when it ends: a second one while it runs is no new
    // rising edge, the running delay goes on
    const auto startDelay = [&](uint64_t length) {
        if (_delayEnd == 0)
            _delayEnd = edge + length;
    };
    const auto typeI = [&]() {
        _read = _write = _sectorCmd = _trackCmd = false;
        _nrd = true;
    };
    const auto motorStart = [&]() {
        if (!(_system & 0x08))
            _indexEpoch = edge;  // motor = motor_start or motor_stop falls: the index counter restarts
    };
    switch (value >> 4)
    {
        case 0x0:  // restore
            typeI();
            _restore = true;
            startDelay(kRestoreEdges);
            break;
        case 0x1:  // seek: the head and the track register take the data register at once
            typeI();
            _seek = true;
            _position = _track = _data;
            startDelay(kTypeIEdges);
            motorStart();
            break;
        case 0x2:
        case 0x3:  // step (with update)
            typeI();
            _step = true;
            if (_stepIn)
                _position++;
            else if (_position > 0)
                _position--;
            else
            {
                startDelay(kTypeIEdges);
                motorStart();
                break;
            }
            if (value & 0x10)
                _track = _position;
            startDelay(kTypeIEdges);
            motorStart();
            break;
        case 0x4:
        case 0x5:  // step in
            typeI();
            _step = true;
            _stepIn = true;
            _position++;
            if (value & 0x10)
                _track = _position;
            startDelay(kTypeIEdges);
            break;
        case 0x6:
        case 0x7:  // step out
            typeI();
            _step = true;
            _stepIn = false;
            if (_position > 0)
            {
                _position--;
                if (value & 0x10)
                    _track = _position;
            }
            startDelay(kTypeIEdges);
            break;
        case 0xC:  // read address
            _read = _write = _sectorCmd = _trackCmd = false;
            _readAddr = true;
            _nrd = true;
            _radrState = 6;
            _waitCpu = false;
            _drq = false;
            _byteEdge = edge + kByteEdges - 1;
            break;
        case 0x8:
        case 0x9:  // read sector (bit 4: multiple)
        case 0xE:  // read track: sectors 1..16
            _write = false;
            _read = true;
            _readAddr = false;
            _trackCmd = (value >> 4) == 0xE;
            _sectorCmd = !_trackCmd;
            if (!_trackCmd)
                _multi = (value & 0x10) != 0;
            _nrd = true;
            _ioSector = _trackCmd ? 1 : _sector;
            _byte = 0;
            _waitCpu = false;
            _drq = false;
            _byteEdge = edge + kByteEdges;
            break;
        case 0xA:
        case 0xB:  // write sector (bit 4: multiple)
        case 0xF:  // write track: raw sectors from 1, one unless the last sector command was a multiple one
            _read = false;
            _write = true;
            _readAddr = false;
            _trackCmd = (value >> 4) == 0xF;
            _sectorCmd = !_trackCmd;
            if (!_trackCmd)
                _multi = (value & 0x10) != 0;
            _nrd = true;
            _ioSector = _trackCmd ? 1 : _sector;
            _byte = 0;
            _waitCpu = false;
            _drq = false;
            _byteEdge = edge + kByteEdges;
            break;
        case 0xD:  // force interrupt: INTRQ now with any of I0..I3, else when its delay ends
        {
            const bool longDelay = _restore;
            Terminate();
            _nrd = false;
            if (value & 0x0F)
                _intrq = true;
            startDelay(longDelay ? kRestoreEdges : kTypeIEdges);
            break;
        }
        default:
            break;
    }
}

uint8_t P2666BetaDisk::ReadSystem(uint64_t edge)
{
    Advance(edge);
    return static_cast<uint8_t>((_intrq ? 0x80 : 0x00) | (_drq ? 0x40 : 0x00) | 0x3F);
}

void P2666BetaDisk::WriteSystem(uint8_t value, uint64_t edge)
{
    Advance(edge);
    const bool wasReset = !(_system & 0x04);
    _system = value;
    if (!(value & 0x04))
    {
        ResetRegisters();  // held: every clock reloads the restore's delay
        return;
    }
    if (wasReset)
        _delayEnd = edge + kRestoreEdges;  // released: the restore the reset loaded runs its 0.56 s
}

/// endregion

size_t P2666BetaDisk::LoadTrd(const uint8_t* image, size_t size)
{
    // TRD: (cylinder x 2 + side) x 16 sectors of 256 bytes; the RAM disk holds 80 cylinders (pages #76..#4F)
    const size_t sectors = std::min<size_t>(size / 256, 80u * 2u * 16u);
    for (size_t i = 0; i < sectors; i++)
    {
        const auto cylinder = static_cast<uint8_t>(i / 32);
        const auto side = static_cast<uint8_t>((i / 16) & 1);
        const auto sector = static_cast<uint8_t>(i % 16 + 1);
        if (uint8_t* dst = ImageByte(cylinder, side, sector, 0))
            std::memcpy(dst, image + i * 256, 256);
    }
    return sectors;
}
