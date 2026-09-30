#pragma once

/// @file rzxplayer.h
/// @brief The RZX playback state machine (design §4-§6, §9): the cursor over
/// the recorded frames, the IN substitution, the fetch counter and the
/// frame-end decision. Used only on the emulation thread; status snapshots
/// are published for other threads under a mutex once per RZX frame.
///
/// The CPU drives it through three calls (Z80::StepInstructionRzx, Z80::in):
///   - OnIn(port, deviceValue) for every IN: the next recorded value;
///   - AddFetches(n) after every step: the R increments of that step;
///   - EndFrame(...) at the first instruction boundary where the frame's
///     fetch count is reached: checks the frame, moves to the next one and
///     says whether the interrupt that ends the frame is raised.
///
/// Worked example: frame {fetchCount 7, INs [#BF]} with the program
/// `IN A,(#FE)` (1 fetch), `NOP` x6: after the IN (value #BF, whatever the
/// port answered) and six NOPs the count is 7; the next boundary ends the
/// frame and, with IFF1 set, the CPU takes the interrupt there.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "emulator/rzx/rzxkeyframes.h"
#include "loaders/rzx/rzxformat.h"

namespace rzx
{
    enum class DesyncMode : uint8_t
    {
        Strict,   ///< stop playback at the first desync
        Tolerant  ///< count desyncs and continue
    };

    /// Playback conventions (requirements RZ-F11); the defaults match
    /// SkoolKit rzxplay.py, which plays the largest share of files
    struct PlayerOptions
    {
        DesyncMode desyncMode = DesyncMode::Strict;
        /// SkoolKit flag 2: a frame of 1-2 fetches right after EI means "the
        /// interrupt was blocked by EI"; off: the interrupt is accepted at every
        /// frame end whenever IFF1 is set
        bool eiShortFrameBlocksInt = false;
        /// SkoolKit flag 1: the NMOS LD A,I / LD A,R parity quirk on the
        /// interrupt that ends a frame; off by default (files recorded without it)
        bool ldAirParityQuirk = false;
        /// SkoolKit flag 4: snapshot blocks after the first are skipped
        bool ignoreLaterSnapshots = false;
        /// Keyframes for seeking back: one every this many frames (0: none)
        /// within this many bytes (RzxKeyframeStore thins them to fit)
        uint32_t keyframeInterval = RzxKeyframeStore::kDefaultInterval;
        size_t keyframeBudget = RzxKeyframeStore::kDefaultBudget;
    };

    enum class PlayerState : uint8_t
    {
        Playing,
        Finished,  ///< every frame played
        Desynced,  ///< strict mode stopped at a desync
        Stopped,   ///< stopped by a command, or at a block the player cannot apply
    };

    enum class DesyncKind : uint8_t
    {
        None,
        TooManyIns,    ///< an IN beyond the frame's recorded values
        TooFewIns,     ///< the frame ended with recorded values left
        FetchOverrun   ///< the frame's count was passed by more than the tolerance
    };

    struct Desync
    {
        DesyncKind kind = DesyncKind::None;
        uint32_t block = 0;      ///< input block, 0-based
        uint64_t frame = 0;      ///< frame over the whole file, 0-based
        uint32_t expected = 0;   ///< INs or fetches recorded
        uint32_t actual = 0;     ///< INs or fetches seen
        uint16_t pc = 0;
        uint16_t port = 0;       ///< TooManyIns only
    };

    struct PlayerStatus
    {
        PlayerState state = PlayerState::Stopped;
        uint32_t block = 0;           ///< current input block, 0-based
        uint32_t blocks = 0;          ///< input blocks in the file
        uint64_t frame = 0;           ///< frames completed
        uint64_t totalFrames = 0;
        uint64_t interrupts = 0;      ///< frame-end interrupts accepted
        uint64_t desyncs = 0;
        Desync firstDesync;
        std::string stopReason;       ///< Stopped / Desynced: why
        /// T-states between the last forced interrupt and the machine's own
        /// interrupt position (positive: later); max: the largest magnitude seen
        int32_t drift = 0;
        int32_t maxDrift = 0;
        uint64_t snapshotsApplied = 0;  ///< snapshot blocks between input blocks (multiload, rollback)
        uint64_t keyframes = 0;       ///< stored for seeking back
        uint64_t keyframeBytes = 0;
        uint32_t keyframeInterval = 0;
    };

    /// What the CPU does at a frame end
    enum class FrameEnd : uint8_t
    {
        Interrupt,    ///< the CPU takes the interrupt that ends the frame
        NoInterrupt,  ///< frame advanced without one (IFF1 clear, the EI convention, or playback ended)
        Snapshot      ///< the next input block starts from a snapshot block: ApplyPendingSnapshot()
    };

    const char* StateName(PlayerState state);
    const char* DesyncName(DesyncKind kind);

    class RzxPlayer
    {
    public:
        /// Fetches past a frame's count still accepted (zxsp: a frame closed
        /// right after an EI, or a boundary delayed by a redundant prefix)
        static constexpr uint32_t kFetchOverrunTolerance = 2;

        RzxPlayer(std::shared_ptr<const File> file, const PlayerOptions& options);

        /// Positions on the first input block (after the start snapshot)
        bool Start(std::string& error);

        /// Every IN the CPU executes: the recorded value
        uint8_t OnIn(uint16_t port, uint8_t deviceValue, uint16_t pc);

        /// R increments of the step just executed (acknowledge excluded)
        void AddFetches(uint32_t fetches)
        {
            _fetches += fetches;
        }

        /// Fetches counted in the current frame (emulation thread)
        uint32_t Fetches() const
        {
            return _fetches;
        }
        /// Frames completed (emulation thread; Status() elsewhere)
        uint64_t FramesDone() const
        {
            return _framesDone;
        }

        bool FrameDue() const
        {
            return _fetches >= _frame->fetchCount;
        }

        /// At an instruction boundary with FrameDue(): check and advance.
        /// `iff1`: maskable interrupts enabled; `eiShadow`: the instruction just
        /// executed was EI; `drift`: T-states from the machine's natural
        /// interrupt position (for the statistic). Interrupt: the CPU takes it now
        FrameEnd EndFrame(uint16_t pc, bool iff1, bool eiShadow, int32_t drift);

        /// Stop from outside (the emulation thread, or with the machine paused)
        void Stop(const std::string& reason);

        /// At a frame boundary (before EndFrame): take a keyframe when one is
        /// due. The machine state comes from captureState (the session)
        void MaybeKeyframe();
        std::function<bool(std::vector<uint8_t>& state)> captureState;

        /// FrameEnd::Snapshot: replace the machine by the snapshot block before
        /// the next input block (applySnapshot, the session); a failure stops
        /// the playback with its reason
        bool ApplyPendingSnapshot();
        std::function<bool(const Snapshot& snapshot, uint32_t tstates, std::string& error)> applySnapshot;

        /// Put the cursor where a keyframe was taken and play on from there
        /// (the caller restored the machine); false past the recording
        bool SeekCursor(const Cursor& cursor);
        Cursor CurrentCursor() const
        {
            return {_framesDone, _fetches, _inPos};
        }
        const RzxKeyframeStore& Keyframes() const
        {
            return _keyframes;
        }
        /// Free the keyframes (stop, a new recording)
        void ClearKeyframes()
        {
            _keyframes.Clear();
            Publish();
        }

        bool IsPlaying() const
        {
            return _state == PlayerState::Playing;
        }
        /// The state left Playing and the owner was not told yet: the CPU
        /// calls NotifyEnded() at the end of the step
        bool EndPending() const
        {
            return _state != PlayerState::Playing && !_endNotified;
        }
        void NotifyEnded();

        /// Called once on the emulation thread when playback ends (the owner
        /// removes the hooks and restores the machine)
        std::function<void(RzxPlayer&)> onEnded;

        const PlayerOptions& Options() const
        {
            return _options;
        }
        const File& GetFile() const
        {
            return *_file;
        }
        PlayerState State() const
        {
            return _state;
        }

        /// Thread-safe copy of the last published status
        PlayerStatus Status() const;

    private:
        void ReportDesync(const Desync& desync);
        /// Enter the frame at _frameIndex of the current block, or the next
        /// block; false when playback ended
        bool EnterFrame();
        /// Move to the next input block in file order (its frame 0), handling
        /// snapshot blocks in between; false when playback ended
        bool NextBlock();
        void Publish();

        std::shared_ptr<const File> _file;
        PlayerOptions _options;
        PlayerState _state = PlayerState::Stopped;
        bool _endNotified = false;
        std::string _stopReason;

        size_t _orderIndex = 0;          ///< File::order position of the current input block
        uint32_t _blockNumber = 0;       ///< input blocks entered so far - 1
        const InputBlock* _block = nullptr;
        size_t _frameIndex = 0;          ///< within the block
        uint64_t _framesDone = 0;
        uint64_t _totalFrames = 0;
        const Frame* _frame = nullptr;
        const uint8_t* _in = nullptr;    ///< the frame's IN values
        uint32_t _inPos = 0;
        uint32_t _fetches = 0;
        uint8_t _lastIn = 0xFF;

        uint64_t _interrupts = 0;
        uint64_t _desyncs = 0;
        Desync _firstDesync;
        int32_t _drift = 0;
        int32_t _maxDrift = 0;

        /// An always-due sentinel for the stopped state: FrameDue() stays
        /// a single compare with no null test
        static const Frame kEndFrame;

        RzxKeyframeStore _keyframes;

        /// Set by NextBlock when a snapshot block precedes the input block just entered
        const Snapshot* _pendingSnapshot = nullptr;
        uint32_t _pendingTstates = 0;
        uint64_t _snapshotsApplied = 0;

        mutable std::mutex _statusMutex;
        PlayerStatus _published;
    };
}  // namespace rzx
