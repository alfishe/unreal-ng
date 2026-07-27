#pragma once

/// @file ttd_dump_format.h
/// @brief C++ constants and helpers for the .ttd binary format.
///
/// Mirrors the canonical Kaitai Struct schema in ttd.ksy. The schema is the
/// single source of truth — this header exists only to give the C++ writer
/// (TimeTravelManager::SerializeSession) symbolic names for magic / version /
/// sentinels without hand-typing integer literals.
///
/// Compatibility contract (see ttd.ksy doc for the full version):
///   - kSchemaVersion here MUST equal meta.schema-version in ttd.ksy
///   - Both are bumped atomically in one commit on any breaking change
///   - Additive-only evolution within a major version
///
/// The .ksy is git-tagged `ttd-schema-vN` on every breaking change so
/// third-party parsers can pin to a known schema version.

#include <cstdint>

namespace ttd::dump {

/// 4-byte magic at the head of every .ttd file. ASCII "TTDD".
constexpr char kMagic[4] = {'T', 'T', 'D', 'D'};

/// Schema version. Bumped on any breaking format change. Must match
/// `meta.schema-version` in ttd.ksy.
constexpr uint16_t kSchemaVersion = 1;

/// Bit 0 of header.flags — set when the writer is little-endian (always 1
/// in v1; we static_assert against little-endian in the writer).
constexpr uint16_t kFlagsLittleEndian = 0x0001;

/// Sentinel page-store reference meaning "this page was never written during
/// the session up to this checkpoint; live RAM content IS the historical
/// content". Matches TTDPageRef::kNeverTouched in ttd_checkpoint.h.
constexpr uint32_t kNeverTouchedPageRef = 0xFFFFFFFFu;

/// Fixed RAM page size (16 KB). Matches TTDPageStore::kPageSize and
/// emulator/platform.h PAGE_SIZE.
constexpr uint32_t kPageSize = 16384u;

/// A reader that encounters a schema_version field higher than the maximum
/// it supports MUST refuse to parse and surface a clear error. Use this
/// constant in the error message ("file is schema vN, this reader supports
/// up to vM").
constexpr uint16_t kMaxSupportedSchemaVersion = kSchemaVersion;

} // namespace ttd::dump
