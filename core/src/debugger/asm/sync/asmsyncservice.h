#pragma once

/// @file asmsyncservice.h
/// @brief The asm-synchronizer's watch in one emulator instance (asm-synchronizer.md §4, phase Y1): the adapter
/// between the machine and unreal-asm's SyncSession. While a watch is on, a worker thread copies the pages the text
/// lives in at a coherent moment every `interval` ms, feeds them to the session, builds a text that stayed the same
/// for `quiet` ms, and publishes the result: the labels as the symbol set `live:sync:<assembler>`, the hints, an
/// optional output file, and NC_ASM_SYNC. With no watch there is no thread and no cost. Owned by DebugManager;
/// SyncControl is the surface.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "emulator/state/statenode.h"

class EmulatorContext;

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
    };

    /// The symbol set priority: above every loaded file, below the user's own labels
    static constexpr int kLabelPriority = 900000;

    explicit AsmSyncService(EmulatorContext* context);
    ~AsmSyncService();

    AsmSyncService(const AsmSyncService&) = delete;
    AsmSyncService& operator=(const AsmSyncService&) = delete;

    /// Starts a watch, replacing a running one
    void Start(const WatchOptions& options);
    /// Stops the watch; the live symbol set stays (drop it with `symbols drop`)
    void Stop();
    bool Watching() const;

    /// {watching, options, state, assembler, ticks, builds, last: {...}, error}
    StateNode StatusValue() const;
    /// {generation, decoded, complete, labels, set, hints: [{severity, line, message}], output}
    StateNode HintsValue() const;

private:
    void Run(WatchOptions options);

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
    std::string _error;
};
