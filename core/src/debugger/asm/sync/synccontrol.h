#pragma once

/// @file synccontrol.h
/// @brief The asm-synchronizer's one layer for every surface (asm-synchronizer.md §8): the source an assembler running
/// in the machine holds in RAM, read as the file its own SAVE would write. WebAPI, CLI, MCP, Lua and Python reach it
/// through AsmControl's `sync-*` verbs, with AsmControl's reply shape.
///
/// Verbs (phase Y0: on request, nothing runs in the background):
///   sync-status  [assembler]                                   the assembler found, its text, the editor state
///   sync-probe                                                 every descriptor that identifies, best first
///   sync-extract [assembler as=text|file|dialect to codepage output]   the text, the live file, or another dialect
///
/// `assembler` picks a descriptor ("alasm-5.09", "tasm-4.12") when the probe finds two. The memory is copied at a
/// coherent moment; the guest is never written.

#include "debugger/asm/asmcontrol.h"

class EmulatorContext;

class SyncControl
{
public:
    explicit SyncControl(EmulatorContext* context);

    /// verb: "status", "probe" or "extract" (AsmControl strips the "sync-")
    AsmReply Execute(const std::string& verb, const AsmRequest& request);

    static const std::vector<std::string>& OptionsFor(const std::string& verb);

private:
    EmulatorContext* _context = nullptr;
};
