#include "emulator/io/serial/esp/zifivfsbridge.h"

#include <cstdio>

#include "emulator/io/serial/esp/zifistate.h"

// Every behavior here is the firmwares' (the files named in the header):
//  S3  = ZiFi-ESP32-S3-Zero 2e5ba83 src/vfs_client.cpp (VfsClient), src/vfs_bridge.cpp (VfsBridge)
//  E01 = ZiFi-ESP-01S-Native-C-Project 90834e4 src/vfs_client.cpp
// The firmwares block in UartTransport::waitFor; here each exchange is a step that waits for its answer frame.

namespace
{
constexpr uint64_t kNormalTimeoutUs = 5000000;     // kNormalTimeoutMs
constexpr uint64_t kFragmentTimeoutUs = 30000000;  // kFragmentTimeoutMs
constexpr uint64_t kMutateTimeoutUs = 60000000;    // kMutateTimeoutMs
constexpr uint64_t kCloseTimeoutUs = 180000000;    // kCloseTimeoutMs
constexpr size_t kMaxPayload = 1024;
constexpr size_t kFirstData = 248;
constexpr size_t kContinueData = 252;
constexpr size_t kWriteWindowHeader = 8;
constexpr size_t kReadWindowHeader = 9;
constexpr size_t kWriteWindowData = kMaxPayload - kWriteWindowHeader;
constexpr uint8_t kCapabilityWriteWindow = 0x01;
constexpr uint8_t kCapabilityReadWindow = 0x02;
constexpr uint8_t kCapabilityExtend = 0x04;
constexpr uint8_t kWindowStart = 0x01;
constexpr uint8_t kWindowEnd = 0x02;
constexpr uint8_t kMetadataSize = 16;
constexpr uint8_t kDirectoryCapabilityBatch = 0x01;
constexpr uint8_t kDirectoryBatchMore = 2;
constexpr size_t kDirectoryBatchEntries = 16;
constexpr size_t kS3Fragment = 1024;   // S3 fragment_[kMaxPayload]
constexpr size_t kE01Fragment = 256;   // E01 fragment_[256]

uint16_t Le16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t Le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void Put16(std::vector<uint8_t>& v, uint16_t x)
{
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

void Put32(std::vector<uint8_t>& v, uint32_t x)
{
    Put16(v, static_cast<uint16_t>(x));
    Put16(v, static_cast<uint16_t>(x >> 16));
}

/// CRC-16/CCITT-FALSE (protocol.cpp crc16CcittFalseUpdate)
uint16_t Crc16Update(uint16_t crc, const uint8_t* data, size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

std::string Format(const char* format, unsigned value)
{
    char text[64];
    std::snprintf(text, sizeof(text), format, value);
    return text;
}
}  // namespace

const char* ZiFiVfsBridge::OpName(Op op)
{
    static const char* const kNames[] = {"reset", "stat", "opendir", "readdir", "open-read", "open-write",
                                         "open-append", "open-random", "read", "write", "read-at", "write-at",
                                         "extend", "set-eof", "close", "close-abort", "delete", "mkdir",
                                         "rename", "move-rename", "set-metadata"};
    const size_t i = static_cast<size_t>(op);
    return i < sizeof(kNames) / sizeof(kNames[0]) ? kNames[i] : "?";
}

ZiFiVfsBridge::ZiFiVfsBridge(bool s3) : _s3(s3)
{
}

void ZiFiVfsBridge::Reset()
{
    const bool s3 = _s3;
    auto send = std::move(sendFrame);
    auto clock = std::move(now);
    auto toT = std::move(micros);
    const uint64_t frames = _frames, read = _bytesRead, written = _bytesWritten;
    const uint32_t timeouts = _timeouts;
    *this = ZiFiVfsBridge(s3);
    sendFrame = std::move(send);
    now = std::move(clock);
    micros = std::move(toT);
    _frames = frames;
    _bytesRead = read;
    _bytesWritten = written;
    _timeouts = timeouts;
}

// --- Submit (VfsBridge::submit*) -----------------------------------------------------------------------------------

bool ZiFiVfsBridge::Accept(const Request& request)
{
    if (_requestPending)
        return false;
    _req = request;
    _requestPending = true;
    _resultReady = false;
    Begin();
    return true;
}

bool ZiFiVfsBridge::Submit(Op op, const std::string& path, uint32_t value)
{
    if (path.size() >= 256)
        return false;   // request.path[256]
    Request r;
    r.op = op;
    r.value = value;
    r.path = path;
    return Accept(r);
}

bool ZiFiVfsBridge::SubmitAt(Op op, uint32_t offset, uint32_t length)
{
    if (op != Op::ReadAt && op != Op::WriteAt)
        return false;
    Request r;
    r.op = op;
    r.value = length;
    r.offset = offset;
    return Accept(r);
}

bool ZiFiVfsBridge::SubmitRename(const std::string& oldPath, const std::string& newName, bool directory)
{
    if (oldPath.size() >= 256 || newName.size() >= 256)
        return false;
    Request r;
    r.op = Op::Rename;
    r.value = directory ? 1 : 0;
    r.path = oldPath;
    r.path2 = newName;
    return Accept(r);
}

bool ZiFiVfsBridge::SubmitMoveRename(const std::string& oldPath, const std::string& newPath, bool directory, bool replace)
{
    if (oldPath.size() >= 256 || newPath.size() >= 256)
        return false;
    Request r;
    r.op = Op::MoveRename;
    r.flags = static_cast<uint8_t>((directory ? 1 : 0) | (replace ? 2 : 0));
    r.path = oldPath;
    r.path2 = newPath;
    return Accept(r);
}

bool ZiFiVfsBridge::SubmitMetadata(const Metadata& metadata)
{
    Request r;
    r.op = Op::SetMetadata;
    r.metadata = metadata;
    return Accept(r);
}

bool ZiFiVfsBridge::TakeResult(Result& out)
{
    if (!_requestPending || !_resultReady)
        return false;
    out = _result;
    _requestPending = false;
    _resultReady = false;
    return true;
}

// --- Rings ---------------------------------------------------------------------------------------------------------

size_t ZiFiVfsBridge::ReadForNetwork(uint8_t* out, size_t capacity)
{
    const size_t n = std::min(capacity, _toNet.size());
    for (size_t i = 0; i < n; ++i)
    {
        out[i] = _toNet.front();
        _toNet.pop_front();
    }
    return n;
}

size_t ZiFiVfsBridge::WriteFromNetwork(const uint8_t* data, size_t length)
{
    const size_t n = std::min(length, FromNetFree());
    _fromNet.insert(_fromNet.end(), data, data + n);
    return n;
}

// --- Exchanges -----------------------------------------------------------------------------------------------------

void ZiFiVfsBridge::Exchange(uint8_t cmd, const std::vector<uint8_t>& payload, uint64_t timeoutUs, Sub sub)
{
    _sub = sub;
    _waiting = true;
    _awaitCmd = cmd;
    _timeoutUs = timeoutUs;
    _deadline = (now ? now() : 0) + (micros ? micros(timeoutUs) : 0);
    ++_frames;
    if (sendFrame)
        sendFrame(cmd, payload);
}

void ZiFiVfsBridge::PathExchange(uint8_t cmd, const std::string& path, uint64_t timeoutUs)
{
    // pathRequest: the Z80 wants the terminating zero
    if (path.size() + 1 > kMaxPayload)
    {
        SetError("path-long");
        _sub = Sub::Simple;
        return SubDone(false);
    }
    std::vector<uint8_t> payload(path.begin(), path.end());
    payload.push_back(0);
    SetError("none");
    Exchange(cmd, payload, timeoutUs, Sub::Simple);
}

void ZiFiVfsBridge::SubDone(bool ok)
{
    _sub = Sub::None;
    _waiting = false;
    _subOk = ok;
    Step();
}

void ZiFiVfsBridge::Tick()
{
    if (!_waiting || !now || now() < _deadline)
        return;
    ++_timeouts;
    _waiting = false;
    char text[40];
    switch (_sub)
    {
        case Sub::ReadWindow: SetError("read-window-timeout"); break;
        case Sub::WriteWindow:
            if (_lastError == "none")
                SetError("write-window-timeout");
            _writeFailed = true;
            break;
        default:
            std::snprintf(text, sizeof(text), "timeout-%02x", _awaitCmd);
            SetError(text);
            break;
    }
    SubDone(false);
}

void ZiFiVfsBridge::OnResponse(const std::vector<uint8_t>& r)
{
    if (!_waiting)
        return;
    switch (_sub)
    {
        case Sub::Simple:
            _response = r;
            _waiting = false;
            return SubDone(true);

        case Sub::ReadFile:
        {
            _waiting = false;
            if (r.empty() || r[0] != 0)
            {
                _lastStatus = r.empty() ? 0xFF : r[0];
                SetError(Format("read-%u", r.empty() ? 255u : r[0]));
                return SubDone(false);
            }
            const size_t received = r.size() - 1;
            // S3: wanted is capped to min(capacity, 512); a longer answer is an overrun. E01: only the capacity
            if (_s3 ? (received > kS3Fragment || received > _partWanted) : received > kBlockSize)
            {
                SetError(_s3 ? "read-overrun" : "read-overflow");
                return SubDone(false);
            }
            _toNet.insert(_toNet.end(), r.begin() + 1, r.end());
            _bytesRead += received;
            _partGot = static_cast<uint32_t>(received);
            return SubDone(true);
        }

        case Sub::ReadWindow:
        {
            // readFileWindow: a stream of frames, the whole window's CRC in the last
            _deadline = (now ? now() : 0) + (micros ? micros(kFragmentTimeoutUs) : 0);
            if (r.size() == 1 && r[0] == 1)
            {
                _waiting = false;
                _lastStatus = 1;
                return SubDone(true);
            }
            if (r.empty() || r[0] != 0)
            {
                _waiting = false;
                _lastStatus = r.empty() ? 0xFF : r[0];
                SetError(Format("read-window-status-%u", r.empty() ? 255u : r[0]));
                return SubDone(false);
            }
            const auto fail = [this](const char* error) {
                _waiting = false;
                SetError(error);
                SubDone(false);
            };
            if (r.size() <= kReadWindowHeader)
                return fail("read-window-len");
            const uint8_t flags = r[1];
            if ((flags & static_cast<uint8_t>(~(kWindowStart | kWindowEnd))) != 0 || r[2] != _windowSeq)
                return fail("read-window-header");
            const uint16_t offset = Le16(&r[3]);
            const uint16_t frameTotal = Le16(&r[5]);
            const size_t part = r.size() - kReadWindowHeader;
            const bool isStart = (flags & kWindowStart) != 0;
            const bool isEnd = (flags & kWindowEnd) != 0;
            if (isStart != (_windowReceived == 0) || offset != _windowReceived || frameTotal == 0 ||
                frameTotal > _partWanted || offset > frameTotal || (_windowTotal != 0 && frameTotal != _windowTotal) ||
                part > static_cast<size_t>(frameTotal - offset))
                return fail("read-window-order");
            _windowTotal = frameTotal;
            const size_t next = _windowReceived + part;
            if ((isEnd && next != _windowTotal) || (!isEnd && next >= _windowTotal))
                return fail("read-window-end");
            if (_toNet.size() + part > kRingCapacity)
                return fail("read-window-sink");
            _toNet.insert(_toNet.end(), r.begin() + kReadWindowHeader, r.end());
            _bytesRead += part;
            _windowCrc = Crc16Update(_windowCrc, r.data() + kReadWindowHeader, part);
            _windowReceived = static_cast<uint32_t>(next);
            if (!isEnd)
                return;   // more frames
            _waiting = false;
            if (_windowCrc != Le16(&r[7]))
                return fail("read-window-crc");
            _readSequence = static_cast<uint8_t>(_windowSeq + 1);
            _lastStatus = 0;
            if (_openMode == 3 && _randomOffsetValid)
                _randomOffset += _windowReceived;
            return SubDone(true);
        }

        case Sub::Batch:
        {
            // fetchDirectoryBatch: entries as ordinary READDIR frames, then a one-byte summary
            _deadline = (now ? now() : 0) + (micros ? micros(kNormalTimeoutUs) : 0);
            if (r.size() >= 6 && r[0] == 0)
            {
                if (_directoryEntries.size() >= kDirectoryBatchEntries)
                {
                    _waiting = false;
                    SetError("readdir-batch-overflow");
                    return SubDone(false);
                }
                Entry e;
                ParseDirectoryEntry(r, e);
                _directoryEntries.push_back(std::move(e));
                return;
            }
            _waiting = false;
            if (r.empty() || r[0] == 0)
            {
                SetError("readdir-short");
                return SubDone(false);
            }
            if (r[0] != kDirectoryBatchMore || _directoryEntries.empty())
                _directoryEnded = true;
            return SubDone(true);
        }

        case Sub::FlushBlock:
        {
            _waiting = false;
            if (!CheckBlockAck(r, _blockSeq, _blockAccepted))
                return SubDone(false);
            if (_blockOffset >= _blockLength)
            {
                _writeSequence = static_cast<uint8_t>(_blockSeq + 1);
                _bytesWritten += _blockLength;
                return SubDone(true);
            }
            return SendBlockFragment();
        }

        case Sub::WriteWindow:
        {
            _waiting = false;
            if (!CheckBlockAck(r, _blockSeq, static_cast<uint16_t>(_blockLength)))
            {
                if (_lastError == "none")
                    SetError("write-window-timeout");
                _writeFailed = true;
                return SubDone(false);
            }
            _writeSequence = static_cast<uint8_t>(_blockSeq + 1);
            _lastStatus = 0;
            if (_openMode == 3 && _randomOffsetValid)
                _randomOffset += _blockLength;
            _bytesWritten += _blockLength;
            return SubDone(true);
        }

        case Sub::None: return;
    }
}

bool ZiFiVfsBridge::CheckBlockAck(const std::vector<uint8_t>& r, uint8_t sequence, uint16_t accepted)
{
    if (r.size() < 4)
    {
        SetError("block-ack-len");
        return false;
    }
    if (r[0] != 0)
    {
        if (_s3)
            _lastStatus = r[0];
        SetError(Format("block-status-%u", r[0]));
        return false;
    }
    if (r[1] != sequence)
    {
        SetError(Format("block-seq-%u", r[1]));
        return false;
    }
    const uint16_t got = Le16(&r[2]);
    if (got != accepted)
    {
        SetError(Format("block-accepted-%u", got));
        return false;
    }
    return true;
}

void ZiFiVfsBridge::StartReadFile(size_t wanted)
{
    // readFile: one READ of at most 512 bytes
    wanted = std::min(wanted, _s3 ? kS3Fragment : kBlockSize);
    wanted = std::min(wanted, kBlockSize);
    _partWanted = static_cast<uint32_t>(wanted);
    _partGot = 0;
    std::vector<uint8_t> p;
    Put16(p, static_cast<uint16_t>(wanted));
    SetError("none");
    Exchange(kRead, p, kNormalTimeoutUs, Sub::ReadFile);
}

void ZiFiVfsBridge::StartReadWindow(size_t wanted)
{
    SetError("none");
    _windowSeq = _readSequence;
    _windowTotal = 0;
    _windowCrc = 0xFFFF;
    _windowReceived = 0;
    _partWanted = static_cast<uint32_t>(wanted);
    std::vector<uint8_t> p = {_readSequence};
    Put16(p, static_cast<uint16_t>(wanted));
    Exchange(kReadWindow, p, kFragmentTimeoutUs, Sub::ReadWindow);
}

void ZiFiVfsBridge::StartBatch()
{
    _directoryEntries.clear();
    _directoryNext = 0;
    SetError("none");
    Exchange(kReadDir, {static_cast<uint8_t>(kDirectoryBatchEntries)}, kNormalTimeoutUs, Sub::Batch);
}

void ZiFiVfsBridge::StartFlushBlock(size_t rawLength)
{
    // flushWriteBlock: [00][seq][raw LE16][stored LE16][crc LE16][data <= 248], then [80][seq][offset LE16][data]
    _blockData.assign(_writeBuffer.begin(), _writeBuffer.begin() + static_cast<std::ptrdiff_t>(rawLength));
    _blockLength = static_cast<uint32_t>(rawLength);
    _blockSeq = _writeSequence;
    _blockOffset = 0;
    SendBlockFragment();
}

void ZiFiVfsBridge::SendBlockFragment()
{
    std::vector<uint8_t> f;
    size_t count = 0;
    if (_blockOffset == 0)
    {
        const uint16_t crc = Crc16Update(0xFFFF, _blockData.data(), _blockLength);
        f = {0x00, _blockSeq};
        Put16(f, static_cast<uint16_t>(_blockLength));
        Put16(f, static_cast<uint16_t>(_blockLength));
        Put16(f, crc);
        count = std::min<size_t>(_blockLength, kFirstData);
    }
    else
    {
        f = {0x80, _blockSeq};
        Put16(f, static_cast<uint16_t>(_blockOffset));
        count = std::min<size_t>(_blockLength - _blockOffset, kContinueData);
    }
    f.insert(f.end(), _blockData.begin() + _blockOffset, _blockData.begin() + _blockOffset + count);
    _blockOffset += static_cast<uint32_t>(count);
    _blockAccepted = static_cast<uint16_t>(_blockOffset);
    SetError("none");
    Exchange(kBlock, f, kFragmentTimeoutUs, Sub::FlushBlock);
}

void ZiFiVfsBridge::StartWriteWindow(size_t length)
{
    // writeFileWindow (window path): every fragment goes out without an answer, one acknowledgment for the window
    SetError("none");
    _blockSeq = _writeSequence;
    _blockLength = static_cast<uint32_t>(length);
    uint16_t crc = 0xFFFF;
    size_t offset = 0;
    _sub = Sub::WriteWindow;
    while (offset < length)
    {
        const size_t count = std::min(length - offset, kWriteWindowData);
        const bool isStart = offset == 0;
        const bool isEnd = offset + count == length;
        std::vector<uint8_t> f = {static_cast<uint8_t>((isStart ? kWindowStart : 0) | (isEnd ? kWindowEnd : 0)), _blockSeq};
        Put16(f, static_cast<uint16_t>(offset));
        Put16(f, static_cast<uint16_t>(length));
        Put16(f, 0);
        const auto from = _fromNet.begin() + static_cast<std::ptrdiff_t>(offset);   // peekAt: from the ring head
        f.insert(f.end(), from, from + static_cast<std::ptrdiff_t>(count));
        crc = Crc16Update(crc, f.data() + kWriteWindowHeader, count);
        if (isEnd)
        {
            f[6] = static_cast<uint8_t>(crc);
            f[7] = static_cast<uint8_t>(crc >> 8);
        }
        offset += count;
        if (!isEnd)
        {
            ++_frames;
            if (sendFrame)
                sendFrame(kWriteWindow, f);
        }
        else
            Exchange(kWriteWindow, f, kCloseTimeoutUs, Sub::WriteWindow);
    }
}

void ZiFiVfsBridge::ParseDirectoryEntry(const std::vector<uint8_t>& r, Entry& entry) const
{
    entry = Entry();
    entry.isDirectory = r[1] != 0;
    entry.size = Le32(&r[2]);
    size_t nameLength = r.size() - 6;
    if (_s3)
    {
        // The new plugin adds [0][date LE16][time LE16] after the name (the old one has a letter there)
        if (r.size() >= 12 && r[r.size() - 5] == 0)
        {
            entry.writeDate = Le16(&r[r.size() - 4]);
            entry.writeTime = Le16(&r[r.size() - 2]);
            nameLength -= 5;
        }
    }
    if (nameLength >= 256)
        nameLength = 255;
    entry.name.assign(reinterpret_cast<const char*>(r.data() + 6), nameLength);
    // A name ends at its first NUL (char name[256])
    const size_t nul = entry.name.find('\0');
    if (nul != std::string::npos)
        entry.name.resize(nul);
    // S3: Wild Commander pads some FAT extensions with a space
    while (!entry.name.empty() && (entry.name.back() == 0 || (_s3 && entry.name.back() == ' ')))
        entry.name.pop_back();
}

void ZiFiVfsBridge::ResetWriteState(bool active)
{
    _writeBuffer.clear();
    _writeSequence = 0;
    _writeActive = active;
    _writeFailed = false;
}

void ZiFiVfsBridge::CopyEntry(const Entry& e)
{
    _result.isDirectory = e.isDirectory;
    _result.size = e.size;
    _result.writeDate = e.writeDate;
    _result.writeTime = e.writeTime;
    _result.hasMetadata = e.hasMetadata;
    _result.attributes = e.attributes;
    _result.createTenth = e.createTenth;
    _result.createTime = e.createTime;
    _result.createDate = e.createDate;
    _result.accessDate = e.accessDate;
}

void ZiFiVfsBridge::Finish(bool success, const char* error)
{
    // VfsBridge::finish
    _result.success = success;
    _result.status = error == nullptr ? _lastStatus : 0xFF;
    _result.error = success ? std::string("none") : std::string(error == nullptr ? _lastError.c_str() : error);
    _resultReady = true;
    _step = 0;
}

// --- Operations ----------------------------------------------------------------------------------------------------

void ZiFiVfsBridge::Begin()
{
    _result = Result();
    _step = 0;
    _sub = Sub::None;
    _opWanted = _opReceived = _opTransferred = _opRequested = _opWindow = _opChunk = 0;
    Step();
}

namespace
{
// Steps shared by the operations
enum : uint8_t
{
    kStart = 0,
    kAnswer,          // a Simple exchange answered
    kSeekAnswer,
    kReadLoop,
    kReadPart,
    kReadWindowDone,
    kReadFinish,
    kWriteLoop,
    kWriteWindowDone,
    kWriteChunkLoop,
    kWriteChunkDone,
    kBatchDone,
    kCloseFlushed,
    kCloseAnswer,
    kCloseAbortAnswer,
    kFileChunkLoop,   // writeFile: buffer the chunk, flush full blocks
    kFileBlockDone,
    kE01ReadDone,
};
}  // namespace

void ZiFiVfsBridge::ContinueWriteFile()
{
    // writeFile: the chunk fills the 512-byte buffer; a full buffer is flushed as one block
    while (_chunkPos < _chunk.size())
    {
        const size_t take = std::min(kBlockSize - _writeBuffer.size(), _chunk.size() - _chunkPos);
        _writeBuffer.insert(_writeBuffer.end(), _chunk.begin() + static_cast<std::ptrdiff_t>(_chunkPos),
                            _chunk.begin() + static_cast<std::ptrdiff_t>(_chunkPos + take));
        _chunkPos += take;
        if (_writeBuffer.size() == kBlockSize)
        {
            _step = kFileBlockDone;
            return StartFlushBlock(kBlockSize);
        }
    }
    _chunk.clear();
    _chunkPos = 0;
    _step = kFileChunkLoop;
    _subOk = true;
}

void ZiFiVfsBridge::Step()
{
    const Request& q = _req;
    const std::vector<uint8_t>& r = _response;
    const auto status = [&r]() { return r.empty() ? 255u : static_cast<unsigned>(r[0]); };

    switch (q.op)
    {
        case Op::ResetBuffers:
            _toNet.clear();
            _fromNet.clear();
            return Finish(true);

        case Op::Stat:
            if (_step == kStart)
            {
                _step = kAnswer;
                return PathExchange(kStat, q.path, kNormalTimeoutUs);
            }
            if (!_subOk)
                return Finish(false);
            if (r.size() < 6 || r[0] != 0)
            {
                SetError(Format("stat-%u", status()));
                return Finish(false);
            }
            _result.isDirectory = r[1] != 0;
            _result.size = Le32(&r[2]);
            // The new plugin adds FILEX GET_METADATA (16 bytes, the first is its size); the old one sends 6 bytes
            if (_s3 && r.size() >= 6u + kMetadataSize && r[6] == kMetadataSize)
            {
                const uint8_t* m = &r[6];
                _result.hasMetadata = true;
                _result.attributes = m[2];
                _result.createTenth = m[4];
                _result.createTime = Le16(m + 5);
                _result.createDate = Le16(m + 7);
                _result.accessDate = Le16(m + 9);
                _result.writeTime = Le16(m + 11);
                _result.writeDate = Le16(m + 13);
            }
            return Finish(true);

        case Op::OpenDirectory:
            if (_step == kStart)
            {
                _directoryBatch = false;
                _directoryEnded = false;
                _directoryEntries.clear();
                _directoryNext = 0;
                _step = kAnswer;
                return PathExchange(kOpenDir, q.path, kNormalTimeoutUs);
            }
            if (!_subOk)
                return Finish(false);
            if (r.empty() || r[0] != 0)
            {
                SetError(Format("opendir-%u", status()));
                return Finish(false);
            }
            // The old plugin answers with the status alone: one entry per request
            _directoryBatch = _s3 && r.size() >= 2 && (r[1] & kDirectoryCapabilityBatch) != 0;
            return Finish(true);

        case Op::ReadDirectory:
            if (_step == kStart)
            {
                if (_directoryBatch)
                {
                    if (_directoryNext < _directoryEntries.size())
                    {
                        const Entry& e = _directoryEntries[_directoryNext++];
                        CopyEntry(e);
                        _result.name = e.name;
                        return Finish(true);
                    }
                    if (_directoryEnded)
                    {
                        _result.atEnd = true;
                        return Finish(true);
                    }
                    _step = kBatchDone;
                    return StartBatch();
                }
                _step = kAnswer;
                SetError("none");
                return Exchange(kReadDir, {}, kNormalTimeoutUs, Sub::Simple);
            }
            if (_step == kBatchDone)
            {
                if (!_subOk)
                    return Finish(false);
                if (_directoryNext >= _directoryEntries.size())
                {
                    _result.atEnd = true;
                    return Finish(true);
                }
                const Entry& e = _directoryEntries[_directoryNext++];
                CopyEntry(e);
                _result.name = e.name;
                return Finish(true);
            }
            if (!_subOk)
                return Finish(false);
            if (_s3)
            {
                if (!r.empty() && r[0] != 0)
                {
                    _result.atEnd = true;   // a non-zero status: the end of the directory
                    return Finish(true);
                }
                if (r.size() < 6)
                {
                    SetError("readdir-short");   // shorter than the header: a broken exchange, not the end
                    return Finish(false);
                }
            }
            else if (r.size() < 6 || r[0] != 0)
            {
                _result.atEnd = true;   // E01: short / non-zero is the end
                return Finish(true);
            }
            {
                Entry e;
                ParseDirectoryEntry(r, e);
                CopyEntry(e);
                _result.name = e.name;
            }
            return Finish(true);

        case Op::OpenRead:
        case Op::OpenWrite:
        case Op::OpenAppend:
        case Op::OpenRandom:
        {
            const uint8_t mode = q.op == Op::OpenRead ? 0 : q.op == Op::OpenWrite ? 1 : q.op == Op::OpenAppend ? 2 : 3;
            const bool forWrite = mode == 1 || mode == 2;
            if (_step == kStart)
            {
                const size_t pathLength = q.path.size() + 1;
                if (pathLength + 1 > (_s3 ? kS3Fragment : kE01Fragment))
                {
                    SetError("open-path-long");
                    return Finish(false);
                }
                if (forWrite || mode == 3)
                    ResetWriteState(false);
                if (_s3)
                {
                    _readSequence = 0;
                    _capabilities = 0;
                    _filexCapabilities = 0;
                    _lastStatus = 0;
                    _randomOffset = 0;
                    _randomOffsetValid = false;
                }
                std::vector<uint8_t> p = {mode};
                p.insert(p.end(), q.path.begin(), q.path.end());
                p.push_back(0);
                _step = kAnswer;
                SetError("none");
                return Exchange(kOpen, p, forWrite ? kMutateTimeoutUs : kNormalTimeoutUs, Sub::Simple);
            }
            if (!_subOk)
                return Finish(false);
            if (r.empty() || r[0] != 0)
            {
                _lastStatus = r.empty() ? 0xFF : r[0];
                SetError(Format("open-%u", status()));
                return Finish(false);
            }
            if (_s3)
            {
                // The old WMF answers one byte; the new one adds its capabilities
                if (r.size() >= 2)
                    _capabilities = r[1];
                if (r.size() >= 3)
                    _filexCapabilities = r[2];
                if (mode == 3 && _filexCapabilities == 0)
                {
                    SetError("open-filex-unsupported");
                    _lastStatus = 0x15;
                    return Finish(false);
                }
                _openMode = mode;
                _randomOffsetValid = mode == 3;
            }
            if (forWrite)
                ResetWriteState(true);
            _writeSession = q.op != Op::OpenRead;
            return Finish(true);
        }

        case Op::Read:
        case Op::ReadAt:
            switch (_step)
            {
                case kStart:
                {
                    if (!_s3)
                    {
                        // E01: one readFile into the 512-byte I/O buffer
                        _step = kE01ReadDone;
                        return StartReadFile(std::min<size_t>(q.value, kBlockSize));
                    }
                    const size_t room = kRingCapacity - std::min(kRingCapacity, _toNet.size());
                    const size_t maxWindow = _openMode == 3 ? kFilexTransferWindow : kTransferWindow;
                    const size_t pumpLimit = q.op == Op::ReadAt ? maxWindow : kTransferWindow;
                    const size_t wanted = std::min(std::min<size_t>(q.value, pumpLimit), room);
                    if (wanted == 0)
                    {
                        _result.wouldBlock = room == 0;
                        return Finish(true);
                    }
                    _opWanted = static_cast<uint32_t>(wanted);
                    if (q.op == Op::ReadAt && _openMode == 3)
                    {
                        // seekFile: the position is always set
                        std::vector<uint8_t> p;
                        Put32(p, q.offset);
                        _step = kSeekAnswer;
                        SetError("none");
                        return Exchange(kSeek, p, kNormalTimeoutUs, Sub::Simple);
                    }
                    _step = kReadLoop;
                    _opReceived = 0;
                    _subOk = true;
                    break;
                }
                case kSeekAnswer:
                    if (!_subOk)
                    {
                        _lastStatus = 0xFF;
                        return Finish(false);
                    }
                    if (r.empty() || r[0] != 0)
                    {
                        _lastStatus = r.empty() ? 0xFF : r[0];
                        SetError(Format("seek-%u", _lastStatus));
                        return Finish(false);
                    }
                    _lastStatus = 0;
                    _randomOffset = q.offset;
                    _randomOffsetValid = true;
                    _step = kReadLoop;
                    _opReceived = 0;
                    break;
                case kE01ReadDone:
                    if (!_subOk)
                        return Finish(false);
                    _result.transferred = _partGot;
                    _result.atEnd = _partGot == 0;
                    return Finish(true);
                default: break;
            }
            // readFileWindow
            if (_step == kReadLoop && _opReceived == 0 && _sub == Sub::None && _opWindow == 0)
            {
                const size_t windowLimit = _openMode == 3 ? kFilexTransferWindow : kTransferWindow;
                if (_opWanted == 0 || _opWanted > windowLimit)
                {
                    SetError("read-window-range");
                    return Finish(false);
                }
                _opWindow = 1;   // the range was checked
                if ((_capabilities & kCapabilityReadWindow) != 0)
                {
                    _step = kReadWindowDone;
                    return StartReadWindow(_opWanted);
                }
            }
            if (_step == kReadWindowDone)
            {
                if (!_subOk)
                    return Finish(false);
                _result.transferred = _windowReceived;
                _result.atEnd = _windowReceived == 0;
                return Finish(true);
            }
            if (_step == kReadPart)
            {
                if (!_subOk)
                    return Finish(false);
                if (_partGot == 0)
                {
                    SetError("read-window-empty");
                    return Finish(false);
                }
                _opReceived += _partGot;
                if (_partGot < _partWanted)
                    _step = kReadFinish;
                else
                    _step = kReadLoop;
            }
            if (_step == kReadLoop)
            {
                // The old WMF: the same window in 512-byte READs
                if (_opReceived < _opWanted)
                {
                    _step = kReadPart;
                    return StartReadFile(std::min<size_t>(_opWanted - _opReceived, kBlockSize));
                }
                _step = kReadFinish;
            }
            _result.transferred = _opReceived;
            _result.atEnd = _opReceived == 0;
            return Finish(true);

        case Op::Write:
        case Op::WriteAt:
            switch (_step)
            {
                case kStart:
                {
                    if (!_s3)
                    {
                        // E01: writeFile(slot bytes) - the server put them in the ring
                        if (!_writeActive)
                        {
                            SetError("write-not-open");
                            return Finish(false);
                        }
                        if (_writeFailed)
                            return Finish(false);
                        const size_t n = std::min<size_t>(q.value, _fromNet.size());
                        _chunk.assign(_fromNet.begin(), _fromNet.begin() + static_cast<std::ptrdiff_t>(n));
                        _fromNet.erase(_fromNet.begin(), _fromNet.begin() + static_cast<std::ptrdiff_t>(n));
                        _chunkPos = 0;
                        _opTransferred = static_cast<uint32_t>(n);
                        _step = kFileChunkLoop;
                        ContinueWriteFile();
                        if (_step != kFileChunkLoop)
                            return;   // a block is being flushed
                        _result.transferred = _opTransferred;
                        return Finish(true);
                    }
                    const size_t requested = std::min<size_t>(q.value, _fromNet.size());
                    if (requested == 0)
                    {
                        _result.wouldBlock = _fromNet.empty();
                        return Finish(true);
                    }
                    _opRequested = static_cast<uint32_t>(requested);
                    _opTransferred = 0;
                    if (q.op == Op::WriteAt && _openMode == 3)
                    {
                        std::vector<uint8_t> p;
                        Put32(p, q.offset);
                        _step = kSeekAnswer;
                        SetError("none");
                        return Exchange(kSeek, p, kNormalTimeoutUs, Sub::Simple);
                    }
                    _step = kWriteLoop;
                    break;
                }
                case kSeekAnswer:
                    if (!_subOk)
                    {
                        _lastStatus = 0xFF;
                        return Finish(false);
                    }
                    if (r.empty() || r[0] != 0)
                    {
                        _lastStatus = r.empty() ? 0xFF : r[0];
                        SetError(Format("seek-%u", _lastStatus));
                        return Finish(false);
                    }
                    _lastStatus = 0;
                    _randomOffset = q.offset;
                    _randomOffsetValid = true;
                    _step = kWriteLoop;
                    break;
                case kFileBlockDone:
                    // E01 writeFile, or one chunk of the S3 legacy path: a block was flushed
                    if (!_subOk)
                    {
                        _writeFailed = true;
                        if (!_s3)
                            return Finish(false);
                        _result.transferred = _opTransferred;
                        return Finish(false);
                    }
                    _writeBuffer.clear();
                    ContinueWriteFile();
                    if (_step != kFileChunkLoop)
                        return;
                    if (!_s3)
                    {
                        _result.transferred = _opTransferred;
                        return Finish(true);
                    }
                    _step = kWriteChunkDone;
                    _subOk = true;
                    break;
                default: break;
            }
            for (;;)
            {
                if (_step == kWriteLoop)
                {
                    if (_opTransferred >= _opRequested)
                    {
                        _result.transferred = _opTransferred;
                        return Finish(true);
                    }
                    const size_t maxWindow = _openMode == 3 ? kFilexTransferWindow : kTransferWindow;
                    const size_t pumpLimit = q.op == Op::WriteAt ? maxWindow : kTransferWindow;
                    _opWindow = static_cast<uint32_t>(std::min<size_t>(_opRequested - _opTransferred, pumpLimit));
                    // writeFileWindow
                    const bool randomWrite = _openMode == 3;
                    bool failed = false;
                    if (!_writeActive && !randomWrite)
                    {
                        SetError("write-not-open");
                        failed = true;
                    }
                    else if (_writeFailed)
                        failed = true;
                    else if (_opWindow == 0 || _opWindow > (randomWrite ? kFilexTransferWindow : kTransferWindow))
                    {
                        SetError("write-window-range");
                        _writeFailed = true;
                        failed = true;
                    }
                    else if ((_capabilities & kCapabilityWriteWindow) == 0 && randomWrite)
                    {
                        SetError("write-window-unsupported");
                        _writeFailed = true;
                        failed = true;
                    }
                    if (failed)
                    {
                        _result.transferred = _opTransferred;
                        return Finish(false);
                    }
                    if ((_capabilities & kCapabilityWriteWindow) != 0)
                    {
                        _step = kWriteWindowDone;
                        return StartWriteWindow(_opWindow);
                    }
                    // The old WMF: the window in fragment-sized pieces through writeFile (512-byte blocks)
                    _opChunk = 0;
                    _step = kWriteChunkLoop;
                    continue;
                }
                if (_step == kWriteChunkLoop)
                {
                    if (_opChunk >= _opWindow)
                    {
                        _subOk = true;
                        _step = kWriteWindowDone;
                        continue;
                    }
                    const size_t count = std::min<size_t>(_opWindow - _opChunk, kS3Fragment);
                    const auto from = _fromNet.begin() + static_cast<std::ptrdiff_t>(_opChunk);
                    _chunk.assign(from, from + static_cast<std::ptrdiff_t>(count));
                    _chunkPos = 0;
                    _opChunk += static_cast<uint32_t>(count);
                    if (!_writeActive)
                    {
                        SetError("write-not-open");
                        _writeFailed = true;
                        _result.transferred = _opTransferred;
                        return Finish(false);
                    }
                    ContinueWriteFile();
                    if (_step != kFileChunkLoop)
                        return;   // flushing
                    _step = kWriteChunkLoop;
                    continue;
                }
                if (_step == kWriteChunkDone)
                {
                    _step = kWriteChunkLoop;
                    continue;
                }
                if (_step == kWriteWindowDone)
                {
                    if (!_subOk)
                    {
                        _result.transferred = _opTransferred;
                        return Finish(false);
                    }
                    // The window left the ring only after the Z80's one answer
                    _fromNet.erase(_fromNet.begin(), _fromNet.begin() + static_cast<std::ptrdiff_t>(_opWindow));
                    _opTransferred += _opWindow;
                    _result.transferred = _opTransferred;
                    _step = kWriteLoop;
                    continue;
                }
                return;
            }

        case Op::Extend:
            if (_step == kStart)
            {
                if (!_writeActive)
                {
                    SetError("extend-not-open");
                    return Finish(false);
                }
                if (_writeFailed)
                    return Finish(false);
                if (q.value == 0)
                    return Finish(true);
                if ((_capabilities & kCapabilityExtend) == 0)
                {
                    SetError("extend-unsupported");
                    return Finish(false);
                }
                if (!_writeBuffer.empty())
                {
                    SetError("extend-buffered");
                    _writeFailed = true;
                    return Finish(false);
                }
                std::vector<uint8_t> p;
                Put32(p, q.value);
                _step = kAnswer;
                SetError("none");
                return Exchange(kExtend, p, kCloseTimeoutUs, Sub::Simple);
            }
            if (!_subOk)
            {
                _writeFailed = true;
                return Finish(false);
            }
            if (r.empty() || r[0] != 0)
            {
                SetError(Format("extend-%u", status()));
                _writeFailed = true;
                return Finish(false);
            }
            _result.transferred = q.value;
            return Finish(true);

        case Op::SetEof:
            if (_step == kStart)
            {
                if (_openMode != 3)
                {
                    SetError("set-eof-not-random");
                    _lastStatus = 0x15;
                    return Finish(false);
                }
                std::vector<uint8_t> p;
                Put32(p, q.value);
                _step = kAnswer;
                SetError("none");
                return Exchange(kSetEof, p, kMutateTimeoutUs, Sub::Simple);
            }
            if (!_subOk)
            {
                _lastStatus = 0xFF;
                return Finish(false);
            }
            if (r.empty() || (r[0] != 0 && r[0] != 0x25))
            {
                _lastStatus = r.empty() ? 0xFF : r[0];
                SetError(Format("set-eof-%u", _lastStatus));
                return Finish(false);
            }
            if (r.size() < 5)
            {
                SetError("set-eof-short");
                _lastStatus = 0xFF;
                return Finish(false);
            }
            _result.size = Le32(&r[1]);
            _lastStatus = r[0];
            return Finish(true);

        case Op::CloseCommit:
        case Op::CloseAbort:
        {
            const bool commit = q.op == Op::CloseCommit;
            switch (_step)
            {
                case kStart:
                {
                    if (_s3 && commit && _writeSession && !_fromNet.empty())
                        return Finish(false, "ingress-pending");
                    // closeFile
                    const bool wasWrite = _writeActive;
                    _opWanted = wasWrite ? 1 : 0;
                    if (wasWrite && commit && _writeFailed)
                    {
                        _savedError = _lastError;
                        _step = kCloseAbortAnswer;
                        return Exchange(kClose, {0}, kCloseTimeoutUs, Sub::Simple);
                    }
                    if (wasWrite && commit && !_writeBuffer.empty())
                    {
                        _step = kCloseFlushed;
                        return StartFlushBlock(_writeBuffer.size());
                    }
                    _step = kCloseAnswer;
                    SetError("none");
                    return Exchange(kClose, commit ? std::vector<uint8_t>{} : std::vector<uint8_t>{0}, kCloseTimeoutUs,
                                    Sub::Simple);
                }
                case kCloseFlushed:
                    if (!_subOk)
                    {
                        _savedError = _lastError;
                        _step = kCloseAbortAnswer;
                        return Exchange(kClose, {0}, kCloseTimeoutUs, Sub::Simple);
                    }
                    _writeBuffer.clear();
                    _step = kCloseAnswer;
                    SetError("none");
                    return Exchange(kClose, {}, kCloseTimeoutUs, Sub::Simple);
                case kCloseAbortAnswer:
                    // The abort's own answer does not matter: the first error stays
                    ResetWriteState(false);
                    _lastError = _savedError;
                    _writeSession = false;
                    return Finish(false);
                case kCloseAnswer:
                {
                    const bool requested = _subOk;
                    const bool closed = requested && !r.empty() && r[0] == 0;
                    if (requested && !closed)
                        SetError(Format("close-%u", status()));
                    if (_s3 || _opWanted)
                        ResetWriteState(false);
                    if (_s3)
                    {
                        _capabilities = 0;
                        _filexCapabilities = 0;
                        _openMode = 0;
                        _randomOffsetValid = false;
                        if (requested)
                            _lastStatus = r.empty() ? 0xFF : r[0];
                    }
                    _writeSession = false;
                    if (!commit)
                        _fromNet.clear();
                    return Finish(closed);
                }
                default: return Finish(false);
            }
        }

        case Op::Delete:
        case Op::Mkdir:
            if (_step == kStart)
            {
                _step = kAnswer;
                return PathExchange(q.op == Op::Delete ? kDelete : kMkdir, q.path, kMutateTimeoutUs);
            }
            if (!_subOk)
                return Finish(false);
            if (r.empty() || r[0] != 0)
            {
                SetError(Format(q.op == Op::Delete ? "delete-%u" : "mkdir-%u", status()));
                return Finish(false);
            }
            return Finish(true);

        case Op::Rename:
        case Op::MoveRename:
            if (_step == kStart)
            {
                const bool move = q.op == Op::MoveRename;
                if (q.path.empty() || q.path2.empty())
                {
                    SetError(move ? "move-path-null" : "rename-path-null");
                    return Finish(false);
                }
                std::vector<uint8_t> p;
                if (move)
                {
                    p.push_back((q.flags & 2) ? 1 : 0);
                    p.push_back((q.flags & 1) ? 0x10 : 0);
                }
                else
                    p.push_back(q.value ? 0x10 : 0x00);
                p.insert(p.end(), q.path.begin(), q.path.end());
                p.push_back(0);
                p.insert(p.end(), q.path2.begin(), q.path2.end());
                p.push_back(0);
                if (p.size() > kS3Fragment)
                {
                    SetError(move ? "move-path-long" : "rename-path-long");
                    return Finish(false);
                }
                _step = kAnswer;
                SetError("none");
                return Exchange(move ? kMoveRename : kRename, p, kMutateTimeoutUs, Sub::Simple);
            }
            if (!_subOk)
            {
                if (q.op == Op::MoveRename)
                    _lastStatus = 0xFF;
                return Finish(false);
            }
            if (r.empty() || r[0] != 0)
            {
                if (q.op == Op::MoveRename)
                {
                    _lastStatus = r.empty() ? 0xFF : r[0];
                    SetError(Format("move-%u", _lastStatus));
                }
                else
                    SetError(Format("rename-%u", status()));
                return Finish(false);
            }
            if (q.op == Op::MoveRename)
                _lastStatus = 0;
            return Finish(true);

        case Op::SetMetadata:
            if (_step == kStart)
            {
                if (_openMode != 3)
                {
                    SetError("metadata-not-random");
                    _lastStatus = 0x15;
                    return Finish(false);
                }
                const Metadata& m = q.metadata;
                std::vector<uint8_t> p = {kMetadataSize, m.attrMask, m.attrValue, m.timeMask, m.createTenth};
                Put16(p, m.createTime);
                Put16(p, m.createDate);
                Put16(p, m.accessDate);
                Put16(p, m.writeTime);
                Put16(p, m.writeDate);
                p.push_back(0);
                _step = kAnswer;
                SetError("none");
                return Exchange(kSetMetadata, p, kMutateTimeoutUs, Sub::Simple);
            }
            if (!_subOk)
            {
                _lastStatus = 0xFF;
                return Finish(false);
            }
            if (r.empty() || r[0] != 0)
            {
                _lastStatus = r.empty() ? 0xFF : r[0];
                SetError(Format("metadata-%u", _lastStatus));
                return Finish(false);
            }
            if (r.size() < 2)
            {
                SetError("metadata-short");
                _lastStatus = 0xFF;
                return Finish(false);
            }
            _result.appliedAttributes = r[1];
            _lastStatus = 0;
            return Finish(true);
    }
}

// --- TTD -----------------------------------------------------------------------------------------------------------

namespace
{
constexpr uint8_t kStateVersion = 1;
}

void ZiFiVfsBridge::Save(ZiFiStateWriter& w) const
{
    w.U8(kStateVersion);
    // Request
    w.U8(static_cast<uint8_t>(_req.op));
    w.U32(_req.value);
    w.U32(_req.offset);
    w.U8(_req.flags);
    const Metadata& m = _req.metadata;
    w.U8(m.attrMask), w.U8(m.attrValue), w.U8(m.timeMask), w.U8(m.createTenth);
    w.U16(m.createTime), w.U16(m.createDate), w.U16(m.accessDate), w.U16(m.writeTime), w.U16(m.writeDate);
    w.Str(_req.path);
    w.Str(_req.path2);
    // Result
    const Result& x = _result;
    w.Bool(x.success), w.Bool(x.atEnd), w.Bool(x.wouldBlock), w.Bool(x.isDirectory);
    w.U8(x.status), w.U8(x.appliedAttributes);
    w.U32(x.size);
    w.U16(x.writeDate), w.U16(x.writeTime);
    w.Bool(x.hasMetadata);
    w.U8(x.attributes), w.U8(x.createTenth);
    w.U16(x.createTime), w.U16(x.createDate), w.U16(x.accessDate);
    w.U32(x.transferred);
    w.Str(x.name);
    w.Str(x.error);
    w.Bool(_requestPending), w.Bool(_resultReady), w.Bool(_writeSession);
    // Progress
    w.U8(_step);
    w.U8(static_cast<uint8_t>(_sub));
    w.Bool(_subOk);
    w.Bytes(_response);
    w.U32(_opWanted), w.U32(_opReceived), w.U32(_opTransferred), w.U32(_opRequested), w.U32(_opWindow), w.U32(_opChunk);
    w.Bytes(_chunk);
    w.U32(static_cast<uint32_t>(_chunkPos));
    w.Str(_savedError);
    w.Bool(_waiting);
    w.U8(_awaitCmd);
    w.U64(_deadline);
    w.U64(_timeoutUs);
    w.U32(_partWanted), w.U32(_partGot);
    w.U8(_windowSeq);
    w.U16(_windowTotal), w.U16(_windowCrc);
    w.U32(_windowReceived);
    w.U8(_blockSeq);
    w.U32(_blockLength), w.U32(_blockOffset);
    w.U16(_blockAccepted);
    w.Bytes(_blockData);
    // Client
    w.Str(_lastError);
    w.Bytes(_writeBuffer);
    w.U8(_writeSequence), w.U8(_readSequence), w.U8(_capabilities), w.U8(_filexCapabilities), w.U8(_lastStatus),
        w.U8(_openMode);
    w.U32(_randomOffset);
    w.Bool(_randomOffsetValid), w.Bool(_writeActive), w.Bool(_writeFailed), w.Bool(_directoryBatch),
        w.Bool(_directoryEnded);
    w.U32(static_cast<uint32_t>(_directoryEntries.size()));
    for (const Entry& e : _directoryEntries)
    {
        w.Bool(e.isDirectory);
        w.U32(e.size);
        w.U16(e.writeDate), w.U16(e.writeTime);
        w.Str(e.name);
    }
    w.U32(static_cast<uint32_t>(_directoryNext));
    w.Bytes(_toNet);
    w.Bytes(_fromNet);
    w.U64(_frames), w.U64(_bytesRead), w.U64(_bytesWritten);
    w.U32(_timeouts);
}

bool ZiFiVfsBridge::Load(ZiFiStateReader& r)
{
    Reset();
    if (r.U8() != kStateVersion)
        return false;
    _req.op = static_cast<Op>(r.U8());
    _req.value = r.U32();
    _req.offset = r.U32();
    _req.flags = r.U8();
    Metadata& m = _req.metadata;
    m.attrMask = r.U8(), m.attrValue = r.U8(), m.timeMask = r.U8(), m.createTenth = r.U8();
    m.createTime = r.U16(), m.createDate = r.U16(), m.accessDate = r.U16(), m.writeTime = r.U16(), m.writeDate = r.U16();
    _req.path = r.Str(256);
    _req.path2 = r.Str(256);
    Result& x = _result;
    x.success = r.Bool(), x.atEnd = r.Bool(), x.wouldBlock = r.Bool(), x.isDirectory = r.Bool();
    x.status = r.U8(), x.appliedAttributes = r.U8();
    x.size = r.U32();
    x.writeDate = r.U16(), x.writeTime = r.U16();
    x.hasMetadata = r.Bool();
    x.attributes = r.U8(), x.createTenth = r.U8();
    x.createTime = r.U16(), x.createDate = r.U16(), x.accessDate = r.U16();
    x.transferred = r.U32();
    x.name = r.Str(256);
    x.error = r.Str(64);
    _requestPending = r.Bool(), _resultReady = r.Bool(), _writeSession = r.Bool();
    _step = r.U8();
    _sub = static_cast<Sub>(r.U8());
    _subOk = r.Bool();
    _response = r.Bytes(kMaxPayload);
    _opWanted = r.U32(), _opReceived = r.U32(), _opTransferred = r.U32(), _opRequested = r.U32(), _opWindow = r.U32(),
    _opChunk = r.U32();
    _chunk = r.Bytes(kMaxPayload);
    _chunkPos = r.U32();
    _savedError = r.Str(64);
    _waiting = r.Bool();
    _awaitCmd = r.U8();
    _deadline = r.U64();
    _timeoutUs = r.U64();
    _partWanted = r.U32(), _partGot = r.U32();
    _windowSeq = r.U8();
    _windowTotal = r.U16(), _windowCrc = r.U16();
    _windowReceived = r.U32();
    _blockSeq = r.U8();
    _blockLength = r.U32(), _blockOffset = r.U32();
    _blockAccepted = r.U16();
    _blockData = r.Bytes(kBlockSize);
    _lastError = r.Str(64);
    _writeBuffer = r.Bytes(kBlockSize);
    _writeSequence = r.U8(), _readSequence = r.U8(), _capabilities = r.U8(), _filexCapabilities = r.U8(),
    _lastStatus = r.U8(), _openMode = r.U8();
    _randomOffset = r.U32();
    _randomOffsetValid = r.Bool(), _writeActive = r.Bool(), _writeFailed = r.Bool(), _directoryBatch = r.Bool(),
    _directoryEnded = r.Bool();
    const uint32_t entries = r.U32();
    for (uint32_t i = 0; i < entries && i <= kDirectoryBatchEntries && r.Ok(); ++i)
    {
        Entry e;
        e.isDirectory = r.Bool();
        e.size = r.U32();
        e.writeDate = r.U16(), e.writeTime = r.U16();
        e.name = r.Str(256);
        _directoryEntries.push_back(std::move(e));
    }
    _directoryNext = r.U32();
    _toNet = r.Deque(kRingCapacity);
    _fromNet = r.Deque(kRingCapacity);
    _frames = r.U64(), _bytesRead = r.U64(), _bytesWritten = r.U64();
    _timeouts = r.U32();
    if (!r.Ok())
    {
        Reset();
        return false;
    }
    return true;
}
