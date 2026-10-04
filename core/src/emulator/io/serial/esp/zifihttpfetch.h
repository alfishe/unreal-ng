#pragma once

/// @file zifihttpfetch.h
/// @brief A whole-body HTTPS GET on its own socket and its own DNS lookup: what the ZiFi S3 firmware's Wild
/// Commander updater fetches GitHub with (NetWcFetcher::get over a NetClient of its own, ZiFi-ESP32-S3-Zero 2e5ba83
/// https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/wc_update_service.cpp, with
/// NetClient::httpGet / readHttpHeader / applyRedirect / receive from src/net_client.cpp): TLS for port 443 (done by
/// the host, hosttls.h), the firmware's HTTP/1.0 request, up to 4 redirects (a downgrade to http:// refused), the
/// header within 10 s and 2048 bytes, chunked refused, the body to EOF or Content-Length, at most `capacity` bytes,
/// 60 s for the body (30 s without a byte is "body timeout").
///
/// Deterministic and checkpointable: it acts on journaled network events and the emulated clock; the body keeps its
/// journal references, so a saved fetch stores runs, not bytes.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "emulator/io/serial/esp/espstack.h"

class ZiFiStateWriter;
class ZiFiStateReader;

class ZiFiHttpFetch
{
public:
    struct Result
    {
        bool ok = false;
        uint16_t status = 0;
        std::string error;
        std::vector<uint8_t> body;
        /// Where the body's bytes came from (journal runs); empty when some byte has no journal source
        std::vector<netstate::Reference> refs;
    };

    ZiFiHttpFetch(EspStack& stack, int slot);

    std::function<uint64_t()> now;
    std::function<uint64_t(uint64_t)> micros;

    /// Start a GET (an earlier one is dropped)
    void Start(const std::string& host, uint16_t port, const std::string& path, size_t capacity);
    /// The fetch ended (Take the result)
    bool Done() const { return _phase == Phase::Done; }
    bool Busy() const { return _phase != Phase::Idle && _phase != Phase::Done; }
    Result Take();
    /// Stop at once ("stopped", the socket closed)
    void Cancel();
    /// Drop the fetch without touching the socket (the module restarted, or a TTD load follows)
    void Forget();
    /// Network events: the aux resolver's answer, the slot's connect, new bytes; and time
    void OnResolve(uint32_t addr);
    void OnConnect(bool ok);
    void Poll();

    const std::string& Host() const { return _host; }
    std::string Activity() const;

    void Save(ZiFiStateWriter& w) const;
    bool Load(ZiFiStateReader& r, const EspStack::ByteSource& bytes);

    /// A body by journal reference when every byte has one, else the bytes (shared with the WC updater's buffer)
    static void SaveBody(ZiFiStateWriter& w, const std::vector<uint8_t>& body, const std::vector<netstate::Reference>& refs);
    static bool LoadBody(ZiFiStateReader& r, const EspStack::ByteSource& bytes, std::vector<uint8_t>& body,
                         std::vector<netstate::Reference>& refs);

private:
    enum class Phase : uint8_t
    {
        Idle,
        Resolve,
        Connect,
        Header,
        Body,
        Done
    };

    void Connect();
    void Fail(const std::string& error);
    void Finish();
    void ParseHeader();
    void AppendBody(const std::vector<EspStack::RxByte>& bytes);

    EspStack& _stack;
    int _slot = 0;
    Phase _phase = Phase::Idle;
    std::string _host;
    uint16_t _port = 443;
    std::string _path;
    size_t _capacity = 0;
    bool _tls = true;
    uint8_t _redirects = 0;
    uint64_t _deadline = 0;
    uint64_t _bodyStart = 0;
    uint64_t _bodyActivity = 0;
    bool _lengthKnown = false;
    uint32_t _expected = 0;
    std::vector<EspStack::RxByte> _header;
    Result _result;
    bool _refsValid = true;
};
