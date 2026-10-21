// Loopback helpers for the multiplexer tests (deckhub_test.cpp, conformance_test.cpp): a fake USB deck, a scripted
// device wire v2 peer on the test's own event loop, and a loop runner that stops as soon as the condition holds.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sdb/deckhub.h"
#include "sdb/devicewire.h"
#include "sdb/eventloop.h"
#include "sdb/models.h"
#include "sdb/protocol.h"
#include "sdb/transport.h"

namespace sdbtest
{

using namespace std::chrono_literals;

// Runs the loop until done() holds (checked before every turn) or the limit passes.
inline bool RunUntil(sdb::net::EventLoop& loop, const std::function<bool()>& done,
                     std::chrono::milliseconds limit = 2000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        loop.RunOnce(5);
    }
    return true;
}

inline std::string Str(const sdb::proto::Report& r)
{
    return std::string(r.begin(), r.end());
}

inline const sdb::Model& PlusXl()
{
    return *sdb::FindModelByName("plusxl");
}

// A real deck without hardware: what the runtime wrote, input the test queues.
struct FakeUsb final : sdb::Transport
{
    struct Shared
    {
        std::mutex mutex;
        std::vector<std::string> writes, features;
        std::deque<std::string> input;
    };
    std::shared_ptr<Shared> s = std::make_shared<Shared>();

    int Write(std::span<const std::uint8_t> r) override
    {
        std::lock_guard lock(s->mutex);
        s->writes.emplace_back(r.begin(), r.end());
        return static_cast<int>(r.size());
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        std::lock_guard lock(s->mutex);
        s->features.emplace_back(r.begin(), r.end());
        return static_cast<int>(r.size());
    }
    int GetFeature(std::span<std::uint8_t> buf) override
    {
        buf[1] = 0x42;
        return static_cast<int>(buf.size());
    }
    int Read(std::span<std::uint8_t> buf, int) override
    {
        std::lock_guard lock(s->mutex);
        if (s->input.empty())
            return 0;
        const std::string r = s->input.front();
        s->input.pop_front();
        std::copy(r.begin(), r.end(), buf.begin());
        return static_cast<int>(r.size());
    }
};

// A viewer on the test's loop: a non-blocking socket, the frames it got.
struct TestViewer
{
    sdb::net::EventLoop& loop;
    sdb::net::SocketHandle s = sdb::net::kInvalidSocket;
    sdb::wire::FrameReader reader;
    std::vector<sdb::wire::Frame> frames;
    std::string raw;  // every byte received, when keepRaw (byte-identical fan-out)
    bool keepRaw = false;
    std::string out;
    bool closed = false;
    bool reading = true;

    TestViewer(sdb::net::EventLoop& l, int port, int receiveBuffer = 0) : loop(l)
    {
        std::string error;
        s = sdb::net::ConnectStart("127.0.0.1", port, &error, receiveBuffer);
        loop.Watch(s, true, true, [this](bool r, bool w, bool e) {
            if (w)
                Flush();
            if ((r || e) && reading)
                Read();
        });
    }
    ~TestViewer()
    {
        Drop();
    }
    TestViewer(const TestViewer&) = delete;
    TestViewer& operator=(const TestViewer&) = delete;

    // Gone without a Leave (a crashed viewer).
    void Drop()
    {
        if (s != sdb::net::kInvalidSocket)
        {
            if (!closed)
                loop.Unwatch(s);
            sdb::net::CloseSocket(s);
            s = sdb::net::kInvalidSocket;
        }
    }
    void Send(std::uint8_t type, const std::string& payload)
    {
        SendRaw(sdb::wire::EncodeFrame(type, payload));
    }
    void SendRaw(const std::string& bytes)
    {
        out += bytes;
        Flush();
    }
    void Flush()
    {
        while (!out.empty())
        {
            const long n = sdb::net::Send(s, out.data(), out.size());
            if (n <= 0)
                break;
            out.erase(0, static_cast<std::size_t>(n));
        }
        if (!closed)
            loop.Modify(s, reading, !out.empty());
    }
    void PauseReading(bool pause)
    {
        reading = !pause;
        loop.Modify(s, reading, !out.empty());
    }
    void Read()
    {
        char buf[65536];
        for (;;)
        {
            const long n = sdb::net::Recv(s, buf, sizeof buf);
            if (n == 0)
                break;
            if (n < 0)
            {
                closed = true;
                loop.Unwatch(s);
                break;
            }
            if (keepRaw)
                raw.append(buf, static_cast<std::size_t>(n));
            reader.Feed(std::string_view(buf, static_cast<std::size_t>(n)));
        }
        sdb::wire::Frame f;
        while (reader.Next(f))
            frames.push_back(f);
    }
    [[nodiscard]] int Count(std::uint8_t type) const
    {
        int n = 0;
        for (const auto& f : frames)
            n += f.type == type;
        return n;
    }
    // The first Welcome.
    [[nodiscard]] nlohmann::json Welcome() const
    {
        for (const auto& f : frames)
            if (f.type == sdb::wire::Welcome)
                return nlohmann::json::parse(f.payload);
        return {};
    }
};

inline sdb::DeckHub::Options HubOptions()
{
    sdb::DeckHub::Options o;
    o.port = 0;
    o.joinTimeout = 200ms;
    return o;
}

inline std::vector<nlohmann::json> DeckStates(const TestViewer& v)
{
    std::vector<nlohmann::json> out;
    for (const auto& f : v.frames)
        if (f.type == sdb::wire::DeckState)
            out.push_back(nlohmann::json::parse(f.payload));
    return out;
}

}  // namespace sdbtest
