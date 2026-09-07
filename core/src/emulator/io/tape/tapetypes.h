#pragma once

#include "stdafx.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

/// region <Documentation>

/// Tape vocabulary types — the pure-data leaf of the tape subsystem
/// (design: docs/inprogress/2026-09-01-tape-manager/design.md §5.1a).
///
/// Dependency rule: this header owns the shared vocabulary every tape
/// participant speaks — block kinds, timing profiles, per-block catalog
/// descriptors, the loader-agnostic TapeImage and its status channel — and
/// must depend on nothing but the standard library. tape.h, tapecatalog.h
/// and loaders/tape/loader_tape.h all include this header; nothing here may
/// include any of them (that direction was the tape.h <-> tapecatalog.h
/// include cycle §5.1a exists to break).

/// endregion </Documentation>

/// region <Types>

enum ZXTapeBlockTypeEnum : uint8_t
{
    TAP_BLOCK_PROGRAM = 0,          // Block contains BASIC program
    TAP_BLOCK_NUM_ARRAY,            // Block contains numeric array
    TAP_BLOCK_CHAR_ARRAY,           // Block contains symbolic array
    TAP_BLOCK_CODE                  // Block contains code
};

inline const char* getTapeBlockTypeName(ZXTapeBlockTypeEnum value)
{
    static const char* names[] =
    {
        "Program",
        "Numeric array",
        "Symbolic array",
        "Code"
    };

    return names[value];
};

enum TapeBlockFlagEnum : uint8_t
{
    TAP_BLOCK_FLAG_HEADER = 0x00,
    TAP_BLOCK_FLAG_DATA = 0xFF
};

inline const char* getTapeBlockFlagEnumName(TapeBlockFlagEnum value)
{
    const char* header = "Header";
    const char* data = "Data";
    const char* unknown = "<Unknown value";

    const char* result;
    switch (value)
    {
        case 0x00:
            result = header;
            break;
        case 0xFF:
            result = data;
            break;
        default:
            result = unknown;
            break;
    }

    return result;
};

/// Catalog classification of a block, independent of the source format.
/// Derived from the flag byte for byte-payload blocks (Header/Data/Custom),
/// from the signal shape for pulse blocks (Tone/PulseStream) and from the
/// structural role for control entries (TZX $20-$2B).
enum class TapeBlockKindEnum : uint8_t
{
    Header,           // standard $00 flag, 19 bytes -> interpretable ZX header
    Data,             // $FF flag byte-payload block
    Custom,           // non-standard flag byte-payload block (custom loaders)
    Tone,             // pilot/tone-only signal (TZX $12, loader custom pilot)
    PulseStream,      // precomputed pulse train, no byte payload (TZX $13/$15/$18, CSW, PZX)
    Control           // structural entry: pause/jump/loop/call/select/stop (TZX $20-$2B)
};

/// Stable wire name of a block kind for the machine surfaces (CLI tables,
/// WebAPI JSON — design §7). Snake_case, one word per kind.
inline const char* getTapeBlockKindName(TapeBlockKindEnum value)
{
    const char* result;
    switch (value)
    {
        case TapeBlockKindEnum::Header:
            result = "header";
            break;
        case TapeBlockKindEnum::Data:
            result = "data";
            break;
        case TapeBlockKindEnum::Custom:
            result = "custom";
            break;
        case TapeBlockKindEnum::Tone:
            result = "tone";
            break;
        case TapeBlockKindEnum::PulseStream:
            result = "pulse_stream";
            break;
        case TapeBlockKindEnum::Control:
            result = "control";
            break;
        default:
            result = "unknown";
            break;
    }

    return result;
};

/// How a block's signal is produced. StandardRom blocks are byte payloads
/// encoded with ROM timings (representation 1); Custom blocks are byte
/// payloads with non-ROM bit timings (representation 2); PulseStream blocks
/// carry the pulse train itself as the content (representation 3).
enum class TapeSpeedProfileEnum : uint8_t
{
    StandardRom,      // ROM timings (equivalent to TZX $10); TAP blocks default to this
    Custom,           // byte payload with non-ROM bit timings (TZX $11 turbo, $14 pure data)
    PulseStream       // timing IS the content; no byte encoding (tone/pulse/DR/CSW/PZX)
};

/// Stable wire name of a speed profile for the machine surfaces (design §7).
/// `turbo` matches the vocabulary of the design's JSON samples: any
/// non-ROM byte timing reads as a turbo block there.
inline const char* getTapeSpeedProfileName(TapeSpeedProfileEnum value)
{
    const char* result;
    switch (value)
    {
        case TapeSpeedProfileEnum::StandardRom:
            result = "standard";
            break;
        case TapeSpeedProfileEnum::Custom:
            result = "turbo";
            break;
        case TapeSpeedProfileEnum::PulseStream:
            result = "pulse_stream";
            break;
        default:
            result = "unknown";
            break;
    }

    return result;
};

/// Full timing parameterization of one block — the exact parameter set
/// generateBitstream() consumes. '0' half-periods mean "inherit the ROM
/// constants" and are only meaningful for the StandardRom profile.
/// A1 (tzx-loader-design.md §1): period fields are u32 — the TZX $11 pilot
/// pulse length is u32 and CSW pulse trains exceed u16 on long pauses;
/// edgePulseTimings is already vector<uint32_t>, so nothing may truncate
/// on the way into the engine.
struct TapeTimingProfile
{
    TapeSpeedProfileEnum profile = TapeSpeedProfileEnum::StandardRom;
    uint32_t pilotPulses = 0;        // pilot edges (half-periods), NOT full cycles — 0 = no pilot emitted
    uint32_t pilotHalfPeriod = 0;    // T-states per pilot edge
    uint32_t sync1 = 0, sync2 = 0;   // T-states of the two sync pulses
    uint32_t zeroHalfPeriod = 0;     // T-states per '0' bit half-period
    uint32_t oneHalfPeriod = 0;      // T-states per '1' bit half-period
    uint16_t pauseMs = 0;            // silence after the block
    uint8_t bitsInLastByte = 8;      // TZX $11/$14 trailing-bit count
    bool invertedLevel = false;      // TZX $2B influence / CSW initial polarity
};

/// One playable tape block: either byte-encoded data (`data` + optional
/// `timing` — nullopt means ROM-standard encoding) or a precomputed pulse
/// train (`edgePulseTimings`, `data` empty). Exactly one of the three
/// representations defined by design §5.7.
struct TapeBlock
{
    // ID of the block itself
    size_t blockIndex;

    TapeBlockFlagEnum type;                 // Header or data block

    std::vector<uint8_t> data;              // Raw data (flag + payload + checksum; empty for pulse blocks)

    // Custom timing for this block; nullopt = ROM-standard encoding of `data`
    // (every existing TAP image and test). Custom profile = generateBitstream()
    // runs with these parameters instead of the ROM constants.
    std::optional<TapeTimingProfile> timing;

    size_t totalBitstreamLength = 0;        // How long in t-states current block will be played
    std::vector<uint32_t> edgePulseTimings; // Block data encoded to pulse edge series
};

/// endregion </Types>

/// region <Catalog and image model>

/// Per-block catalog entry — everything the Tape Manager table, `tape blocks`
/// and GET /tape display for one block. Loaders supply the ground-truth
/// fields they uniquely know (kind from the format's block ID, timing,
/// group labels, playability); TapeCatalogParser derives the rest
/// (design §5.5). Indexing is parallel to TapeImage::blocks.
struct TapeBlockDescriptor
{
    // identity & payload
    size_t index = 0;                        // == TapeBlock index
    TapeBlockKindEnum kind = TapeBlockKindEnum::Data;
    size_t rawSize = 0;                      // byte-payload size incl. flag+checksum; 0 for pulse blocks
    uint8_t rawFlag = 0;                     // preserved for Custom blocks; $00/$FF otherwise
    bool flagBytePresent = true;             // false: raw-bit payload, no flag/checksum framing (TZX $14/$19)
    std::vector<uint8_t> payloadPreview;     // first 16 bytes for details pane / API preview

    // header interpretation (byte-payload blocks with flag $00)
    std::string name;                        // trimmed 10 chars, bit 7 stripped
    ZXTapeBlockTypeEnum headerType = TAP_BLOCK_PROGRAM;
    uint16_t declaredLength = 0;
    uint16_t param1 = 0, param2 = 0;
    bool headerValid = false;                // flag $00 + 19 bytes + type <= CODE

    // classification
    bool headerless = false;                 // byte-payload block not preceded by a valid Header (n/a for pulse/control)
    size_t pairedHeaderIndex = SIZE_MAX;     // inverse link from Data/Custom back to its Header
    size_t pairedDataIndex = SIZE_MAX;       // Header -> following Data block

    // encoding
    TapeTimingProfile timing;                // full profile — details pane & API expose it verbatim
    uint32_t baudEstimate = 0;               // 3500000 / (zeroHalf + oneHalf); 0 for pulse streams
    std::string groupLabel;                  // TZX $21 group, attached to the following block

    // validation & derived
    bool checksumValid = false;              // XOR(byte payload) == 0; false + rawSize == 0 -> "n/a"
    bool playable = true;                    // false: unplayable-in-v1 entries (TZX $19), control leftovers
    double estimatedSeconds = 0.0;           // profile model (bytes) or pulse sum (pulse blocks)
};

/// Explicit outcome channel for a whole-image decode. The image may hold a
/// usable partial prefix even when status is Malformed — IsUsable() is the
/// single decision point (design §5.3).
enum class TapeLoadStatus : uint8_t
{
    Ok,            // clean parse
    Warnings,      // usable image, parseWarnings non-empty (degraded/quirky source)
    Unsupported,   // recognized format, but nothing in it is playable by this build
    Malformed,     // framing broken beyond recovery; blocks may hold a partial prefix
    IoError        // source unreadable — set by the caller, never by a loader
};

/// TAP-family framing variants (Sinclair Wiki "TAP format — Similar formats").
/// One loader, one enum: the unified model and catalog see byte-payload
/// blocks identical to plain TAP; only the framing walk and the checksum
/// rule adjust per variant so checksumValid stays honest (design §5.4).
enum class TapVariantEnum : uint8_t
{
    Standard,        // .tap — [u16 len][flag...checksum], parity = XOR incl. flag
    Spc,             // .spc — len = TAP-2, parity excludes flag (SP emulator)
    Sta,             // .sta — len = TAP-2, no parity byte stored (Speculator)
    Ltp,             // .ltp — len = TAP-2, otherwise identical (Nuclear ZX)
    Zxt              // .zxt — 128-byte +3DOS 'TAPEFILE' header, then Standard
};

/// The loader-agnostic result of a whole-image decode: playable blocks,
/// (possibly partial) descriptors, format identity and the status channel.
struct TapeImage
{
    std::vector<TapeBlock> blocks;                 // playable: bytes(+timing) or precomputed pulses
    std::vector<TapeBlockDescriptor> descriptors;  // loader-supplied ground truth (may be partial; parser fills the rest)
    std::string formatId;                          // "tap" / "tzx" / "csw" / "pzx" — registry-owned ids
    TapVariantEnum tapVariant = TapVariantEnum::Standard;  // meaningful when formatId == "tap"
    std::string title;                             // TZX $30/$32 archive info
    std::string hardwareNote;                      // TZX $33
    bool controlFlowLinearized = true;             // false if jump/loop expansion hit the bound (design §5.4)
    std::vector<std::string> parseWarnings;        // human-readable load anomalies (UI warning badge)

    TapeLoadStatus status = TapeLoadStatus::Ok;
    std::string errorText;                         // populated for Unsupported / Malformed / IoError

    bool IsUsable() const
    {
        return !blocks.empty() && (status == TapeLoadStatus::Ok || status == TapeLoadStatus::Warnings);
    }
};

/// Format identity + content probe. The probe never reads the filesystem and
/// never throws; magic-bearing formats answer 100/0, structural formats
/// (the TAP family) walk the framing and answer a graded score
/// (design §5.3).
struct TapeFormatInfo
{
    std::string id;                               // "tap"
    std::string displayName;                      // "TAP (raw ZX blocks)"
    std::vector<std::string> extensions;          // { "tap" }
    /// Confidence 0-100 that this buffer is this format. 0 = "not mine".
    int (*Probe)(std::span<const uint8_t> bytes) = nullptr;
};

/// endregion </Catalog and image model>
