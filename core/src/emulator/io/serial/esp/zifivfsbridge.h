#pragma once

/// @file zifivfsbridge.h
/// @brief The ZIFI-NATIVE firmwares' file client: the ESP has no SD card, so every file operation of its FTP /
/// WebDAV servers becomes a VFS request frame to the Z80 (40..5E), answered by the Wild Commander plugin that runs
/// there (ZIFIFTP.WMF, ZIFIWDAV.WMF). Ported from the firmwares' own sources:
///  - S3 (s3-native-0.6.94): VfsClient + VfsBridge, ZiFi-ESP32-S3-Zero 2e5ba83
///    (https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/vfs_client.cpp,
///    https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/vfs_bridge.cpp, docs/PROTOCOL.md
///    "VFS: команды ESP -> Z80"): one operation at a time; 64 KiB rings between the network and the VFS; read and
///    write windows up to 16 KiB (58 / 57, CRC-16/CCITT-FALSE over the window) when the plugin's OPEN answer
///    announces them, else 512-byte READ (51) / BLOCK (56) exchanges; batched READDIR (42 [count]) when OPENDIR
///    announces it; FILEX random access (OPEN mode 3, SEEK, SET_EOF, MOVE_RENAME, SET_METADATA)
///  - ESP01S (native-0.2.2): VfsClient, ZiFi-ESP-01S-Native-C-Project 90834e4
///    (https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project/blob/main/src/vfs_client.cpp): STAT,
///    OPENDIR / READDIR one entry per request, OPEN read / write, READ 512, BLOCK, CLOSE, DELETE, MKDIR
///
/// The bridge talks to the Z80 through the module (sendFrame) and is told the frame it waits for (OnResponse);
/// while it waits the module's UART owner is "inside waitFor": only PING and SYS_RESET are served, every other
/// frame is dropped (S3 VfsClient::handleUnexpected, E01 the same). Timeouts per exchange as in the sources
/// (5 s normal, 30 s per fragment, 60 s for a change, 180 s for a close / window acknowledgment).
///
/// Deterministic: it acts on frames from the Z80 and the emulated clock only. TTD: Save / Load (zifistate.h).

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

class ZiFiStateWriter;
class ZiFiStateReader;

class ZiFiVfsBridge
{
public:
    // Commands, ESP -> Z80 (protocol.hpp)
    static constexpr uint8_t kStat = 0x40, kOpenDir = 0x41, kReadDir = 0x42, kFsInfo = 0x43, kFatWindow = 0x44,
                             kOpen = 0x50, kRead = 0x51, kClose = 0x53, kDelete = 0x54, kMkdir = 0x55, kBlock = 0x56,
                             kWriteWindow = 0x57, kReadWindow = 0x58, kRename = 0x59, kExtend = 0x5A, kSeek = 0x5B,
                             kSetEof = 0x5C, kMoveRename = 0x5D, kSetMetadata = 0x5E;
    static bool IsVfsCommand(uint8_t cmd) { return cmd >= 0x40 && cmd <= 0x5E; }

    static constexpr size_t kTransferWindow = 16 * 1024;           ///< VfsClient::kTransferWindowSize
    static constexpr size_t kFilexTransferWindow = 16 * 1024 - 32; ///< FILEX's parameter block shares the page
    static constexpr size_t kRingCapacity = 64 * 1024;             ///< VfsBridge::kPsramRingCapacity
    static constexpr size_t kBlockSize = 512;

    enum class Op : uint8_t
    {
        ResetBuffers,
        Stat,
        OpenDirectory,
        ReadDirectory,
        OpenRead,
        OpenWrite,
        OpenAppend,
        OpenRandom,
        Read,
        Write,
        ReadAt,
        WriteAt,
        Extend,
        SetEof,
        CloseCommit,
        CloseAbort,
        Delete,
        Mkdir,
        Rename,
        MoveRename,
        SetMetadata,
    };
    static const char* OpName(Op op);

    /// FILEX SET_METADATA / GET_METADATA (16 bytes on the line)
    struct Metadata
    {
        uint8_t attrMask = 0, attrValue = 0, timeMask = 0, createTenth = 0;
        uint16_t createTime = 0, createDate = 0, accessDate = 0, writeTime = 0, writeDate = 0;
    };

    struct Result
    {
        bool success = false;
        bool atEnd = false;
        bool wouldBlock = false;
        bool isDirectory = false;
        uint8_t status = 0;
        uint8_t appliedAttributes = 0;
        uint32_t size = 0;
        uint16_t writeDate = 0, writeTime = 0;
        bool hasMetadata = false;
        uint8_t attributes = 0, createTenth = 0;
        uint16_t createTime = 0, createDate = 0, accessDate = 0;
        uint32_t transferred = 0;
        std::string name;
        std::string error;
    };

    /// @param s3 the S3 firmware (else the ESP-01S one)
    explicit ZiFiVfsBridge(bool s3);

    /// A frame to the Z80
    std::function<void(uint8_t cmd, const std::vector<uint8_t>& payload)> sendFrame;
    /// The emulated clock (T-states) and microseconds -> T-states
    std::function<uint64_t()> now;
    std::function<uint64_t(uint64_t)> micros;

    // --- Network side (VfsBridge::submit*, takeResult) -------------------------------------------------------

    bool Submit(Op op, const std::string& path = {}, uint32_t value = 0);
    bool SubmitAt(Op op, uint32_t offset, uint32_t length);
    bool SubmitRename(const std::string& oldPath, const std::string& newName, bool directory);
    bool SubmitMoveRename(const std::string& oldPath, const std::string& newPath, bool directory, bool replace);
    bool SubmitMetadata(const Metadata& metadata);
    bool TakeResult(Result& out);
    /// A request is submitted and its result not taken yet
    bool RequestPending() const { return _requestPending; }
    /// The operation of the pending request (status views)
    Op PendingOp() const { return _req.op; }

    // --- The UART side -----------------------------------------------------------------------------------------

    /// The client waits for a frame from the Z80 (inside waitFor)
    bool Waiting() const { return _waiting; }
    uint8_t AwaitedCommand() const { return _awaitCmd; }
    /// The awaited frame arrived
    void OnResponse(const std::vector<uint8_t>& payload);
    /// Exchange timeouts (call at least once per frame)
    void Tick();

    // --- The rings (VfsBridge: VFS -> network for RETR / GET, network -> VFS for STOR / PUT) ------------------

    size_t ToNetAvailable() const { return _toNet.size(); }
    size_t ReadForNetwork(uint8_t* out, size_t capacity);
    size_t FromNetAvailable() const { return _fromNet.size(); }
    size_t FromNetFree() const { return kRingCapacity - std::min(kRingCapacity, _fromNet.size()); }
    size_t WriteFromNetwork(const uint8_t* data, size_t length);

    /// The module restarted / the services stopped: no request, empty rings, a fresh client
    void Reset();

    // --- Status ------------------------------------------------------------------------------------------------

    const std::string& LastError() const { return _lastError; }
    uint8_t OpenMode() const { return _openMode; }
    uint8_t Capabilities() const { return _capabilities; }
    uint8_t FilexCapabilities() const { return _filexCapabilities; }
    uint64_t Requests() const { return _frames; }
    uint64_t BytesRead() const { return _bytesRead; }
    uint64_t BytesWritten() const { return _bytesWritten; }
    uint32_t Timeouts() const { return _timeouts; }

    // --- TTD -----------------------------------------------------------------------------------------------------

    void Save(ZiFiStateWriter& w) const;
    bool Load(ZiFiStateReader& r);

private:
    struct Request
    {
        Op op = Op::ResetBuffers;
        uint32_t value = 0;
        uint32_t offset = 0;
        uint8_t flags = 0;
        Metadata metadata;
        std::string path;
        std::string path2;
    };
    struct Entry
    {
        bool isDirectory = false;
        uint32_t size = 0;
        uint16_t writeDate = 0, writeTime = 0;
        bool hasMetadata = false;
        uint8_t attributes = 0, createTenth = 0;
        uint16_t createTime = 0, createDate = 0, accessDate = 0;
        std::string name;
    };
    /// The exchange in progress inside an operation
    enum class Sub : uint8_t
    {
        None,
        Simple,       ///< one request, one answer
        ReadFile,     ///< 51
        ReadWindow,   ///< 58, a stream of frames
        Batch,        ///< 42 [count], entries then a summary frame
        FlushBlock,   ///< 56 first fragment, then continuations, each acknowledged
        WriteWindow,  ///< 57 fragments, one acknowledgment
    };

    bool Accept(const Request& request);
    void Begin();
    /// Run the operation until it waits for the Z80 or finishes
    void Step();
    void Finish(bool success, const char* error = nullptr);
    void SetError(const std::string& text) { _lastError = text; }

    // Exchanges
    void Exchange(uint8_t cmd, const std::vector<uint8_t>& payload, uint64_t timeoutUs, Sub sub);
    void PathExchange(uint8_t cmd, const std::string& path, uint64_t timeoutUs);
    void SubDone(bool ok);
    void StartReadFile(size_t wanted);
    void StartReadWindow(size_t wanted);
    void StartFlushBlock(size_t rawLength);
    void SendBlockFragment();
    void StartWriteWindow(size_t length);
    void StartBatch();
    bool CheckBlockAck(const std::vector<uint8_t>& r, uint8_t sequence, uint16_t accepted);
    void ParseDirectoryEntry(const std::vector<uint8_t>& r, Entry& entry) const;
    void ResetWriteState(bool active);
    void CopyEntry(const Entry& e);
    /// writeFile: buffer, flush full 512-byte blocks (the op's chunk is _chunk)
    void ContinueWriteFile();

    bool _s3 = true;

    // Request / result (VfsBridge::Exchange)
    Request _req;
    Result _result;
    bool _requestPending = false;
    bool _resultReady = false;
    bool _writeSession = false;

    // The operation's progress
    uint8_t _step = 0;
    Sub _sub = Sub::None;
    bool _subOk = false;
    std::vector<uint8_t> _response;   ///< Simple: the answer
    uint32_t _opWanted = 0, _opReceived = 0, _opTransferred = 0, _opRequested = 0, _opWindow = 0, _opChunk = 0;
    std::vector<uint8_t> _chunk;      ///< writeFile: the bytes it buffers now
    size_t _chunkPos = 0;
    std::string _savedError;

    // The exchange
    bool _waiting = false;
    uint8_t _awaitCmd = 0;
    uint64_t _deadline = 0;
    uint64_t _timeoutUs = 0;
    // ReadFile / ReadWindow
    uint32_t _partWanted = 0;
    uint32_t _partGot = 0;
    uint8_t _windowSeq = 0;
    uint16_t _windowTotal = 0;
    uint16_t _windowCrc = 0xFFFF;
    uint32_t _windowReceived = 0;
    // FlushBlock / WriteWindow
    uint8_t _blockSeq = 0;
    uint32_t _blockLength = 0;
    uint32_t _blockOffset = 0;
    uint16_t _blockAccepted = 0;
    std::vector<uint8_t> _blockData;

    // VfsClient
    std::string _lastError = "none";
    std::vector<uint8_t> _writeBuffer;
    uint8_t _writeSequence = 0;
    uint8_t _readSequence = 0;
    uint8_t _capabilities = 0;
    uint8_t _filexCapabilities = 0;
    uint8_t _lastStatus = 0;
    uint8_t _openMode = 0;
    uint32_t _randomOffset = 0;
    bool _randomOffsetValid = false;
    bool _writeActive = false;
    bool _writeFailed = false;
    bool _directoryBatch = false;
    bool _directoryEnded = false;
    std::vector<Entry> _directoryEntries;
    size_t _directoryNext = 0;

    // Rings
    std::deque<uint8_t> _toNet;
    std::deque<uint8_t> _fromNet;

    // Status (not TTD state beyond what equals)
    uint64_t _frames = 0;
    uint64_t _bytesRead = 0;
    uint64_t _bytesWritten = 0;
    uint32_t _timeouts = 0;
};
