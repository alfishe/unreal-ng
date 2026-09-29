#pragma once

/// @file rzxsession.h
/// @brief RZX playback on one emulator (design §7-§10): checks the recording
/// against the machine, loads the start snapshot, installs the player's hooks
/// (IN substitution, the per-step gate, the frame-INT mask), holds the
/// shortcut and live-input locks, and removes everything when playback ends.
///
/// Owned by Emulator (PlayRzx / StopRzx / GetRzxStatus). Play and Stop may be
/// called from any thread: they pause the machine around the change, as a
/// snapshot load does. The end of playback (last frame, strict desync) is
/// handled on the emulation thread.
///
/// Worked example: Play("game.rzx") on a 48K with a recording whose start
/// snapshot is a Z80 v3 of a 128K fails with ModelMismatch and
/// requiredModel "128k"; the caller switches the model (RzxLauncher) and
/// plays again on the new machine.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "emulator/rzx/rzxplayer.h"
#include "loaders/rzx/rzxformat.h"
#include "loaders/rzx/rzxsnapshot.h"

class Emulator;
class EmulatorContext;

namespace rzx
{
    enum class PlayError : uint8_t
    {
        None,
        BadFile,             ///< not a readable RZX file
        NoSnapshot,          ///< no start snapshot (embedded or found on disk)
        UnsupportedMachine,  ///< the snapshot's machine, or this machine, cannot play RZX
        ModelMismatch,       ///< the snapshot needs another model: requiredModel
        SnapshotLoadFailed,
        Refused,             ///< the machine cannot take a snapshot now (TTD recording, shutting down)
    };

    const char* PlayErrorName(PlayError error);

    struct PlayResult
    {
        PlayError error = PlayError::None;
        std::string message;
        std::string requiredModel;  ///< ModelMismatch: the model short name (Config::mem_model)
        uint32_t requiredRamKb = 0;

        bool Ok() const
        {
            return error == PlayError::None;
        }
    };

    /// Playback state for every surface
    struct SessionStatus
    {
        bool active = false;      ///< the player's hooks are installed
        bool loaded = false;      ///< a recording was played on this machine (status kept after the end)
        std::string path;         ///< the RZX file, or the name given with the data
        std::string creator;      ///< "Spectaculator 80.3092"
        std::string version;      ///< "0.12"
        std::string snapshot;     ///< "Z80 v3, hardware 4"
        PlayerOptions options;
        PlayerStatus player;
    };

    class RzxSession
    {
    public:
        explicit RzxSession(Emulator& emulator);
        ~RzxSession();

        RzxSession(const RzxSession&) = delete;
        RzxSession& operator=(const RzxSession&) = delete;

        /// Parse the file and play it from its start snapshot
        PlayResult PlayFile(const std::string& path, const PlayerOptions& options);
        /// Play a parsed recording; `sourcePath` locates an external start snapshot
        PlayResult Play(std::shared_ptr<const File> file, const std::string& sourcePath,
                        const PlayerOptions& options);
        /// Stop playing; the machine continues live. False when nothing plays
        bool Stop(const std::string& reason = "stopped");

        bool IsActive() const;
        SessionStatus Status() const;

        /// The start snapshot a recording plays from (SkoolKit rule: the last
        /// snapshot before the first input block, or the first one with
        /// ignoreLaterSnapshots), with its bytes and extension resolved
        struct StartSnapshot
        {
            std::string extension;
            std::vector<uint8_t> data;
            SnapshotMachine machine;
        };
        static bool ResolveStartSnapshot(const File& file, const std::string& sourcePath,
                                         const PlayerOptions& options, StartSnapshot& snapshot, PlayResult& result);

        /// Whether a machine can replay recordings at all: the ULA frame INT
        /// and IN-only input (requirements §2)
        static bool MachineSupported(EmulatorContext* context, std::string& reason);

    private:
        bool LoadStartSnapshot(const StartSnapshot& snapshot, const std::string& sourcePath, PlayResult& result);
        void Install();
        /// Remove the hooks and restore the machine; idempotent
        void Uninstall();
        /// Emulation thread: the player left Playing
        void OnPlayerEnded(RzxPlayer& player);
        void PostEvent(const std::string& event, const std::string& message);

        Emulator& _emulator;
        EmulatorContext* _context;

        mutable std::mutex _mutex;  ///< guards the fields below against Status() from other threads
        std::unique_ptr<RzxPlayer> _player;
        std::shared_ptr<const File> _file;
        std::string _path;
        std::string _snapshotDescription;
        PlayerOptions _options;
        bool _installed = false;
        bool _savedFrameIntMasked = false;
    };
}  // namespace rzx
