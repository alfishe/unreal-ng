#include "stdafx.h"

#include "atapicdrom.h"

#include <algorithm>
#include <cstring>

#include "emulator/io/ide/ata/ataidentify.h"
#include "emulator/io/storage/cd/cdimage.h"

using namespace ata;

namespace
{
    namespace Scsi
    {
        constexpr uint8_t TestUnitReady = 0x00;
        constexpr uint8_t RequestSense = 0x03;
        constexpr uint8_t Inquiry = 0x12;
        constexpr uint8_t ModeSelect6 = 0x15;
        constexpr uint8_t ModeSense6 = 0x1A;
        constexpr uint8_t StartStopUnit = 0x1B;
        constexpr uint8_t PreventAllow = 0x1E;
        constexpr uint8_t ReadCapacity = 0x25;
        constexpr uint8_t Read10 = 0x28;
        constexpr uint8_t Seek10 = 0x2B;
        constexpr uint8_t SynchronizeCache = 0x35;
        constexpr uint8_t ReadSubChannel = 0x42;
        constexpr uint8_t ReadToc = 0x43;
        constexpr uint8_t ReadHeader = 0x44;
        constexpr uint8_t PlayAudio10 = 0x45;
        constexpr uint8_t PlayAudioMsf = 0x47;
        constexpr uint8_t PlayAudioTrackIndex = 0x48;
        constexpr uint8_t GetEventStatus = 0x4A;
        constexpr uint8_t PauseResume = 0x4B;
        constexpr uint8_t StopPlayScan = 0x4E;
        constexpr uint8_t ModeSelect10 = 0x55;
        constexpr uint8_t ModeSense10 = 0x5A;
        constexpr uint8_t PlayAudio12 = 0xA5;
        constexpr uint8_t Read12 = 0xA8;
        constexpr uint8_t ReadCdMsf = 0xB9;
        constexpr uint8_t SetCdSpeed = 0xBB;
        constexpr uint8_t ReadCd = 0xBE;
    }  // namespace Scsi

    /// MODE SELECT parameters are gathered here, behind the chunk the host is writing
    constexpr uint32_t kParameterStage = 1024;
    constexpr uint32_t kMaxParameters = sizeof(AtaDeviceState::buffer) - kParameterStage;

    uint16_t Be16(const uint8_t* p)
    {
        return static_cast<uint16_t>((p[0] << 8) | p[1]);
    }
    uint32_t Be24(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
    }
    uint32_t Be32(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
    }

    void PutBig16(uint8_t* out, uint16_t value)
    {
        out[0] = static_cast<uint8_t>(value >> 8);
        out[1] = static_cast<uint8_t>(value);
    }

    void PutBig32(uint8_t* out, uint32_t value)
    {
        out[0] = static_cast<uint8_t>(value >> 24);
        out[1] = static_cast<uint8_t>(value >> 16);
        out[2] = static_cast<uint8_t>(value >> 8);
        out[3] = static_cast<uint8_t>(value);
    }

    /// LBA -> 0, minutes, seconds, frames (150 frames of lead-in)
    void PutMsf(uint8_t* out, uint32_t lba)
    {
        const cd::Msf msf = cd::LbaToMsf(lba);
        out[0] = 0;
        out[1] = msf.m;
        out[2] = msf.s;
        out[3] = msf.f;
    }

    /// An address field: MSF or a big-endian LBA
    void PutAddress(uint8_t* out, uint32_t lba, bool msf)
    {
        if (msf)
            PutMsf(out, lba);
        else
            PutBig32(out, lba);
    }

    uint16_t Crc16Ccitt(const uint8_t* data, size_t length)
    {
        uint16_t crc = 0;
        for (size_t i = 0; i < length; i++)
        {
            crc = static_cast<uint16_t>(crc ^ (data[i] << 8));
            for (int bit = 0; bit < 8; bit++)
                crc = static_cast<uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
        }
        return static_cast<uint16_t>(~crc);
    }

    /// A bare block device as a one-track data disc (2048-byte blocks)
    class BlockSource : public cd::IFrameSource
    {
    public:
        explicit BlockSource(IBlockDevice& device) : _device(device) {}
        bool Read(uint64_t offset, uint8_t* dst, uint32_t length) override
        {
            uint8_t sector[IBlockDevice::kSectorSize];
            while (length)
            {
                const uint64_t lba = offset / IBlockDevice::kSectorSize;
                const uint32_t at = static_cast<uint32_t>(offset % IBlockDevice::kSectorSize);
                const uint32_t take = std::min<uint32_t>(length, static_cast<uint32_t>(IBlockDevice::kSectorSize) - at);
                if (lba >= _device.SectorCount())
                    std::memset(sector, 0, sizeof(sector));
                else if (!_device.ReadSector(lba, sector))
                    return false;
                std::memcpy(dst, sector + at, take);
                dst += take;
                offset += take;
                length -= take;
            }
            return true;
        }
        std::string Describe() const override { return _device.Describe(); }

    private:
        IBlockDevice& _device;
    };
}  // namespace

AtapiCdrom::AtapiCdrom() : AtaDevice(AtaDeviceKind::Cdrom)
{
    HardReset();
}

AtapiCdrom::~AtapiCdrom() = default;

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
    _ownDisc.reset();
    _disc = nullptr;
    if (_medium)
    {
        if (_pendingDisc)
        {
            _disc = _pendingDisc;
        }
        else
        {
            // A bare block device: one data track of its 2048-byte blocks
            std::vector<std::unique_ptr<cd::IFrameSource>> sources;
            sources.push_back(std::make_unique<BlockSource>(*_medium));
            cd::StoredTrack track;
            track.track.number = 1;
            track.track.mode = cd::TrackMode::Mode1;
            track.track.endLba = Blocks();
            track.source = 0;
            track.format = cd::StoredFormat::Cooked2048;
            track.stride = kBlockSize;
            track.storedEndLba = track.track.endLba;
            _ownDisc = std::make_unique<CdImage>(std::move(sources), std::vector<cd::StoredTrack>{track}, "block",
                                                 _medium->Describe(), _medium->ContentId());
            _disc = _ownDisc.get();
        }
    }
    _pendingDisc = nullptr;
    // A new disc: the drive reads its TOC from the lead-in, the head waits at the start
    _audio.SetDisc(_disc);
    _audio.SeekTo(0);
}

void AtapiCdrom::PowerOnReset()
{
    _audio.Reset();
    _stage = AtapiStage{};
}

bool AtapiCdrom::CountsAsActivity() const
{
    // A real drive's busy LED shows the head reading the disc for the host: data transfers
    // only. Audio play (the CD row of the mixer shows it) and status polls leave it dark
    if (static_cast<AtaPhase>(_s.phase) != AtaPhase::DataIn || _s.command != Command::Packet)
        return false;
    const uint8_t op = _s.cdb[0];
    return op == Scsi::Read10 || op == Scsi::Read12 || op == Scsi::ReadCd || op == Scsi::ReadCdMsf;
}

void AtapiCdrom::ExecuteCommand(uint8_t command)
{
    switch (command)
    {
        case Command::DeviceReset:
        {
            // The device control register (nIEN) is the host's, not the drive's;
            // a soft reset leaves the audio playing (only the reset line stops it)
            const uint8_t control = _s.control;
            const CdAudioState audio = _audio.State();
            HardReset();  // keeps a pending unit attention
            _s.control = control;
            _audio.MutableState() = audio;
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
    if (static_cast<AtaPhase>(_s.phase) == AtaPhase::PacketCommand)
    {
        std::memcpy(_s.cdb, _s.buffer, sizeof(_s.cdb));
        ExecutePacket();
        return;
    }

    // MODE SELECT parameters: gather the chunk, ask for the next or apply them
    const uint16_t chunk = _s.bufferLen;
    std::memcpy(_s.buffer + kParameterStage + _s.bufferFill, _s.buffer, chunk);
    _s.bufferFill = static_cast<uint16_t>(_s.bufferFill + chunk);
    _s.transferLeft -= std::min<uint32_t>(_s.transferLeft, chunk);
    if (_s.transferLeft == 0)
    {
        ModeSelectDone();
        return;
    }
    const uint16_t next = static_cast<uint16_t>(std::min<uint32_t>(_s.transferLeft, ChunkLimit()));
    _s.lbaMid = static_cast<uint8_t>(next);
    _s.lbaHigh = static_cast<uint8_t>(next >> 8);
    _s.sectorCount = ReasonDataOut;
    StartDataOut(next, true);
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
            if (_s.senseKey == 0 && _s.asc == 0 && _s.ascq == 0)
            {
                // No sense pending: the audio state as ASC 00h (MMC: 11h playing, 12h paused)
                const uint8_t audio = _audio.PeekStatusCode();
                if (audio == 0x11 || audio == 0x12)
                    _s.buffer[13] = audio;
            }
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
            ReadData(Be32(cdb + 2), Be16(cdb + 7));
            break;
        case Scsi::Read12:
            ReadData(Be32(cdb + 2), Be32(cdb + 6));
            break;

        case Scsi::ReadToc:
            ReadToc();
            break;
        case Scsi::ReadSubChannel:
            ReadSubChannel();
            break;
        case Scsi::ReadHeader:
            ReadHeader();
            break;

        case Scsi::PlayAudio10:
            PlayAudio(Be32(cdb + 2), Be16(cdb + 7));
            break;
        case Scsi::PlayAudio12:
            PlayAudio(Be32(cdb + 2), Be32(cdb + 6));
            break;
        case Scsi::PlayAudioMsf:
            PlayAudioMsf();
            break;
        case Scsi::PlayAudioTrackIndex:
            PlayAudioTrackIndex();
            break;

        case Scsi::PauseResume:
        {
            if (!DiscReady())
                break;
            const bool ok = (cdb[8] & 0x01) ? _audio.Resume() : _audio.Pause();
            if (ok)
                CompletePacket();
            else
                CheckCondition(kSenseIllegalRequest, kAscCommandSequenceError);  // no play in progress
            break;
        }

        case Scsi::StopPlayScan:
            if (!DiscReady())
                break;
            _audio.Stop();
            CompletePacket();
            break;

        case Scsi::ReadCd:
        {
            if (!DiscReady())
                break;
            ReadCd(Be32(cdb + 2), Be24(cdb + 6));
            break;
        }
        case Scsi::ReadCdMsf:
        {
            if (!DiscReady())
                break;
            const int32_t start = cd::MsfToLba(cdb[3], cdb[4], cdb[5]);
            const int32_t end = cd::MsfToLba(cdb[6], cdb[7], cdb[8]);
            if (start < 0 || end < start)
            {
                CheckCondition(kSenseIllegalRequest, kAscInvalidField);
                break;
            }
            ReadCd(static_cast<uint32_t>(start), static_cast<uint32_t>(end - start));
            break;
        }

        case Scsi::Seek10:
        {
            if (!DiscReady())
                break;
            const uint32_t lba = Be32(cdb + 2);
            if (lba >= Blocks() || InSessionGap(lba, 1))
            {
                CheckCondition(kSenseIllegalRequest, kAscLbaOutOfRange);
                break;
            }
            _audio.SeekTo(lba);  // a seek ends audio play
            CompletePacket();
            break;
        }

        case Scsi::ModeSense6:
            ModeSense(false);
            break;
        case Scsi::ModeSense10:
            ModeSense(true);
            break;

        case Scsi::ModeSelect6:
            StartParameters(cdb[4]);
            break;
        case Scsi::ModeSelect10:
            StartParameters(Be16(cdb + 7));
            break;

        case Scsi::GetEventStatus:
        {
            std::memset(_s.buffer, 0, 4);
            _s.buffer[1] = 2;     // the header alone: no event descriptor follows
            _s.buffer[2] = 0x80;  // no event available
            _s.buffer[3] = 0x10;  // media class supported
            StartReply(std::min<uint32_t>(4, Be16(cdb + 7) ? Be16(cdb + 7) : 4u));
            break;
        }

        case Scsi::StartStopUnit:
            // START = 0 without LOEJ spins the disc down: audio play ends
            if ((cdb[4] & 0x03) == 0)
                _audio.Stop();
            CompletePacket();
            break;

        case Scsi::PreventAllow:
        case Scsi::SynchronizeCache:
        case Scsi::SetCdSpeed:
            CompletePacket();
            break;

        default:
            CheckCondition(kSenseIllegalRequest, kAscInvalidCommand);
            break;
    }
}

void AtapiCdrom::ReadData(uint32_t lba, uint32_t blocks)
{
    if (!DiscReady())
        return;
    if (blocks == 0)
    {
        CompletePacket();
        return;
    }
    if (lba >= Blocks() || blocks > Blocks() - lba)
    {
        CheckCondition(kSenseIllegalRequest, kAscLbaOutOfRange);
        return;
    }
    if (blocks > kMaxBlocksPerCommand)
    {
        CheckCondition(kSenseIllegalRequest, kAscInvalidField);  // 4 GiB and more in one command
        return;
    }
    if (InSessionGap(lba, blocks))
    {
        CheckCondition(kSenseIllegalRequest, kAscLbaOutOfRange);  // a lead-out / lead-in between sessions
        return;
    }
    // An audio frame has no user data: the whole range must be data
    for (size_t i = 0; i < _disc->TrackCount(); i++)
    {
        const cd::Track& track = _disc->TrackAt(i);
        if (track.IsAudio() && track.pregapLba < lba + blocks && track.endLba > lba)
        {
            CheckCondition(kSenseIllegalRequest, kAscIllegalModeForTrack);
            return;
        }
    }
    _audio.Stop();  // the head leaves to read
    _s.lba = lba;
    _s.sectorsLeft = blocks;
    _s.bufferFill = 0;
    _s.transferLeft = blocks * kBlockSize;
    SendChunk();
}

void AtapiCdrom::ReadToc()
{
    if (!DiscReady())
        return;
    const uint8_t* cdb = _s.cdb;
    const bool msf = cdb[1] & 0x02;
    uint8_t format = cdb[2] & 0x0F;
    if (format == 0 && (cdb[9] >> 6))
        format = static_cast<uint8_t>(cdb[9] >> 6);  // SFF-8020: the format in the control byte
    const uint8_t start = cdb[6];
    const CdImage& disc = *_disc;
    const uint8_t first = disc.FirstTrackNumber();
    const uint8_t last = disc.LastTrackNumber();
    // Format 0 lists the tracks of every session and the last session's lead-out (MMC-3 5.23.1)
    const uint8_t lastControl = disc.TrackCount() ? disc.TrackAt(disc.TrackCount() - 1).Control() : 0x04;
    uint8_t* b = _s.buffer;
    std::memset(b, 0, 4);
    size_t length = 4;

    switch (format)
    {
        case 0:
        {
            if (start > last && start != cd::kLeadOutTrack)
            {
                CheckCondition(kSenseIllegalRequest, kAscInvalidField);
                return;
            }
            b[2] = first;
            b[3] = last;
            for (size_t i = 0; i < disc.TrackCount() && start != cd::kLeadOutTrack; i++)
            {
                const cd::Track& track = disc.TrackAt(i);
                if (track.number < start)
                    continue;
                uint8_t* d = b + length;
                std::memset(d, 0, 8);
                d[1] = static_cast<uint8_t>(0x10 | track.Control());
                d[2] = track.number;
                PutAddress(d + 4, track.startLba, msf);
                length += 8;
            }
            uint8_t* d = b + length;
            std::memset(d, 0, 8);
            d[1] = static_cast<uint8_t>(0x10 | lastControl);
            d[2] = cd::kLeadOutTrack;
            PutAddress(d + 4, disc.LeadOutLba(), msf);
            length += 8;
            break;
        }
        case 1:
        {
            // Session information (MMC-3 5.23, table 235): the first and last complete
            // session, and the first track of the last session
            const uint8_t sessions = disc.SessionCount();
            b[2] = 1;
            b[3] = sessions;
            uint8_t* d = b + length;
            std::memset(d, 0, 8);
            const int firstOfLast = disc.FirstTrackIndexOfSession(sessions);
            const cd::Track& track = disc.TrackAt(firstOfLast < 0 ? 0 : static_cast<size_t>(firstOfLast));
            d[1] = static_cast<uint8_t>(0x10 | track.Control());
            d[2] = track.number;
            PutAddress(d + 4, track.startLba, msf);
            length += 8;
            break;
        }
        case 2:
        {
            // The full TOC as each session's lead-in Q channel has it (MMC-3 5.23, table 236;
            // binary MSF): per session A0 (first track, disc type), A1 (last track), A2 (the
            // session's lead-out), its tracks; after every session but the last the mode-5
            // pointers B0 (where the next session's program area starts, the latest lead-out
            // start) and, in the first session, C0 (the first lead-in's start)
            const uint8_t sessions = disc.SessionCount();
            b[2] = 1;
            b[3] = sessions;
            auto entry = [&](uint8_t session, uint8_t adrControl, uint8_t point, cd::Msf at, uint8_t zero, cd::Msf p) {
                uint8_t* d = b + length;
                std::memset(d, 0, 11);
                d[0] = session;
                d[1] = adrControl;
                d[3] = point;
                d[4] = at.m;
                d[5] = at.s;
                d[6] = at.f;
                d[7] = zero;
                d[8] = p.m;
                d[9] = p.s;
                d[10] = p.f;
                length += 11;
            };
            for (uint8_t session = 1; session <= sessions; session++)
            {
                const int firstIndex = disc.FirstTrackIndexOfSession(session);
                const int lastIndex = disc.LastTrackIndexOfSession(session);
                if (firstIndex < 0)
                    continue;
                bool xa = false;
                for (int i = firstIndex; i <= lastIndex; i++)
                    xa = xa || disc.TrackAt(static_cast<size_t>(i)).mode == cd::TrackMode::Mode2;
                const cd::Track& firstTrack = disc.TrackAt(static_cast<size_t>(firstIndex));
                const cd::Track& lastTrack = disc.TrackAt(static_cast<size_t>(lastIndex));
                // A0 PSEC: the program area format, 00h CD-DA / CD-ROM, 20h CD-ROM XA (MMC-3 4.2.3.7)
                entry(session, static_cast<uint8_t>(0x10 | firstTrack.Control()), 0xA0, {}, 0,
                      cd::Msf{firstTrack.number, static_cast<uint8_t>(xa ? 0x20 : 0x00), 0});
                entry(session, static_cast<uint8_t>(0x10 | lastTrack.Control()), 0xA1, {}, 0, cd::Msf{lastTrack.number, 0, 0});
                entry(session, static_cast<uint8_t>(0x10 | lastTrack.Control()), 0xA2, {}, 0, cd::LbaToMsf(lastTrack.endLba));
                for (int i = firstIndex; i <= lastIndex; i++)
                {
                    const cd::Track& track = disc.TrackAt(static_cast<size_t>(i));
                    entry(session, static_cast<uint8_t>(0x10 | track.Control()), track.number, {}, 0, cd::LbaToMsf(track.startLba));
                }
                if (session < sessions)
                {
                    const int next = disc.FirstTrackIndexOfSession(static_cast<uint8_t>(session + 1));
                    const cd::Msf nextArea = cd::LbaToMsf(disc.TrackAt(static_cast<size_t>(next)).pregapLba);
                    entry(session, 0x50, 0xB0, nextArea, 0x02, cd::Msf{79, 59, 74});
                    if (session == 1)
                        entry(session, 0x50, 0xC0, {}, 0, cd::Msf{95, 0, 0});
                }
            }
            break;
        }
        default:
            // PMA, ATIP, CD-TEXT: a pressed disc has none of them
            CheckCondition(kSenseIllegalRequest, kAscInvalidField);
            return;
    }
    PutBig16(b, static_cast<uint16_t>(length - 2));
    const uint32_t allocation = Be16(cdb + 7);
    StartReply(std::min<uint32_t>(static_cast<uint32_t>(length), allocation ? allocation : static_cast<uint32_t>(length)));
}

void AtapiCdrom::ReadSubChannel()
{
    if (!DiscReady())
        return;
    const uint8_t* cdb = _s.cdb;
    const bool msf = cdb[1] & 0x02;
    const bool subQ = cdb[2] & 0x40;
    const uint8_t format = cdb[3];
    if (subQ && (format < 1 || format > 3))
    {
        CheckCondition(kSenseIllegalRequest, kAscInvalidField);
        return;
    }
    uint8_t* b = _s.buffer;
    std::memset(b, 0, 24);
    b[1] = _audio.TakeStatusCode();
    size_t length = 4;
    if (subQ)
    {
        const CdImage& disc = *_disc;
        b[4] = format;
        if (format == 1)
        {
            // Current position: past the lead-out (a play that ended there) reads as the last frame
            uint32_t lba = _audio.HeadLba();
            if (disc.LeadOutLba() && lba >= disc.LeadOutLba())
                lba = disc.LeadOutLba() - 1;
            const int index = disc.TrackIndexAt(lba);
            const cd::Track& track = disc.TrackAt(index < 0 ? 0 : static_cast<size_t>(index));
            b[5] = static_cast<uint8_t>(0x10 | track.Control());
            b[6] = track.number;
            b[7] = lba < track.startLba ? 0 : 1;
            PutAddress(b + 8, lba, msf);
            const int64_t relative = static_cast<int64_t>(lba) - track.startLba;
            if (msf)
            {
                // In the pregap the relative time counts down to INDEX 01
                const cd::Msf rel = cd::FramesToMsf(static_cast<uint32_t>(relative < 0 ? -relative : relative));
                b[12] = 0;
                b[13] = rel.m;
                b[14] = rel.s;
                b[15] = rel.f;
            }
            else
            {
                PutBig32(b + 12, static_cast<uint32_t>(relative));
            }
            length = 16;
        }
        else if (format == 2)
        {
            length = 24;  // media catalog number: MCVAL 0, none recorded
        }
        else
        {
            // ISRC of the track asked for: TCVAL 0, none recorded
            const int index = disc.TrackIndexForNumber(cdb[6]);
            if (index >= 0)
            {
                b[5] = static_cast<uint8_t>(0x10 | disc.TrackAt(static_cast<size_t>(index)).Control());
                b[6] = cdb[6];
            }
            length = 24;
        }
    }
    PutBig16(b + 2, static_cast<uint16_t>(length - 4));
    const uint32_t allocation = Be16(cdb + 7);
    if (allocation == 0)
    {
        CompletePacket();
        return;
    }
    StartReply(std::min<uint32_t>(static_cast<uint32_t>(length), allocation));
}

void AtapiCdrom::ReadHeader()
{
    if (!DiscReady())
        return;
    const uint32_t lba = Be32(_s.cdb + 2);
    const int index = _disc->TrackIndexAt(lba);
    if (index < 0)
    {
        CheckCondition(kSenseIllegalRequest, kAscLbaOutOfRange);
        return;
    }
    const cd::Track& track = _disc->TrackAt(static_cast<size_t>(index));
    std::memset(_s.buffer, 0, 8);
    _s.buffer[0] = track.IsAudio() ? 0 : track.mode == cd::TrackMode::Mode2 ? 2 : 1;
    PutAddress(_s.buffer + 4, lba, _s.cdb[1] & 0x02);
    const uint32_t allocation = Be16(_s.cdb + 7);
    StartReply(std::min<uint32_t>(8, allocation ? allocation : 8u));
}

void AtapiCdrom::PlayAudio(uint32_t start, uint32_t length, bool msf)
{
    if (!DiscReady())
        return;
    if (start == 0xFFFFFFFFu)
        start = _audio.HeadLba();  // from the head's current position
    if (length == 0)
    {
        CompletePacket();  // no play, not an error
        return;
    }
    // MMC-3 r10g 5.11-5.13 (PLAY AUDIO (10) / (12) / MSF) check the STARTING address only: not found
    // (past the lead-out, between two sessions): LOGICAL BLOCK ADDRESS OUT OF RANGE; not in an audio
    // track: ILLEGAL MODE FOR THIS TRACK. Nothing moves then: the head and the audio status stay
    if (start >= _disc->LeadOutLba() || InSessionGap(start, 1))
    {
        CheckCondition(kSenseIllegalRequest, kAscLbaOutOfRange);
        return;
    }
    const int index = _disc->TrackIndexAt(start);
    if (index < 0 || !_disc->TrackAt(static_cast<size_t>(index)).IsAudio())
    {
        CheckCondition(kSenseIllegalRequest, kAscIllegalModeForTrack);
        return;
    }
    // The end is not checked: "All contiguous audio sectors between the starting and the ending MSF
    // address shall be played" (5.13). An end past the disc - players ask for 80:00:74 or FF:FF:FF
    // ("to the end", the Sprinter's CDPLAYER.FLX) - plays to the start track's session lead-out, where
    // the head finds the lead-out
    uint64_t playEnd = std::min<uint64_t>(static_cast<uint64_t>(start) + length,
                                          _disc->SessionLeadOutLba(_disc->TrackAt(static_cast<size_t>(index)).session));
    for (size_t i = static_cast<size_t>(index); i < _disc->TrackCount(); i++)
    {
        const cd::Track& track = _disc->TrackAt(i);
        if (track.pregapLba >= playEnd)
            break;
        if (track.IsAudio())
            continue;
        // A data track inside the range. PLAY AUDIO (10) / (12): "If the CD Sub-channel mode type (data vs.
        // audio) ... changes within the transfer length" - END OF USER AREA ENCOUNTERED ON THIS TRACK.
        // PLAY AUDIO MSF has no such clause: the contiguous audio before the data track plays
        if (!msf)
        {
            CheckCondition(kSenseIllegalRequest, kAscEndOfUserArea);
            return;
        }
        playEnd = track.pregapLba;
        break;
    }
    _audio.Play(start, static_cast<uint32_t>(playEnd));
    CompletePacket();
}

void AtapiCdrom::PlayAudioMsf()
{
    if (!DiscReady())
        return;
    const uint8_t* cdb = _s.cdb;
    int64_t start = 0;
    if (cdb[3] == 0xFF && cdb[4] == 0xFF && cdb[5] == 0xFF)
        start = _audio.HeadLba();
    else
        start = std::max<int32_t>(0, cd::MsfToLba(cdb[3], cdb[4], cdb[5]));
    const int64_t end = std::max<int32_t>(0, cd::MsfToLba(cdb[6], cdb[7], cdb[8]));
    if (end == start)
    {
        CompletePacket();  // nothing to play, not an error
        return;
    }
    if (end < start)
    {
        CheckCondition(kSenseIllegalRequest, kAscInvalidField);
        return;
    }
    PlayAudio(static_cast<uint32_t>(start), static_cast<uint32_t>(end - start), /*msf*/ true);
}

void AtapiCdrom::PlayAudioTrackIndex()
{
    if (!DiscReady())
        return;
    const uint8_t* cdb = _s.cdb;
    const CdImage& disc = *_disc;
    const int first = disc.TrackIndexForNumber(cdb[4]);
    uint8_t endTrack = std::min(cdb[7], disc.LastTrackNumber());
    const int last = disc.TrackIndexForNumber(endTrack);
    if (first < 0 || last < 0 || last < first)
    {
        CheckCondition(kSenseIllegalRequest, kAscInvalidField);
        return;
    }
    const cd::Track& from = disc.TrackAt(static_cast<size_t>(first));
    const cd::Track& to = disc.TrackAt(static_cast<size_t>(last));
    const uint32_t start = cdb[5] == 0 ? from.pregapLba : from.startLba;
    // Through the end index: index 0 of the end track ends where its INDEX 01 begins
    const uint32_t end = cdb[8] == 0 ? to.startLba : to.endLba;
    if (end <= start)
    {
        CheckCondition(kSenseIllegalRequest, kAscInvalidField);
        return;
    }
    PlayAudio(start, end - start);
}

uint32_t AtapiCdrom::ReadCdSectorBytes(uint32_t lba, bool& ok)
{
    ok = false;
    const uint8_t* cdb = _s.cdb;
    const int index = _disc->TrackIndexAt(lba);
    if (index < 0)
    {
        CheckCondition(kSenseIllegalRequest, kAscLbaOutOfRange);
        return 0;
    }
    const cd::Track& track = _disc->TrackAt(static_cast<size_t>(index));
    const uint8_t expected = (cdb[1] >> 2) & 0x07;
    const bool typeOk = expected == 0 || (expected == 1 && track.IsAudio()) || (expected == 2 && track.mode == cd::TrackMode::Mode1) ||
                        ((expected == 3 || expected == 4) && track.mode == cd::TrackMode::Mode2);
    if (!typeOk)
    {
        CheckCondition(kSenseIllegalRequest, kAscIllegalModeForTrack);
        return 0;
    }
    const uint8_t flags = cdb[9];
    uint32_t bytes = 0;
    if (track.IsAudio())
    {
        bytes = (flags & 0xF8) ? cd::kFrameBytes : 0;
    }
    else
    {
        const bool mode2 = track.mode == cd::TrackMode::Mode2;
        if (flags & 0x80)
            bytes += 12;
        if (flags & 0x20)
            bytes += 4;
        if (mode2 && (flags & 0x40))
            bytes += 8;
        if (flags & 0x10)
            bytes += cd::kUserBytes;
        if (flags & 0x08)
            bytes += mode2 ? 280 : 288;
    }
    switch ((flags >> 1) & 0x03)
    {
        case 1: bytes += 294; break;
        case 2: bytes += 296; break;
        default: break;
    }
    switch (cdb[10] & 0x07)
    {
        case 0: break;
        case 1:
        case 4: bytes += cd::kSubcodeBytes; break;
        case 2: bytes += 16; break;
        default:
            CheckCondition(kSenseIllegalRequest, kAscInvalidField);
            return 0;
    }
    ok = true;
    return bytes;
}

void AtapiCdrom::ReadCd(uint32_t lba, uint32_t blocks)
{
    if (blocks == 0)
    {
        CompletePacket();
        return;
    }
    if (lba >= Blocks() || blocks > Blocks() - lba)
    {
        CheckCondition(kSenseIllegalRequest, kAscLbaOutOfRange);
        return;
    }
    // Every frame of a track has the same layout: sum the sizes track by track
    uint64_t total = 0;
    for (uint32_t at = lba; at < lba + blocks;)
    {
        const int index = _disc->TrackIndexAt(at);
        bool ok = false;
        const uint32_t bytes = ReadCdSectorBytes(at, ok);
        if (!ok)
            return;
        const uint32_t trackEnd = std::min(lba + blocks, _disc->TrackAt(static_cast<size_t>(index)).endLba);
        total += static_cast<uint64_t>(bytes) * (trackEnd - at);
        at = trackEnd;
    }
    if (total > 0xFFFFFFFFull)
    {
        CheckCondition(kSenseIllegalRequest, kAscInvalidField);
        return;
    }
    _audio.Stop();  // the head leaves to read
    _s.lba = lba;
    _s.sectorsLeft = blocks;
    _s.bufferFill = 0;
    _stage.pos = _stage.len = 0;
    _s.transferLeft = static_cast<uint32_t>(total);
    if (total == 0)
    {
        CompletePacket();
        return;
    }
    SendChunk();
}

void AtapiCdrom::BuildQ(uint32_t lba, uint8_t* q)
{
    const int index = _disc->TrackIndexAt(lba);
    const cd::Track& track = _disc->TrackAt(index < 0 ? 0 : static_cast<size_t>(index));
    const bool pregap = lba < track.startLba;
    const uint32_t relative = pregap ? track.startLba - lba : lba - track.startLba;
    const cd::Msf rel = cd::FramesToMsf(relative);
    const cd::Msf abs = cd::LbaToMsf(lba);
    q[0] = static_cast<uint8_t>((track.Control() << 4) | 0x01);
    q[1] = cd::ToBcd(track.number);
    q[2] = cd::ToBcd(pregap ? 0 : 1);
    q[3] = cd::ToBcd(rel.m);
    q[4] = cd::ToBcd(rel.s);
    q[5] = cd::ToBcd(rel.f);
    q[6] = 0;
    q[7] = cd::ToBcd(abs.m);
    q[8] = cd::ToBcd(abs.s);
    q[9] = cd::ToBcd(abs.f);
    const uint16_t crc = Crc16Ccitt(q, 10);
    q[10] = static_cast<uint8_t>(crc >> 8);
    q[11] = static_cast<uint8_t>(crc);
}

bool AtapiCdrom::LoadCdSector()
{
    bool ok = false;
    const uint32_t lba = static_cast<uint32_t>(_s.lba);
    const uint32_t bytes = ReadCdSectorBytes(lba, ok);
    if (!ok)
        return true;  // the CHECK CONDITION is raised
    uint8_t frame[cd::kFrameBytes];
    if (_disc->ReadFrame(lba, frame) != CdImage::ReadResult::Ok)
        return false;

    const int index = _disc->TrackIndexAt(lba);
    const cd::Track& track = _disc->TrackAt(static_cast<size_t>(index));
    const uint8_t flags = _s.cdb[9];
    uint8_t* out = _stage.bytes;
    size_t at = 0;
    auto put = [&](const uint8_t* src, size_t length) {
        std::memcpy(out + at, src, length);
        at += length;
    };
    if (track.IsAudio())
    {
        if (flags & 0xF8)
            put(frame, cd::kFrameBytes);
    }
    else
    {
        const bool mode2 = track.mode == cd::TrackMode::Mode2;
        if (flags & 0x80)
            put(frame, 12);
        if (flags & 0x20)
            put(frame + 12, 4);
        if (mode2 && (flags & 0x40))
            put(frame + 16, 8);
        if (flags & 0x10)
            put(frame + (mode2 ? 24 : 16), cd::kUserBytes);
        if (flags & 0x08)
            put(frame + (mode2 ? 2072 : 2064), mode2 ? 280 : 288);
    }
    const uint32_t c2 = ((flags >> 1) & 0x03) == 1 ? 294 : ((flags >> 1) & 0x03) == 2 ? 296 : 0;
    std::memset(out + at, 0, c2);  // no C2 errors
    at += c2;
    const uint8_t sub = _s.cdb[10] & 0x07;
    if (sub == 2)
    {
        uint8_t q[16] = {};
        BuildQ(lba, q);
        put(q, 16);
    }
    else if (sub == 1)
    {
        // Raw P-W: one bit of each channel per byte; P marks the pause (pregap), Q its 96 bits
        uint8_t q[12];
        BuildQ(lba, q);
        const bool pause = lba < track.startLba;
        for (int i = 0; i < 96; i++)
            out[at + i] = static_cast<uint8_t>((pause ? 0x80 : 0) | (((q[i / 8] >> (7 - i % 8)) & 1) << 6));
        at += 96;
    }
    else if (sub == 4)
    {
        std::memset(out + at, 0, 96);  // R-W: no CD+G / CD-TEXT
        at += 96;
    }
    (void)bytes;
    _s.lba++;
    _s.sectorsLeft--;
    _stage.len = static_cast<uint16_t>(at);
    _stage.pos = 0;
    UnstagePiece();
    return true;
}

void AtapiCdrom::UnstagePiece()
{
    const uint16_t piece = static_cast<uint16_t>(std::min<uint32_t>(_stage.len - _stage.pos, kBlockSize));
    std::memcpy(_s.buffer, _stage.bytes + _stage.pos, piece);
    _stage.pos = static_cast<uint16_t>(_stage.pos + piece);
    _s.bufferFill = piece;
    if (_stage.pos >= _stage.len)
        _stage.pos = _stage.len = 0;
}

void AtapiCdrom::ModeSense(bool ten)
{
    const uint8_t* cdb = _s.cdb;
    const uint8_t control = static_cast<uint8_t>(cdb[2] >> 6);
    const uint8_t page = cdb[2] & 0x3F;
    const uint32_t header = ten ? 8 : 4;
    uint8_t* b = _s.buffer;
    std::memset(b, 0, 256);
    uint32_t length = header;
    if (page == 0x3F)
    {
        for (const uint8_t code : {0x01, 0x0D, 0x0E, 0x2A})
            length += BuildPage(code, control, b + length);
    }
    else
    {
        length += BuildPage(page, control, b + length);  // an unknown page: the header alone
    }

    uint8_t medium = 0x70;  // no disc
    if (HasDisc() && _disc)
    {
        bool data = false;
        for (size_t i = 0; i < _disc->TrackCount(); i++)
            data = data || !_disc->TrackAt(i).IsAudio();
        medium = _disc->HasAudio() ? (data ? 0x03 : 0x02) : 0x01;  // 120 mm data / audio / mixed
    }
    uint32_t allocation = 0;
    if (ten)
    {
        PutBig16(b, static_cast<uint16_t>(length - 2));
        b[2] = medium;
        allocation = Be16(cdb + 7);
    }
    else
    {
        b[0] = static_cast<uint8_t>(length - 1);
        b[1] = medium;
        allocation = cdb[4];
    }
    StartReply(std::min<uint32_t>(length, allocation ? allocation : length));
}

uint32_t AtapiCdrom::BuildPage(uint8_t code, uint8_t control, uint8_t* out)
{
    const bool changeable = control == 1;
    const bool defaults = control == 2 || control == 3;  // no saved pages: the defaults
    switch (code)
    {
        case 0x01:  // read error recovery
            std::memset(out, 0, 8);
            out[0] = 0x01;
            out[1] = 6;
            if (!changeable)
                out[3] = 5;  // read retry count
            return 8;
        case 0x0D:  // CD device parameters
            std::memset(out, 0, 8);
            out[0] = 0x0D;
            out[1] = 6;
            if (!changeable)
            {
                PutBig16(out + 4, 60);  // S units per M unit
                PutBig16(out + 6, 75);  // F units per S unit
            }
            return 8;
        case 0x0E:  // CD audio control
        {
            std::memset(out, 0, 16);
            out[0] = 0x0E;
            out[1] = 14;
            if (changeable)
            {
                out[2] = 0x02;  // SOTC
                for (int port = 0; port < 4; port++)
                {
                    out[8 + 2 * port] = 0x0F;
                    out[9 + 2 * port] = 0xFF;
                }
                return 16;
            }
            const CdAudioState state = defaults ? CdAudioState{} : _audio.State();
            out[2] = static_cast<uint8_t>(0x04 | (state.sotc ? 0x02 : 0));  // IMMED: play returns at once
            for (int port = 0; port < 4; port++)
            {
                out[8 + 2 * port] = state.portSelect[port];
                out[9 + 2 * port] = state.portVolume[port];
            }
            return 16;
        }
        case 0x2A:  // capabilities and mechanical status
            std::memset(out, 0, 20);
            out[0] = 0x2A;
            out[1] = 0x12;
            if (changeable)
                return 20;
            out[4] = 0x01 | 0x10;           // audio play, mode 2 form 1
            out[5] = 0x01 | 0x02;           // CD-DA commands, accurate CD-DA stream
            out[6] = 0x01 | 0x08 | 0x20;    // lock, eject, tray loading
            out[7] = 0x01 | 0x02;           // separate volume levels, separate channel mute
            PutBig16(out + 8, 176 * 24);    // max read speed, kB/s (24x)
            PutBig16(out + 10, 256);        // volume levels
            PutBig16(out + 12, 128);        // buffer, kB
            PutBig16(out + 14, 176 * 24);   // current read speed
            return 20;
        default:
            return 0;
    }
}

void AtapiCdrom::StartParameters(uint32_t length)
{
    if (length == 0)
    {
        CompletePacket();
        return;
    }
    if (length > kMaxParameters)
    {
        CheckCondition(kSenseIllegalRequest, kAscInvalidField);
        return;
    }
    _s.transferLeft = length;
    _s.bufferFill = 0;
    const uint16_t chunk = static_cast<uint16_t>(std::min<uint32_t>(length, ChunkLimit()));
    _s.lbaMid = static_cast<uint8_t>(chunk);
    _s.lbaHigh = static_cast<uint8_t>(chunk >> 8);
    _s.sectorCount = ReasonDataOut;
    StartDataOut(chunk, true);
}

void AtapiCdrom::ModeSelectDone()
{
    const uint8_t* p = _s.buffer + kParameterStage;
    const uint32_t length = _s.bufferFill;
    const bool ten = _s.cdb[0] == Scsi::ModeSelect10;
    uint32_t at = 0;
    if (ten)
        at = length >= 8 ? 8u + Be16(p + 6) : length;
    else
        at = length >= 4 ? 4u + p[3] : length;

    while (at + 2 <= length)
    {
        const uint8_t code = p[at] & 0x3F;
        const uint32_t pageLength = p[at + 1];
        if (at + 2 + pageLength > length)
        {
            CheckCondition(kSenseIllegalRequest, kAscInvalidParameter);
            return;
        }
        if (code == 0x0E && pageLength >= 14)
        {
            CdAudioState& state = _audio.MutableState();
            state.sotc = (p[at + 2] & 0x02) ? 1 : 0;
            for (int port = 0; port < 4; port++)
            {
                state.portSelect[port] = p[at + 8 + 2 * port] & 0x0F;
                state.portVolume[port] = p[at + 9 + 2 * port];
            }
        }
        at += 2 + pageLength;  // the other pages hold nothing this drive changes
    }
    CompletePacket();
}

bool AtapiCdrom::InSessionGap(uint32_t lba, uint32_t blocks) const
{
    if (!_disc)
        return false;
    const uint64_t end = static_cast<uint64_t>(lba) + blocks;
    for (size_t i = 1; i < _disc->TrackCount(); i++)
    {
        const cd::Track& previous = _disc->TrackAt(i - 1);
        const cd::Track& next = _disc->TrackAt(i);
        if (previous.endLba < next.pregapLba && lba < next.pregapLba && end > previous.endLba)
            return true;
    }
    return false;
}

bool AtapiCdrom::DiscReady()
{
    if (HasDisc() && _disc)
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
    if (_s.cdb[0] == Scsi::ReadCd || _s.cdb[0] == Scsi::ReadCdMsf)
        return LoadCdSector();
    if (!_disc || _disc->ReadUser(static_cast<uint32_t>(_s.lba), _s.buffer) != CdImage::ReadResult::Ok)
        return false;
    _s.lba++;
    _s.sectorsLeft--;
    _s.bufferFill = kBlockSize;
    return true;
}

void AtapiCdrom::SendChunk()
{
    if (_s.bufferFill == 0 && _stage.len > 0)
    {
        UnstagePiece();  // the rest of a READ CD sector longer than the buffer
    }
    else if (_s.bufferFill == 0 && _s.sectorsLeft > 0)
    {
        if (!LoadBlock())
        {
            CheckCondition(kSenseMediumError, kAscUnrecoveredRead);
            return;
        }
        if (_s.status & Status::ERR)
            return;  // the sector was refused (a READ CD type check)
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
