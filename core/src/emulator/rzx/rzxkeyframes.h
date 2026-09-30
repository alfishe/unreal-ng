#pragma once

/// @file rzxkeyframes.h
/// @brief Keyframes of an RZX playback: the machine state (a compressed SZX
/// image) and the player's cursor at RZX frame boundaries, so a seek back
/// restores the nearest one and replays from there instead of from the start.
///
/// The store owns its memory and keeps it bounded: a keyframe is taken every
/// `interval` frames; when the total passes the budget every second keyframe
/// goes (frame 0 stays) and the interval doubles, so a long recording keeps an
/// even spread within the budget. Clear() (stop, a new recording, the
/// machine's end) frees everything.
///
/// Worked example: interval 250, budget 1 MB, 60 KB per keyframe. After 17
/// keyframes (frames 0 … 4000, 1020 KB) the 18th passes the budget: frames
/// 250, 750, … go, 9 stay (0, 500, … 4000), the next is due at 4500.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rzx
{
    /// The player's cursor at a frame boundary (everything a seek restores
    /// besides the machine)
    struct Cursor
    {
        uint64_t frame = 0;    ///< frames completed (the index of the frame in progress)
        uint32_t fetches = 0;  ///< fetches counted in that frame
        uint32_t inPos = 0;    ///< IN values consumed in that frame
    };

    struct Keyframe
    {
        Cursor cursor;
        std::vector<uint8_t> state;  ///< the machine as an SZX image
    };

    class RzxKeyframeStore
    {
    public:
        static constexpr uint32_t kDefaultInterval = 250;                   ///< 5 s of play at 50 Hz
        static constexpr size_t kDefaultBudget = 32u * 1024u * 1024u;

        void Configure(uint32_t interval, size_t budgetBytes);

        /// A keyframe is wanted at this frame boundary (none there yet)
        bool Due(uint64_t frame) const
        {
            return _interval > 0 && frame >= _nextDue;
        }

        /// Store one (frames arrive in increasing order during play; a frame
        /// already covered after a seek back is ignored), then keep the budget
        void Add(Keyframe&& keyframe);

        /// The latest keyframe at or before `frame`; null when there is none
        const Keyframe* AtOrBefore(uint64_t frame) const;

        /// After a seek: the next keyframe is due past the ones already stored
        void Rewound(uint64_t frame);

        void Clear();

        size_t Count() const
        {
            return _keyframes.size();
        }
        size_t Bytes() const
        {
            return _bytes;
        }
        uint32_t Interval() const
        {
            return _interval;
        }
        size_t Budget() const
        {
            return _budget;
        }

    private:
        void Thin();

        std::vector<Keyframe> _keyframes;  ///< sorted by frame
        size_t _bytes = 0;
        uint32_t _interval = kDefaultInterval;
        uint32_t _baseInterval = kDefaultInterval;
        size_t _budget = kDefaultBudget;
        uint64_t _nextDue = 0;
    };
}  // namespace rzx
