#include "sdb/reconnect.h"

#include <algorithm>
#include <thread>

#include "sdb/devicewire.h"

namespace sdb
{

ReconnectingTransport::ReconnectingTransport(std::unique_ptr<Transport> usb, const Model& model, std::string serial,
                                             Reopen reopen, ReconnectOptions options)
    : _usb(std::move(usb)),
      _serial(std::move(serial)),
      _reopen(std::move(reopen)),
      _options(std::move(options)),
      _cache(model)
{
    _connected = _usb != nullptr;
    if (!_usb)
        _lostAt = _nextTry = Clock::now();
}

ReconnectingTransport::~ReconnectingTransport() = default;

// A Reconnect() from another thread: done here, on the device thread.
bool ReconnectingTransport::TakeRequest()
{
    const long long ms = _requestMs.exchange(-1);
    if (ms < 0)
        return false;
    Lose("reconnect requested", std::chrono::milliseconds(ms));
    return true;
}

void ReconnectingTransport::Lose(const std::string& why, std::chrono::milliseconds away)
{
    const auto now = Clock::now();
    if (_usb)
    {
        _usb.reset();  // closes the handle (hid_close)
        _lostAt = now;
        _otherReported = false;
        _why = why;
        _connected = false;
        if (_options.onChange)
            _options.onChange(false, why);
    }
    _nextTry = now + away;
}

bool ReconnectingTransport::Reconnect(std::chrono::milliseconds away)
{
    _requestMs = std::max<long long>(0, away.count());
    return true;
}

int ReconnectingTransport::Write(std::span<const std::uint8_t> r)
{
    TakeRequest();
    _cache.Output(r);  // what the deck should show, there or not
    if (_usb)
    {
        if (const int n = _usb->Write(r); n >= 0)
            return n;
        Lose(_usb->LastError(), _options.retry);
    }
    return static_cast<int>(r.size());  // away: the picture is kept for the replay, the image goes on whole
}

int ReconnectingTransport::SendFeature(std::span<const std::uint8_t> r)
{
    TakeRequest();
    _cache.Feature(r);
    if (_usb)
    {
        if (const int n = _usb->SendFeature(r); n >= 0)
            return n;
        Lose(_usb->LastError(), _options.retry);
    }
    return static_cast<int>(r.size());
}

int ReconnectingTransport::GetFeature(std::span<std::uint8_t> buf)
{
    TakeRequest();
    // a failed feature read is an answer (a model without that report), not an unplug: reads and writes tell
    return _usb ? _usb->GetFeature(buf) : -1;
}

int ReconnectingTransport::Read(std::span<std::uint8_t> buf, int timeoutMs)
{
    if (TakeRequest())
        return 0;  // away for one call at least: whoever follows Connected() sees it go and come back
    if (_usb)
    {
        if (const int n = _usb->Read(buf, timeoutMs); n >= 0)
            return n;
        Lose(_usb->LastError(), _options.retry);
        return 0;
    }
    TryReopen();
    if (!_usb && timeoutMs > 0)
    {
        // the device thread's own wait: nothing to read until the deck is back (the next attempt is up to `retry`
        // away; the caller polls viewers' input between its short reads)
        const auto wait = std::min(std::chrono::milliseconds(timeoutMs),
                                   std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::max(Clock::duration::zero(), _nextTry - Clock::now())));
        if (wait.count() > 0)
            std::this_thread::sleep_for(wait);
    }
    return 0;
}

void ReconnectingTransport::TryReopen()
{
    const auto now = Clock::now();
    if (now < _nextTry || !_reopen)
        return;
    _nextTry = now + _options.retry;
    Probe p = _reopen(now - _lostAt >= _options.otherDeckAfter);
    if (p.same)
    {
        _usb = std::move(p.same);
        if (!Replay())
        {
            _usb.reset();  // gone again while it was drawn
            return;
        }
        _why.clear();
        _connected = true;
        ++_reconnects;
        if (_options.onChange)
            _options.onChange(true, {});
        return;
    }
    if (!p.other.empty() && !_otherReported)
    {
        _otherReported = true;
        if (_options.onOtherDeck)
            _options.onOtherDeck(p.other);
    }
}

// A replugged deck shows its logo: the picture it had (and the brightness) goes back to it.
bool ReconnectingTransport::Replay()
{
    for (const auto& e : _cache.Replay())
    {
        const std::span<const std::uint8_t> r(reinterpret_cast<const std::uint8_t*>(e.payload.data()),
                                              e.payload.size());
        if ((e.wireType == wire::SetFeature ? _usb->SendFeature(r) : _usb->Write(r)) < 0)
            return false;
    }
    return true;
}

std::string ReconnectingTransport::LastError() const
{
    return _usb ? _usb->LastError() : "the deck " + _serial + " is unplugged (" + _why + ")";
}

InputSource ReconnectingTransport::LastSource() const
{
    return _usb ? _usb->LastSource() : InputSource::Usb;
}

void ReconnectingTransport::Note(std::string_view text)
{
    if (_usb)
        _usb->Note(text);
}

void ReconnectingTransport::PublishPage(std::string_view json)
{
    if (_usb)
        _usb->PublishPage(json);
}

}  // namespace sdb
