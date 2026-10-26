#pragma once

/// @file signatures.h
/// @brief The signature catalog of the L2 signature detector: masked byte patterns (MemorySearch syntax: hex bytes,
/// "??" any byte, "A?" a nibble) with a check at the match, one file per family (rom48.cpp: the 48K ROM routines
/// and its character set; players and packers follow). A match claims the pattern's bytes (or `size` bytes) as
/// `kind`, labelled.

#include <cstdint>
#include <vector>

#include "debugger/analyzers/analysis/l2model.h"

namespace analysis
{

enum class SignatureCheck : uint8_t
{
    AtAddress,  ///< only at `address`, in the page mapped there at the start (ROM routines)
    Entry,      ///< anywhere, where the replay entered a routine (a call, a jump in) at the match
    Anywhere,   ///< anywhere (data blocks: fonts, tables)
};

struct Signature
{
    const char* id;       ///< "rom48.beeper"
    const char* family;   ///< "rom48"
    const char* label;    ///< "BEEPER"
    const char* pattern;  ///< MemorySearch pattern text
    uint16_t address;     ///< AtAddress: where it is
    uint16_t size;        ///< bytes claimed (0: the pattern's length)
    RegionKind kind;
    SignatureCheck check;
    const char* title;
};

namespace signatures
{
/// The 48K ROM family
const std::vector<Signature>& Rom48();
/// Every family, in a fixed order
const std::vector<Signature>& Builtin();
}  // namespace signatures

}  // namespace analysis
