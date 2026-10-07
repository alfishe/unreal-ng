// Host-side TLS (hosttls.h) through the host bridge, loopback only: a TLS echo server of the test's own with a
// fresh self-signed certificate (EC P-256: fast) for "localhost", trusted through HostTls::TrustExtraPem. The guest
// side sees plaintext; a wrong name or an untrusted certificate fails the connect with TlsFailed.

#include <gtest/gtest.h>

#include "common/network/hostnetbridge.h"
#include "common/network/hosttls.h"

#if defined(UNREAL_TEST_TLS)

#include <atomic>
#include <string>
#include <thread>

#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "_helpers/testwaithelper.h"
#include "common/network/netsockets.h"

namespace
{
constexpr uint32_t kLoopback = NetIp(127, 0, 0, 1);

struct Credentials
{
    EVP_PKEY* key = nullptr;
    X509* cert = nullptr;
    std::string pem;
    ~Credentials()
    {
        X509_free(cert);
        EVP_PKEY_free(key);
    }
};

/// A self-signed certificate for `name` (SAN DNS:name)
void MakeCredentials(const std::string& name, Credentials& c)
{
    c.key = EVP_EC_gen("P-256");
    c.cert = X509_new();
    X509_set_version(c.cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(c.cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(c.cert), -60);
    X509_gmtime_adj(X509_getm_notAfter(c.cert), 3600);
    X509_set_pubkey(c.cert, c.key);
    // A name of its own, set as subject and issuer: OpenSSL 4 hands out the certificate's names as const
    X509_NAME* subject = X509_NAME_new();
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(name.c_str()), -1, -1, 0);
    X509_set_subject_name(c.cert, subject);
    X509_set_issuer_name(c.cert, subject);
    X509_NAME_free(subject);
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, c.cert, c.cert, nullptr, nullptr, 0);
    const std::string san = "DNS:" + name;
    if (X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name, san.c_str()))
    {
        X509_add_ext(c.cert, ext, -1);
        X509_EXTENSION_free(ext);
    }
    if (X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "critical,CA:TRUE"))
    {
        X509_add_ext(c.cert, ext, -1);
        X509_EXTENSION_free(ext);
    }
    X509_sign(c.cert, c.key, EVP_sha256());
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, c.cert);
    char* data = nullptr;
    const long length = BIO_get_mem_data(bio, &data);
    c.pem.assign(data, static_cast<size_t>(length));
    BIO_free(bio);
}

/// One-connection TLS echo server on an ephemeral loopback port (memory BIOs over netsock, like the bridge)
class TlsEchoServer
{
public:
    explicit TlsEchoServer(const Credentials& c)
    {
        _ctx = SSL_CTX_new(TLS_server_method());
        SSL_CTX_use_certificate(_ctx, c.cert);
        SSL_CTX_use_PrivateKey(_ctx, c.key);
        _listener = netsock::OpenTcp();
        netsock::Bind(_listener, kLoopback, 0, true);
        netsock::Listen(_listener, 1);
        port = netsock::LocalPort(_listener);
        _thread = std::thread([this] { Run(); });
    }
    ~TlsEchoServer()
    {
        _stop = true;
        _thread.join();
        netsock::Close(_listener);
        SSL_CTX_free(_ctx);
    }
    uint16_t port = 0;

private:
    void Run()
    {
        netsock::Handle conn = netsock::kInvalid;
        SSL* ssl = nullptr;
        BIO* in = nullptr;
        BIO* out = nullptr;
        uint8_t buf[4096];
        const auto flush = [&]() {
            int n;
            while ((n = BIO_read(out, buf, sizeof(buf))) > 0)
            {
                size_t sent = 0, at = 0;
                while (at < static_cast<size_t>(n))
                {
                    if (netsock::Send(conn, buf + at, static_cast<size_t>(n) - at, sent) == netsock::Result::Ok)
                        at += sent;
                    std::vector<netsock::PollItem> w(1);
                    w[0].handle = conn;
                    w[0].wantRead = false;
                    w[0].wantWrite = true;
                    netsock::Poll(w, 5);
                }
            }
        };
        while (!_stop)
        {
            std::vector<netsock::PollItem> items(1);
            items[0].handle = conn == netsock::kInvalid ? _listener : conn;
            if (netsock::Poll(items, 5) <= 0)
                continue;
            if (conn == netsock::kInvalid)
            {
                NetEndpoint peer;
                conn = netsock::Accept(_listener, peer);
                ssl = SSL_new(_ctx);
                in = BIO_new(BIO_s_mem());
                out = BIO_new(BIO_s_mem());
                BIO_set_mem_eof_return(in, -1);
                SSL_set_bio(ssl, in, out);
                SSL_set_accept_state(ssl);
                continue;
            }
            size_t n = 0;
            const netsock::Result r = netsock::Recv(conn, buf, sizeof(buf), n);
            if (r != netsock::Result::Ok || !n)
            {
                if (r != netsock::Result::WouldBlock)
                    break;
                continue;
            }
            BIO_write(in, buf, static_cast<int>(n));
            if (!SSL_is_init_finished(ssl))
                SSL_do_handshake(ssl);
            int got;
            while (SSL_is_init_finished(ssl) && (got = SSL_read(ssl, buf, sizeof(buf))) > 0)
                SSL_write(ssl, buf, got);   // echo
            flush();
        }
        if (ssl)
            SSL_free(ssl);
        if (conn != netsock::kInvalid)
            netsock::Close(conn);
    }

    SSL_CTX* _ctx = nullptr;
    netsock::Handle _listener = netsock::kInvalid;
    std::thread _thread;
    std::atomic<bool> _stop{false};
};

bool NextEvent(HostNetBridge& bridge, HostNetEvent& ev)
{
    return TestWait::For([&] { return bridge.PollEvent(ev); });
}
}  // namespace

TEST(HostTls_Test, PlaintextThroughATlsConnectionToALoopbackServer)
{
    ASSERT_TRUE(HostTls::Available());
    Credentials c;
    MakeCredentials("localhost", c);
    HostTls::TrustExtraPem(c.pem);
    TlsEchoServer server(c);
    HostNetBridge bridge;
    bridge.TcpConnectTls(1, NetEndpoint{kLoopback, server.port}, "localhost");
    HostNetEvent ev;
    ASSERT_TRUE(NextEvent(bridge, ev));
    ASSERT_EQ(ev.type, NetEventType::Connected) << NetStatusText(ev.status);
    const std::string hello = "GET / HTTP/1.0\r\n\r\n";
    bridge.TcpSend(1, reinterpret_cast<const uint8_t*>(hello.data()), static_cast<uint32_t>(hello.size()));
    std::string echoed;
    while (echoed.size() < hello.size() && NextEvent(bridge, ev))
    {
        ASSERT_EQ(ev.type, NetEventType::Data);
        echoed.append(ev.data.begin(), ev.data.end());
    }
    EXPECT_EQ(echoed, hello) << "the guest sees plaintext both ways";
    bridge.Close(1);
}

TEST(HostTls_Test, AWrongNameFailsTheConnect)
{
    Credentials c;
    MakeCredentials("localhost", c);
    HostTls::TrustExtraPem(c.pem);
    TlsEchoServer server(c);
    HostNetBridge bridge;
    bridge.TcpConnectTls(2, NetEndpoint{kLoopback, server.port}, "example.org");
    HostNetEvent ev;
    ASSERT_TRUE(NextEvent(bridge, ev));
    EXPECT_EQ(ev.type, NetEventType::ConnectFailed);
    EXPECT_EQ(ev.status, NetEventStatus::TlsFailed) << "the certificate is for localhost";
}

TEST(HostTls_Test, AnUntrustedCertificateFailsTheConnect)
{
    Credentials c;
    MakeCredentials("localhost", c);   // not trusted: its PEM is never handed over
    TlsEchoServer server(c);
    HostNetBridge bridge;
    bridge.TcpConnectTls(3, NetEndpoint{kLoopback, server.port}, "localhost");
    HostNetEvent ev;
    ASSERT_TRUE(NextEvent(bridge, ev));
    EXPECT_EQ(ev.type, NetEventType::ConnectFailed);
    EXPECT_EQ(ev.status, NetEventStatus::TlsFailed);
}

#else

#include "_helpers/testwaithelper.h"

TEST(HostTls_Test, WithoutOpenSslTheConnectFails)
{
    EXPECT_FALSE(HostTls::Available());
    HostNetBridge bridge;
    bridge.TcpConnectTls(1, NetEndpoint{NetIp(127, 0, 0, 1), 9}, "localhost");
    HostNetEvent ev;
    ASSERT_TRUE(TestWait::For([&] { return bridge.PollEvent(ev); }));
    EXPECT_EQ(ev.type, NetEventType::ConnectFailed);
    EXPECT_EQ(ev.status, NetEventStatus::TlsFailed);
}

#endif
