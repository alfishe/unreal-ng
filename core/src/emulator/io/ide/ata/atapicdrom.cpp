#include "stdafx.h"

#include "atapicdrom.h"

#include <algorithm>
#include <cstring>

#include "emulator/io/ide/ata/ataidentify.h"

using namespace ata;

namespace
{
    namespace Scsi
    {
        constexpr uint8_t TestUnitReady = 0x00;
        constexpr uint8_t RequestSense = 0x03;
        constexpr uint8_t Inquiry = 0x12;
        constexpr uint8_t ModeSense6 = 0x1A;
        constexpr uint8_t StartStopUnit = 0x1B;
        constexpr uint8_t PreventAllow = 0x1E;
        constexpr uint8_t ReadCapacity = 0x25;
        constexpr uint8_t Read10 = 0x28;
        constexpr uint8_t Seek10 = 0x2B;
        constexpr uint8_t SynchronizeCache = 0x35;
        constexpr uint8_t ReadToc = 0x43;
        constexpr uint8_t GetEventStatus = 0x4A;
        constexpr uint8_t ModeSense10 = 0x5A;
        constexpr uint8_t Read12 = 0xA8;
        constexpr uint8_t SetCdSpeed = 0xBB;
    }  // namespace Scsi

    void PutBig32(uint8_t* out, uint32_t value)
    {
        out[0] = static_cast<uint8_t>(value >> 24);
        out[1] = static_cast<uint8_t>(value >> 16);
        out[2] = static_cast<uint8_t>(value >> 8);
        out[3] = static_cast<uint8_t>(value);
    }

    /// LBA → minutes / seconds / frames (150 frames of lead-in)
    void PutMsf(uint8_t* out, uint32_t lba)
    {
        const uint32_t frames = lba + 150;
        out[0] = 0;
        out[1] = static_cast<uint8_t>(frames / (60 * 75));
        out[2] = static_cast<uint8_t>((frames / 75) % 60);
        out[3] = static_cast<uint8_t>(frames % 75);
    }
}  // namespace

AtapiCdrom::AtapiCdrom() : AtaDevice(AtaDeviceKind::Cdrom)
{
    HardReset();
}

/// region <Hooks>

void AtapiCdrom::SetSignature()
{
    _s.sectorCount = 1;
    _s.lbaLow = 1;
    _s.lbaMid = kAtapiSignatureLow;
    _s.lbaHigh = kAtapiSignatureHigh;
}

uint8_t AtapiCdrom::ReadyStatus() const
{
    return Status::DRDY;
}

void AtapiCdrom::MediumChanged()
{
    // The drive stays on the bus; its next command learns of the new disc
    _s.unitAttention = 1;
}

void AtapiCdrom::ExecuteCommand(uint8_t command)
{
    switch (command)
    {
        case Command::DeviceReset:
        {
            // The device control register (nIEN) is the host's, not the drive's
            const uint8_t control = _s.control;
            HardReset();  // keeps a pending unit attention
            _s.control = control;
            break;
        }
        case Command::IdentifyPacket:
            BuildIdentifyPacket(_s.buffer);
            StartDataIn(kSectorSize);
            break;
        case Command::Packet:
            if (_s.features & 0x01)
            {
                Abort();  // DMA: not on a Spectrum
                break;
            }
            _s.byteLimit = static_cast<uint16_t>(_s.lbaMid | (_s.lbaHigh << 8));
            _s.phase = static_cast<uint8_t>(AtaPhase::PacketCommand);
            _s.bufferPos = 0;
            _s.bufferLen = 12;
            _s.sectorCount = ReasonCommand;
            _s.status = Status::DRDY | Status::DRQ;
            break;
        case Command::SetFeatures:
        case Command::StandbyImmediate:
        case Command::IdleImmediate:
        case Command::Standby:
        case Command::Idle:
        case Command::Sleep:
            Complete();
            break;
        case Command::CheckPowerMode:
            _s.sectorCount = 0xFF;
            Complete();
            break;
        default:
            // IDENTIFY DEVICE and the disk commands: aborted, the signature says "ATAPI"
            SetSignature();
            Abort();
            break;
    }
}

void AtapiCdrom::DataOutDone()
{
    if (static_cast<AtaPhase>(_s.phase) != AtaPhase::PacketCommand)
        return;
    std::memcpy(_s.cdb, _s.buffer, sizeof(_s.cdb));
    ExecutePacket();
}

void AtapiCdrom::DataInDone()
{
    if (_s.command != Command::Packet)
    {
        // IDENTIFY PACKET DEVICE: the one block has been read
        _s.phase = static_cast<uint8_t>(AtaPhase::Idle);
        _s.status = ReadyStatus();
        return;
    }

    const uint16_t sent = _s.bufferLen;
    _s.transferLeft -= std::min<uint32_t>(_s.transferLeft, sent);
    _s.bufferFill = static_cast<uint16_t>(_s.bufferFill - std::min<uint16_t>(_s.bufferFill, sent));
    if (_s.bufferFill)
        std::memmove(_s.buffer, _s.buffer + sent, _s.bufferFill);
    if (_s.transferLeft == 0)
    {
        CompletePacket();
        return;
    }
    SendChunk();
}

/// endregion </Hooks>

/// region <Packet commands>

void AtapiCdrom::ExecutePacket()
{
    const uint8_t* cdb = _s.cdb;
    const uint8_t op = cdb[0];

    // A disc change is reported once (SPC unit attention): REQUEST SENSE
    // returns it as its sense data; INQUIRY and GET EVENT STATUS NOTIFICATION
    // (MMC) pass without consuming it; any other command gets CHECK CONDITION
    if (_s.unitAttention && op == Scsi::RequestSense)
    {
        _s.unitAttention = 0;
        _s.senseKey = kSenseUnitAttention;
        _s.asc = kAscMediumChanged;
        _s.ascq = 0;
    }
    else if (_s.unitAttention && op != Scsi::Inquiry && op != Scsi::GetEventStatus)
    {
        _s.unitAttention = 0;
        CheckCondition(kSenseUnitAttention, kAscMediumChanged);
        return;
    }

    switch (op)
    {
        case Scsi::TestUnitReady:
            if (DiscReady())
                CompletePacket();
            break;

        case Scsi::RequestSense:
        {
            std::memset(_s.buffer, 0, 18);
            _s.buffer[0] = 0x70;  // current error, fixed format
            _s.buffer[2] = _s.senseKey;
            _s.buffer[7] = 10;
            _s.buffer[12] = _s.asc;
            _s.buffer[13] = _s.ascq;
            _s.senseKey = _s.asc = _s.ascq = 0;  // reported: cleared
            StartReply(std::min<uint32_t>(18, cdb[4] ? cdb[4] : 18));
            break;
        }

        case Scsi::Inquiry:
        {
            std::memset(_s.buffer, 0, 36);
            _s.buffer[0] = 0x05;  // CD-ROM
            _s.buffer[1] = 0x80;  // removable
            _s.buffer[3] = 0x21;  // response data format
            _s.buffer[4] = 31;    // additional length
            std::memcpy(_s.buffer + 8, "UNREALNG", 8);
            std::memcpy(_s.buffer + 16, "CD-ROM          ", 16);
            std::memcpy(_s.buffer + 32, "ung1", 4);
            StartReply(std::min<uint32_t>(36, cdb[4] ? cdb[4] : 36));
            break;
        }

        case Scsi::ReadCapacity:
            if (!DiscReady())
                break;
            PutBig32(_s.buffer, Blocks() ? Blocks() - 1 : 0);
            PutBig32(_s.buffer + 4, kBlockSize);
            StartReply(8);
            break;

        case Scsi::Read10:
        case Scsi::Read12:
        {
            if (!DiscReady())
                break;
            const uint32_t lba = (static_cast<uint32_t>(cdb[2]) << 24) | (static_cast<uint32_t>(cdb[3]) << 16) |
                                 (static_cast<uint32_t>(cdb[4]) << 8) | cdb[5];
            const uint32_t blocks = op == Scsi::Read10
                                        ? static_cast<uint32_t>((cdb[7] << 8) | cdb[8])
                                        : (static_cast<uint32_t>(cdb[6]) << 24) | (static_cast<uint32_t>(cdb[7]) << 16) |
                                              (static_cast<uint32_t>(cdb[8]) << 8) | cdb[9];
            if (blocks == 0)
            {
                CompletePacket();
                break;
            }
            if (lba >= Blocks() || blocks > Blocks() - lba)
            {
                CheckCondition(kSenseIllegalRequest, kAscLbaOutOfRange);
                break;
            }
            if (blocks > kMaxBlocksPerCommand)
            {
                CheckCondition(kSenseIllegalRequest, kAscInvalidField);  // 4 GiB and more in one command
                break;
            }
            _s.lba = lba;
            _s.sectorsLeft = blocks;
            _s.bufferFill = 0;
            _s.transferLeft = blocks * kBlockSize;
            SendChunk();
            break;
        }

        case Scsi::ReadToc:
        {
            if (!DiscReady())
                break;
            const uint8_t format = cdb[2] & 0x0F;
            if (format > 1)
            {
                CheckCondition(kSenseIllegalRequest, kAscInvalidField);
                break;
            }
            const bool msf = cdb[1] & 0x02;
            uint8_t* b = _s.buffer;
            std::memset(b, 0, 20);
            b[2] = 1;  // first track
            b[3] = 1;  // last track (format 1: first / last session)
            // Track 1 (a data track at LBA 0) and the lead-out (#AA)
            b[5] = 0x14;
            b[6] = 1;
            if (msf)
                PutMsf(b + 8, 0);
            size_t length = 12;
            if (format == 0)
            {
                b[13] = 0x14;
                b[14] = 0xAA;
                if (msf)
                    PutMsf(b + 16, Blocks());
                else
                    PutBig32(b + 16, Blocks());
                length = 20;
            }
            b[1] = static_cast<uint8_t>(length - 2);
            const uint32_t allocation = static_cast<uint32_t>((cdb[7] << 8) | cdb[8]);
            StartReply(std::min<uint32_t>(static_cast<uint32_t>(length), allocation ? allocation : static_cast<uint32_t>(length)));
            break;
        }

        case Scsi::ModeSense6:
            std::memset(_s.buffer, 0, 4);
            _s.buffer[0] = 3;
            _s.buffer[1] = HasDisc() ? 0x01 : 0x70;  // 120 mm data disc / no disc
            StartReply(std::min<uint32_t>(4, cdb[4] ? cdb[4] : 4));
            break;

        case Scsi::ModeSense10:
        {
            std::memset(_s.buffer, 0, 8);
            _s.buffer[1] = 6;
            _s.buffer[2] = HasDisc() ? 0x01 : 0x70;
            const uint32_t allocation = static_cast<uint32_t>((cdb[7] << 8) | cdb[8]);
            StartReply(std::min<uint32_t>(8, allocation ? allocation : 8));
            break;
        }

        case Scsi::GetEventStatus:
        {
            std::memset(_s.buffer, 0, 4);
            _s.buffer[1] = 2;     // the header alone: no event descriptor follows
            _s.buffer[2] = 0x80;  // no event available
            _s.buffer[3] = 0x10;  // media class supported
            const uint32_t allocation = static_cast<uint32_t>((cdb[7] << 8) | cdb[8]);
            StartReply(std::min<uint32_t>(4, allocation ? allocation : 4));
            break;
        }

        case Scsi::StartStopUnit:
        case Scsi::PreventAllow:
        case Scsi::Seek10:
        case Scsi::SynchronizeCache:
        case Scsi::SetCdSpeed:
            CompletePacket();
            break;

        default:
            CheckCondition(kSenseIllegalRequest, kAscInvalidCommand);
            break;
    }
}

bool AtapiCdrom::DiscReady()
{
    if (HasDisc())
        return true;
    CheckCondition(kSenseNotReady, kAscMediumNotPresent);
    return false;
}

uint16_t AtapiCdrom::ChunkLimit() const
{
    // The host's byte count limit, even; 0 (or #FFFF) means "as much as you like"
    uint16_t limit = _s.byteLimit & 0xFFFE;
    if (limit == 0 || _s.byteLimit == 0xFFFF)
        limit = 0xFFFE;
    return limit;
}

void AtapiCdrom::StartReply(uint32_t length)
{
    _s.sectorsLeft = 0;
    _s.transferLeft = length;
    _s.bufferFill = static_cast<uint16_t>(std::min<uint32_t>(length, kBlockSize));
    if (length == 0)
    {
        CompletePacket();
        return;
    }
    SendChunk();
}

bool AtapiCdrom::LoadBlock()
{
    for (uint32_t i = 0; i < kSectorsPerBlock; i++)
    {
        if (!_medium || !_medium->ReadSector(_s.lba * kSectorsPerBlock + i, _s.buffer + i * kSectorSize))
            return false;
    }
    _s.lba++;
    _s.sectorsLeft--;
    _s.bufferFill = kBlockSize;
    return true;
}

void AtapiCdrom::SendChunk()
{
    if (_s.bufferFill == 0 && _s.sectorsLeft > 0 && !LoadBlock())
    {
        CheckCondition(0x03, 0x11);  // MEDIUM ERROR, unrecovered read error
        return;
    }
    const uint16_t chunk = static_cast<uint16_t>(std::min<uint32_t>({_s.transferLeft, ChunkLimit(), _s.bufferFill}));
    _s.lbaMid = static_cast<uint8_t>(chunk);
    _s.lbaHigh = static_cast<uint8_t>(chunk >> 8);
    _s.sectorCount = ReasonDataIn;
    StartDataIn(chunk, true);
}

void AtapiCdrom::CompletePacket()
{
    _s.sectorCount = ReasonStatus;
    Complete();
}

void AtapiCdrom::CheckCondition(uint8_t senseKey, uint8_t asc, uint8_t ascq)
{
    _s.senseKey = senseKey;
    _s.asc = asc;
    _s.ascq = ascq;
    _s.sectorCount = ReasonStatus;
    Abort(static_cast<uint8_t>(senseKey << 4));
}

/// endregion </Packet commands>

void AtapiCdrom::BuildIdentifyPacket(uint8_t* out) const
{
    std::memset(out, 0, kSectorSize);
    PutWord(out, 0, 0x85C0);  // ATAPI, CD-ROM, removable, 12-byte packets
    PutString(out, 10, 10, _config.serial.empty() ? std::string("UNREALNG-CD") : _config.serial);
    PutString(out, 23, 4, "ung1");
    PutString(out, 27, 20, _config.model.empty() ? std::string("UNREAL-NG CD-ROM") : _config.model);
    PutWord(out, 49, 0x0200);  // LBA
    PutWord(out, 53, 0x0002);  // words 64-70 are valid
    PutWord(out, 64, 0x0001);  // PIO mode 3
    PutWord(out, 80, 0x001E);  // ATA / ATAPI-1 .. 4
}
