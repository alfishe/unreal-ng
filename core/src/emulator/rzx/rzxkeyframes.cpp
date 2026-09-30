#include "emulator/rzx/rzxkeyframes.h"

#include <algorithm>

namespace rzx
{
    void RzxKeyframeStore::Configure(uint32_t interval, size_t budgetBytes)
    {
        Clear();
        _interval = interval;
        _baseInterval = interval;
        _budget = budgetBytes;
    }

    void RzxKeyframeStore::Add(Keyframe&& keyframe)
    {
        const uint64_t frame = keyframe.cursor.frame;
        if (!_keyframes.empty() && frame <= _keyframes.back().cursor.frame)
            return;

        _bytes += keyframe.state.size();
        _keyframes.push_back(std::move(keyframe));
        _nextDue = frame + _interval;

        while (_bytes > _budget && _keyframes.size() > 1)
            Thin();
    }

    const Keyframe* RzxKeyframeStore::AtOrBefore(uint64_t frame) const
    {
        auto it = std::upper_bound(_keyframes.begin(), _keyframes.end(), frame,
                                   [](uint64_t f, const Keyframe& k) { return f < k.cursor.frame; });
        if (it == _keyframes.begin())
            return nullptr;
        return &*(it - 1);
    }

    void RzxKeyframeStore::Rewound(uint64_t frame)
    {
        // Playing forward again from `frame`: nothing is due until past the
        // stored ones (their frames are covered), then at the usual spacing
        const uint64_t last = _keyframes.empty() ? 0 : _keyframes.back().cursor.frame;
        _nextDue = std::max(frame, last + (_keyframes.empty() ? 0 : _interval));
    }

    void RzxKeyframeStore::Clear()
    {
        _keyframes.clear();
        _keyframes.shrink_to_fit();
        _bytes = 0;
        _interval = _baseInterval;
        _nextDue = 0;
    }

    void RzxKeyframeStore::Thin()
    {
        // Every second keyframe goes (the first one stays: frame 0, the
        // recording's start), the spacing doubles
        std::vector<Keyframe> kept;
        kept.reserve(_keyframes.size() / 2 + 1);
        size_t bytes = 0;
        for (size_t i = 0; i < _keyframes.size(); i++)
        {
            if (i % 2 == 0)
            {
                bytes += _keyframes[i].state.size();
                kept.push_back(std::move(_keyframes[i]));
            }
        }
        _keyframes = std::move(kept);
        _bytes = bytes;
        _interval *= 2;
        _nextDue = _keyframes.back().cursor.frame + _interval;
    }
}  // namespace rzx
