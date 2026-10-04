#include "emulator/rzx/rzxsession.h"

#include <atomic>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "3rdparty/message-center/messagecenter.h"
#include "common/filehelper.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskautostart.h"
#include "emulator/notifications.h"
#include "loaders/rzx/rzxreader.h"
#include "loaders/snapshot/szx/loaderszx.h"
#include "loaders/snapshot/szx/szxreader.h"
#include "loaders/snapshot/szx/szxwriter.h"
#include "debugger/ttd/timetravelmanager.h"

namespace rzx
{
    namespace
    {
        std::string ToUtf8(const std::filesystem::path& path)
        {
            const auto text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        std::string ModelShortName(MEM_MODEL model)
        {
            const TMemModel* entry = Config::FindModelByEnum(model);
            return entry ? entry->ShortName : std::string("?");
        }

        std::string MachineName(MEM_MODEL model, uint32_t ramKb)
        {
            std::string name = ModelShortName(model);
            if (model == MM_PENTAGON && ramKb != 128)
                name += " (" + std::to_string(ramKb) + "K)";
            return name;
        }

        bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& bytes)
        {
            if (!FileHelper::FileExists(path))
                return false;
            const size_t size = FileHelper::GetFileSize(path);
            bytes.resize(size);
            return size == 0 || FileHelper::ReadFileToBuffer(path, bytes.data(), size) == size;
        }
    }  // namespace

    const char* PlayErrorName(PlayError error)
    {
        switch (error)
        {
            case PlayError::None: return "none";
            case PlayError::BadFile: return "bad_file";
            case PlayError::NoSnapshot: return "no_snapshot";
            case PlayError::UnsupportedMachine: return "unsupported_machine";
            case PlayError::ModelMismatch: return "model_mismatch";
            case PlayError::SnapshotLoadFailed: return "snapshot_load_failed";
            case PlayError::Refused: return "refused";
        }
        return "?";
    }

    RzxSession::RzxSession(Emulator& emulator) : _emulator(emulator), _context(emulator.GetContext())
    {
    }

    RzxSession::~RzxSession()
    {
        Uninstall();
    }

    bool RzxSession::MachineSupported(EmulatorContext* context, std::string& reason)
    {
        if (!context || !context->pCore || !context->pCore->GetZ80())
        {
            reason = "no machine";
            return false;
        }
        // The INT comes from the ULA frame pulse on every machine RZX was
        // recorded on; machine-owned INT logic (TSConf, Sprinter) cannot be
        // replaced by the recorded schedule
        if (context->HasStepWork(EmulatorContext::kStepWorkInterruptSource))
        {
            reason = "this machine's interrupt is not the ULA frame interrupt";
            return false;
        }
        switch (context->config.mem_model)
        {
            case MM_SPECTRUM48:
            case MM_SPECTRUM128:
            case MM_PLUS2:
            case MM_PLUS2A:
            case MM_PLUS3:
            case MM_PENTAGON:
            case MM_SCORP:
                return true;
            default:
                reason = Config::GetModelFullName(context->config.mem_model) +
                         " is not supported for RZX playback (48K, 128K, +2, +2A, +3, Pentagon, Scorpion)";
                return false;
        }
    }

    bool RzxSession::SnapshotBytes(const Snapshot& snapshot, const std::string& sourcePath, std::string& extension,
                                   std::vector<uint8_t>& bytes, std::string& error)
    {
        extension = snapshot.extension;
        if (snapshot.external)
        {
            // Next to the RZX first, then the stored name as it is
            std::vector<std::string> candidates;
            if (!sourcePath.empty())
            {
                const std::filesystem::path folder = FileHelper::ToFsPath(sourcePath).parent_path();
                const std::string name = ToUtf8(FileHelper::ToFsPath(snapshot.externalName).filename());
                candidates.push_back(FileHelper::PathCombine(ToUtf8(folder), name));
            }
            candidates.push_back(snapshot.externalName);

            bool found = false;
            for (const std::string& candidate : candidates)
            {
                if (ReadWholeFile(candidate, bytes))
                {
                    found = true;
                    if (extension.empty())
                        extension = FileHelper::GetFileExtension(candidate);
                    break;
                }
            }
            if (!found)
            {
                error = "external snapshot '" + snapshot.externalName + "' not found";
                return false;
            }
        }
        else
        {
            bytes = snapshot.data;
        }

        for (char& c : extension)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension == "zxs")
            extension = "szx";
        return true;
    }

    bool RzxSession::ResolveStartSnapshot(const File& file, const std::string& sourcePath,
                                          const PlayerOptions& options, StartSnapshot& snapshot,
                                          PlayResult& result)
    {
        // SkoolKit: each snapshot before the first input block replaces the
        // previous one, unless later snapshots are ignored
        const Snapshot* start = nullptr;
        for (const BlockRef& ref : file.order)
        {
            if (ref.type == BlockType::Input)
                break;
            if (start == nullptr || !options.ignoreLaterSnapshots)
                start = &file.snapshots[ref.index];
        }
        if (start == nullptr || file.order.empty() || file.order.front().type != BlockType::Snapshot)
        {
            result.error = PlayError::NoSnapshot;
            result.message = "the recording has no start snapshot before its first input block";
            return false;
        }

        std::string error;
        if (!SnapshotBytes(*start, sourcePath, snapshot.extension, snapshot.data, error))
        {
            result.error = PlayError::NoSnapshot;
            result.message = error;
            return false;
        }

        if (!DetectSnapshotMachine(snapshot.extension, snapshot.data, snapshot.machine, error))
        {
            result.error = PlayError::UnsupportedMachine;
            result.message = "start snapshot: " + error;
            return false;
        }
        return true;
    }

    PlayResult RzxSession::PlayFile(const std::string& path, const PlayerOptions& options)
    {
        PlayResult result;
        auto file = std::make_shared<File>();
        std::string error;
        const std::string absolutePath = FileHelper::AbsolutePath(path);
        if (!RzxReader::ParseFile(absolutePath, *file, error))
        {
            result.error = PlayError::BadFile;
            result.message = error;
            PostEvent("failed", error);
            return result;
        }
        return Play(file, absolutePath, options);
    }

    PlayResult RzxSession::Play(std::shared_ptr<const File> file, const std::string& sourcePath,
                                const PlayerOptions& options)
    {
        PlayResult result;
        if (!file)
        {
            result.error = PlayError::BadFile;
            result.message = "no recording";
            return result;
        }

        std::string reason;
        if (!MachineSupported(_context, reason))
        {
            result.error = PlayError::UnsupportedMachine;
            result.message = reason;
            PostEvent("failed", reason);
            return result;
        }

        StartSnapshot start;
        if (!ResolveStartSnapshot(*file, sourcePath, options, start, result))
        {
            PostEvent("failed", result.message);
            return result;
        }

        const CONFIG& config = _context->config;
        if (!MachineMatches(start.machine, config.mem_model, config.ramsize))
        {
            result.error = PlayError::ModelMismatch;
            result.requiredModel = ModelShortName(start.machine.model);
            result.requiredRamKb = start.machine.ramKb;
            result.message = "the recording was made on a " + MachineName(start.machine.model, start.machine.ramKb) +
                             " (" + start.machine.description + "); this machine is a " +
                             MachineName(config.mem_model, config.ramsize) + ": switch the model first";
            PostEvent("failed", result.message);
            return result;
        }

        for (const InputBlock& input : file->inputs)
        {
            if (input.protectedFrames)
            {
                result.error = PlayError::BadFile;
                result.message = "the recording has protected (encrypted) input frames";
                PostEvent("failed", result.message);
                return result;
            }
        }

        // The machine stays still from the snapshot load to the hooks: no
        // instruction may run live in between
        const bool wasRunning = _emulator.IsRunning() && !_emulator.IsPaused();
        if (wasRunning)
        {
            _emulator.Pause(false);
            _emulator.WaitForPauseConfirmation(1000);
        }

        // A previous playback ends first (its player may still be installed)
        Uninstall();

        if (!LoadStartSnapshot(start, sourcePath, result))
        {
            if (wasRunning)
                _emulator.Resume(false);
            PostEvent("failed", result.message);
            return result;
        }

        auto player = std::make_unique<RzxPlayer>(file, options);
        std::string error;
        if (!player->Start(error))
        {
            result.error = PlayError::BadFile;
            result.message = error;
            if (wasRunning)
                _emulator.Resume(false);
            PostEvent("failed", error);
            return result;
        }
        player->onEnded = [this](RzxPlayer& ended) { OnPlayerEnded(ended); };
        player->captureState = [this](std::vector<uint8_t>& out) { return CaptureState(out); };
        player->applySnapshot = [this](const Snapshot& snapshot, uint32_t tstates, std::string& error) {
            return ApplyRecordedSnapshot(snapshot, tstates, error);
        };

        // The first frame starts where the recording's T-state counter says,
        // counted from its INT (Fuse writes it; SkoolKit ignores it). The CPU
        // path does not depend on it; the raster alignment does
        const InputBlock& first = file->inputs.front();
        Z80& cpu = *_context->pCore->GetZ80();
        const uint32_t frameLength = _context->config.frame;
        if (frameLength > 0 && first.tstates < frameLength)
        {
            cpu.t = LoaderSZX::FramePositionFromIntCount(_context, first.tstates);
            _emulator.RestartFrame();
        }

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _player = std::move(player);
            _file = file;
            _path = sourcePath;
            _snapshotDescription = start.machine.description;
            _options = options;
        }
        Install();

        // Keyframe 0: the start, so a seek can always go back to the beginning
        _player->MaybeKeyframe();

        PostEvent("started", _file->hasCreator ? "recorded with " + _file->creator.name : std::string());
        if (wasRunning)
            _emulator.Resume(false);
        return result;
    }

    bool RzxSession::LoadStartSnapshot(const StartSnapshot& snapshot, const std::string& sourcePath,
                                       PlayResult& result)
    {
        // From memory, reported as the RZX file it came from
        if (!_emulator.LoadSnapshotData(snapshot.data, snapshot.extension, sourcePath))
        {
            result.error = PlayError::SnapshotLoadFailed;
            result.message = "the start snapshot (" + snapshot.extension + ") did not load";
            return false;
        }
        return true;
    }

    bool RzxSession::ApplyRecordedSnapshot(const Snapshot& snapshot, uint32_t tstates, std::string& error)
    {
        // Emulation thread, at the RZX frame boundary before the next input
        // block (design §7, RZ-F9): the machine is replaced as the recording
        // says, the playback goes on (frame numbering, keyframes, hooks kept)
        std::string extension;
        std::vector<uint8_t> bytes;
        if (!SnapshotBytes(snapshot, _path, extension, bytes, error))
            return false;
        SnapshotMachine machine;
        if (!DetectSnapshotMachine(extension, bytes, machine, error))
            return false;
        const CONFIG& config = _context->config;
        if (!MachineMatches(machine, config.mem_model, config.ramsize))
        {
            error = "the recording goes on on another machine (" + machine.description + ")";
            return false;
        }

        // A snapshot replaces the machine: not while TTD records, a stopped
        // history is dropped (as for any snapshot load)
        ttd::TimeTravelManager* ttd = _context->pTimeTravelManager;
        if (ttd && ttd->IsRecording())
        {
            error = "a TTD recording runs: the recording's snapshot block cannot be applied";
            return false;
        }
        if (ttd)
            ttd->InvalidateSession("rzx-snapshot");

        if (!_emulator.ApplySnapshotData(bytes, extension, error))
            return false;

        // The loaders reset the CPU: the playback keeps the frame INT masked
        Z80& cpu = *_context->pCore->GetZ80();
        cpu.frameIntMasked = true;
        cpu.int_pending = false;
        if (_context->config.frame > 0 && tstates < _context->config.frame)
            cpu.t = LoaderSZX::FramePositionFromIntCount(_context, tstates);
        _emulator.RestartFrame();

        PostEvent("snapshot", extension + " snapshot applied (" + machine.description + ")");
        return true;
    }

    bool RzxSession::Stop(const std::string& reason)
    {
        if (!IsActive())
            return false;

        const bool wasRunning = _emulator.IsRunning() && !_emulator.IsPaused();
        if (wasRunning)
        {
            _emulator.Pause(false);
            _emulator.WaitForPauseConfirmation(1000);
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_player)
            {
                _player->Stop(reason);
                _player->ClearKeyframes();  // no seeking after a stop: the memory goes
            }
        }
        Uninstall();
        if (ttd::TimeTravelManager* ttd = _context->pTimeTravelManager)
            ttd->NoteReplaySource(ttd::TTDReplaySource::LiveInput);
        PostEvent("stopped", reason);
        if (wasRunning)
            _emulator.Resume(false);
        return true;
    }

    bool RzxSession::CaptureState(std::vector<uint8_t>& out)
    {
        szx::Stage stage;
        std::string error;
        if (!LoaderSZX::Capture(_context, stage, error))
            return false;
        out = SzxWriter::Write(stage);
        return !out.empty();
    }

    bool RzxSession::RestoreState(const std::vector<uint8_t>& state, std::string& error)
    {
        szx::Stage stage;
        if (!SzxReader::Parse(state.data(), state.size(), stage, error))
            return false;
        szx::Report report;
        if (!LoaderSZX::Commit(_context, stage, report, error))
            return false;
        _emulator.RestartFrame();
        return true;
    }

    bool RzxSession::Seek(uint64_t frame, std::string& error)
    {
        RzxPlayer* player = nullptr;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            player = _player.get();
        }
        if (!player)
        {
            error = "no RZX recording played on this machine";
            return false;
        }
        const uint64_t total = player->Status().totalFrames;
        if (frame > total)
            frame = total;

        // A seek back replaces the machine like a snapshot load: refused while
        // TTD records, a stopped TTD history is dropped
        ttd::TimeTravelManager* ttd = _context->pTimeTravelManager;
        if (ttd && ttd->IsRecording())
        {
            error = "a TTD recording runs: stop it before seeking the RZX playback";
            return false;
        }

        const bool wasRunning = _emulator.IsRunning() && !_emulator.IsPaused();
        if (wasRunning)
        {
            _emulator.Pause(false);
            _emulator.WaitForPauseConfirmation(1000);
        }

        bool ok = true;
        const bool forward = player->IsPlaying() && frame >= player->FramesDone();
        if (!forward)
        {
            // The latest keyframe that leaves frame `frame` to be played: one
            // taken at a boundary holds the frame before it done but not ended
            const Keyframe* keyframe = player->Keyframes().AtOrBefore(frame == 0 ? 0 : frame - 1);
            if (!keyframe)
            {
                error = "no keyframe to seek back from (keyframes are off)";
                ok = false;
            }
            else
            {
                if (ttd)
                    ttd->InvalidateSession("rzx-seek");
                const Keyframe copy = *keyframe;  // the store may thin while playing on
                Uninstall();
                ok = RestoreState(copy.state, error) && player->SeekCursor(copy.cursor);
                if (ok)
                    Install();
                else if (error.empty())
                    error = "the keyframe did not restore";
            }
        }

        // Play on to the target (at full speed, on this thread, the machine paused)
        if (ok && frame > player->FramesDone())
        {
            EmulatorContext* context = _context;
            _emulator.RunUntilCondition(
                [context, player, frame](const Z80State&) {
                    return context->rzxPlayer == nullptr || player->FramesDone() >= frame;
                },
                0, false);
        }

        if (ok)
            PostEvent("seek", "frame " + std::to_string(player->FramesDone()));
        if (wasRunning)
            _emulator.Resume(false);
        return ok;
    }

    bool RzxSession::SaveTtdState(RzxPlayer::SavedState& out, uint64_t& fingerprint) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_player)
            return false;
        out = _player->Save();
        fingerprint = _player->Fingerprint();
        return true;
    }

    bool RzxSession::RestoreTtdState(const RzxPlayer::SavedState& saved, uint64_t fingerprint)
    {
        // The control thread, the machine paused (a seek); the CPU is already restored
        RzxPlayer* player = nullptr;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            player = _player.get();
        }
        if (!player || player->Fingerprint() != fingerprint || !player->Restore(saved))
            return false;
        if (saved.state == PlayerState::Playing)
            Install();
        else
            Uninstall();
        return true;
    }

    bool RzxSession::IsActive() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _installed;
    }

    SessionStatus RzxSession::Status() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        SessionStatus status;
        status.active = _installed;
        status.loaded = _player != nullptr;
        status.path = _path;
        status.options = _options;
        status.snapshot = _snapshotDescription;
        if (_file)
        {
            status.version = _file->VersionText();
            if (_file->hasCreator)
                status.creator = _file->creator.name + " " + std::to_string(_file->creator.major) + "." +
                                 std::to_string(_file->creator.minor);
        }
        if (_player)
            status.player = _player->Status();
        return status;
    }

    void RzxSession::Install()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_installed || !_player)
            return;

        Z80& cpu = *_context->pCore->GetZ80();

        // The recorded schedule replaces the machine's frame INT (design §5)
        _savedFrameIntMasked = cpu.frameIntMasked;
        cpu.frameIntMasked = true;
        cpu.int_pending = false;

        // Shortcuts that change the CPU path: the disk autostart rewrite is
        // disarmed here; fast tape / turbo tape / fast disk read as off while
        // rzxPlayer is set (FeatureManager)
        if (_context->pDiskAutostart)
            _context->pDiskAutostart->Disarm();

        _context->rzxPlayer = _player.get();
        _context->SetStepWork(EmulatorContext::kStepWorkRzx, true);
        _installed = true;
    }

    void RzxSession::Uninstall()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_installed)
            return;

        _context->SetStepWork(EmulatorContext::kStepWorkRzx, false);
        _context->rzxPlayer = nullptr;
        if (_context->pCore && _context->pCore->GetZ80())
            _context->pCore->GetZ80()->frameIntMasked = _savedFrameIntMasked;
        _installed = false;
    }

    void RzxSession::OnPlayerEnded(RzxPlayer& player)
    {
        // Emulation thread, at the end of a step: the machine continues live
        const PlayerStatus status = player.Status();
        Uninstall();
        if (ttd::TimeTravelManager* ttd = _context->pTimeTravelManager)
            ttd->NoteReplaySource(ttd::TTDReplaySource::LiveInput);

        switch (status.state)
        {
            case PlayerState::Finished:
                PostEvent("finished", std::to_string(status.frame) + " frames played");
                break;
            case PlayerState::Desynced:
                PostEvent("desync", status.stopReason);
                break;
            default:
                PostEvent("stopped", status.stopReason);
                break;
        }
    }

    void RzxSession::PostEvent(const std::string& event, const std::string& message)
    {
        PlayerStatus status;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_player)
                status = _player->Status();
        }
        MessageCenter::DefaultMessageCenter().Post(
            NC_RZX_PLAYBACK, new RzxPlaybackPayload(_emulator.GetId(), event, message, status.frame,
                                                    status.totalFrames, _path));
    }
}  // namespace rzx
