// Transports that are not a local USB device: the emulator over TCP, and a tee that drives two transports at once.
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

#include "sdb/device.h"
#include "sdb/emuwire.h"
#include "sdb/eventloop.h"
#include "sdb/transport.h"

namespace sdb
{
namespace
{
using clk = std::chrono::steady_clock;

class TcpTransport final : public Transport
{
public:
    explicit TcpTransport(net::SocketHandle fd) : _fd(fd) {}
    ~TcpTransport() override
    {
        net::CloseSocket(_fd);
    }

    int Write(std::span<const std::uint8_t> r) override
    {
        return SendFrame(emuwire::Output, r) ? static_cast<int>(r.size()) : -1;
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        return SendFrame(emuwire::SetFeature, r) ? static_cast<int>(r.size()) : -1;
    }
    int GetFeature(std::span<std::uint8_t> buf) override
    {
        std::lock_guard l(_m);
        if (!SendFrameLocked(emuwire::GetFeature, buf))
            return -1;
        const auto deadline = clk::now() + std::chrono::seconds(1);
        while (clk::now() < deadline)
        {
            if (!Pump(25))
                return -1;
            for (auto it = _frames.begin(); it != _frames.end(); ++it)
                if (it->first == emuwire::FeatureReply)
                {
                    const size_t n = std::min(buf.size(), it->second.size());
                    std::copy_n(it->second.begin(), n, buf.begin());
                    _frames.erase(it);
                    return static_cast<int>(n);
                }
        }
        return -1;
    }
    int Read(std::span<std::uint8_t> buf, int timeoutMs) override
    {
        std::lock_guard l(_m);
        const auto deadline = clk::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
        bool polled = false;
        for (;;)
        {
            for (auto it = _frames.begin(); it != _frames.end(); ++it)
                if (it->first == emuwire::Input)
                {
                    const size_t n = std::min(buf.size(), it->second.size());
                    std::copy_n(it->second.begin(), n, buf.begin());
                    std::fill(buf.begin() + static_cast<std::ptrdiff_t>(n), buf.end(), 0);
                    _frames.erase(it);
                    return static_cast<int>(n);
                }
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clk::now()).count();
            if (left <= 0)
            {
                if (polled)
                    return 0;
                polled = true;  // timeout 0 (a mirror polls this way): still look at the socket once, without waiting
                if (!Pump(0))
                    return -1;
                continue;
            }
            if (!Pump(static_cast<int>(left)))
                return -1;
        }
    }
    std::string LastError() const override
    {
        return _err;
    }
    [[nodiscard]] InputSource LastSource() const override
    {
        return InputSource::Emulator;
    }
    void Note(std::string_view text) override
    {
        (void)SendFrame(emuwire::HostNote, std::span(reinterpret_cast<const std::uint8_t*>(text.data()),
                                                     std::min<size_t>(text.size(), 0xFFFF)));
    }
    void ForwardInput(std::span<const std::uint8_t> r)
    {
        (void)SendFrame(emuwire::HostInput, r);
    }
    void PublishPage(std::string_view j) override
    {
        if (j.size() <= 0xFFFF)
            (void)SendFrame(emuwire::HostPage, std::span(reinterpret_cast<const std::uint8_t*>(j.data()), j.size()));
    }

    // Waits for a Hello frame (after connect or SetModel); returns false on timeout / disconnect.
    bool WaitHello(std::uint16_t& pid, std::string& serial, int timeoutMs)
    {
        std::lock_guard l(_m);
        const auto deadline = clk::now() + std::chrono::milliseconds(timeoutMs);
        while (clk::now() < deadline)
        {
            for (auto it = _frames.begin(); it != _frames.end(); ++it)
                if (it->first == emuwire::Hello && it->second.size() >= 6)
                {
                    const auto& p = it->second;
                    pid = static_cast<std::uint16_t>(p[3] | (p[4] << 8));
                    const size_t n = std::min<size_t>(p[5], p.size() - 6);
                    serial.assign(reinterpret_cast<const char*>(p.data() + 6), n);
                    _frames.erase(it);
                    return true;
                }
            if (!Pump(50))
                return false;
        }
        return false;
    }
    // Sends a frame and waits for the answer of type `reply`; nullopt on timeout or a lost connection.
    std::optional<std::vector<std::uint8_t>> SendRequest(std::uint8_t type, std::span<const std::uint8_t> payload,
                                                         std::uint8_t reply, int timeoutMs)
    {
        if (!SendFrame(type, payload))
            return std::nullopt;
        std::lock_guard l(_m);
        const auto deadline = clk::now() + std::chrono::milliseconds(timeoutMs);
        while (clk::now() < deadline)
        {
            for (auto it = _frames.begin(); it != _frames.end(); ++it)
                if (it->first == reply)
                {
                    auto p = std::move(it->second);
                    _frames.erase(it);
                    return p;
                }
            if (!Pump(50))
                return std::nullopt;
        }
        return std::nullopt;
    }
    bool Snapshot(const std::string& path, int timeoutMs)
    {
        const auto r =
            SendRequest(emuwire::Snapshot, std::span(reinterpret_cast<const std::uint8_t*>(path.data()), path.size()),
                        emuwire::SnapshotDone, timeoutMs);
        return r && !r->empty() && (*r)[0] == 1;
    }
    bool AnnounceMirror(const std::string& serial)
    {
        return SendFrame(emuwire::HostMirror,
                         std::span(reinterpret_cast<const std::uint8_t*>(serial.data()), serial.size()));
    }
    bool SetModel(std::uint16_t pid)
    {
        const std::uint8_t p[2] = {static_cast<std::uint8_t>(pid & 0xff), static_cast<std::uint8_t>(pid >> 8)};
        return SendFrame(emuwire::SetModel, p);
    }

private:
    bool SendFrame(std::uint8_t type, std::span<const std::uint8_t> payload)
    {
        std::lock_guard l(_m);
        return SendFrameLocked(type, payload);
    }
    bool SendFrameLocked(std::uint8_t type, std::span<const std::uint8_t> payload)
    {
        std::vector<std::uint8_t> f(3 + payload.size());
        f[0] = type;
        f[1] = static_cast<std::uint8_t>(payload.size() & 0xff);
        f[2] = static_cast<std::uint8_t>(payload.size() >> 8);
        std::copy(payload.begin(), payload.end(), f.begin() + 3);
        if (!net::SendAll(_fd, f.data(), f.size(), 2000))
        {
            _err = "emulator connection lost";
            return false;
        }
        return true;
    }
    // Reads whatever arrives within timeoutMs into complete frames. false = connection closed.
    bool Pump(int timeoutMs)
    {
        const int r = net::WaitSocket(_fd, true, false, timeoutMs);
        if (r < 0)
        {
            _err = "poll failed";
            return false;
        }
        if (r > 0)
        {
            std::uint8_t tmp[8192];
            const long n = net::Recv(_fd, tmp, sizeof tmp);
            if (n < 0)
            {
                _err = "emulator closed the connection";
                return false;
            }
            _rx.insert(_rx.end(), tmp, tmp + n);
        }
        while (_rx.size() >= 3)
        {
            const size_t len = _rx[1] | (_rx[2] << 8);
            if (_rx.size() < 3 + len)
                break;
            _frames.emplace_back(
                _rx[0], std::vector<std::uint8_t>(_rx.begin() + 3, _rx.begin() + 3 + static_cast<std::ptrdiff_t>(len)));
            _rx.erase(_rx.begin(), _rx.begin() + 3 + static_cast<std::ptrdiff_t>(len));
        }
        return true;
    }

    net::SocketHandle _fd;
    std::mutex _m;
    std::vector<std::uint8_t> _rx;
    std::deque<std::pair<std::uint8_t, std::vector<std::uint8_t>>> _frames;
    std::string _err;
};

// Writes go to both; feature reads come from the first (the real device); input comes from either.
class TeeTransport final : public Transport
{
public:
    TeeTransport(std::unique_ptr<Transport> a, std::unique_ptr<TcpTransport> b) : _a(std::move(a)), _b(std::move(b)) {}
    int Write(std::span<const std::uint8_t> r) override
    {
        (void)_b->Write(r);
        return _a->Write(r);
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        (void)_b->SendFeature(r);
        return _a->SendFeature(r);
    }
    int GetFeature(std::span<std::uint8_t> buf) override
    {
        return _a->GetFeature(buf);
    }
    int Read(std::span<std::uint8_t> buf, int timeoutMs) override
    {
        const auto deadline = clk::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
        for (;;)
        {
            if (int n = _b->Read(buf, 0); n > 0)
            {
                _last = InputSource::Emulator;
                return n;
            }  // emulator input never waits
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clk::now()).count();
            const int slice = static_cast<int>(std::clamp<long long>(left, 0, 5));
            if (int n = _a->Read(buf, slice); n != 0)
            {
                if (n > 0)
                {
                    _last = _a->LastSource();
                    _b->ForwardInput(buf.first(static_cast<size_t>(n)));
                }  // the emulator logs it
                return n;
            }
            if (left <= 0)
                return 0;
        }
    }
    std::string LastError() const override
    {
        return _a->LastError();
    }
    [[nodiscard]] InputSource LastSource() const override
    {
        return _last;
    }
    void Note(std::string_view text) override
    {
        _b->Note(text);
    }
    void PublishPage(std::string_view j) override
    {
        _b->PublishPage(j);
    }
    [[nodiscard]] bool Connected() const override
    {
        return _a->Connected();
    }
    bool Reconnect(std::chrono::milliseconds away) override
    {
        return _a->Reconnect(away);
    }

private:
    std::unique_ptr<Transport> _a;
    std::unique_ptr<TcpTransport> _b;
    InputSource _last = InputSource::Usb;
};

std::unique_ptr<TcpTransport> ConnectEmulator(const std::string& addr, std::string* error)
{
    std::string host = "127.0.0.1", port = std::to_string(emuwire::kDefaultPort);
    if (!addr.empty())
    {
        const auto c = addr.rfind(':');
        if (c == std::string::npos)
            host = addr;
        else
        {
            if (c > 0)
                host = addr.substr(0, c);
            port = addr.substr(c + 1);
        }
    }
    int portNumber = port.empty() || port.size() > 5 ? -1 : 0;
    for (char ch : port)
        if (portNumber >= 0)
            portNumber = ch >= '0' && ch <= '9' ? portNumber * 10 + (ch - '0') : -1;
    const net::SocketHandle fd =
        portNumber > 0 && portNumber <= 65535 ? net::Connect(host, portNumber, 2000, nullptr) : net::kInvalidSocket;
    if (fd == net::kInvalidSocket)
    {
        if (error)
            *error = "no emulator listening on " + host + ":" + port + " (start Stream Deck Studio)";
        return nullptr;
    }
    return std::make_unique<TcpTransport>(fd);
}
}  // namespace

std::unique_ptr<Device> OpenEmulator(const std::string& addr, std::string* error)
{
    auto t = ConnectEmulator(addr, error);
    if (!t)
        return nullptr;
    std::uint16_t pid = 0;
    std::string serial;
    if (!t->WaitHello(pid, serial, 2000))
    {
        if (error)
            *error = "emulator did not say hello";
        return nullptr;
    }
    const Model* m = FindModel(kElgatoVid, pid);
    if (!m)
    {
        if (error)
            *error = "emulator presents an unknown PID";
        return nullptr;
    }
    return std::make_unique<Device>(*m, std::move(t), serial.empty() ? "EMULATOR" : serial);
}

bool EmulatorSetModel(const std::string& addr, std::uint16_t pid, std::string* error)
{
    auto t = ConnectEmulator(addr, error);
    if (!t)
        return false;
    std::uint16_t got = 0;
    std::string serial;
    if (!t->WaitHello(got, serial, 2000))
    {
        if (error)
            *error = "emulator did not say hello";
        return false;
    }
    if (got == pid)
        return true;
    t->SetModel(pid);
    if (!t->WaitHello(got, serial, 2000) || got != pid)
    {
        if (error)
            *error = "emulator did not switch";
        return false;
    }
    return true;
}

bool EmulatorSnapshot(const std::string& addr, const std::string& path, std::string* error)
{
    auto t = ConnectEmulator(addr, error);
    if (!t)
        return false;
    std::uint16_t pid = 0;
    std::string serial;
    if (!t->WaitHello(pid, serial, 2000))
    {
        if (error)
            *error = "emulator did not say hello";
        return false;
    }
    if (!t->Snapshot(path, 3000))
    {
        if (error)
            *error = "emulator could not save " + path;
        return false;
    }
    return true;
}

bool EmulatorRecord(const std::string& addr, const std::string& path, int fps, std::string* result, std::string* error)
{
    auto t = ConnectEmulator(addr, error);
    if (!t)
        return false;
    std::uint16_t pid = 0;
    std::string serial;
    if (!t->WaitHello(pid, serial, 2000))
    {
        if (error)
            *error = "emulator did not say hello";
        return false;
    }
    std::vector<std::uint8_t> p{static_cast<std::uint8_t>(std::clamp(fps, 0, 255))};
    p.insert(p.end(), path.begin(), path.end());
    const auto r = t->SendRequest(emuwire::Record, p, emuwire::RecordDone, 20000);  // stopping finalizes the file
    if (!r || r->empty())
    {
        if (error)
            *error = "emulator did not answer";
        return false;
    }
    const std::string text(r->begin() + 1, r->end());
    if ((*r)[0] != 1)
    {
        if (error)
            *error = text;
        return false;
    }
    if (result)
        *result = text;
    return true;
}

bool Device::AttachMirror(const std::string& addr, std::string* error)
{
    auto t = ConnectEmulator(addr, error);
    if (!t)
        return false;
    std::uint16_t pid = 0;
    std::string serial;
    if (!t->WaitHello(pid, serial, 2000))
    {
        if (error)
            *error = "emulator did not say hello";
        return false;
    }
    if (pid != _m.pid)
    {  // make the emulator show the same model as the real device
        t->SetModel(_m.pid);
        if (!t->WaitHello(pid, serial, 2000) || pid != _m.pid)
        {
            if (error)
                *error = "emulator could not switch model";
            return false;
        }
    }
    t->AnnounceMirror(_serial);  // the emulator may then mark presses on this very deck (it draws the same picture)
    _t = std::make_unique<TeeTransport>(std::move(_t), std::move(t));
    return true;
}

}  // namespace sdb
