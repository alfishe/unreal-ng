#pragma once

#include "stdafx.h"

#include <cstdint>

/// region <Documentation>

/// What a program does with a read of the tape port (#FE): is it listening to
/// the tape, or reading the keyboard? Decided from the code right after the IN
/// (design: docs/inprogress/2026-08-30-fast-tape-loading/loader-follow-design.md §4.1).
///
/// A small bit tracker follows the value that was read through up to six
/// instructions and watches where the EAR bit (bit 6) and the five key bits
/// (bits 0-4) end up. It decides at the first instruction that tests one of
/// them:
///   - AND n / BIT b covering the EAR bit                 -> Ear
///   - a conditional jump on carry holding the EAR bit    -> Ear
///   - AND n covering key bits only                       -> Key
///   - a key test that falls through (BREAK check) keeps tracking; the EAR bit
///     wins if it is tested later, otherwise the read is Key
///   - anything the tracker cannot follow                 -> Key if the EAR bit
///     is already lost and key bits are still tracked, otherwise Other
///
/// Worked examples:
///   ROM LD-SAMPLE   IN A,(#FE); RRA; RET NC; XOR C; AND #20   -> Ear
///   ROM KEY-SCAN    IN A,(C); CPL; AND #1F                    -> Key
///   any-key wait    IN A,(#FE); OR #E0; INC A; JR Z           -> Key
///   custom loader   IN A,(#FE); RLA; RLA; JR NC               -> Ear
///   custom loader   IN E,(C); BIT 6,E                         -> Ear

/// endregion </Documentation>

enum class TapeReadKind : uint8_t
{
    Other = 0,  // the code does something the tracker cannot follow
    Ear,        // the code tests the EAR bit: a loader listening to the tape
    Key         // the code tests key bits only: a keyboard or joystick read
};

class TapeReadClassifier
{
public:
    /// Reads one byte of the Z80 address space as the CPU sees it now.
    using ByteReader = uint8_t (*)(void* context, uint16_t address);

    /// @param pcAfterIn  Address of the instruction after the IN. The IN
    ///                   itself sits at pcAfterIn-2 (IN A,(n) = DB nn;
    ///                   IN r,(C) = ED xx).
    static TapeReadKind Classify(ByteReader read, void* context, uint16_t pcAfterIn);
};
