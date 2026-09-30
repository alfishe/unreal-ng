#include "stdafx.h"

#include "atadisk.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "emulator/io/ide/ata/ataidentify.h"

using namespace ata;

namespace
{
    constexpr uint32_t kMaxChsCylinders = 16383;
    constexpr uint32_t kLba28Max = 0x0FFFFFFF;

}  // namespace

AtaDisk::AtaDisk() : AtaDevice(AtaDeviceKind::Disk)
{
    HardReset();
}

/// region <Geometry>

BlockGeometry AtaDisk::GeometryFor(uint64_t sectors)
{
    BlockGeometry g;
    if (sectors == 0)
        return g;
    if (sectors < 16 * 63)
    {
        // A tiny disk (tests): one head, up to 63 sectors per track
        g.heads = 1;
        g.sectors = static_cast<uint32_t>(std::min<uint64_t>(63, sectors));
    }
    else
    {
        g.heads = 16;
        g.sectors = 63;
    }
    g.cylinders = static_cast<uint32_t>(std::min<uint64_t>(kMaxChsCylinders, std::max<uint64_t>(1, sectors / (g.heads * g.sectors))));
    return g;
}

std::optional<BlockGeometry> AtaDisk::DetectProfiGeometry(IBlockDevice& medium)
{
    static const uint64_t kHeaderLbas[] = {256, 1008};
    uint8_t sector[kSectorSize];
    for (uint64_t lba : kHeaderLbas)
    {
        if (lba >= medium.SectorCount() || !medium.ReadSector(lba, sector) || std::memcmp(sector + 16, "rPfoHiDD", 8) != 0)
            continue;
        BlockGeometry g;
        g.heads = static_cast<uint32_t>((sector[0] << 8) | sector[1]);
        g.sectors = static_cast<uint32_t>((sector[2] << 8) | sector[3]);
        if (g.heads < 1 || g.heads > 16 || g.sectors < 1 || g.sectors > 255)
        {
            g.heads = 16;  // a header with nonsense in it: the layout its position implies
            g.sectors = lba == 256 ? 16 : 63;
        }
        g.cylinders = static_cast<uint32_t>(std::min<uint64_t>(0xFFFF, medium.SectorCount() / (g.heads * g.sectors)));
        return g;
    }
    return std::nullopt;
}

void AtaDisk::MediumChanged()
{
    _detected.reset();
    if (_medium && _config.profiGeometry)
    {
        _detected = DetectProfiGeometry(*_medium);
        if (!_detected)
        {
            BlockGeometry g;  // an unformatted disk: the SYS ROM's own format
            g.heads = 16;
            g.sectors = 16;
            g.cylinders = static_cast<uint32_t>(std::min<uint64_t>(0xFFFF, std::max<uint64_t>(1, Capacity() / 256)));
            _detected = g;
        }
    }
    HardReset();
}

BlockGeometry AtaDisk::DefaultGeometry() const
{
    const BlockGeometry& configured = _config.geometry;
    if (configured.heads && configured.sectors)
    {
        BlockGeometry g = configured;
        if (!g.cylinders)
            g.cylinders = static_cast<uint32_t>(std::min<uint64_t>(kMaxChsCylinders, Capacity() / (g.heads * g.sectors)));
        return g;
    }
    if (_medium)
    {
        if (auto native = _medium->NativeGeometry(); native && native->heads && native->sectors)
            return *native;
    }
    if (_detected)
        return *_detected;
    return GeometryFor(Capacity());
}

void AtaDisk::RestoreTranslation()
{
    const BlockGeometry g = DefaultGeometry();
    _s.cylinders = g.cylinders;
    _s.heads = static_cast<uint16_t>(g.heads);
    _s.sectors = static_cast<uint16_t>(g.sectors);
}

/// endregion </Geometry>

/// region <Hooks>

void AtaDisk::SetSignature()
{
    _s.sectorCount = 1;
    _s.lbaLow = 1;
    _s.lbaMid = 0;
    _s.lbaHigh = 0;
    RestoreTranslation();
}

uint8_t AtaDisk::ReadyStatus() const
{
    return Status::DRDY | Status::DSC;
}

bool AtaDisk::WriteAllowed() const
{
    return _medium && !_config.writeProtect && !_protectSwitch.load(std::memory_order_relaxed) && _medium->IsWritable();
}

uint8_t AtaDisk::BlockSize() const
{
    switch (_s.command)
    {
        case Command::ReadMultiple:
        case Command::WriteMultiple:
        case Command::ReadMultipleExt:
        case Command::WriteMultipleExt:
            return _s.multiple;
        default:
            return 1;
    }
}

bool AtaDisk::IsReadCommand() const
{
    switch (_s.command)
    {
        case Command::ReadSectors:
        case Command::ReadSectorsNoRetry:
        case Command::ReadSectorsExt:
        case Command::ReadMultiple:
        case Command::ReadMultipleExt:
            return true;
        default:
            return false;
    }
}

bool AtaDisk::IsWriteCommand() const
{
    switch (_s.command)
    {
        case Command::WriteSectors:
        case Command::WriteSectorsNoRetry:
        case Command::WriteSectorsExt:
        case Command::WriteMultiple:
        case Command::WriteMultipleExt:
            return true;
        default:
            return false;
    }
}

void AtaDisk::ExecuteCommand(uint8_t command)
{
    if (!_medium)
        return;  // not on the bus

    if (command >= Command::RecalibrateFirst && command <= Command::RecalibrateLast)
    {
        Complete();
        return;
    }
    if (command >= Command::SeekFirst && command <= Command::SeekLast)
    {
        Complete();
        return;
    }

    switch (command)
    {
        case Command::ReadSectors:
        case Command::ReadSectorsNoRetry:
            StartRead(false, false);
            break;
        case Command::ReadSectorsExt:
            StartRead(false, true);
            break;
        case Command::ReadMultiple:
            StartRead(true, false);
            break;
        case Command::ReadMultipleExt:
            StartRead(true, true);
            break;
        case Command::WriteSectors:
        case Command::WriteSectorsNoRetry:
            StartWrite(false, false);
            break;
        case Command::WriteSectorsExt:
            StartWrite(false, true);
            break;
        case Command::WriteMultiple:
            StartWrite(true, false);
            break;
        case Command::WriteMultipleExt:
            StartWrite(true, true);
            break;
        case Command::ReadVerify:
        case Command::ReadVerifyNoRetry:
            StartVerify(false);
            break;
        case Command::ReadVerifyExt:
            StartVerify(true);
            break;
        case Command::FormatTrack:
            if (!WriteAllowed())
            {
                Abort();
                break;
            }
            StartDataOut(kSectorSize);  // the interleave table: taken and dropped
            break;
        case Command::InitializeDeviceParameters:
        {
            const uint16_t heads = static_cast<uint16_t>((_s.device & 0x0F) + 1);
            const uint16_t sectors = _s.sectorCount;
            if (sectors == 0)
            {
                Abort();
                break;
            }
            _s.heads = heads;
            _s.sectors = sectors;
            _s.cylinders = static_cast<uint32_t>(std::min<uint64_t>(0xFFFF, Capacity() / (heads * sectors)));
            Complete();
            break;
        }
        case Command::SetMultipleMode:
        {
            const uint8_t count = _s.sectorCount;
            const bool powerOfTwo = count && !(count & (count - 1));
            if (count != 0 && (!powerOfTwo || count > 16))
            {
                Abort();
                break;
            }
            _s.multiple = count;
            Complete();
            break;
        }
        case Command::Identify:
            BuildIdentify(_s.buffer);
            _s.sectorsLeft = 0;
            StartDataIn(kSectorSize);
            break;
        case Command::CheckPowerMode:
            _s.sectorCount = 0xFF;  // active or idle
            Complete();
            break;
        case Command::SetFeatures:
        case Command::FlushCache:
        case Command::FlushCacheExt:
        case Command::StandbyImmediate:
        case Command::IdleImmediate:
        case Command::Standby:
        case Command::Idle:
        case Command::Sleep:
            Complete();
            break;
        default:
            // DEVICE RESET, PACKET, IDENTIFY PACKET are for ATAPI drives: a disk aborts them
            Abort();
            break;
    }
}

void AtaDisk::DataInDone()
{
    if (!IsReadCommand())
    {
        // IDENTIFY: the one block has been read
        _s.phase = static_cast<uint8_t>(AtaPhase::Idle);
        _s.status = ReadyStatus();
        return;
    }

    _s.lba++;
    _s.sectorsLeft--;
    StoreAddress(_s.lba, _s.sectorsLeft);
    if (_s.blockLeft)
        _s.blockLeft--;

    if (_s.sectorsLeft == 0)
    {
        _s.phase = static_cast<uint8_t>(AtaPhase::Idle);
        _s.status = ReadyStatus();
        return;
    }
    const bool newBlock = _s.blockLeft == 0;
    if (newBlock)
        _s.blockLeft = static_cast<uint8_t>(std::min<uint32_t>(BlockSize(), _s.sectorsLeft));
    ReadIntoBuffer(newBlock);
}

void AtaDisk::DataOutDone()
{
    if (_s.command == Command::FormatTrack)
    {
        Complete();
        return;
    }
    if (!IsWriteCommand())
        return;

    if (_s.lba >= Capacity())
    {
        Abort(Error::IDNF);
        return;
    }
    if (!WriteAllowed() || !_medium->WriteSector(_s.lba, _s.buffer))
    {
        Abort(Error::ABRT);
        return;
    }
    NotifyWrite();

    _s.lba++;
    _s.sectorsLeft--;
    StoreAddress(_s.lba, _s.sectorsLeft);
    if (_s.blockLeft)
        _s.blockLeft--;

    if (_s.sectorsLeft == 0)
    {
        Complete();
        return;
    }
    const bool newBlock = _s.blockLeft == 0;
    if (newBlock)
        _s.blockLeft = static_cast<uint8_t>(std::min<uint32_t>(BlockSize(), _s.sectorsLeft));
    StartDataOut(kSectorSize, newBlock);
}

/// endregion </Hooks>

/// region <Transfers>

bool AtaDisk::DecodeTransfer(bool ext, uint64_t& lba, uint32_t& count)
{
    _s.lba48 = ext ? 1 : 0;
    if (ext)
    {
        lba = static_cast<uint64_t>(_s.lbaLow) | (static_cast<uint64_t>(_s.lbaMid) << 8) |
              (static_cast<uint64_t>(_s.lbaHigh) << 16) | (static_cast<uint64_t>(_s.hobLbaLow) << 24) |
              (static_cast<uint64_t>(_s.hobLbaMid) << 32) | (static_cast<uint64_t>(_s.hobLbaHigh) << 40);
        count = static_cast<uint32_t>(_s.sectorCount | (_s.hobSectorCount << 8));
        if (count == 0)
            count = 65536;
    }
    else
    {
        count = _s.sectorCount ? _s.sectorCount : 256;
        if (_s.device & DeviceBits::LBA)
        {
            lba = static_cast<uint64_t>(_s.lbaLow) | (static_cast<uint64_t>(_s.lbaMid) << 8) |
                  (static_cast<uint64_t>(_s.lbaHigh) << 16) | (static_cast<uint64_t>(_s.device & 0x0F) << 24);
        }
        else
        {
            const uint32_t cylinder = static_cast<uint32_t>(_s.lbaMid | (_s.lbaHigh << 8));
            const uint32_t head = _s.device & 0x0F;
            const uint32_t sector = _s.lbaLow;
            if (sector == 0 || sector > _s.sectors || head >= _s.heads)
            {
                Abort(Error::IDNF);
                return false;
            }
            lba = (static_cast<uint64_t>(cylinder) * _s.heads + head) * _s.sectors + (sector - 1);
        }
    }
    if (lba >= Capacity())
    {
        Abort(Error::IDNF);
        return false;
    }
    return true;
}

void AtaDisk::StoreAddress(uint64_t lba, uint32_t left)
{
    if (_s.lba48)
    {
        _s.lbaLow = static_cast<uint8_t>(lba);
        _s.lbaMid = static_cast<uint8_t>(lba >> 8);
        _s.lbaHigh = static_cast<uint8_t>(lba >> 16);
        _s.hobLbaLow = static_cast<uint8_t>(lba >> 24);
        _s.hobLbaMid = static_cast<uint8_t>(lba >> 32);
        _s.hobLbaHigh = static_cast<uint8_t>(lba >> 40);
        _s.sectorCount = static_cast<uint8_t>(left);
        _s.hobSectorCount = static_cast<uint8_t>(left >> 8);
        return;
    }
    _s.sectorCount = static_cast<uint8_t>(left);
    if (_s.device & DeviceBits::LBA)
    {
        _s.lbaLow = static_cast<uint8_t>(lba);
        _s.lbaMid = static_cast<uint8_t>(lba >> 8);
        _s.lbaHigh = static_cast<uint8_t>(lba >> 16);
        _s.device = static_cast<uint8_t>((_s.device & 0xF0) | ((lba >> 24) & 0x0F));
        return;
    }
    if (!_s.heads || !_s.sectors)
        return;
    const uint64_t track = lba / _s.sectors;
    const uint32_t cylinder = static_cast<uint32_t>(track / _s.heads);
    _s.lbaLow = static_cast<uint8_t>(lba % _s.sectors + 1);
    _s.lbaMid = static_cast<uint8_t>(cylinder);
    _s.lbaHigh = static_cast<uint8_t>(cylinder >> 8);
    _s.device = static_cast<uint8_t>((_s.device & 0xF0) | (track % _s.heads));
}

void AtaDisk::StartRead(bool multiple, bool ext)
{
    if (multiple && _s.multiple == 0)
    {
        Abort();
        return;
    }
    uint64_t lba = 0;
    uint32_t count = 0;
    if (!DecodeTransfer(ext, lba, count))
        return;
    _s.lba = lba;
    _s.sectorsLeft = count;
    _s.blockLeft = static_cast<uint8_t>(std::min<uint32_t>(BlockSize(), count));
    ReadIntoBuffer(true);
}

void AtaDisk::ReadIntoBuffer(bool interrupt)
{
    if (_s.lba >= Capacity())
    {
        Abort(Error::IDNF);
        return;
    }
    if (!_medium->ReadSector(_s.lba, _s.buffer))
    {
        Abort(Error::UNC);
        return;
    }
    StartDataIn(kSectorSize, interrupt);
}

void AtaDisk::StartWrite(bool multiple, bool ext)
{
    if ((multiple && _s.multiple == 0) || !WriteAllowed())
    {
        Abort();
        return;
    }
    uint64_t lba = 0;
    uint32_t count = 0;
    if (!DecodeTransfer(ext, lba, count))
        return;
    _s.lba = lba;
    _s.sectorsLeft = count;
    _s.blockLeft = static_cast<uint8_t>(std::min<uint32_t>(BlockSize(), count));
    StartDataOut(kSectorSize);  // the first block: no interrupt, DRQ only
}

void AtaDisk::StartVerify(bool ext)
{
    uint64_t lba = 0;
    uint32_t count = 0;
    if (!DecodeTransfer(ext, lba, count))
        return;
    if (lba + count > Capacity())
    {
        StoreAddress(Capacity(), static_cast<uint32_t>(lba + count - Capacity()));
        Abort(Error::IDNF);
        return;
    }
    StoreAddress(lba + count, 0);
    Complete();
}

/// endregion </Transfers>

/// region <IDENTIFY>

void AtaDisk::BuildIdentify(uint8_t* out) const
{
    std::memset(out, 0, kSectorSize);
    const BlockGeometry g = DefaultGeometry();
    const uint64_t capacity = Capacity();
    const bool lba48 = capacity > kLba28Max;

    std::string serial = _config.serial;
    if (serial.empty())
    {
        char text[24];
        std::snprintf(text, sizeof(text), "UNREALNG-%011llX",
                      static_cast<unsigned long long>((_medium ? _medium->ContentId() : 0) & 0xFFFFFFFFFFFull));
        serial = text;
    }
    const std::string model = _config.model.empty() ? std::string("UNREAL-NG HDD") : _config.model;

    PutWord(out, 0, 0x045A);
    PutWord(out, 1, static_cast<uint16_t>(g.cylinders));
    PutWord(out, 3, static_cast<uint16_t>(g.heads));
    PutWord(out, 6, static_cast<uint16_t>(g.sectors));
    PutString(out, 10, 10, serial);
    PutString(out, 23, 4, "ung1");
    PutString(out, 27, 20, model);
    PutWord(out, 47, 0x8010);  // READ / WRITE MULTIPLE: up to 16 sectors
    PutWord(out, 49, 0x0200);  // LBA
    PutWord(out, 51, 0x0200);  // PIO mode 2
    PutWord(out, 53, 0x0001);  // words 54-58 are valid
    PutWord(out, 54, static_cast<uint16_t>(_s.cylinders));
    PutWord(out, 55, _s.heads);
    PutWord(out, 56, _s.sectors);
    const uint32_t chsCapacity = static_cast<uint32_t>(
        std::min<uint64_t>(0xFFFFFFFF, static_cast<uint64_t>(_s.cylinders) * _s.heads * _s.sectors));
    PutWord(out, 57, static_cast<uint16_t>(chsCapacity));
    PutWord(out, 58, static_cast<uint16_t>(chsCapacity >> 16));
    PutWord(out, 59, _s.multiple ? static_cast<uint16_t>(0x0100 | _s.multiple) : 0);
    const uint32_t lba28 = static_cast<uint32_t>(std::min<uint64_t>(capacity, kLba28Max));
    PutWord(out, 60, static_cast<uint16_t>(lba28));
    PutWord(out, 61, static_cast<uint16_t>(lba28 >> 16));
    PutWord(out, 80, 0x007E);  // ATA-1 .. ATA-6
    PutWord(out, 82, 0x4000);
    PutWord(out, 83, static_cast<uint16_t>(0x4000 | 0x1000 | (lba48 ? 0x0400 : 0)));  // FLUSH CACHE, LBA48
    PutWord(out, 84, 0x4000);
    PutWord(out, 85, 0x4000);
    PutWord(out, 86, static_cast<uint16_t>(0x1000 | (lba48 ? 0x0400 : 0)));
    PutWord(out, 87, 0x4000);
    if (lba48)
    {
        for (size_t i = 0; i < 4; i++)
            PutWord(out, 100 + i, static_cast<uint16_t>(capacity >> (16 * i)));
    }

    // Word 255: signature #A5, then the checksum that makes the 512 bytes sum to 0
    out[510] = 0xA5;
    uint8_t sum = 0;
    for (size_t i = 0; i < kSectorSize - 1; i++)
        sum = static_cast<uint8_t>(sum + out[i]);
    out[511] = static_cast<uint8_t>(0x100 - sum);
}

/// endregion </IDENTIFY>
