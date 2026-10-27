#include "common/network/hosttls.h"

#if defined(UNREAL_HOST_TLS)

#include <cstdio>
#include <mutex>

// The platform's trust store. Before OpenSSL: types.h undefines wincrypt's X509_NAME & co.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#endif

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace
{
std::mutex g_ctxMutex;
SSL_CTX* g_ctx = nullptr;
std::vector<std::string> g_extraPem;

#if defined(_WIN32) || defined(__APPLE__)
void AddDer(X509_STORE* store, const unsigned char* der, long length)
{
    if (X509* cert = d2i_X509(nullptr, &der, length))
    {
        X509_STORE_add_cert(store, cert);   // a duplicate is refused, harmless
        X509_free(cert);
    }
}
#endif

/// The host's root certificates, wherever the platform keeps them: OpenSSL's default paths are those of the
/// OpenSSL build (a release package links its own, whose OPENSSLDIR is not on the user's machine)
void AddPlatformRoots(SSL_CTX* ctx)
{
#if defined(_WIN32)
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    for (const wchar_t* name : { L"ROOT", L"CA" })
    {
        HCERTSTORE system = CertOpenSystemStoreW(0, name);
        if (!system)
            continue;
        PCCERT_CONTEXT cert = nullptr;
        while ((cert = CertEnumCertificatesInStore(system, cert)) != nullptr)
            AddDer(store, cert->pbCertEncoded, static_cast<long>(cert->cbCertEncoded));
        CertCloseStore(system, 0);
    }
#elif defined(__APPLE__)
    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    auto addAll = [store](CFArrayRef certs) {
        for (CFIndex i = 0; i < CFArrayGetCount(certs); i++)
        {
            auto cert = static_cast<SecCertificateRef>(const_cast<void*>(CFArrayGetValueAtIndex(certs, i)));
            if (CFDataRef der = SecCertificateCopyData(cert))
            {
                AddDer(store, CFDataGetBytePtr(der), static_cast<long>(CFDataGetLength(der)));
                CFRelease(der);
            }
        }
        CFRelease(certs);
    };
    CFArrayRef certs = nullptr;
    if (SecTrustCopyAnchorCertificates(&certs) == errSecSuccess && certs)
        addAll(certs);
    // Roots an administrator or the user added (a company CA)
    for (SecTrustSettingsDomain domain : { kSecTrustSettingsDomainAdmin, kSecTrustSettingsDomainUser })
    {
        certs = nullptr;
        if (SecTrustSettingsCopyCertificates(domain, &certs) == errSecSuccess && certs)
            addAll(certs);
    }
#else
    // The distributions' bundles (an AppImage runs on all of them)
    for (const char* file : { "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                              "/etc/ssl/ca-bundle.pem", "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
                              "/etc/ssl/cert.pem" })
    {
        if (FILE* f = std::fopen(file, "rb"))
        {
            std::fclose(f);
            SSL_CTX_load_verify_locations(ctx, file, nullptr);
        }
    }
#endif
    ERR_clear_error();   // a missing store or an unparsable entry is not a session error
}

SSL_CTX* Context()
{
    std::lock_guard<std::mutex> lock(g_ctxMutex);
    if (g_ctx)
        return g_ctx;
    g_ctx = SSL_CTX_new(TLS_client_method());
    if (!g_ctx)
        return nullptr;
    SSL_CTX_set_min_proto_version(g_ctx, TLS1_2_VERSION);
    SSL_CTX_set_default_verify_paths(g_ctx);   // SSL_CERT_FILE / SSL_CERT_DIR
    AddPlatformRoots(g_ctx);
    SSL_CTX_set_verify(g_ctx, SSL_VERIFY_PEER, nullptr);
    X509_STORE* store = SSL_CTX_get_cert_store(g_ctx);
    for (const std::string& pem : g_extraPem)
    {
        BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
        if (X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr))
        {
            X509_STORE_add_cert(store, cert);
            X509_free(cert);
        }
        BIO_free(bio);
    }
    return g_ctx;
}

std::string LastError()
{
    const unsigned long e = ERR_get_error();
    ERR_clear_error();
    if (!e)
        return "tls error";
    char text[160];
    ERR_error_string_n(e, text, sizeof(text));
    return text;
}
}  // namespace

struct HostTls::Impl
{
    SSL* ssl = nullptr;
    BIO* in = nullptr;    ///< ciphertext from the server (SSL reads it)
    BIO* out = nullptr;   ///< ciphertext for the server (SSL writes it)
    ~Impl()
    {
        if (ssl)
            SSL_free(ssl);   // frees both BIOs
    }
};

bool HostTls::Available()
{
    return true;
}

void HostTls::TrustExtraPem(const std::string& pem)
{
    std::lock_guard<std::mutex> lock(g_ctxMutex);
    g_extraPem.push_back(pem);
    if (g_ctx)
    {
        // Already made: add to its store too
        BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
        if (X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr))
        {
            X509_STORE_add_cert(SSL_CTX_get_cert_store(g_ctx), cert);
            X509_free(cert);
        }
        BIO_free(bio);
    }
}

HostTls::HostTls(const std::string& serverName) : _impl(std::make_unique<Impl>())
{
    SSL_CTX* ctx = Context();
    if (!ctx)
    {
        _error = "no TLS context";
        return;
    }
    _impl->ssl = SSL_new(ctx);
    _impl->in = BIO_new(BIO_s_mem());
    _impl->out = BIO_new(BIO_s_mem());
    if (!_impl->ssl || !_impl->in || !_impl->out)
    {
        _error = "no TLS session";
        return;
    }
    BIO_set_mem_eof_return(_impl->in, -1);   // empty input = "want more", not EOF
    SSL_set_bio(_impl->ssl, _impl->in, _impl->out);
    SSL_set_connect_state(_impl->ssl);
    // SNI and the name the certificate must carry (an address is checked as an IP SAN)
    SSL_set_tlsext_host_name(_impl->ssl, serverName.c_str());
    X509_VERIFY_PARAM* param = SSL_get0_param(_impl->ssl);
    if (X509_VERIFY_PARAM_set1_ip_asc(param, serverName.c_str()) != 1)
    {
#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 4
        SSL_set1_dnsname(_impl->ssl, serverName.c_str());   // 4.0 deprecates SSL_set1_host
#else
        SSL_set1_host(_impl->ssl, serverName.c_str());
#endif
    }
    _state = State::Handshaking;
}

HostTls::~HostTls() = default;

void HostTls::Feed(const uint8_t* data, size_t length)
{
    if (_impl->in && length)
        BIO_write(_impl->in, data, static_cast<int>(length));
}

HostTls::State HostTls::Step()
{
    if (_state != State::Handshaking || !_impl->ssl)
        return _state;
    const int r = SSL_do_handshake(_impl->ssl);
    if (r == 1)
    {
        _state = State::Ready;
        if (!_pendingPlain.empty())
        {
            std::vector<uint8_t> queued;
            queued.swap(_pendingPlain);
            WritePlain(queued.data(), queued.size());
        }
        return _state;
    }
    const int e = SSL_get_error(_impl->ssl, r);
    if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
        return _state;
    _error = LastError();
    const long verify = SSL_get_verify_result(_impl->ssl);
    if (verify != X509_V_OK)
        _error = std::string("certificate: ") + X509_verify_cert_error_string(verify);
    _state = State::Failed;
    return _state;
}

void HostTls::ReadPlain(std::vector<uint8_t>& out)
{
    if (_state != State::Ready)
        return;
    uint8_t buffer[16 * 1024];
    for (;;)
    {
        const int n = SSL_read(_impl->ssl, buffer, sizeof(buffer));
        if (n > 0)
        {
            out.insert(out.end(), buffer, buffer + n);
            continue;
        }
        const int e = SSL_get_error(_impl->ssl, n);
        if (e == SSL_ERROR_ZERO_RETURN)
            _peerClosed = true;
        else if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE)
        {
            _error = LastError();
            _peerClosed = true;   // a broken stream ends like a close
        }
        return;
    }
}

void HostTls::WritePlain(const uint8_t* data, size_t length)
{
    if (!length)
        return;
    if (_state == State::Handshaking)
    {
        _pendingPlain.insert(_pendingPlain.end(), data, data + length);
        return;
    }
    if (_state != State::Ready)
        return;
    size_t done = 0;
    while (done < length)
    {
        const int n = SSL_write(_impl->ssl, data + done, static_cast<int>(length - done));
        if (n <= 0)
            return;   // a memory BIO takes everything; a failure ends the stream
        done += static_cast<size_t>(n);
    }
}

void HostTls::Shutdown()
{
    if (_state == State::Ready)
        SSL_shutdown(_impl->ssl);
}

void HostTls::TakeCipher(std::vector<uint8_t>& out)
{
    if (!_impl->out)
        return;
    uint8_t buffer[16 * 1024];
    for (;;)
    {
        const int n = BIO_read(_impl->out, buffer, sizeof(buffer));
        if (n <= 0)
            return;
        out.insert(out.end(), buffer, buffer + n);
    }
}

#else  // no TLS in this build

struct HostTls::Impl
{
};

bool HostTls::Available()
{
    return false;
}

void HostTls::TrustExtraPem(const std::string&)
{
}

HostTls::HostTls(const std::string&) : _impl(std::make_unique<Impl>())
{
    _error = "this build has no TLS (UNREAL_HOST_TLS off: OpenSSL was not found)";
}

HostTls::~HostTls() = default;

void HostTls::Feed(const uint8_t*, size_t)
{
}

HostTls::State HostTls::Step()
{
    return _state;
}

void HostTls::ReadPlain(std::vector<uint8_t>&)
{
}

void HostTls::WritePlain(const uint8_t*, size_t)
{
}

void HostTls::Shutdown()
{
}

void HostTls::TakeCipher(std::vector<uint8_t>&)
{
}

#endif
