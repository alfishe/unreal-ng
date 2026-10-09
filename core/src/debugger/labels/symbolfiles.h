#pragma once

/// @file symbolfiles.h
/// @brief What LabelManager::ImportSymbols / ExportSymbols take and report (the surfaces' import / export,
/// symbols/tdd.md section 8). Apart from labelmanager.h, so the automation targets that only show labels need no
/// unreal-asm headers.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "unrealasm/diagnostics.h"
#include "unrealasm/symbols/codec.h"
#include "unrealasm/symbols/store.h"

struct SymbolImportRequest
{
    std::string format;                                      // a codec id; "" = by the extension, else detected
    std::string set;                                         // "" = the file's own set (replaced on a reload)
    std::optional<unrealasm::symbols::AddressSpace> space;   // records without a space of their own go here
    uint32_t base = 0;                                       // added to every offset
    std::optional<unrealasm::symbols::MergePolicy> policy;   // merging into a set (DT-2); default "both"
};
struct SymbolImportResult
{
    bool ok = false;
    std::string message;                     // why it failed
    std::string format;                      // the codec that read it
    int score = 0;                           // its detection score (100 when given)
    size_t records = 0;                      // what the file holds
    unrealasm::symbols::ImportReport report; // set, added, aliased, updated, skipped, conflicts, diagnostics
};
struct SymbolExportRequest
{
    std::string format;                      // a codec id; "" = by the extension
    std::vector<std::string> sets;           // empty = the labels as they show (every enabled set)
    unrealasm::symbols::Unrepresentable pages = unrealasm::symbols::Unrepresentable::Fold;
};
struct SymbolExportResult
{
    bool ok = false;
    std::string message;
    std::string format;
    size_t written = 0;
    unrealasm::Diagnostics diagnostics;
};
