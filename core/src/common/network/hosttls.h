#pragma once

/// @file hosttls.h
/// @brief A TLS client session for the host network bridge (HostNetBridge): an emulated device whose firmware
/// speaks TLS itself (the ZiFi ESP32-S3's WiFiClientSecure) gets the connection made by the host - the host does
/// the handshake and the encryption, the guest sees the plaintext. The plaintext is what the virtual network
/// journals, so a TTD replay needs no host and no keys.
///
/// OpenSSL (the vetted TLS library the build already uses for the WebAPI) through memory BIOs: no socket coupling,
/// the bridge moves the ciphertext with its own non-blocking sockets. The peer is verified as the firmware does
/// (WiFiClientSecure with its CA bundle): the host's trust store (the system roots - Windows certificate store,
/// macOS keychains, the Linux distributions' CA bundles - plus OpenSSL's default paths, SSL_CERT_FILE /
/// SSL_CERT_DIR honored) and the server name. Built without OpenSSL (UNREAL_HOST_TLS off): Available() is false
/// and every session fails.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class HostTls
{
public:
    enum class State : uint8_t
    {
        Handshaking,
        Ready,
        Failed,
    };

    /// The build has TLS (OpenSSL)
    static bool Available();
    /// Tests: trust this PEM certificate as well (an in-process server's)
    static void TrustExtraPem(const std::string& pem);

    explicit HostTls(const std::string& serverName);
    ~HostTls();

    HostTls(const HostTls&) = delete;
    HostTls& operator=(const HostTls&) = delete;

    /// Ciphertext that arrived from the server
    void Feed(const uint8_t* data, size_t length);
    /// Drive the handshake / decryption; the state after it
    State Step();
    State GetState() const { return _state; }
    /// Decrypted bytes (Ready); appended to `out`
    void ReadPlain(std::vector<uint8_t>& out);
    /// The server sent close_notify
    bool PeerClosed() const { return _peerClosed; }
    /// Plaintext to send (queued until Ready)
    void WritePlain(const uint8_t* data, size_t length);
    /// close_notify
    void Shutdown();
    /// Ciphertext to put on the socket; appended to `out`
    void TakeCipher(std::vector<uint8_t>& out);
    /// Why it failed (OpenSSL's reason, for logs)
    const std::string& Error() const { return _error; }

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
    State _state = State::Failed;
    bool _peerClosed = false;
    std::string _error;
    std::vector<uint8_t> _pendingPlain;
};
