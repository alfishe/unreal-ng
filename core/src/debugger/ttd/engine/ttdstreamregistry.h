#pragma once

/// @file ttdstreamregistry.h
/// @brief Optional streams captured at frame boundaries (engine decision D19).
///
/// An optional stream records extra data at each frame boundary, for example
/// a screenshot to debug quickly without restoring the frame. Streams can be
/// switched on and off at run time. A stream that is off costs nothing: the
/// frame-boundary path reads one bitmask and returns when it is zero, so the
/// cost is one check per frame, never one per event.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ttdtime.h"

namespace ttd
{

class TTDStreamRegistry
{
public:
    /// Most streams a session can have: the enabled set is one 64-bit mask
    static constexpr uint32_t kMaxStreams = 64;

    using CaptureFn = std::function<void(const TTDPosition& at)>;

    /// Register a stream under a stable @p id (0..63, stored in files from
    /// Phase 4 on). Registered streams start switched off. False when the id
    /// is out of range or taken
    bool Register(uint32_t id, const std::string& name, CaptureFn capture);

    /// Switch a registered stream on or off; false when @p id is unknown
    bool SetEnabled(uint32_t id, bool on);
    bool IsEnabled(uint32_t id) const { return id < kMaxStreams && ((_enabledMask >> id) & 1u) != 0; }
    bool IsRegistered(uint32_t id) const { return id < _streams.size() && static_cast<bool>(_streams[id].capture); }
    std::string Name(uint32_t id) const { return IsRegistered(id) ? _streams[id].name : std::string(); }

    /// The frame-boundary call: runs the enabled streams. With every stream
    /// off this is a single comparison
    void CaptureEnabled(const TTDPosition& at)
    {
        if (_enabledMask == 0)
            return;
        CaptureEnabledSlow(at);
    }

    uint64_t EnabledMask() const { return _enabledMask; }

    /// Calls made by CaptureEnabled since the registry was created (tests)
    uint64_t CaptureCalls() const { return _captureCalls; }

private:
    struct Stream
    {
        std::string name;
        CaptureFn capture;
    };

    void CaptureEnabledSlow(const TTDPosition& at);

    std::vector<Stream> _streams;
    uint64_t _enabledMask = 0;
    uint64_t _captureCalls = 0;
};

}  // namespace ttd
