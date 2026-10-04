#pragma once

/// @file zifiwebdavserver.h
/// @brief The ESP-01S native firmware's WebDAV server (native-0.2.2,
/// https://github.com/andrewinsidelazarev/ZiFi-ESP-01S-Native-C-Project/blob/main/src/webdav_server.cpp): a minimal
/// WebDAV class 1 on port 80 over the same VFS as the FTP server, started and stopped with it (FTP_START /
/// FTP_STOP; there is no command of its own). One client at a time (a second gets 503), one request per connection
/// (Connection: close): OPTIONS, PROPFIND (Depth 0 or 1; "infinity" is one level), GET, HEAD, PUT (Content-Length
/// required; the body through four 256-byte slots while the Z80 writes 512-byte blocks), DELETE, MKCOL. No
/// authentication (the plugin says "trusted local network only").
///
/// Sockets (EspStack slots): 12 the listener, 13 the client, 15 a refused client getting its 503.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "emulator/io/serial/esp/zifivfsbridge.h"

class ZiFiBridgeHost;
class ZiFiStateWriter;
class ZiFiStateReader;

class ZiFiWebDavServer
{
public:
    static constexpr uint16_t kDefaultPort = 80;
    static constexpr int kListenSlot = 12;
    static constexpr int kClientSlot = 13;
    static constexpr int kRefuseSlot = 15;

    explicit ZiFiWebDavServer(ZiFiBridgeHost& host);

    bool Start(uint16_t port, std::string& error);
    void Stop();
    /// Forget everything without touching the sockets (a restart or a TTD load already took them)
    void Forget();
    void Poll();

    bool Running() const { return _running; }
    uint16_t Port() const { return _port; }
    bool JobActive() const { return _job.kind != Job::None; }
    bool ClientConnected() const { return _clientActive; }

    // Status
    std::string JobText() const;
    const std::string& LastRequest() const { return _lastRequest; }
    uint16_t LastStatus() const { return _lastStatus; }
    uint64_t BytesSent() const { return _bytesSent; }
    uint64_t BytesReceived() const { return _bytesReceived; }
    uint32_t Requests() const { return _requests; }

    void Save(ZiFiStateWriter& w) const;
    bool Load(ZiFiStateReader& r);

private:
    struct Job
    {
        enum Kind : uint8_t
        {
            None,
            Discard,    ///< reading a request body the method does not use, then the method
            Propfind,
            Get,
            Head,
            Put,
            Delete,
            Mkcol
        } kind = None;
        Kind after = None;          ///< Discard: the method that follows (None: answer `afterStatus`)
        uint16_t afterStatus = 0;   ///< Discard before OPTIONS (200) or an unknown method (405)
        uint8_t step = 0;
        std::string path;
        std::string depth;
        uint32_t contentLength = 0;
        uint32_t remaining = 0;     ///< Discard
        uint64_t idleSince = 0;
        ZiFiVfsBridge::Result root; ///< the STAT of the request's path
        uint32_t sent = 0;
        uint32_t stored = 0;
        bool failed = false;
    };

    void AcceptClient();
    void ReceiveHeader();
    void CloseClient();
    void ProcessRequest(const std::string& header);
    bool SendText(const std::string& text);
    bool SendBytes(const uint8_t* data, size_t length);
    bool SendStatus(uint16_t code, const char* reason, const std::string& extraHeaders = {}, uint32_t contentLength = 0);
    bool SendProp(const std::string& path, const ZiFiVfsBridge::Result& entry);
    bool SendEncodedHref(const std::string& path);
    static bool DecodePath(const std::string& target, std::string& output);
    static bool FindHeader(const std::string& headers, const char* wanted, std::string& value);
    static bool ParseContentLength(const std::string& headers, uint32_t& contentLength, bool& present);

    void StartJob(Job::Kind kind, const std::string& path);
    void RunJob();
    bool StepDiscard(Job& j);
    bool StepPropfind(Job& j);
    bool StepGet(Job& j, bool headersOnly);
    bool StepPut(Job& j);
    bool StepSimple(Job& j);
    bool PrefetchUpload();
    void ResetUploadRing();
    bool Request(ZiFiVfsBridge::Op op, const std::string& path = {}, uint32_t value = 0);
    /// 1 ok, -1 failed, 0 waiting
    int Await(ZiFiVfsBridge::Result& result);

    uint64_t Now() const;
    uint64_t Us(uint64_t us) const;
    bool ClientConnectedNow() const;
    size_t ClientAvailable() const;

    ZiFiBridgeHost& _host;
    bool _running = false;
    uint16_t _port = kDefaultPort;
    bool _clientActive = false;
    uint64_t _acceptedAt = 0;
    std::string _header;
    Job _job;
    ZiFiVfsBridge::Result _result;

    // PUT: four 256-byte slots
    std::deque<std::vector<uint8_t>> _uploadSlots;
    bool _uploadActive = false;
    bool _uploadEof = false;
    bool _uploadError = false;
    uint32_t _uploadReadBytes = 0;
    uint32_t _uploadWantedBytes = 0;
    uint64_t _uploadLastProgress = 0;

    // Status
    std::string _lastRequest;
    uint16_t _lastStatus = 0;
    uint64_t _bytesSent = 0;
    uint64_t _bytesReceived = 0;
    uint32_t _requests = 0;
};
