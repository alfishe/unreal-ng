#pragma once

/// @file asmsyncservice.h
/// @brief The asm-synchronizer's watch in one emulator instance (asm-synchronizer.md §4, phase Y1): the adapter
/// between the machine and unreal-asm's SyncSession. While a watch is on, a worker thread copies the pages the text
/// lives in at a coherent moment every `interval` ms, feeds them to the session, builds a text that stayed the same
/// for `quiet` ms, and publishes the result: the labels as the symbol set `live:sync:<assembler>`, the hints, an
/// optional output file, and NC_ASM_SYNC. With no watch there is no thread and no cost. Owned by DebugManager;
/// SyncControl is the surface.
///
/// The push watch (part B, Z2) goes the other way: a host source and the files it INCLUDEs are looked at every
/// `interval` ms; a change that stayed `quiet` ms is written into the assembler by sync-inject once the assembler waits
/// for a command (retried while it is busy). A text in memory the watch itself wrote is replaced; one the user edited
/// in the guest since is a conflict and stays, unless `force`. Its own thread, independent of the watch.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "emulator/state/statenode.h"

class EmulatorContext;
namespace unrealasm::sync
{
struct BuildInput;
}

class AsmSyncService
{
public:
    struct WatchOptions
    {
        std::string assembler;          ///< a descriptor id; "" = the one the probe finds
        uint32_t intervalMs = 250;      ///< how often the text is looked at
        uint32_t quietMs = 500;         ///< how long a change must stay before a build
        std::string output;             ///< written after each build: a host file or disk:A/NAME.T ("" = none)
        std::string as = "text";        ///< the output: text, file (the assembler's format) or dialect
        std::string to;                 ///< as = dialect: the target dialect
        /// Where the sources the text INCLUDEs come from (asm-synchronizer.md §5.4): the disk in a drive (0-3; -1 =
        /// none) and a host folder; the disk wins over the folder when both have a name
        int projectDrive = 0;
        std::string projectDir;
    };

    /// The symbol set priority: above every loaded file, below the user's own labels
    static constexpr int kLabelPriority = 900000;

    explicit AsmSyncService(EmulatorContext* context);
    ~AsmSyncService();

    AsmSyncService(const AsmSyncService&) = delete;
    AsmSyncService& operator=(const AsmSyncService&) = delete;

    struct PushOptions
    {
        std::string path;               ///< the host source
        std::string from = "sjasmplus"; ///< its dialect
        std::string assembler;          ///< a descriptor id; "" = the one the probe finds
        std::string title;              ///< the text's name ("" = the name of the text it replaces)
        bool force = false;             ///< overwrite the user's own changes in the guest too
        std::string drive;              ///< the drive taking the sources an assembler INCLUDEs from its disk ("" = A)
        uint32_t intervalMs = 500;
        uint32_t quietMs = 300;         ///< a change must stay this long before it is written (an editor's save in parts)
    };

    /// Starts a watch, replacing a running one
    void Start(const WatchOptions& options);
    /// Stops the watch; the live symbol set stays (drop it with `symbols drop`)
    void Stop();
    bool Watching() const;

    /// {watching, options, state, assembler, ticks, builds, last: {...}, error}
    StateNode StatusValue() const;
    /// {generation, decoded, complete, labels, set, hints: [{severity, line, message}], output}
    StateNode HintsValue() const;
    /// The text of the last build that decoded (UTF-8, one line per source line) and its build number (0 = none yet)
    std::string LastText(uint64_t& generation) const;

    /// Starts the push watch, replacing a running one; the first write comes as soon as the assembler is idle
    void StartPush(const PushOptions& options);
    void StopPush();
    bool Pushing() const;
    /// {pushing, options, state (off, waiting, pending, pushed, conflict, error), files, pushes, last: inject reply, error}
    StateNode PushStatusValue() const;

private:
    void RunPush(PushOptions options);
    void Run(WatchOptions options);
    /// The rest of the project for one build: the folder's sources and file sizes, then the disk's
    void GatherProject(const WatchOptions& options, unrealasm::sync::BuildInput& input, std::string& note) const;

    EmulatorContext* _context = nullptr;
    std::thread _thread;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    bool _stop = false;
    bool _watching = false;

    // Shown by StatusValue / HintsValue (under _mutex)
    WatchOptions _options;
    std::string _state = "off";
    std::string _assembler;
    uint64_t _ticks = 0;
    uint64_t _builds = 0;
    StateNode _text = StateNode::Object();
    StateNode _hints = StateNode::Object();
    std::string _lastText;
    uint64_t _lastTextGeneration = 0;
    std::string _error;

    // The push watch (under _mutex; _pushWake shares it)
    std::thread _pushThread;
    std::condition_variable _pushWake;
    bool _pushStop = false;
    bool _pushing = false;
    PushOptions _push;
    std::string _pushState = "off";
    uint64_t _pushes = 0;
    size_t _pushFiles = 0;
    StateNode _pushLast = StateNode::Object();
    std::string _pushError;
};
