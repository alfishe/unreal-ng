#pragma once

/// @file zififtpserver.h
/// @brief The ZIFI-NATIVE firmwares' FTP server: the ESP listens, a PC's FTP client logs in, and every file command
/// becomes VFS requests to the Z80 (zifivfsbridge.h), where the Wild Commander plugin (ZIFIFTP.WMF) reads and writes
/// the SD card. Ported from the firmwares' sources:
///  - S3 (s3-native-0.6.94): https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/ftp_server.cpp
///    - up to three control sessions, each with its passive port 2122..2124; one VFS path for all: a file command of
///      a second session waits until the first one's is done (one more queued is "503 Another command is already
///      queued"); the other sessions' control lines are served meanwhile
///    - USER PASS SYST FEAT OPTS NOOP TYPE PWD/XPWD CWD/XCWD CDUP PORT EPRT PASV EPSV SIZE LIST NLST MLSD MLST MDTM
///      MFMT RETR STOR DELE MKD/XMKD QUIT; LIST dates as "ls -l" (the time for files younger than half a year by the
///      ESP's SNTP clock), MLSD / MDTM / MFMT in UTC by the zifi.ini zone
///    - RETR streams 16 KiB VFS windows, STOR collects 16 KiB in the 64 KiB ring before each window write
///  - ESP01S (native-0.2.2): https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project/blob/main/src/ftp_server.cpp
///    - one session ("421 Only one FTP session is allowed"), passive port 2122, the commands above without MLSD MLST
///      MDTM MFMT; LIST dates "Jan 01 00:00"; RETR / STOR in 512-byte READ / BLOCK exchanges, STOR through four
///      256-byte slots read from TCP while the Z80 writes
///    - one loop: while a command runs nothing else is served
///
/// Events to the Z80: 60 client state (0 none, 1 connected, 2 logged in), 61 the command (USER / PASS without their
/// argument, at most 30 characters).
///
/// Sockets (EspStack slots, fixed): 2 the control listener, 3..5 control sessions, 6..8 passive listeners, 9..11
/// data connections, 14 a refused client getting its 421.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "emulator/io/serial/esp/zifivfsbridge.h"

class ZiFiBridgeHost;
class ZiFiStateWriter;
class ZiFiStateReader;

class ZiFiFtpServer
{
public:
    static constexpr uint16_t kPassivePort = 2122;
    static constexpr int kListenSlot = 2;
    static constexpr int kControlSlot = 3;
    static constexpr int kPassiveSlot = 6;
    static constexpr int kDataSlot = 9;
    static constexpr int kRefuseSlot = 14;

    ZiFiFtpServer(ZiFiBridgeHost& host, bool s3);

    /// FTP_START [port LE16][user\0][password\0] (all optional: 21, zx / zx); false with `error` ("ftp no wifi")
    bool Start(const std::vector<uint8_t>& payload, uint16_t& actualPort, std::string& error);
    void Stop();
    /// The Wi-Fi association dropped (S3 WiFi.disconnect): every connection is gone, the listener stays
    void LinkLost();
    /// The module restarted: forget everything without touching a socket (the stack was reset / reloaded)
    void Forget();
    /// Accept clients, read control lines, run the command in progress, time out idle sessions
    void Poll();

    bool Running() const { return _running; }
    uint16_t Port() const { return _port; }
    int MaxSessions() const { return _s3 ? 3 : 1; }
    /// A file command runs (S3: the VFS owner's; ESP01S: the loop is inside it)
    bool JobActive() const { return _job.kind != Job::None; }
    /// FTP_RAM_STATS: [count][at LE32, free LE32]...
    std::vector<uint8_t> RamStats() const;

    /// Status views
    struct SessionInfo
    {
        int index = 0;
        std::string remote;
        bool loggedIn = false;
        std::string cwd;
        std::string mode;   ///< "passive" / "active" / ""
    };
    std::vector<SessionInfo> Sessions() const;
    std::string JobText() const;
    const std::string& LastCommand() const { return _lastCommand; }
    const std::string& LastReply() const { return _lastReply; }
    uint64_t BytesSent() const { return _bytesSent; }
    uint64_t BytesReceived() const { return _bytesReceived; }
    uint32_t FilesSent() const { return _filesSent; }
    uint32_t FilesReceived() const { return _filesReceived; }

    void Save(ZiFiStateWriter& w) const;
    bool Load(ZiFiStateReader& r);

private:
    enum class ListFormat : uint8_t
    {
        Long,
        Names,
        Machine
    };

    struct Session
    {
        bool active = false;
        bool passiveListening = false;
        bool loggedIn = false;
        bool userAccepted = false;
        bool discardLine = false;
        bool activeEndpointSet = false;
        bool pendingCommand = false;
        uint16_t passivePort = 0;
        uint16_t activePort = 0;
        uint32_t activeAddress = 0;
        uint64_t lastControlActivity = 0;
        std::string cwd = "/";
        std::string line;
        std::string pendingLine;
    };

    /// The file command in progress (the firmware's call stack, as data)
    struct Job
    {
        enum Kind : uint8_t
        {
            None,
            Cwd,
            Size,
            List,
            Mlst,
            Mdtm,
            Mfmt,
            Retr,
            Stor,
            Dele,
            Mkd
        } kind = None;
        uint8_t step = 0;
        int session = 0;
        std::string path;
        std::string rest;                 ///< MFMT: the path as the client gave it
        ListFormat format = ListFormat::Long;
        ZiFiVfsBridge::Result stat;       ///< RETR: statResult; MFMT: the stamp result
        uint32_t sent = 0;
        bool failed = false;
        bool set = false;                 ///< MFMT: SET_METADATA succeeded
        std::string failure = "none";
        int64_t requested = 0;            ///< MFMT: the time asked for
        uint16_t stampDate = 0, stampTime = 0;
        uint64_t vfsDeadline = 0;         ///< awaitVfs
        uint8_t vfsOp = 0;
        uint64_t dataDeadline = 0;        ///< openData: the passive client / the active connect
        bool connecting = false;
        uint8_t failureKind = 0;          ///< E01 STOR: recv / zvfs / recv-timeout / recv-empty / zvfs-close
    };

    // Sessions and control
    void AcceptControl();
    void ServiceSessions(int excluded);
    void ReceiveControl(int index);
    void DispatchCommand(int index, std::string line);
    void ExecuteCommand(int index, std::string line);
    void RunPendingCommand();
    void DropControl(int index);
    void SendClientState();
    void SendClientEvent(uint8_t state);
    void SendCommandEvent(const std::string& command, const std::string& argument);
    static bool CommandUsesVfs(const std::string& line);
    bool Reply(int index, const std::string& text);
    bool SendAll(int slot, const std::string& text);
    bool SendAll(int slot, const uint8_t* data, size_t length);

    bool NormalizePath(const Session& session, const std::string& argument, std::string& output) const;
    static bool ParsePort(const std::string& argument, uint32_t& address, uint16_t& port);
    static bool ParseEprt(const std::string& argument, uint32_t& address, uint16_t& port);

    void ClosePassive(int index);
    void CloseData(int index);
    void EnterPassive(int index, bool extended);
    void SetActive(int index, const std::string& argument, bool extended);
    /// openData as a step: 1 open, 0 still waiting, -1 failed (replied)
    int OpenData();

    // The job
    void StartJob(Job::Kind kind, int index, const std::string& argument, ListFormat format = ListFormat::Long);
    void RunJob();
    void EndJob();
    /// requestVfs: false = refused ("bridge-busy"); the result comes to AwaitVfs
    bool RequestVfs(ZiFiVfsBridge::Op op, const std::string& path, uint32_t value, uint64_t timeoutUs);
    bool RequestMetadata(const ZiFiVfsBridge::Metadata& metadata);
    /// 1 done ok, -1 done failed (lastVfsError set), 0 still waiting
    int AwaitVfs(ZiFiVfsBridge::Result& result);
    bool StepCwd(Job& j);
    bool StepSize(Job& j);
    bool StepList(Job& j);
    bool StepMlst(Job& j);
    bool StepMdtm(Job& j);
    bool StepMfmt(Job& j);
    bool StepRetr(Job& j);
    bool StepStor(Job& j);
    bool StepStorE01(Job& j);
    bool StepDele(Job& j);
    bool StepMkd(Job& j);
    /// STOR: TCP into the ring (S3) / the slots (E01) while the Z80 writes
    bool PrefetchStore(int index);
    bool PrefetchStoreE01(int index);
    std::string FormatListDate(const ZiFiVfsBridge::Result& entry) const;
    int FormatFacts(const ZiFiVfsBridge::Result& entry, const std::string& name, std::string& output) const;
    int32_t TimezoneSeconds() const;

    uint64_t Now() const;
    uint64_t Us(uint64_t us) const;

    ZiFiBridgeHost& _host;
    bool _s3 = true;
    bool _running = false;
    uint16_t _port = 21;
    std::string _user = "zx";
    std::string _password = "zx";
    Session _sessions[3];
    int _vfsOwner = -1;
    Job _job;
    std::string _lastVfsError = "none";
    ZiFiVfsBridge::Result _vfsResult;

    // STOR (S3: the ring is the bridge's; E01: four slots of 256 bytes)
    bool _storEof = false;
    bool _storError = false;
    uint32_t _storReceived = 0;
    uint64_t _storLastProgress = 0;
    std::deque<std::vector<uint8_t>> _storSlots;   ///< E01: queued slots
    bool _storSlotActive = false;                  ///< E01: one slot is being written

    uint8_t _ramStatCount = 0;
    uint32_t _ramStatAt[2] = {0, 0};
    uint32_t _ramStatFree[2] = {0, 0};

    // Status (not firmware state)
    std::string _lastCommand;
    std::string _lastReply;
    uint64_t _bytesSent = 0;
    uint64_t _bytesReceived = 0;
    uint32_t _filesSent = 0;
    uint32_t _filesReceived = 0;
};
