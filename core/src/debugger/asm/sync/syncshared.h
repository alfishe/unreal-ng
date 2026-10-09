#pragma once

/// @file syncshared.h
/// @brief What SyncControl (on request) and AsmSyncService (the watch) both do with the machine: copy the memory a
/// reader needs at a coherent moment, describe a text, write it out as text, the assembler's file or a dialect.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "debugger/asm/asmcontrol.h"
#include "unrealasm/sync/reader.h"

class EmulatorContext;

namespace asmsync
{
/// RAM pages copied at one coherent moment, with the window map, and a view over them
struct MachineCopy
{
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> pages;
    unrealasm::sync::MachineView view;
};

/// Every RAM page (`only` = nullptr), or the pages the CPU sees plus `only`; false with the reason
bool CopyMachine(EmulatorContext* context, MachineCopy& out, std::string& error, const std::vector<int>* only = nullptr);

StateNode DiagnosticsValue(const unrealasm::Diagnostics& diagnostics);

/// The status fields of a text: assembler, title, format, version, family, state, name, page, bytes, lines,
/// current_line, editor, typing, changed, diagnostics
void Describe(const unrealasm::sync::SyncDescriptor& descriptor, const unrealasm::sync::SyncText& text, StateNode& body);

/// The live file as `as` (text, file, dialect with `to`): written to `output` when given, else in the reply (text, or
/// the file as base64 in `data`)
AsmReply Render(EmulatorContext* context, const unrealasm::sync::SyncDescriptor& descriptor, const std::vector<uint8_t>& file,
                const std::string& name, const std::string& as, const std::string& to, const std::string& codepage,
                const std::string& output);
}  // namespace asmsync
