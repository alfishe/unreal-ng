#pragma once

/// @file ttddumpformat.h
/// @brief The constants left from the deleted .ttd v1 format.
///
/// v1, its writer, its reader and its schema (ttd.ksy before 2026-10-30) were
/// deleted in Phase 6. What is still read: the magic both formats start with,
/// the schema numbers that identify a v1 file (refused with a reason, see
/// IsReadableSchema) and the "ROM not read" signature.

#include <cstdint>

namespace ttd::dump {

/// 4-byte magic at the head of every .ttd file. ASCII "TTDD".
constexpr char kMagic[4] = {'T', 'T', 'D', 'D'};

/// The last schema number of the v1 format (its masks as word lists)
constexpr uint16_t kSchemaVersion = 3;

/// The first schema number of the v1 format (u64 masks)
constexpr uint16_t kSchemaVersionFirst = 1;

/// A schema number of a v1 file: 1 (older files) and 3; such a file is
/// refused with a reason (2 is the engine container, read by another path)
constexpr bool IsReadableSchema(uint16_t version) { return version == kSchemaVersionFirst || version == kSchemaVersion; }

/// Sentinel rom_signature meaning "the writer could not read the ROM region"
/// (no Memory attached). A file carrying it skips the compatibility check
/// rather than failing every load.
constexpr uint64_t kRomSignatureUnknown = 0;

} // namespace ttd::dump
