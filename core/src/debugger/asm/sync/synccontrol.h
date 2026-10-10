#pragma once

/// @file synccontrol.h
/// @brief The asm-synchronizer's one layer for every surface (asm-synchronizer.md §8): the source an assembler running
/// in the machine holds in RAM, read as the file its own SAVE would write. WebAPI, CLI, MCP, Lua and Python reach it
/// through AsmControl's `sync-*` verbs, with AsmControl's reply shape.
///
/// Verbs:
///   sync-status  [assembler snapshot]                          the assembler found, its text, the editor state, the watch
///   sync-probe   [snapshot]                                    every descriptor that identifies, best first
///   sync-extract [assembler as=text|file|dialect to codepage output snapshot]   the text, the live file, or another dialect
///   sync-watch   [assembler interval quiet as to output]       start the watch (AsmSyncService): live labels, hints
///   sync-unwatch                                               stop it
///   sync-hints                                                 the last build: labels, hints
///   sync-inject  [assembler text|path|data name codec version file from title force backup]
///                part B: a source (any dialect with a frontend) written as the assembler's own text over the one in
///                RAM, while it waits for a command (its idle rule); refused when the text there has unsaved changes
///                unless `force`; `backup` keeps the replaced file on the host
///
/// `snapshot` (a .sna / .z80 / .szx path, Z1): the verb reads that file's memory instead of the machine's; sync-inject
/// then writes the snapshot with the text's page replaced to `output` (the machine is not touched).
///
/// `assembler` picks a descriptor ("alasm-5.09", "tasm-4.12") when the probe finds two. The memory is copied at a
/// coherent moment; only sync-inject writes the guest, at one coherent moment, as a TTD tool edit.

#include "debugger/asm/asmcontrol.h"

class EmulatorContext;

namespace unrealasm::sync
{
struct SyncDescriptor;
struct SyncText;
}  // namespace unrealasm::sync
namespace unrealasm::containers
{
struct Snapshot;
}  // namespace unrealasm::containers

class SyncControl
{
public:
    explicit SyncControl(EmulatorContext* context);

    /// verb: "status", "probe", "extract", "watch", "unwatch", "hints" or "inject" (AsmControl strips the "sync-")
    AsmReply Execute(const std::string& verb, const AsmRequest& request);

    static const std::vector<std::string>& OptionsFor(const std::string& verb);

private:
    /// `snapshot`: into that snapshot file's memory (written to the request's `output`), else into the running machine
    AsmReply Inject(const unrealasm::sync::SyncDescriptor& descriptor, const unrealasm::sync::SyncText& current, const AsmRequest& request,
                    const unrealasm::containers::Snapshot* snapshot);

    EmulatorContext* _context = nullptr;
};
