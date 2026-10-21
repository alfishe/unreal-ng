// A USB deck that may be unplugged and plugged back in (docs/companions/streamdeck/daemon.md, "Hot plug").
//
// ReconnectingTransport wraps the deck's USB transport. A failed read, write or feature report (the deck is gone)
// closes it; from then on writes only update a picture cache (what the deck should show), reads wait out their
// timeout, and every `retry` the device thread's own read asks `reopen` for the deck again: no thread of its own, no
// timer elsewhere. When the same deck (serial and model) answers, the cache is replayed to it (brightness, every key,
// the strip) and the runtime above goes on as if nothing happened: same menu stack, same scenario. When only another
// deck is there (the lost one away for `otherDeckAfter`, and it matches the --device filter), `onOtherDeck` tells the
// owner, who ends this logical deck and starts one for the other deck (its own serial, its own model).
//
// Threads: one device thread calls Write / SendFeature / GetFeature / Read. Connected() and Reconnect() from any.
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "sdb/models.h"
#include "sdb/picturecache.h"
#include "sdb/transport.h"

namespace sdb
{

struct ReconnectOptions
{
    std::chrono::milliseconds retry{1000};           // between reopen attempts while the deck is away
    std::chrono::milliseconds otherDeckAfter{3000};  // then another deck may take its place (a short glitch never does)
    std::string filter;                              // --device: another deck's serial must contain it ("" = any)
    // Device thread: the lost deck stayed away and this deck is there instead.
    std::function<void(const std::string& serial)> onOtherDeck;
    // Device thread: unplugged (why = the error) / back (why empty).
    std::function<void(bool connected, const std::string& why)> onChange;
};

class ReconnectingTransport final : public Transport
{
public:
    // What one reopen attempt found: the same deck opened, or the serial of another deck that is there.
    struct Probe
    {
        std::unique_ptr<Transport> same;
        std::string other;
    };
    // anyDeck: another deck may be reported (the lost one has been away long enough).
    using Reopen = std::function<Probe(bool anyDeck)>;

    ReconnectingTransport(std::unique_ptr<Transport> usb, const Model& model, std::string serial, Reopen reopen,
                          ReconnectOptions options);
    ~ReconnectingTransport() override;

    int Write(std::span<const std::uint8_t> report) override;
    int SendFeature(std::span<const std::uint8_t> report) override;
    int GetFeature(std::span<std::uint8_t> buf) override;
    int Read(std::span<std::uint8_t> buf, int timeoutMs) override;
    std::string LastError() const override;
    [[nodiscard]] InputSource LastSource() const override;
    void Note(std::string_view text) override;
    void PublishPage(std::string_view json) override;
    [[nodiscard]] bool Connected() const override
    {
        return _connected.load();
    }
    bool Reconnect(std::chrono::milliseconds away) override;

    [[nodiscard]] int Reconnects() const
    {
        return _reconnects.load();
    }

private:
    using Clock = std::chrono::steady_clock;
    bool TakeRequest();
    void Lose(const std::string& why, std::chrono::milliseconds away);
    void TryReopen();
    bool Replay();

    std::unique_ptr<Transport> _usb;  // null while the deck is away
    std::string _serial;
    Reopen _reopen;
    ReconnectOptions _options;
    PictureCache _cache;
    std::string _why;  // why it went
    Clock::time_point _lostAt{}, _nextTry{};
    bool _otherReported = false;
    std::atomic<bool> _connected{true};
    std::atomic<long long> _requestMs{-1};  // Reconnect(): away (ms) for the device thread, -1 = none
    std::atomic<int> _reconnects{0};
};

}  // namespace sdb
