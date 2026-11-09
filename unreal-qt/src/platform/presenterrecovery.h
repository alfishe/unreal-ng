#pragma once

#include <cstdint>
#include <deque>

/// The screen window's native presenter through its failures (DeviceScreenGLWindow::renderNative): a presenter that
/// stops presenting (BeginFrame failing for a while, or its device gone) is made anew, making one is retried for a
/// while (the system may hold the window a moment after a GPU reset), and when that does not help the window presents
/// with GL for the rest of its life. A new screen window (a GPU or display change rebuilds it) tries native again.
/// Pure logic on the steady clock passed in (presenterrecovery_test.cpp).
class PresenterRecovery
{
public:
    static constexpr int64_t kRestartAfterNs = 2'000'000'000;  ///< BeginFrame failing this long: a new presenter
    static constexpr int kRetries = 20;            ///< after a restart: tries to make the presenter, kRetryMs apart
    static constexpr int kRetryMs = 500;
    static constexpr int kNewWindowAfter = 2;      ///< failed tries before a new native window (Windows)
    static constexpr int kMaxRestarts = 3;         ///< restarts within kRestartWindowNs; one more: GL
    static constexpr int64_t kRestartWindowNs = 60'000'000'000;

    enum class CreateFailure
    {
        Retry,           ///< again kRetryMs later
        RetryNewWindow,  ///< ... on a new native window
        FallBack,        ///< GL from now on
    };
    enum class BeginFailure
    {
        First,     ///< the first of a run: tell it
        Wait,      ///< counted; the presenter may come back
        Restart,   ///< a new presenter now
        FallBack,  ///< GL from now on
    };

    /// GL from now on: restarts or tries used up
    bool GlOnly() const { return _glOnly; }

    /// A presenter made: the tries start afresh with the next restart
    void Created() { _retries = 0; }
    /// Making a presenter failed: the first one of the window falls back at once (the platform lacks what it needs),
    /// one after a restart is tried kRetries times
    CreateFailure CreateFailed()
    {
        if (_retries <= 0)
        {
            _glOnly = true;
            return CreateFailure::FallBack;
        }
        _retries--;
        return _retries == kRetries - kNewWindowAfter ? CreateFailure::RetryNewWindow : CreateFailure::Retry;
    }
    int RetriesLeft() const { return _retries; }

    /// A picture presented. @return how long BeginFrame failed before it (0: it did not)
    int64_t Presented(int64_t nowNs)
    {
        const int64_t failed = _failingSinceNs != 0 ? nowNs - _failingSinceNs : 0;
        _failingSinceNs = 0;
        return failed;
    }
    /// BeginFrame gave no framebuffer at `nowNs`; `lost`: the presenter's device is gone, waiting cannot help
    BeginFailure BeginFailed(int64_t nowNs, bool lost)
    {
        if (!lost)
        {
            if (_failingSinceNs == 0)
            {
                _failingSinceNs = nowNs;
                _failures = 1;
                return BeginFailure::First;
            }
            if (nowNs - _failingSinceNs <= kRestartAfterNs)
            {
                _failures++;
                return BeginFailure::Wait;
            }
        }
        _failures++;
        return Restart(nowNs) ? BeginFailure::Restart : BeginFailure::FallBack;
    }
    /// Pictures not presented in the run of failures (the last one included)
    uint64_t Failures() const { return _failures; }
    /// When the run of failures started (0: none)
    int64_t FailingSinceNs() const { return _failingSinceNs; }

    /// The presenter dropped to be made anew (it stopped presenting, or the GL context was lost). @return false: too
    /// many restarts within kRestartWindowNs, GL from now on
    bool Restart(int64_t nowNs)
    {
        _failingSinceNs = 0;
        while (!_restarts.empty() && nowNs - _restarts.front() > kRestartWindowNs)
            _restarts.pop_front();
        if (static_cast<int>(_restarts.size()) >= kMaxRestarts)
        {
            _glOnly = true;
            return false;
        }
        _restarts.push_back(nowNs);
        _retries = kRetries;
        return true;
    }

private:
    int _retries = 0;
    int64_t _failingSinceNs = 0;
    uint64_t _failures = 0;
    std::deque<int64_t> _restarts;
    bool _glOnly = false;
};
