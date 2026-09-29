#pragma once

/// @file atapicdrom.h
/// @brief An ATAPI CD-ROM drive on an IDE channel (IDE design §7.5): the
/// drive is always on the bus; the disc (an ISO, 2048-byte blocks = four
/// 512-byte sectors of the medium) comes and goes.
///
/// ATA side: the ATAPI signature (#14 / #EB in the cylinder registers) after a
/// reset, DEVICE RESET (#08), IDENTIFY PACKET DEVICE (#A1), PACKET (#A0);
/// IDENTIFY DEVICE (#EC) and the disk commands abort with the signature, which
/// is how drivers tell a CD drive from a disk.
///
/// Packet side (the pico-spec list): TEST UNIT READY, REQUEST SENSE, INQUIRY,
/// MODE SENSE (6) / (10), START STOP UNIT, PREVENT ALLOW, READ CAPACITY,
/// READ (10) / (12), SEEK (10), SYNCHRONIZE CACHE, READ TOC (formats 0 / 1),
/// GET EVENT STATUS NOTIFICATION, SET CD SPEED. Data CDs only, read-only.
///
/// Worked example (read block 16): the host writes the byte count limit 2048
/// into the cylinder registers and #A0; the drive asks for the packet (DRQ,
/// interrupt reason 1); the host writes `28 00 00 00 00 10 00 00 01 00 00 00`
/// as six words; the drive loads the block, puts 2048 into the cylinder
/// registers, interrupt reason 2 (data to the host), DRQ; the host reads 1024
/// words; the drive completes (interrupt reason 3, DRDY).

#include "emulator/io/ide/ata/atadevice.h"

class AtapiCdrom : public AtaDevice
{
public:
    static constexpr uint16_t kBlockSize = 2048;
    static constexpr uint32_t kSectorsPerBlock = kBlockSize / kSectorSize;
    /// One READ moves at most this many blocks (its byte count fits 32 bits)
    static constexpr uint32_t kMaxBlocksPerCommand = 0xFFFFFFFFu / kBlockSize;

    /// Sense keys and codes the drive reports
    static constexpr uint8_t kSenseNotReady = 0x02;
    static constexpr uint8_t kSenseIllegalRequest = 0x05;
    static constexpr uint8_t kSenseUnitAttention = 0x06;
    static constexpr uint8_t kAscInvalidCommand = 0x20;
    static constexpr uint8_t kAscLbaOutOfRange = 0x21;
    static constexpr uint8_t kAscInvalidField = 0x24;
    static constexpr uint8_t kAscMediumChanged = 0x28;
    static constexpr uint8_t kAscMediumNotPresent = 0x3A;

    AtapiCdrom();

    bool IsPresent() const override { return true; }
    bool HasDisc() const { return _medium != nullptr; }
    uint32_t Blocks() const { return _medium ? static_cast<uint32_t>(_medium->SectorCount() / kSectorsPerBlock) : 0; }

    void BuildIdentifyPacket(uint8_t* out) const;

protected:
    void ExecuteCommand(uint8_t command) override;
    void DataInDone() override;
    void DataOutDone() override;
    void SetSignature() override;
    uint8_t ReadyStatus() const override;
    uint8_t ResetStatus() const override { return 0; }
    void MediumChanged() override;

private:
    /// Interrupt reason (the sector count register): C/D and I/O
    enum Reason : uint8_t
    {
        ReasonDataOut = 0x00,
        ReasonCommand = 0x01,
        ReasonDataIn = 0x02,
        ReasonStatus = 0x03
    };

    void ExecutePacket();
    /// `length` bytes of the buffer go to the host (and, for READ, more blocks)
    void StartReply(uint32_t length);
    void SendChunk();
    bool LoadBlock();
    void CompletePacket();
    void CheckCondition(uint8_t senseKey, uint8_t asc, uint8_t ascq = 0);
    /// A command that needs a disc: false (and NOT READY / UNIT ATTENTION reported) when it cannot run
    bool DiscReady();
    uint16_t ChunkLimit() const;
};
