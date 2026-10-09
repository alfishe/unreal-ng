#pragma once

// A watch over the source an assembler holds in RAM (asm-synchronizer.md §4-§5, phase Y1): the caller copies the
// memory now and then and feeds it in; the session finds the assembler, reads its text, notices a change, waits for a
// pause in typing and then hands out the text for a build. The build (decode, sjasmplus conversion, layout) gives the
// labels with their values and the hints on the source's lines. No threads and no clock of its own: the caller gives
// the time and decides where a build runs, so the emulator runs it on a worker and a test runs it in line.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "unrealasm/diagnostics.h"
#include "unrealasm/document.h"
#include "unrealasm/symbols/symbol.h"
#include "unrealasm/sync/reader.h"

namespace unrealasm::sync
{
struct SessionOptions
{
    std::string assembler;          ///< a descriptor id; "" = the one the probe finds (alone)
    uint32_t quietMs = 500;         ///< a changed text is built once it has stayed the same this long
};

enum class TickEvent : uint8_t
{
    None,          ///< nothing new
    Found,         ///< the assembler was found (first tick, or back after it was gone)
    Changed,       ///< the text is not what it was at the last tick
    Lost,          ///< no assembler identifies any more (the guest left it, or another program runs)
    Ambiguous,     ///< two assemblers identify alike: the options must name one
    Unreadable,    ///< identified, but the text does not read at this moment (pointers mid-update)
};

struct TickResult
{
    TickEvent event = TickEvent::None;
    SyncText text;                  ///< the text as read at this tick
};

/// What a build needs: the text as the assembler would save it, and which assembler
struct BuildInput
{
    const SyncDescriptor* descriptor = nullptr;
    std::vector<uint8_t> file;
    std::string name;               ///< the text's name when the layout keeps one
    uint64_t generation = 0;        ///< counts the builds handed out; a result older than the last one is stale
};

struct BuildResult
{
    uint64_t generation = 0;
    bool decoded = false;           ///< the codec read the file
    bool complete = false;          ///< every label got its value
    SourceDocument document;        ///< the decoded text
    symbols::SymbolSet labels;      ///< the labels the source defines, with values, kinds and source lines
    Diagnostics hints;              ///< the codec's, the conversion's and the layout's messages, on the source's lines
};

class SyncSession
{
public:
    explicit SyncSession(SessionOptions options = {});

    /// One look at the memory (a copy the caller made at a coherent moment) at time `nowMs`
    TickResult Tick(const MachineView& machine, uint64_t nowMs);

    /// The text changed and then stayed the same for quietMs: take it for a build (the change is no longer pending)
    std::optional<BuildInput> TakeBuild(uint64_t nowMs);

    /// The build itself: pure, may run on any thread
    static BuildResult Build(const BuildInput& input);

    /// The caller may copy only the pages a tick reads: all of them until the text has been read once, then the
    /// pages the CPU sees plus these
    bool NeedsAllPages() const;
    std::vector<int> TextPages() const;

    const SyncDescriptor* Descriptor() const { return _descriptor; }
    const SyncText& Last() const { return _last; }
    bool Pending() const { return _pending; }
    uint64_t Generation() const { return _generation; }
    const SessionOptions& Options() const { return _options; }

private:
    SessionOptions _options;
    const SyncDescriptor* _descriptor = nullptr;
    SyncText _last;
    uint64_t _hash = 0;
    bool _haveHash = false;
    bool _pending = false;
    uint64_t _changedAt = 0;
    uint64_t _generation = 0;
};
}  // namespace unrealasm::sync
