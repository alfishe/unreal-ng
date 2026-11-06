#pragma once

/// @file memoryprofile.h
/// @brief The `memory` section of a software profile (docs/inprogress/2026-11-05-software-metadata-profile): what
/// the program's memory holds (regions of a kind), its labels, label files, entry points and comments. Parsed and
/// validated here; the debugger's memory markup lays it over the machine (a later step).

#include <cstdint>
#include <string>
#include <vector>

#include "profiles/json.h"

namespace controllers
{

/// Where a place lives: the CPU's 64K as mapped at the time, or a page (RAM / ROM page number, as the haptic probes'
/// `page:<n>:<addr>`)
enum class MemorySpace : uint8_t
{
    Cpu,
    Ram,
    Rom,
};

/// An address or an inclusive range. Page places keep offsets inside the page (0x0000-0x3FFF) and never cross it
struct MemoryPlace
{
    MemorySpace space = MemorySpace::Cpu;
    uint16_t page = 0;
    uint16_t lo = 0;
    uint16_t hi = 0;
};

struct MemoryRegionSpec
{
    MemoryPlace place;
    std::string kind;    ///< an analysis region kind name (l2model.cpp: code, data, text, font, sprite, ...)
    std::string title;
    std::string sha256;  ///< the bytes' SHA-256 (lower-case hex) when given: the region holds only while they match
    uint16_t confidence = 1000;  ///< 0-1000, as the analysis claims
    JsonValue geometry;          ///< as written (an object), or null
};

struct MemoryLabelSpec
{
    std::string name;
    MemoryPlace place;  ///< one address (lo == hi)
    std::string comment;
};

struct MemoryCommentSpec
{
    MemoryPlace place;  ///< one address
    std::string text;
};

struct MemoryProfile
{
    std::vector<MemoryRegionSpec> regions;
    std::vector<MemoryLabelSpec> labels;
    std::vector<std::string> symbols;  ///< label files next to the profile file (relative paths)
    std::vector<MemoryLabelSpec> entries;
    std::vector<MemoryCommentSpec> comments;
};

/// The region kinds a profile may name (the analysis' RegionKind names, l2model.cpp kKindNames)
const std::vector<std::string>& MemoryRegionKinds();

/// A place as written: `0x6A00`, `#6A00`, `$6A00`, `27136`, a range `lo-hi`, a page place `page:<n>:<addr>[-<addr>]`
/// or `rom:<n>:<addr>[-<addr>]` (offsets taken inside the page, as the haptic probes do); a number is a CPU address.
/// @p range allows `lo-hi`; false with the reason
bool ParseMemoryPlace(const JsonValue& value, bool range, MemoryPlace& out, std::string& error);

/// Parse and validate a `memory` section; false with the reason, which names the place (`memory.regions[2]: ...`)
bool ParseMemoryProfile(const JsonValue& section, MemoryProfile& out, std::string& error);

/// A place as ParseMemoryPlace reads it back: `0x6A00`, `0x6A00-0x6AFF`, `page:3:0x0100-0x01FF`, `rom:1:0x0C0A`
std::string FormatMemoryPlace(const MemoryPlace& place);
/// The `memory` section of @p profile, the lists it has (the save to a profile writes it into the profile file)
JsonValue MemoryProfileToJson(const MemoryProfile& profile);

}  // namespace controllers
