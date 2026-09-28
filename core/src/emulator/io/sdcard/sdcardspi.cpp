#include "stdafx.h"

#include "sdcardspi.h"

#include <algorithm>
#include <cstring>

#include "common/statebytes.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/io/storage/sessionwritemap.h"

namespace
{
// R1 bits
constexpr uint8_t R1_IDLE = 0x01;
constexpr uint8_t R1_ILLEGAL = 0x04;
constexpr uint8_t R1_CRC_ERROR = 0x08;
constexpr uint8_t R1_PARAMETER = 0x40;
constexpr uint8_t R1_ADDRESS = 0x20;

constexpr uint8_t TOKEN_START = 0xFE;
constexpr uint8_t TOKEN_MULTI_WRITE = 0xFC;
constexpr uint8_t TOKEN_STOP_TRAN = 0xFD;

constexpr uint8_t DATA_ACCEPTED = 0x05;
constexpr uint8_t DATA_WRITE_ERROR = 0x0D;

constexpr uint64_t SDSC_LIMIT = 2ull * 1024 * 1024 * 1024;
} // namespace

SdCardSpi::~SdCardSpi()
{
    close();
}

bool SdCardSpi::open(const std::string& path, WriteMode mode, Type type)
{
    close();
    auto image = RawImage::Open(path, mode == WriteMode::Persist ? RawImage::Access::ReadWrite : RawImage::Access::ReadOnly);
    if (!image)
        return false;
    const bool inserted = insert(std::move(image), mode, type);
    _path = path;
    return inserted;
}

bool SdCardSpi::insert(std::unique_ptr<IBlockDevice> media, WriteMode mode, Type type)
{
    close();
    if (!media)
        return false;

    _writeMode = mode;
    _blocks = media->SectorCount();
    _path = media->Describe();
    if (mode == WriteMode::Session)
    {
        auto session = std::make_unique<SessionWriteMap>(std::move(media));
        _session = session.get();
        _media = std::move(session);
    }
    else
    {
        _media = std::move(media);
    }

    switch (type)
    {
        case Type::SDSC: _sdhc = false; break;
        case Type::SDHC: _sdhc = true; break;
        default: _sdhc = _blocks * BLOCK > SDSC_LIMIT; break;
    }
    powerOn();
    return true;
}

void SdCardSpi::close()
{
    _session = nullptr;
    _media.reset();
    _path.clear();
    _blocks = 0;
    powerOn();
}

void SdCardSpi::powerOn()
{
    _state = State::PowerOn;
    _mode = Mode::Command;
    _crcOn = false;
    _appCommand = false;
    _acmd41Count = 0;
    _wpViolation = false;
    _cmdLength = 0;
    _out.clear();
    _rxCount = 0;
    _multiWrite = false;
}

void SdCardSpi::select(bool selected)
{
    _selected = selected;
    if (!selected)
    {
        // Deselecting abandons a half-received command; a streaming read
        // stops being clocked out (the card keeps its state machine)
        _cmdLength = 0;
    }
}

void SdCardSpi::truncatedByte()
{
    // The master restarted mid-byte: the bit stream lost its framing. A
    // command in progress is abandoned; the host will resend.
    _cmdLength = 0;
}

bool SdCardSpi::readBlock(uint64_t block, uint8_t* out)
{
    if (!_media || block >= _blocks)
        return false;
    if (!_media->ReadSector(block, out))
    {
        // A host read error: the card still delivers a block (a real card
        // would answer with a data error token; no driver here handles one)
        memset(out, 0, BLOCK);
    }
    return true;
}

bool SdCardSpi::writeBlock(uint64_t block, const uint8_t* data)
{
    if (!_media || block >= _blocks || _writeMode == WriteMode::Off)
        return false;
    return _media->WriteSector(block, data);
}

uint8_t SdCardSpi::r1Flags() const
{
    return _state == State::Ready ? 0x00 : R1_IDLE;
}

void SdCardSpi::respondR1(uint8_t r1)
{
    // NCR: one byte of #FF before the response
    _out.push_back(0xFF);
    _out.push_back(r1);
}

void SdCardSpi::queueBlock(uint64_t block)
{
    uint8_t data[BLOCK];
    readBlock(block, data);
    for (int i = 0; i < READ_LATENCY_BYTES; i++)
        _out.push_back(0xFF);
    _out.push_back(TOKEN_START);
    _out.insert(_out.end(), data, data + BLOCK);
    const uint16_t crc = crc16(data, BLOCK);
    _out.push_back(static_cast<uint8_t>(crc >> 8));
    _out.push_back(static_cast<uint8_t>(crc));
    _blocksRead++;
}

uint8_t SdCardSpi::exchange(uint8_t mosi)
{
    if (!_media || !_selected)
        return 0xFF;

    // The card shifts its next byte out while this one comes in
    uint8_t miso = 0xFF;
    if (!_out.empty())
    {
        miso = _out.front();
        _out.pop_front();
    }

    switch (_mode)
    {
        case Mode::ReadMulti:
            // Keep the stream fed; the host may send CMD12 at any time
            if (_out.size() < BLOCK && _nextReadBlock < _blocks)
                queueBlock(_nextReadBlock++);
            break;

        case Mode::WaitToken:
            if (mosi == TOKEN_START || (_multiWrite && mosi == TOKEN_MULTI_WRITE))
            {
                _mode = Mode::ReceiveData;
                _rxCount = 0;
            }
            else if (_multiWrite && mosi == TOKEN_STOP_TRAN)
            {
                // Stop transmission: busy, then back to commands
                _out.push_back(0xFF);
                for (int i = 0; i < WRITE_BUSY_BYTES; i++)
                    _out.push_back(0x00);
                _multiWrite = false;
                _mode = Mode::Command;
            }
            return miso;

        case Mode::ReceiveData:
            _rx[_rxCount++] = mosi;
            if (_rxCount == BLOCK + 2)
                finishWrite();
            return miso;

        case Mode::Command:
            break;
    }

    // Command reception (also during CMD18 streaming, for CMD12)
    if (_cmdLength == 0)
    {
        if ((mosi & 0xC0) != 0x40)
            return miso;
    }
    _cmd[_cmdLength++] = mosi;
    if (_cmdLength == 6)
    {
        _cmdLength = 0;
        onCommand();
    }
    return miso;
}

void SdCardSpi::finishWrite()
{
    const bool ok = _writeMode != WriteMode::Off && writeBlock(_writeBlock, _rx.data());
    if (!ok)
        _wpViolation = _writeMode == WriteMode::Off;
    _out.push_back(ok ? DATA_ACCEPTED : DATA_WRITE_ERROR);
    for (int i = 0; i < WRITE_BUSY_BYTES; i++)
        _out.push_back(0x00);
    if (ok)
    {
        _blocksWritten++;
        if (_onWrite)
            _onWrite(_writeBlock);
    }
    _writeBlock++;
    _mode = _multiWrite && ok ? Mode::WaitToken : Mode::Command;
    if (!ok)
        _multiWrite = false;
}

void SdCardSpi::onCommand()
{
    const uint8_t index = _cmd[0] & 0x3F;
    const uint32_t arg = (static_cast<uint32_t>(_cmd[1]) << 24) | (static_cast<uint32_t>(_cmd[2]) << 16) |
                         (static_cast<uint32_t>(_cmd[3]) << 8) | _cmd[4];
    _lastCommand = index;
    _lastArgument = arg;

    const bool app = _appCommand;
    _appCommand = false;

    // CMD12 ends a streaming read: one stuff byte, R1, a short busy
    if (_mode == Mode::ReadMulti)
    {
        if (index == 12)
        {
            _out.clear();
            _mode = Mode::Command;
            _out.push_back(0xFF);
            _out.push_back(r1Flags());
            for (int i = 0; i < 4; i++)
                _out.push_back(0x00);
        }
        return; // anything else is ignored while streaming
    }
    _out.clear();

    const bool crcValid = _cmd[5] == static_cast<uint8_t>((crc7(_cmd, 5) << 1) | 1);

    // Not in SPI mode yet: only a CMD0 with a valid CRC gets through (in SD
    // mode every command carries a CRC); anything else is ignored silently
    if (_state == State::PowerOn && (index != 0 || !crcValid))
        return;

    if (_onCommand)
        _onCommand(index);

    // CRC: always for CMD0 and CMD8, for everything once CMD59 enabled it
    if ((_crcOn || index == 0 || index == 8) && !crcValid)
    {
        respondR1(static_cast<uint8_t>(r1Flags() | R1_CRC_ERROR));
        return;
    }

    if (app)
    {
        switch (index)
        {
            case 41: // SD_SEND_OP_COND
                if (_state == State::Idle && ++_acmd41Count > ACMD41_IDLE_RESPONSES)
                    _state = State::Ready;
                respondR1(r1Flags());
                return;
            case 13: // SD_STATUS: R2 + 64 bytes, all zero
                respondR1(r1Flags());
                _out.push_back(0x00);
                return;
            default:
                respondR1(static_cast<uint8_t>(r1Flags() | R1_ILLEGAL));
                return;
        }
    }

    switch (index)
    {
        case 0: // GO_IDLE_STATE
            _state = State::Idle;
            _acmd41Count = 0;
            _crcOn = false;
            respondR1(R1_IDLE);
            return;

        case 8: // SEND_IF_COND: R7 echoes the voltage and check pattern
            respondR1(r1Flags());
            _out.push_back(0x00);
            _out.push_back(0x00);
            _out.push_back(static_cast<uint8_t>(arg >> 8) & 0x0F);
            _out.push_back(static_cast<uint8_t>(arg));
            return;

        case 55: // APP_CMD
            _appCommand = true;
            respondR1(r1Flags());
            return;

        case 59: // CRC_ON_OFF
            _crcOn = (arg & 1) != 0;
            respondR1(r1Flags());
            return;

        case 58: // READ_OCR: power-up done (bit 31) once ready, CCS (bit 30) for SDHC
        {
            respondR1(r1Flags());
            const bool ready = _state == State::Ready;
            _out.push_back(static_cast<uint8_t>((ready ? 0x80 : 0) | (ready && _sdhc ? 0x40 : 0)));
            _out.push_back(0xFF); // 2.7-3.6 V
            _out.push_back(0x80);
            _out.push_back(0x00);
            return;
        }

        case 16: // SET_BLOCKLEN: only 512 (SDHC ignores it)
            respondR1(static_cast<uint8_t>(r1Flags() | (_sdhc || arg == BLOCK ? 0 : R1_PARAMETER)));
            return;

        case 9:  // SEND_CSD
        case 10: // SEND_CID
        {
            uint8_t reg[16];
            if (index == 9)
                buildCsd(reg);
            else
                buildCid(reg);
            respondR1(r1Flags());
            _out.push_back(0xFF);
            _out.push_back(TOKEN_START);
            _out.insert(_out.end(), reg, reg + 16);
            const uint16_t crc = crc16(reg, 16);
            _out.push_back(static_cast<uint8_t>(crc >> 8));
            _out.push_back(static_cast<uint8_t>(crc));
            return;
        }

        case 13: // SEND_STATUS: R2
            respondR1(r1Flags());
            _out.push_back(_wpViolation ? 0x20 : 0x00);
            _wpViolation = false;
            return;

        case 17: // READ_SINGLE_BLOCK
        case 18: // READ_MULTIPLE_BLOCK
        {
            if (_state != State::Ready)
            {
                respondR1(static_cast<uint8_t>(r1Flags() | R1_ILLEGAL));
                return;
            }
            const uint64_t block = blockOfArgument(arg);
            if (block >= _blocks)
            {
                respondR1(R1_ADDRESS);
                return;
            }
            respondR1(0x00);
            queueBlock(block);
            if (index == 18)
            {
                _nextReadBlock = block + 1;
                _mode = Mode::ReadMulti;
            }
            return;
        }

        case 24: // WRITE_BLOCK
        case 25: // WRITE_MULTIPLE_BLOCK
        {
            if (_state != State::Ready)
            {
                respondR1(static_cast<uint8_t>(r1Flags() | R1_ILLEGAL));
                return;
            }
            const uint64_t block = blockOfArgument(arg);
            if (block >= _blocks)
            {
                respondR1(R1_ADDRESS);
                return;
            }
            respondR1(0x00);
            _writeBlock = block;
            _multiWrite = index == 25;
            _mode = Mode::WaitToken;
            return;
        }

        case 12: // STOP_TRANSMISSION outside a read: harmless
            respondR1(r1Flags());
            return;

        default:
            respondR1(static_cast<uint8_t>(r1Flags() | R1_ILLEGAL));
            return;
    }
}

void SdCardSpi::buildCsd(uint8_t out[16]) const
{
    memset(out, 0, 16);
    if (_sdhc)
    {
        // CSD v2: C_SIZE (22 bits) = blocks / 1024 - 1
        const uint32_t cSize = static_cast<uint32_t>(std::max<uint64_t>(_blocks / 1024, 1) - 1);
        out[0] = 0x40;             // CSD_STRUCTURE = 1
        out[1] = 0x0E;             // TAAC
        out[3] = 0x32;             // TRAN_SPEED 25 MHz
        out[4] = 0x5B;             // CCC
        out[5] = 0x59;             // CCC | READ_BL_LEN = 9
        out[7] = static_cast<uint8_t>((cSize >> 16) & 0x3F);
        out[8] = static_cast<uint8_t>(cSize >> 8);
        out[9] = static_cast<uint8_t>(cSize);
        out[10] = 0x7F;
        out[11] = 0x80;
        out[12] = 0x0A;
        out[13] = 0x40;
    }
    else
    {
        // CSD v1: capacity = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 512
        uint32_t mult = 7; // x512
        uint64_t cSize = _blocks / (1ull << (mult + 2));
        while (cSize > 4096 && mult < 7)
            mult++;
        cSize = std::max<uint64_t>(cSize, 1) - 1;
        cSize = std::min<uint64_t>(cSize, 4095);
        out[0] = 0x00;
        out[1] = 0x26;
        out[3] = 0x32;
        out[4] = 0x5F;
        out[5] = 0x59; // READ_BL_LEN = 9
        out[6] = static_cast<uint8_t>(0x80 | ((cSize >> 10) & 0x03));
        out[7] = static_cast<uint8_t>(cSize >> 2);
        out[8] = static_cast<uint8_t>(((cSize & 0x03) << 6) | 0x3F);
        out[9] = static_cast<uint8_t>(0xFC | ((mult >> 1) & 0x03));
        out[10] = static_cast<uint8_t>(((mult & 1) << 7) | 0x7F);
        out[11] = 0x80;
        out[12] = 0x0A;
        out[13] = 0x40;
    }
    out[15] = static_cast<uint8_t>((crc7(out, 15) << 1) | 1);
}

void SdCardSpi::buildCid(uint8_t out[16]) const
{
    static const uint8_t cid[15] = {0x55, 'U', 'N', 'R', 'E', 'A', 'L', 'N', 0x10, 0x00, 0x00, 0x00, 0x01, 0x01, 0xA9};
    memcpy(out, cid, 15);
    out[15] = static_cast<uint8_t>((crc7(out, 15) << 1) | 1);
}

uint8_t SdCardSpi::crc7(const uint8_t* data, size_t length)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < length; i++)
    {
        uint8_t byte = data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            crc = static_cast<uint8_t>(crc << 1);
            if ((byte ^ crc) & 0x80)
                crc ^= 0x09;
            byte = static_cast<uint8_t>(byte << 1);
        }
    }
    return static_cast<uint8_t>(crc & 0x7F);
}

uint16_t SdCardSpi::crc16(const uint8_t* data, size_t length)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < length; i++)
    {
        crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
        for (int bit = 0; bit < 8; bit++)
            crc = static_cast<uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1));
    }
    return crc;
}

/// region <State snapshot>

// Layout: 0 version, 1 state, 2 mode, 3 flags, 4 acmd41Count, 5..10 cmd,
// 11 cmdLength, 12 lastCommand, 13 lastArgument u32, 17 nextReadBlock u64,
// 25 writeBlock u64, 33 rxCount u16, 35 blocksRead u64, 43 blocksWritten u64,
// 51 image blocks u64 (identity check only), 59 out length u16,
// 64 rx (514), 578 out (up to 2048)
namespace
{
constexpr size_t kStRx = 64;
constexpr size_t kStOut = kStRx + SdCardSpi::BLOCK + 2;
static_assert(kStOut + SdCardSpi::STATE_OUT_MAX <= SdCardSpi::STATE_SIZE);
} // namespace

void SdCardSpi::saveState(uint8_t* dst) const
{
    using namespace statebytes;
    memset(dst, 0, STATE_SIZE);
    dst[0] = 1;
    dst[1] = static_cast<uint8_t>(_state);
    dst[2] = static_cast<uint8_t>(_mode);
    dst[3] = static_cast<uint8_t>((_selected ? 1 : 0) | (_crcOn ? 2 : 0) | (_appCommand ? 4 : 0) | (_wpViolation ? 8 : 0) |
                                  (_multiWrite ? 16 : 0));
    dst[4] = static_cast<uint8_t>(_acmd41Count);
    memcpy(dst + 5, _cmd, 6);
    dst[11] = static_cast<uint8_t>(_cmdLength);
    dst[12] = _lastCommand;
    put32(dst + 13, _lastArgument);
    putU64(dst + 17, _nextReadBlock);
    putU64(dst + 25, _writeBlock);
    put16(dst + 33, static_cast<uint16_t>(_rxCount));
    putU64(dst + 35, _blocksRead);
    putU64(dst + 43, _blocksWritten);
    putU64(dst + 51, _blocks);
    const size_t out = std::min(_out.size(), STATE_OUT_MAX); // never more by construction
    put16(dst + 59, static_cast<uint16_t>(out));
    memcpy(dst + kStRx, _rx.data(), _rx.size());
    std::copy_n(_out.begin(), out, dst + kStOut);
}

void SdCardSpi::loadState(const uint8_t* src)
{
    using namespace statebytes;
    _state = static_cast<State>(src[1]);
    _mode = static_cast<Mode>(src[2]);
    _selected = (src[3] & 1) != 0;
    _crcOn = (src[3] & 2) != 0;
    _appCommand = (src[3] & 4) != 0;
    _wpViolation = (src[3] & 8) != 0;
    _multiWrite = (src[3] & 16) != 0;
    _acmd41Count = src[4];
    memcpy(_cmd, src + 5, 6);
    _cmdLength = src[11];
    _lastCommand = src[12];
    _lastArgument = get32(src + 13);
    _nextReadBlock = getU64(src + 17);
    _writeBlock = getU64(src + 25);
    _rxCount = get16(src + 33);
    _blocksRead = getU64(src + 35);
    _blocksWritten = getU64(src + 43);
    const size_t out = std::min<size_t>(get16(src + 59), STATE_OUT_MAX);
    memcpy(_rx.data(), src + kStRx, _rx.size());
    _out.assign(src + kStOut, src + kStOut + out);
}

/// endregion </State snapshot>
