#include "stdafx.h"

#include "vdac4bomzh.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "3rdparty/bomzh/bomzh.h"
#include "vdac4espstate.h"

// Port of Quake-II-for-ZX-Evolution-VDAC4 gpu/rtl/bz_top.sv (MIT, Andrew Lazarev); see the header.

namespace
{
// bz_top.sv LINK registers (byte addresses)
constexpr uint32_t kRegId = 0x00;
constexpr uint32_t kRegVersion = 0x04;
constexpr uint32_t kRegStatus = 0x08;
constexpr uint32_t kRegFifoFree = 0x0C;
constexpr uint32_t kRegFrames = 0x10;
constexpr uint32_t kRegEvoPage = 0x14;
constexpr uint32_t kRegEvoBytes = 0x18;
constexpr uint32_t kRegFreeGray = 0x1C;
constexpr uint32_t kRegScratch = 0x3C;
constexpr uint32_t kRegSize = 0x84;
constexpr uint32_t kRegRdAddr = 0x88;
constexpr uint32_t kRegRdLen = 0x8C;
constexpr uint32_t kRegMark = 0x90;
constexpr uint32_t kRegSdTrain = 0xEC;
constexpr uint32_t kRegBoot = 0xF0;
constexpr uint32_t kWindow = 0x1000;
constexpr uint32_t kWindowEnd = 0x2000;
constexpr uint32_t kQueue = 0x40000;
constexpr uint32_t kQueueEnd = 0x80000;

constexpr uint32_t kLinkId = 0x5644'4334;   // "VDC4"
constexpr uint32_t kSizeKey = 0x535A;
constexpr uint32_t kBootKey = 0x424F'4F54;  // "BOOT"
constexpr uint32_t kGrayTag = 0x47u << 24;
/// M_SEL = 0 and the Evo's SPI quiet this long after M_SEL was 1: the Evo was reset (IDLE_CLKS, 0.5 s)
constexpr uint64_t kIdleMs = 500;
/// WRITE data goes to SDRAM in pieces of this many words
constexpr uint32_t kWriteChunk = 1024;

// SPEC.md command codes (the first word's top byte)
constexpr uint32_t kOpPoly = 0x03;
constexpr uint32_t kOpPoint = 0x04;
constexpr uint32_t kOpPic = 0x05;
constexpr uint32_t kOpFill = 0x06;
constexpr uint32_t kOpFrame = 0x07;
constexpr uint32_t kOpSetup = 0x0A;
constexpr uint32_t kOpTris = 0x0E;
constexpr uint32_t kOpWrite = 0x09;
constexpr uint32_t kOpVbuf = 0x0C;
constexpr uint32_t kOpStbuf = 0x0D;

/// Save's mark and the largest lengths it may hold
constexpr uint32_t kStateMagic = 0x3143'5A42;  // "BZC1"
constexpr uint32_t kEvoSideMagic = 0x4543'5A42;  // "BZCE": the Evo side of the state
constexpr uint32_t kMaxPending = 1 + 3 * 512;  // VBUF of 512 entries, the longest command
constexpr uint32_t kMaxWriteChunk = 2 + kWriteChunk;

/// Two ranges of SDRAM meet (addresses wrap at kMemoryBytes)
bool Overlap(uint32_t a, uint32_t aBytes, uint32_t b, uint32_t bBytes)
{
    constexpr uint32_t mask = Vdac4BomzhCore::kMemoryBytes - 1;
    if (aBytes == 0 || bBytes == 0)
        return false;
    return ((a - b) & mask) < bBytes || ((b - a) & mask) < aBytes;
}

uint32_t Gray(uint32_t value)
{
    return value ^ (value >> 1);
}

/// bz_exec's lengths: the words of the command `w0` starts, 0 for a word that starts none
uint32_t CommandLength(uint32_t w0)
{
    switch (w0 >> 24)
    {
        case 0x01: return 3;   // TEX
        case 0x02: return 5;   // LMAP
        case kOpPoly:
        {
            const uint32_t n = w0 & 0xFF;
            const uint32_t mode = (w0 >> 8) & 0xFFFF;
            if (n < 3 || n > 16)
                return 0;
            return 1 + n + 9 + ((mode & 6) == 2 ? 3 : 0);  // BZ_L_VERT: the light gradient
        }
        case 0x04: return 3;   // POINT
        case 0x05: return 7;   // PIC
        case 0x06: return 4;   // FILL
        case kOpFrame: return 2;
        case 0x08: return 2;   // TIME
        case kOpWrite: return 2;  // the header; the data streams (Push)
        case 0x0A: return 9;   // SETUP
        case 0x0B: return 3;   // SCISSOR
        case kOpVbuf:
        {
            const uint32_t n = w0 & 0x3FF;
            return n >= 1 && n <= 512 ? 1 + 3 * n : 0;
        }
        case kOpStbuf:
        {
            const uint32_t n = w0 & 0x3FF;
            return n >= 1 && n <= 512 ? 1 + n : 0;
        }
        case 0x0E: return 1 + 2 * (w0 & 0xFF);  // TRIS
        case 0x0F: return 2;   // MARK
        default: return 0;
    }
}
}  // namespace

Vdac4BomzhCore::Vdac4BomzhCore() : _model(bz_create(kMemoryBytes))
{
    _writeChunk.reserve(2 + kWriteChunk);
}

Vdac4BomzhCore::~Vdac4BomzhCore()
{
    bz_free(_model);
}

uint32_t Vdac4BomzhCore::LinkRead(uint32_t address) const
{
    if (address >= kWindow && address < kWindowEnd)
        return _window[((address - kWindow) >> 2) & 1023];
    switch (address)
    {
        case kRegId:
            return kLinkId;
        case kRegVersion:
            return kVersion;
        case kRegStatus:
        {
            const bool memReady = _model != nullptr;
            return (_msel ? 1u << 5 : 0u) | (_armed ? 1u << 4 : 0u) | (_bootRequest ? 1u << 3 : 0u) |
                   (memReady ? 1u << 2 : 0u) | (_badCommand ? 1u << 1 : 0u);
        }
        case kRegFifoFree:
            return kQueueWords;
        case kRegFreeGray:
            return kGrayTag | Gray(kQueueWords);
        case kRegFrames:
            return _frames;
        case kRegEvoPage:
            return _evoPage;
        case kRegEvoBytes:
            return 0;
        case kRegScratch:
            return _scratch;
        case kRegSize:
            return (kSizeKey << 16) | _size;
        case kRegMark:
            return _model ? bz_mark(_model) : 0;
        case kRegSdTrain:
            return 0;
        case kRegBoot:
            return (0xB00u << 20) | (_bootRequest ? 1u << 16 : 0u);
        default:
            return 0;
    }
}

void Vdac4BomzhCore::LinkWrite(uint32_t address, uint32_t value)
{
    if (address >= kQueue && address < kQueueEnd)
    {
        Push(value);
        return;
    }
    switch (address)
    {
        case kRegRdAddr:
            _rdAddress = value & 0x7FFFFF;
            break;
        case kRegRdLen:
            CopyToWindow(value & 0xFF);
            break;
        case kRegEvoPage:
            _evoPage = value & 1;
            break;
        case kRegScratch:
            _scratch = value;
            break;
        case kRegSize:
            if ((value >> 16) == kSizeKey && (value & 3) != 3)
                _size = value & 3;
            break;
        case kRegBoot:
            if (value == kBootKey)
                _bootRequest = true;
            break;
        default:
            break;
    }
}

void Vdac4BomzhCore::CopyToWindow(uint32_t bursts)
{
    // bz_rdwin: bursts of 32 bytes from RD_ADDR (a multiple of 32) into the window, done before the next LINK
    // access here
    if (_model == nullptr)
        return;
    const uint32_t base = _rdAddress & ~31u;
    for (uint32_t k = 0; k < bursts; ++k)
    {
        uint8_t bytes[32];
        bz_read(_model, base + 32 * k, bytes, sizeof(bytes));
        for (uint32_t i = 0; i < 8; ++i)
        {
            uint32_t word = 0;
            std::memcpy(&word, bytes + 4 * i, 4);
            _window[(k * 8 + i) & 1023] = word;  // little-endian host; the core's window is little-endian too
        }
    }
}

void Vdac4BomzhCore::Push(uint32_t word)
{
    ++_words;
    if (_writeLeft != 0)
    {
        _writeChunk.push_back(word);
        --_writeLeft;
        if (_writeLeft == 0 || _writeChunk.size() - 2 >= kWriteChunk)
            FlushWrite();
        return;
    }
    if (_pending.empty())
    {
        _need = CommandLength(word);
        if (_need == 0)
        {
            // Not a command: bad_cmd stays set; the word is dropped and the next one may start a command
            _badCommand = true;
            ++_badWords;
            return;
        }
    }
    _pending.push_back(word);
    if (_pending.size() < _need)
        return;
    if ((_pending[0] >> 24) == kOpWrite)
    {
        // WRITE n, address, then n data words: they go to SDRAM as they come
        _writeLeft = _pending[0] & 0xFFFFFF;
        _writeAddress = _pending[1];
        _pending.clear();
        _need = 0;
        ++_commands;
        _writeChunk.clear();
        _writeChunk.push_back(0);
        _writeChunk.push_back(0);
        return;
    }
    Execute();
}

void Vdac4BomzhCore::FlushWrite()
{
    const uint32_t count = static_cast<uint32_t>(_writeChunk.size() - 2);
    if (count != 0 && _model != nullptr)
    {
        _writeChunk[0] = (kOpWrite << 24) | count;
        _writeChunk[1] = _writeAddress;
        bz_exec(_model, _writeChunk.data(), static_cast<uint32_t>(_writeChunk.size()));
        // Over the shown frame: the picture is not the one latched any more
        uint32_t layout[4];
        bz_layout(_model, layout);
        const uint32_t frameBytes = static_cast<uint32_t>(bz_stride(_model)) * static_cast<uint32_t>(bz_height(_model));
        if (Overlap(_writeAddress, 4 * count, layout[(layout[3] ^ 1) & 1], frameBytes))
            ++_shownSerial;
        _writeAddress += 4 * count;
    }
    _writeChunk.resize(2);
}

void Vdac4BomzhCore::Execute()
{
    const uint32_t op = _pending[0] >> 24;
    const bool frame = op == kOpFrame;
    const bool draws = op == kOpPoly || op == kOpTris || op == kOpPoint || op == kOpPic || op == kOpFill;
    if (_model != nullptr && bz_exec(_model, _pending.data(), static_cast<uint32_t>(_pending.size())) != 0)
        _badCommand = true;
    if (draws)
    {
        if (_aliased)
            ++_shownSerial;
    }
    else if (frame || op == kOpSetup)
    {
        ++_shownSerial;
        if (op == kOpSetup)
            UpdateAliasing();
    }
    ++_commands;
    if (frame)
        ++_frames;
    _pending.clear();
    _need = 0;
}

void Vdac4BomzhCore::SetMsel(bool msel, uint64_t nowMs)
{
    _msel = msel;
    if (msel)
    {
        _armed = true;
        _quietSinceMs = nowMs;
    }
}

void Vdac4BomzhCore::EvoByte(uint8_t mosi, uint64_t nowMs)
{
    _quietSinceMs = nowMs;
    if (_evoBytes == 0)
        _evoFirst = mosi;
    else if (mosi != 0)
        _evoRestZero = false;
    if (_evoBytes < UINT32_MAX)
        ++_evoBytes;
}

void Vdac4BomzhCore::EvoDeselect(uint64_t nowMs)
{
    _quietSinceMs = nowMs;
    // The FT812 host command RST_PULSE (68 00 00): the Evo BIOS resets the video card
    if (_evoBytes == 3 && _evoFirst == 0x68 && _evoRestZero)
        _bootRequest = true;
    _evoBytes = 0;
    _evoFirst = 0;
    _evoRestZero = true;
}

bool Vdac4BomzhCore::BootRequested(uint64_t nowMs)
{
    if (_msel)
        _quietSinceMs = nowMs;
    else if (_armed && nowMs >= _quietSinceMs + kIdleMs)
        _bootRequest = true;
    return _bootRequest;
}

bool Vdac4BomzhCore::ShownPicture(std::vector<uint8_t>& rgba, uint16_t& width, uint16_t& height) const
{
    if (_model == nullptr)
        return false;
    const int w = bz_width(_model);
    const int h = bz_height(_model);
    if (w <= 0 || h <= 0 || w > kMaxWidth || h > kMaxHeight)
        return false;
    const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<uint8_t> rgb(pixels * 3);
    bz_shown_rgb(_model, rgb.data());
    rgba.resize(pixels * 4);
    for (size_t i = 0; i < pixels; ++i)
    {
        rgba[i * 4] = rgb[i * 3];
        rgba[i * 4 + 1] = rgb[i * 3 + 1];
        rgba[i * 4 + 2] = rgb[i * 3 + 2];
        rgba[i * 4 + 3] = 0xFF;
    }
    width = static_cast<uint16_t>(w);
    height = static_cast<uint16_t>(h);
    return true;
}

Vdac4BomzhCore::Stats Vdac4BomzhCore::GetStats() const
{
    Stats stats;
    stats.words = _words;
    stats.commands = _commands;
    stats.badWords = _badWords;
    stats.frames = _frames;
    stats.mark = _model ? bz_mark(_model) : 0;
    stats.width = _model ? static_cast<uint32_t>(std::max(0, bz_width(_model))) : 0;
    stats.height = _model ? static_cast<uint32_t>(std::max(0, bz_height(_model))) : 0;
    stats.badCommand = _badCommand;
    stats.bootRequest = _bootRequest;
    return stats;
}

/// region <State (TTD)>

void Vdac4BomzhCore::Reset()
{
    if (_model != nullptr)
        bz_reset(_model);
    _pending.clear();
    _need = 0;
    _writeLeft = 0;
    _writeAddress = 0;
    _writeChunk.clear();
    _frames = 0;
    _scratch = 0;
    _size = 0;
    _evoPage = 1;
    _rdAddress = 0;
    std::fill(std::begin(_window), std::end(_window), 0u);
    _badCommand = false;
    _bootRequest = false;
    _armed = false;
    _msel = false;
    _quietSinceMs = 0;
    _evoBytes = 0;
    _evoFirst = 0;
    _evoRestZero = true;
    _words = 0;
    _commands = 0;
    _badWords = 0;
    _aliased = false;
    ++_shownSerial;
}

uint8_t* Vdac4BomzhCore::Memory()
{
    return _model != nullptr ? bz_mem(_model) : nullptr;
}

void Vdac4BomzhCore::UpdateAliasing()
{
    _aliased = false;
    if (_model == nullptr)
        return;
    uint32_t layout[4];
    bz_layout(_model, layout);
    const uint32_t frameBytes = static_cast<uint32_t>(bz_stride(_model)) * static_cast<uint32_t>(bz_height(_model));
    _aliased = Overlap(layout[0], frameBytes, layout[1], frameBytes) ||
               Overlap(layout[2], 2 * frameBytes, layout[0], frameBytes) ||
               Overlap(layout[2], 2 * frameBytes, layout[1], frameBytes);
}

void Vdac4BomzhCore::Save(Vdac4EspStateWriter& out) const
{
    out.U32(kStateMagic);
    // The model's tables first: they change seldom, a store of the pieces that changed keeps them once
    out.Bool(_model != nullptr);
    if (_model != nullptr)
    {
        uint8_t model[BZ_STATE_BYTES];
        bz_state_save(_model, model);
        out.Bytes(model, sizeof(model));
    }
    for (uint32_t word : _window)
        out.U32(word);
    out.U32(_frames);
    out.U32(_scratch);
    out.U32(_size);
    out.U32(_evoPage);
    out.U32(_rdAddress);
    out.Bool(_badCommand);
    out.Bool(_bootRequest);
    out.Bool(_armed);
    out.Bool(_msel);
    out.U64(_quietSinceMs);
    out.U32(_evoBytes);
    out.U8(_evoFirst);
    out.Bool(_evoRestZero);
    out.U64(_words);
    out.U64(_commands);
    out.U64(_badWords);
    out.U32(_need);
    out.U32(_writeLeft);
    out.U32(_writeAddress);
    // The command being received and the WRITE data not yet in SDRAM: last, their length varies
    out.U32(static_cast<uint32_t>(_pending.size()));
    for (uint32_t word : _pending)
        out.U32(word);
    out.U32(static_cast<uint32_t>(_writeChunk.size()));
    for (uint32_t word : _writeChunk)
        out.U32(word);
}

void Vdac4BomzhCore::SaveEvoSide(Vdac4EspStateWriter& out) const
{
    out.U32(kEvoSideMagic);
    out.Bool(_bootRequest);
    out.Bool(_armed);
    out.Bool(_msel);
    out.U64(_quietSinceMs);
    out.U32(_evoBytes);
    out.U8(_evoFirst);
    out.Bool(_evoRestZero);
}

bool Vdac4BomzhCore::LoadEvoSide(Vdac4EspStateReader& in)
{
    if (in.U32() != kEvoSideMagic)
        return false;
    const bool bootRequest = in.Bool();
    const bool armed = in.Bool();
    const bool msel = in.Bool();
    const uint64_t quietSinceMs = in.U64();
    const uint32_t evoBytes = in.U32();
    const uint8_t evoFirst = in.U8();
    const bool evoRestZero = in.Bool();
    if (!in.Ok())
        return false;
    _bootRequest = bootRequest;
    _armed = armed;
    _msel = msel;
    _quietSinceMs = quietSinceMs;
    _evoBytes = evoBytes;
    _evoFirst = evoFirst;
    _evoRestZero = evoRestZero;
    return true;
}

uint64_t Vdac4BomzhCore::StateHash() const
{
    std::vector<uint8_t> bytes;
    Vdac4EspStateWriter w(bytes);
    Save(w);
    // FNV-1a over 64-bit words, the tail bytes last
    uint64_t h = 1469598103934665603ull;
    size_t i = 0;
    for (; i + 8 <= bytes.size(); i += 8)
    {
        uint64_t word = 0;
        std::memcpy(&word, bytes.data() + i, 8);
        h = (h ^ word) * 1099511628211ull;
    }
    for (; i < bytes.size(); ++i)
        h = (h ^ bytes[i]) * 1099511628211ull;
    return h;
}

bool Vdac4BomzhCore::Load(Vdac4EspStateReader& in)
{
    if (in.U32() != kStateMagic)
        return false;
    const bool hasModel = in.Bool();
    if (!in.Ok() || hasModel != (_model != nullptr))
        return false;
    uint8_t model[BZ_STATE_BYTES];
    if (hasModel)
        in.Bytes(model, sizeof(model));
    uint32_t window[1024];
    for (uint32_t& word : window)
        word = in.U32();
    const uint32_t frames = in.U32();
    const uint32_t scratch = in.U32();
    const uint32_t size = in.U32();
    const uint32_t evoPage = in.U32();
    const uint32_t rdAddress = in.U32();
    const bool badCommand = in.Bool();
    const bool bootRequest = in.Bool();
    const bool armed = in.Bool();
    const bool msel = in.Bool();
    const uint64_t quietSinceMs = in.U64();
    const uint32_t evoBytes = in.U32();
    const uint8_t evoFirst = in.U8();
    const bool evoRestZero = in.Bool();
    const uint64_t words = in.U64();
    const uint64_t commands = in.U64();
    const uint64_t badWords = in.U64();
    const uint32_t need = in.U32();
    const uint32_t writeLeft = in.U32();
    const uint32_t writeAddress = in.U32();
    const uint32_t pendingCount = in.U32();
    if (!in.Ok() || pendingCount > kMaxPending)
        return false;
    std::vector<uint32_t> pending(pendingCount);
    for (uint32_t& word : pending)
        word = in.U32();
    const uint32_t chunkCount = in.U32();
    if (!in.Ok() || chunkCount > kMaxWriteChunk)
        return false;
    std::vector<uint32_t> chunk(chunkCount);
    for (uint32_t& word : chunk)
        word = in.U32();
    if (!in.Ok())
        return false;

    if (hasModel)
        bz_state_load(_model, model);
    std::copy(std::begin(window), std::end(window), std::begin(_window));
    _frames = frames;
    _scratch = scratch;
    _size = size;
    _evoPage = evoPage;
    _rdAddress = rdAddress;
    _badCommand = badCommand;
    _bootRequest = bootRequest;
    _armed = armed;
    _msel = msel;
    _quietSinceMs = quietSinceMs;
    _evoBytes = evoBytes;
    _evoFirst = evoFirst;
    _evoRestZero = evoRestZero;
    _words = words;
    _commands = commands;
    _badWords = badWords;
    _need = need;
    _writeLeft = writeLeft;
    _writeAddress = writeAddress;
    _pending = std::move(pending);
    _writeChunk.assign(chunk.begin(), chunk.end());
    _writeChunk.reserve(kMaxWriteChunk);
    UpdateAliasing();
    ++_shownSerial;
    return true;
}

/// endregion
