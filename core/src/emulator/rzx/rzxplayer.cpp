#include "emulator/rzx/rzxplayer.h"

#include <cstdlib>

namespace rzx
{
    const Frame RzxPlayer::kEndFrame{};

    const char* StateName(PlayerState state)
    {
        switch (state)
        {
            case PlayerState::Playing: return "playing";
            case PlayerState::Finished: return "finished";
            case PlayerState::Desynced: return "desynced";
            case PlayerState::Stopped: return "stopped";
        }
        return "?";
    }

    const char* DesyncName(DesyncKind kind)
    {
        switch (kind)
        {
            case DesyncKind::None: return "none";
            case DesyncKind::TooManyIns: return "too_many_ins";
            case DesyncKind::TooFewIns: return "too_few_ins";
            case DesyncKind::FetchOverrun: return "fetch_overrun";
        }
        return "?";
    }

    RzxPlayer::RzxPlayer(std::shared_ptr<const File> file, const PlayerOptions& options)
        : _file(std::move(file)), _options(options), _frame(&kEndFrame)
    {
        _totalFrames = _file ? _file->TotalFrames() : 0;
    }

    bool RzxPlayer::Start(std::string& error)
    {
        if (!_file || _file->order.empty() || _file->order.front().type != BlockType::Snapshot)
        {
            error = "the recording does not start with a snapshot";
            return false;
        }
        for (const InputBlock& input : _file->inputs)
        {
            if (input.protectedFrames)
            {
                error = "the recording has protected (encrypted) input frames";
                return false;
            }
        }

        _state = PlayerState::Playing;
        _endNotified = false;
        _orderIndex = 0;
        _blockNumber = 0;
        _block = nullptr;
        _framesDone = 0;
        if (!NextBlock() || !EnterFrame())
        {
            error = _stopReason.empty() ? "no input frames after the start snapshot" : _stopReason;
            return false;
        }
        Publish();
        return true;
    }

    uint8_t RzxPlayer::OnIn(uint16_t port, uint8_t deviceValue, uint16_t pc)
    {
        (void)deviceValue;
        if (_inPos < _frame->inCount) [[likely]]
        {
            _lastIn = _in[_inPos++];
            return _lastIn;
        }

        if (_state != PlayerState::Playing)
            return deviceValue;

        // More INs than the frame recorded: the CPU left the recorded path
        Desync desync;
        desync.kind = DesyncKind::TooManyIns;
        desync.expected = _frame->inCount;
        desync.actual = ++_inPos;
        desync.pc = pc;
        desync.port = port;
        ReportDesync(desync);
        return _lastIn;
    }

    FrameEnd RzxPlayer::EndFrame(uint16_t pc, bool iff1, bool eiShadow, int32_t drift)
    {
        if (_state != PlayerState::Playing)
            return FrameEnd::NoInterrupt;

        // Leftover IN values: the CPU executed fewer INs than recorded
        if (_inPos < _frame->inCount)
        {
            Desync desync;
            desync.kind = DesyncKind::TooFewIns;
            desync.expected = _frame->inCount;
            desync.actual = _inPos;
            desync.pc = pc;
            ReportDesync(desync);
        }
        if (_state == PlayerState::Playing && _fetches > _frame->fetchCount + kFetchOverrunTolerance)
        {
            Desync desync;
            desync.kind = DesyncKind::FetchOverrun;
            desync.expected = _frame->fetchCount;
            desync.actual = _fetches;
            desync.pc = pc;
            ReportDesync(desync);
        }
        if (_state != PlayerState::Playing)
            return FrameEnd::NoInterrupt;

        _drift = drift;
        if (std::abs(drift) > std::abs(_maxDrift))
            _maxDrift = drift;

        _framesDone++;
        _frameIndex++;
        const bool more = EnterFrame();

        // Every frame ends with an interrupt when IFF1 is set, the last one too
        // (SkoolKit); flag 2: EI followed by a 1-2 fetch frame means "blocked"
        const bool blockedByEi = more && _options.eiShortFrameBlocksInt && eiShadow && _frame->fetchCount <= 2;
        if (!iff1 || blockedByEi)
        {
            Publish();
            return FrameEnd::NoInterrupt;
        }
        _interrupts++;
        Publish();
        return FrameEnd::Interrupt;
    }

    void RzxPlayer::Stop(const std::string& reason)
    {
        if (_state != PlayerState::Playing)
            return;
        _state = PlayerState::Stopped;
        _stopReason = reason;
        _frame = &kEndFrame;
        Publish();
    }

    void RzxPlayer::NotifyEnded()
    {
        if (_endNotified)
            return;
        _endNotified = true;
        if (onEnded)
            onEnded(*this);
    }

    PlayerStatus RzxPlayer::Status() const
    {
        std::lock_guard<std::mutex> lock(_statusMutex);
        return _published;
    }

    void RzxPlayer::ReportDesync(const Desync& desync)
    {
        Desync report = desync;
        report.block = _blockNumber;
        report.frame = _framesDone;
        if (_desyncs++ == 0)
            _firstDesync = report;

        if (_options.desyncMode == DesyncMode::Strict)
        {
            _state = PlayerState::Desynced;
            _stopReason = std::string("desync (") + DesyncName(report.kind) + ") in frame " +
                          std::to_string(report.frame) + ": expected " + std::to_string(report.expected) +
                          ", got " + std::to_string(report.actual);
            _frame = &kEndFrame;
            _inPos = 0;
        }
        Publish();
    }

    bool RzxPlayer::EnterFrame()
    {
        // A frame of 0 fetches is skipped with its IN values, no interrupt
        // (SkoolKit RZXTracer.next_frame): counted as played
        for (;;)
        {
            if (_frameIndex >= _block->frames.size() && !NextBlock())
                return false;

            const Frame& frame = _block->frames[_frameIndex];
            if (frame.fetchCount > 0)
            {
                _frame = &frame;
                _in = _block->inValues.data() + frame.inOffset;
                _inPos = 0;
                _fetches = 0;
                return true;
            }
            _frameIndex++;
            _framesDone++;
        }
    }

    bool RzxPlayer::NextBlock()
    {
        // _orderIndex points at the current input block (or the start snapshot)
        const bool first = _block == nullptr;
        bool snapshotBetween = false;
        for (size_t i = _orderIndex + 1; i < _file->order.size(); i++)
        {
            const BlockRef& ref = _file->order[i];
            if (ref.type == BlockType::Snapshot)
            {
                snapshotBetween = true;
                continue;
            }

            const InputBlock& input = _file->inputs[ref.index];
            if (input.frames.empty())
                continue;

            // A snapshot between input blocks (multiload, rollback point) is
            // applied at the frame boundary in phase R3; until then only the
            // "ignore later snapshots" convention plays past it
            if (snapshotBetween && !first && !_options.ignoreLaterSnapshots)
            {
                _state = PlayerState::Stopped;
                _stopReason = "snapshot block before input block " + std::to_string(_blockNumber + 2) +
                              " at frame " + std::to_string(_framesDone) +
                              ": mid-recording snapshots are not supported yet (option ignore_later_snapshots "
                              "plays past them)";
                _frame = &kEndFrame;
                return false;
            }

            if (!first)
                _blockNumber++;
            _orderIndex = i;
            _block = &input;
            _frameIndex = 0;
            return true;
        }

        if (first)
        {
            _state = PlayerState::Stopped;
            _stopReason = "no input frames after the start snapshot";
        }
        else
        {
            _state = PlayerState::Finished;
        }
        _frame = &kEndFrame;
        return false;
    }

    void RzxPlayer::Publish()
    {
        PlayerStatus status;
        status.state = _state;
        status.block = _blockNumber;
        status.blocks = static_cast<uint32_t>(_file ? _file->inputs.size() : 0);
        status.frame = _framesDone;
        status.totalFrames = _totalFrames;
        status.interrupts = _interrupts;
        status.desyncs = _desyncs;
        status.firstDesync = _firstDesync;
        status.stopReason = _stopReason;
        status.drift = _drift;
        status.maxDrift = _maxDrift;

        std::lock_guard<std::mutex> lock(_statusMutex);
        _published = std::move(status);
    }
}  // namespace rzx
