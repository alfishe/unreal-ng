#pragma once

/// @file atapicdrom.h
/// @brief An ATAPI CD-ROM drive on an IDE channel (IDE design §7.5): the
/// drive is always on the bus; the disc comes and goes. A disc is a CdImage
/// (ISO, CUE/BIN, CHD: data and audio tracks); a bare block device attached
/// without one (tests, old callers) is read as one data track of 2048-byte
/// blocks (four 512-byte sectors each).
///
/// ATA side: the ATAPI signature (#14 / #EB in the cylinder registers) after a
/// reset, DEVICE RESET (#08), IDENTIFY PACKET DEVICE (#A1), PACKET (#A0);
/// IDENTIFY DEVICE (#EC) and the disk commands abort with the signature, which
/// is how drivers tell a CD drive from a disk.
///
/// Packet side (MMC / SFF-8020): TEST UNIT READY, REQUEST SENSE, INQUIRY,
/// MODE SENSE (6) / (10) and MODE SELECT (6) / (10) (pages 01h, 0Dh, 0Eh CD audio
/// control, 2Ah capabilities, 3Fh all), START STOP UNIT, PREVENT ALLOW, READ
/// CAPACITY, READ (10) / (12), SEEK (10), SYNCHRONIZE CACHE, READ SUB-CHANNEL
/// (formats 1-3), READ TOC (formats 0, 1, 2; the SFF-8020 format field),
/// READ HEADER, PLAY AUDIO (10) / (12) / MSF / TRACK INDEX, PAUSE / RESUME,
/// STOP PLAY / SCAN, READ CD and READ CD MSF (any sector type, sync / header /
/// user / EDC-ECC / C2 fields, Q or raw P-W subchannel), GET EVENT STATUS
/// NOTIFICATION, SET CD SPEED. Read-only. The audio itself: CdAudioPlayer.
///
/// Audio rules (MMC-3 PLAY AUDIO): only the start is checked - past the lead-out
/// or between sessions LBA OUT OF RANGE (05h / 21h), outside an audio track
/// ILLEGAL MODE FOR THIS TRACK (05h / 64h); an end past the disc plays to the
/// start track's session lead-out. A data track inside the range: PLAY AUDIO
/// (10) / (12) END OF USER AREA ENCOUNTERED ON THIS TRACK (05h / 63h), PLAY
/// AUDIO MSF plays the audio before it. A refused PLAY moves nothing. Multisession discs (Enhanced CD): READ TOC format 0 lists every
/// track and the last session's lead-out, format 1 the sessions, format 2 each
/// session's A0 / A1 / A2 and tracks plus the B0 / C0 pointers; nothing between
/// two sessions is readable (READ, SEEK: LBA OUT OF RANGE).
///
/// Activity LED: only the blocks of READ (10) / (12), READ CD and READ CD MSF
/// count (CountsAsActivity), as a drive's busy LED shows the head reading for
/// the host; packets, status polls and audio play do not.
///
/// Timing: every command completes when its packet arrives (the drive is
/// never BSY between commands), as the data path always has; audio starts
/// playing at the moment PLAY arrives and moves 75 frames per second of
/// emulated time from there.
///
/// Worked example (read block 16): the host writes the byte count limit 2048
/// into the cylinder registers and #A0; the drive asks for the packet (DRQ,
/// interrupt reason 1); the host writes `28 00 00 00 00 10 00 00 01 00 00 00`
/// as six words; the drive loads the block, puts 2048 into the cylinder
/// registers, interrupt reason 2 (data to the host), DRQ; the host reads 1024
/// words; the drive completes (interrupt reason 3, DRDY).

#include <memory>
#include <type_traits>

#include "emulator/io/ide/ata/atadevice.h"
#include "emulator/io/ide/ata/cdaudioplayer.h"

class CdImage;

/// A READ CD sector with its C2 and subchannel fields can be longer than the
/// 2048-byte data buffer (2352 + 296 + 96 = 2744 bytes): it waits here and goes
/// to the buffer in pieces. TTD keeps it with the audio state (CdDrive blob)
struct AtapiStage
{
    uint16_t pos = 0;   ///< bytes of the staged sector already in the data buffer
    uint16_t len = 0;   ///< bytes of the staged sector; 0: none
    uint8_t trayOpen = 0;   ///< START STOP UNIT LoEj + Start 0 opened the tray: the disc is out of the drive
    uint8_t preventRemoval = 0;  ///< PREVENT ALLOW MEDIUM REMOVAL: eject refused (bit 0 persistent prevent)
    uint8_t reserved[2] = {};
    uint8_t bytes[2816] = {};
};
static_assert(std::has_unique_object_representations_v<AtapiStage>, "AtapiStage must have no padding: its bytes are the TTD blob");

class AtapiCdrom : public AtaDevice
{
public:
    static constexpr uint16_t kBlockSize = 2048;
    static constexpr uint32_t kSectorsPerBlock = kBlockSize / kSectorSize;
    /// One READ moves at most this many blocks (its byte count fits 32 bits)
    static constexpr uint32_t kMaxBlocksPerCommand = 0xFFFFFFFFu / kBlockSize;

    /// Sense keys and codes the drive reports
    static constexpr uint8_t kSenseNotReady = 0x02;
    static constexpr uint8_t kSenseMediumError = 0x03;
    static constexpr uint8_t kSenseIllegalRequest = 0x05;
    static constexpr uint8_t kSenseUnitAttention = 0x06;
    static constexpr uint8_t kAscUnrecoveredRead = 0x11;
    static constexpr uint8_t kAscInvalidCommand = 0x20;
    static constexpr uint8_t kAscLbaOutOfRange = 0x21;
    static constexpr uint8_t kAscInvalidField = 0x24;
    static constexpr uint8_t kAscInvalidParameter = 0x26;
    static constexpr uint8_t kAscMediumChanged = 0x28;
    static constexpr uint8_t kAscCommandSequenceError = 0x2C;
    static constexpr uint8_t kAscMediumNotPresent = 0x3A;  ///< ASCQ 02h: tray open
    static constexpr uint8_t kAscMediumRemovalPrevented = 0x53;  ///< ASCQ 02h
    static constexpr uint8_t kAscEndOfUserArea = 0x63;   ///< END OF USER AREA ENCOUNTERED ON THIS TRACK
    static constexpr uint8_t kAscIllegalModeForTrack = 0x64;

    AtapiCdrom();
    ~AtapiCdrom() override;

    bool IsPresent() const override { return true; }
    bool HasDisc() const { return _medium != nullptr; }
    /// The guest opened the tray (START STOP UNIT eject): the drive reads no disc until it loads
    /// it again (LoEj + Start 1) or another disc is inserted; the medium stays in the slot
    bool TrayOpen() const { return _stage.trayOpen != 0; }
    uint32_t Blocks() const { return _medium ? static_cast<uint32_t>(_medium->SectorCount() / kSectorsPerBlock) : 0; }

    /// The disc's tracks and frames for the next AttachMedium (the media manager
    /// passes the medium's CdImage; nullptr: the block device is one data track)
    void SetDisc(CdImage* disc) { _pendingDisc = disc; }
    /// The disc in the drive (a built one for a bare block device); nullptr without a disc
    CdImage* Disc() const { return _disc; }

    /// The audio side: head, play state, page 0Eh, the mixer output
    CdAudioPlayer& Audio() { return _audio; }
    const CdAudioPlayer& Audio() const { return _audio; }

    /// TTD (CdDrive blob): the READ CD sector waiting for the data buffer
    const AtapiStage& Stage() const { return _stage; }
    void SetStage(const AtapiStage& stage) { _stage = stage; }

    void BuildIdentifyPacket(uint8_t* out) const;

protected:
    void ExecuteCommand(uint8_t command) override;
    void DataInDone() override;
    void DataOutDone() override;
    void SetSignature() override;
    uint8_t ReadyStatus() const override;
    uint8_t ResetStatus() const override { return 0; }
    void MediumChanged() override;
    void PowerOnReset() override;
    bool CountsAsActivity() const override;

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
    /// The host is to send `length` parameter bytes (MODE SELECT)
    void StartParameters(uint32_t length);
    void SendChunk();
    bool LoadBlock();
    void CompletePacket();
    void CheckCondition(uint8_t senseKey, uint8_t asc, uint8_t ascq = 0);
    /// A command that needs a disc: false (and NOT READY / UNIT ATTENTION reported) when it cannot run
    bool DiscReady();
    /// [lba, lba + blocks) touches the lead-out / lead-in between two sessions (nothing readable there)
    bool InSessionGap(uint32_t lba, uint32_t blocks) const;
    uint16_t ChunkLimit() const;

    /// region <Commands>
    void ReadData(uint32_t lba, uint32_t blocks);
    void ReadToc();
    void ReadSubChannel();
    void ReadHeader();
    void ReadCd(uint32_t lba, uint32_t blocks);
    /// PLAY AUDIO (10) / (12) / MSF / TRACK INDEX: `msf` selects the MSF form's rule for a data track in the range
    void PlayAudio(uint32_t start, uint32_t length, bool msf = false);
    void PlayAudioMsf();
    void PlayAudioTrackIndex();
    void ModeSense(bool ten);
    void ModeSelectDone();
    /// Bytes of one READ CD sector for the flags in the packet; 0 with a CHECK CONDITION already raised
    uint32_t ReadCdSectorBytes(uint32_t lba, bool& ok);
    bool LoadCdSector();
    /// Page `code` (current / changeable / default values) at `out`; its length, 0 for an unknown page
    uint32_t BuildPage(uint8_t code, uint8_t control, uint8_t* out);
    /// The formatted Q subchannel of a frame (12 bytes: 10 data + CRC)
    void BuildQ(uint32_t lba, uint8_t* q);
    /// endregion </Commands>

    /// The next piece of the staged READ CD sector into the data buffer
    void UnstagePiece();

    CdAudioPlayer _audio;
    AtapiStage _stage;
    CdImage* _pendingDisc = nullptr;
    CdImage* _disc = nullptr;
    std::unique_ptr<CdImage> _ownDisc;  ///< built over a bare block device
};
