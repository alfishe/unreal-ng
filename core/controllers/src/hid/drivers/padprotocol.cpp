#include "hid/drivers/padprotocol.h"

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "platform/windows/winthread.h"
#endif

#include <chrono>
#include <utility>

namespace controllers
{

PadPump::PadPump(std::unique_ptr<IPadProtocol> protocol, Write write, GetFeature getFeature, Clock clock,
                 Write setFeature)
    : _protocol(std::move(protocol)),
      _write(std::move(write)),
      _getFeature(std::move(getFeature)),
      _clock(std::move(clock)),
      _setFeature(std::move(setFeature)),
      _thread([this] { Run(); })
{
}

PadPump::~PadPump()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
    }
    _wake.notify_all();
    _thread.join();
    std::optional<PadOutReport> last;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        last = _protocol->Final();
    }
    // A device already gone refuses it at once
    if (last && last->kind == PadOutReport::Kind::Output && _write)
        _write(last->bytes);
    else if (last && last->kind == PadOutReport::Kind::SetFeature && _setFeature)
        _setFeature(last->bytes);
}

uint64_t PadPump::Now() const
{
    if (_clock)
        return _clock();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

void PadPump::SetRumble(const RumbleLevels& levels)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _protocol->SetRumble(levels);
    }
    _wake.notify_all();
}

bool PadPump::SetLight(const LightColor& color)
{
    bool taken = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        taken = _protocol->SetLight(color);
    }
    _wake.notify_all();
    return taken;
}

void PadPump::SetPlayer(int player)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _protocol->SetPlayer(player);
    }
    _wake.notify_all();
}

void PadPump::Input(const uint8_t* report, size_t size)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _protocol->OnInput(report, size, Now());
    }
    _wake.notify_all();
}

uint64_t PadPump::Written() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _written;
}

uint64_t PadPump::Failed() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _failed;
}

void PadPump::Run()
{
#if defined(__APPLE__)
    pthread_setname_np("pad-pump");
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), "pad-pump");
#elif defined(_WIN32)
    NameThisThread(L"pad-pump");
#endif
    std::unique_lock<std::mutex> lock(_mutex);
    while (!_stopping)
    {
        uint64_t wakeMs = 0;
        const uint64_t now = Now();
        std::optional<PadOutReport> next = _protocol->Next(now, wakeMs);
        if (!next)
        {
            // A setter or an input report wakes it sooner; the clock may be a test's, so wait in short steps
            if (wakeMs == 0)
                _wake.wait(lock);
            else
            {
                const uint64_t wait = wakeMs <= now ? 1 : wakeMs - now > 100 ? 100 : wakeMs - now;
                _wake.wait_for(lock, std::chrono::milliseconds(wait));
            }
            continue;
        }
        lock.unlock();
        bool ok = false;
        std::vector<uint8_t> data;
        if (next->kind == PadOutReport::Kind::Output)
            ok = _write && _write(next->bytes);
        else if (next->kind == PadOutReport::Kind::SetFeature)
            ok = _setFeature && _setFeature(next->bytes);
        else
            ok = _getFeature && _getFeature(next->featureId, next->featureSize, data);
        lock.lock();
        ++(ok ? _written : _failed);
        if (next->kind != PadOutReport::Kind::GetFeature)
            _protocol->OnWritten(ok, Now());
        else
        {
            if (!ok)
                data.clear();
            _protocol->OnFeature(next->featureId, data, Now());
        }
    }
}

}  // namespace controllers
