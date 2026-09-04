#pragma once

#include "stdafx.h"

#include "emulator/io/tape/tapetypes.h"

/// region <Documentation>

/// Catalog derivation from a loaded TapeImage (design §5.5).
///
/// TapeCatalogParser is a pure function over descriptors and blocks: no
/// format knowledge, no `Tape`, no `EmulatorContext`. Loaders supply the
/// ground-truth fields they uniquely know (kind from the format's block ID,
/// timing profiles, group labels, playability); the parser derives the rest
/// — header interpretation, pairing, headerless classification, baud and
/// duration. Where a loader hint exists, the hint wins.
///
/// Runs once per image load inside Tape::EnsureImageLoaded() (NFR-5) — never
/// per frame, never per query surface.

/// endregion </Documentation>

class TapeCatalogParser
{
public:
    /// Derive the complete per-block catalog for `image`. The result is
    /// parallel to image.blocks (same indexing — FR-2) and is cheap to
    /// recompute: O(n) in image bytes for the duration model.
    static std::vector<TapeBlockDescriptor> Build(const TapeImage& image);

private:
    static void DeriveByteBlock(TapeBlockDescriptor& descriptor, const TapeBlock& block);
    static void DerivePulseBlock(TapeBlockDescriptor& descriptor, const TapeBlock& block);
    static double EstimateByteBlockSeconds(const TapeBlockDescriptor& descriptor, const TapeBlock& block);
    static void DerivePairing(std::vector<TapeBlockDescriptor>& descriptors);
};

/// region <Fast-load eligibility>

enum class FastLoadVerdictEnum : uint8_t
{
    Full,        // every byte block is trap-shaped: the whole tape can fast-load
    Partial,     // a trap-shaped prefix, then a block that forces the signal path forever after
    None,        // the first byte block already forces the signal path
    Empty        // no playable blocks
};

enum class FastLoadRejectEnum : uint8_t
{
    None = 0,
    NonStandardTiming,   // timing profile != StandardRom — TZX $11/$14 turbo, custom-timed PZX DATA
    PulseStream,         // no byte payload at all — TZX $12/$13/$15/$18, CSW, PZX PULSE
    NonStandardFlag,     // flag not $00/$FF: custom-loader payload the ROM would never accept
    ChecksumInvalid,     // XOR != 0 — trap declines, signal path reproduces "R Tape loading error"
    Headerless,          // byte block with no preceding valid header — its custom loader never calls the hooked LD-BYTES (r12)
    Unplayable,          // catalogued but not playable in this build (TZX $19)
    ControlFlowInert,    // image linearization hit its bound; play order is no longer authoritative
    ControlBlock         // not a reject: structural Control entry, skipped by the horizon walk (§5.8)
};

/// Stable wire names for the machine surfaces (CLI `FAST` column, WebAPI
/// `fast_load` strings — design §7). Snake_case, one word per value.
inline const char* getFastLoadVerdictName(FastLoadVerdictEnum value)
{
    const char* result;
    switch (value)
    {
        case FastLoadVerdictEnum::Full:
            result = "full";
            break;
        case FastLoadVerdictEnum::Partial:
            result = "partial";
            break;
        case FastLoadVerdictEnum::None:
            result = "none";
            break;
        case FastLoadVerdictEnum::Empty:
            result = "empty";
            break;
        default:
            result = "unknown";
            break;
    }

    return result;
}

/// `none` never appears in a per-block context (None == trap-shaped there);
/// `control_block` marks structural entries the horizon walk skips.
inline const char* getFastLoadRejectName(FastLoadRejectEnum value)
{
    const char* result;
    switch (value)
    {
        case FastLoadRejectEnum::None:
            result = "none";
            break;
        case FastLoadRejectEnum::NonStandardTiming:
            result = "non_standard_timing";
            break;
        case FastLoadRejectEnum::PulseStream:
            result = "pulse_stream";
            break;
        case FastLoadRejectEnum::NonStandardFlag:
            result = "non_standard_flag";
            break;
        case FastLoadRejectEnum::ChecksumInvalid:
            result = "checksum_invalid";
            break;
        case FastLoadRejectEnum::Headerless:
            result = "headerless";
            break;
        case FastLoadRejectEnum::Unplayable:
            result = "unplayable";
            break;
        case FastLoadRejectEnum::ControlFlowInert:
            result = "control_flow_inert";
            break;
        case FastLoadRejectEnum::ControlBlock:
            result = "control_block";
            break;
        default:
            result = "unknown";
            break;
    }

    return result;
}

/// Whole-image turbo pre-analysis (design §5.8). One line, ready for any
/// surface: verdict, horizon, the responsible block and the reason.
struct TapeFastLoadPlan
{
    FastLoadVerdictEnum verdict = FastLoadVerdictEnum::Empty;
    std::vector<FastLoadRejectEnum> perBlock;   // parallel to descriptors; None == trap-shaped,
                                                // ControlBlock == structurally skipped

    size_t eligibleBlocks = 0;                  // trap-shaped blocks anywhere in the image
    size_t stickinessHorizon = 0;               // blocks [0, horizon) — what fast load ACTUALLY covers
    size_t firstRejectIndex = SIZE_MAX;         // == stickinessHorizon when < blockCount
    FastLoadRejectEnum firstRejectReason = FastLoadRejectEnum::None;

    double acceleratedSeconds = 0.0;            // wall-clock the trap can remove (prefix only)
    double totalSeconds = 0.0;                  // whole playable image at real speed
    std::string summary;                        // one line, ready for CLI / API / UI badge
};

/// Pure function over descriptors — no format knowledge, no `Tape`, no
/// `EmulatorContext`. The honesty contract (design §5.8) is asymmetric:
/// `Ineligible` is definitive, `Eligible` is necessary and never sufficient.
/// The plan NEVER gates the runtime trap — it is advisory only.
class TapeFastLoadEligibility
{
public:
    /// Over descriptors — the parser-filled catalog is the correct input:
    /// loader-supplied descriptors are partial by contract (§5.5), so
    /// analyzing the raw image would classify checksums, flags and durations
    /// that were never derived (every tape would read "checksum_invalid",
    /// "0.0 s").
    static TapeFastLoadPlan Analyze(std::span<const TapeBlockDescriptor> descriptors, bool controlFlowLinearized);

    /// Image convenience wrapper — the descriptors as supplied (callers that
    /// build descriptors by hand, i.e. tests).
    static TapeFastLoadPlan Analyze(const TapeImage& image);

private:
    static FastLoadRejectEnum ClassifyBlock(const TapeBlockDescriptor& descriptor);
    static double PauseSeconds(const TapeBlockDescriptor& descriptor);
    static const char* ReasonPhrase(FastLoadRejectEnum reason);
};

/// endregion </Fast-load eligibility>
