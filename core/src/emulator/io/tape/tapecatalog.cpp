#include "stdafx.h"
#include "tapecatalog.h"

#include "emulator/io/tape/tape.h"  // ROM timing constants (no cycle: tape.h does not include tapecatalog.h)

/// region <Constants>

namespace
{
    // ZX Spectrum CPU clock, T-states per second — the one constant the
    // duration model and baud estimate need (mirrors generateBitstream()'s
    // pause encoding: 1 ms == 3500 T-states).
    constexpr uint32_t CPU_CLOCK_HZ = 3500000;

    constexpr size_t PREVIEW_BYTES = 16;

    // Standard ROM header block: [flag][type][name:10][len:2][param1:2][param2:2][checksum]
    constexpr size_t HEADER_BLOCK_SIZE = 19;
    constexpr size_t HEADER_NAME_OFFSET = 2;
    constexpr size_t HEADER_NAME_LENGTH = 10;

    // The ROM loader's post-block pause — inherited by StandardRom profiles
    // with pauseMs 0 (design §5.2: "0 fields = inherit ROM constants").
    constexpr uint32_t ROM_PAUSE_MS = 1000;
}

/// endregion </Constants>

/// region <Catalog derivation>

std::vector<TapeBlockDescriptor> TapeCatalogParser::Build(const TapeImage& image)
{
    // Loader hints win (design §5.5): start from whatever the loader supplied,
    // pad the rest with defaults, then derive everything the loader cannot know.
    std::vector<TapeBlockDescriptor> result(image.blocks.size());
    for (size_t i = 0; i < image.blocks.size() && i < image.descriptors.size(); i++)
    {
        result[i] = image.descriptors[i];
    }

    for (size_t i = 0; i < image.blocks.size(); i++)
    {
        const TapeBlock& block = image.blocks[i];
        TapeBlockDescriptor& descriptor = result[i];

        descriptor.index = i;
        // Loader hint wins (design §5.5): catalog-only entries (compressed
        // $18, $19) carry their source size in rawSize with an empty
        // block.data — overwriting it here would erase the only place it lives.
        if (descriptor.rawSize == 0)
        {
            descriptor.rawSize = block.data.size();
        }
        // Flag byte is meaningful only for flag-framed payloads (TAP, TZX $10/$11).
        // TZX $14/$19 carry raw bits — the loader's rawFlag hint stands.
        if (!block.data.empty() && descriptor.flagBytePresent)
        {
            descriptor.rawFlag = block.data[0];
        }
        descriptor.payloadPreview.assign(block.data.begin(),
                                         block.data.begin() + std::min(PREVIEW_BYTES, block.data.size()));

        if (!block.data.empty())
        {
            DeriveByteBlock(descriptor, block);
        }
        else if (!block.edgePulseTimings.empty())
        {
            DerivePulseBlock(descriptor, block);
        }
        else
        {
            // Neither bytes nor pulses: a silent entry — nothing to derive.
        }
    }

    DerivePairing(result);

    return result;
}

/// endregion </Catalog derivation>

/// region <Per-kind derivation>

void TapeCatalogParser::DeriveByteBlock(TapeBlockDescriptor& descriptor, const TapeBlock& block)
{
    // Kind by flag byte (design §5.5): $00 -> Header, $FF -> Data, anything
    // else -> Custom. A loader hint (TZX block ID) already set in the
    // descriptor wins over the flag guess.
    if (descriptor.kind == TapeBlockKindEnum::Data && descriptor.rawFlag == TAP_BLOCK_FLAG_HEADER)
    {
        descriptor.kind = TapeBlockKindEnum::Header;
    }
    else if (descriptor.kind == TapeBlockKindEnum::Data && descriptor.rawFlag != TAP_BLOCK_FLAG_DATA)
    {
        descriptor.kind = TapeBlockKindEnum::Custom;
    }

    // Checksum: the bitwise XOR of every byte (flag ... checksum) is zero for
    // a well-formed ROM-protocol block. Same rule LoaderTAP::isBlockValid()
    // applies, computed directly. Meaningless for flag-less payloads (TZX
    // $14/$19) — the loader's value stands.
    if (descriptor.flagBytePresent)
    {
        uint8_t parity = 0;
        for (uint8_t byte : block.data)
        {
            parity ^= byte;
        }
        descriptor.checksumValid = parity == 0;
    }

    // Header interpretation: field-by-field decode, never a struct overlay
    // (design §3.5 — ZXTapeHeader is dead and layout-wrong).
    if (descriptor.kind == TapeBlockKindEnum::Header)
    {
        descriptor.headerValid = block.data.size() == HEADER_BLOCK_SIZE;

        if (block.data.size() >= HEADER_BLOCK_SIZE)
        {
            uint8_t typeByte = block.data[1];
            descriptor.headerType = static_cast<ZXTapeBlockTypeEnum>(typeByte);
            if (typeByte > TAP_BLOCK_CODE)
            {
                descriptor.headerValid = false;
            }

            // Filename: 10 bytes, bit 7 stripped, truncated at the first NUL, then
            // trailing spaces trimmed (design §5.2: "trimmed 10 chars" — NUL and
            // space padding both occur in real dumps)
            std::string name;
            for (size_t k = 0; k < HEADER_NAME_LENGTH; k++)
            {
                char c = static_cast<char>(block.data[HEADER_NAME_OFFSET + k] & 0x7F);
                if (c == '\0') break;
                name.push_back(c);
            }
            size_t last = name.find_last_not_of(' ');
            descriptor.name = last == std::string::npos ? "" : name.substr(0, last + 1);

            descriptor.declaredLength = static_cast<uint16_t>(block.data[12] | (block.data[13] << 8));
            descriptor.param1 = static_cast<uint16_t>(block.data[14] | (block.data[15] << 8));
            descriptor.param2 = static_cast<uint16_t>(block.data[16] | (block.data[17] << 8));
        }
    }

    // Timing profile: the block's own profile, or the ROM-standard default.
    if (block.timing.has_value())
    {
        descriptor.timing = *block.timing;
    }
    else
    {
        descriptor.timing.profile = TapeSpeedProfileEnum::StandardRom;
        descriptor.timing.pilotPulses = descriptor.kind == TapeBlockKindEnum::Header
                                            ? PILOT_DURATION_HEADER
                                            : PILOT_DURATION_DATA;
    }

    // Baud estimate: bits per second at the block's own bit encoding.
    uint32_t zeroHalf = descriptor.timing.zeroHalfPeriod;
    uint32_t oneHalf = descriptor.timing.oneHalfPeriod;
    if (descriptor.timing.profile != TapeSpeedProfileEnum::PulseStream)
    {
        if (zeroHalf == 0) zeroHalf = ZERO_ENCODE_HALF_PERIOD;
        if (oneHalf == 0) oneHalf = ONE_ENCODE_HALF_PERIOD;
        uint32_t fullPeriod = zeroHalf + oneHalf;
        descriptor.baudEstimate = fullPeriod > 0 ? CPU_CLOCK_HZ / fullPeriod : 0;
    }

    descriptor.estimatedSeconds = EstimateByteBlockSeconds(descriptor, block);
}

void TapeCatalogParser::DerivePulseBlock(TapeBlockDescriptor& descriptor, const TapeBlock& block)
{
    // Pulse blocks: timing IS the content (representation 3, design §5.7).
    // Kind defaults to PulseStream; a loader hint of Tone (TZX $12) wins.
    descriptor.timing.profile = TapeSpeedProfileEnum::PulseStream;
    descriptor.baudEstimate = 0;

    uint64_t total = 0;
    for (uint32_t pulse : block.edgePulseTimings)
    {
        total += pulse;
    }
    descriptor.estimatedSeconds = static_cast<double>(total) / CPU_CLOCK_HZ;
}

/// endregion </Per-kind derivation>

/// region <Derived statistics>

double TapeCatalogParser::EstimateByteBlockSeconds(const TapeBlockDescriptor& descriptor, const TapeBlock& block)
{
    // Exactly the arithmetic generateBitstream() performs (design §5.5: the
    // duration model "cannot disagree with playback") — walked over the bytes
    // without materializing the edge vector.
    const TapeTimingProfile& profile = descriptor.timing;

    uint32_t pilotHalf = profile.pilotHalfPeriod;
    uint32_t sync1 = profile.sync1;
    uint32_t sync2 = profile.sync2;
    uint32_t zeroHalf = profile.zeroHalfPeriod;
    uint32_t oneHalf = profile.oneHalfPeriod;

    if (profile.profile == TapeSpeedProfileEnum::StandardRom)
    {
        pilotHalf = PILOT_TONE_HALF_PERIOD;
        sync1 = PILOT_SYNCHRO_1;
        sync2 = PILOT_SYNCHRO_2;
        zeroHalf = ZERO_ENCODE_HALF_PERIOD;
        oneHalf = ONE_ENCODE_HALF_PERIOD;
    }

    uint64_t total = 0;

    // Pilot + sync — the engine emits the sync pair only when a pilot exists
    if (profile.pilotPulses > 0)
    {
        total += static_cast<uint64_t>(profile.pilotPulses) * pilotHalf;
        total += sync1;
        total += sync2;
    }

    // Data: every bit of every byte, two half-periods per bit
    for (uint8_t byte : block.data)
    {
        for (uint8_t bitMask = 0x80; bitMask != 0; bitMask >>= 1)
        {
            uint32_t half = (byte & bitMask) ? oneHalf : zeroHalf;
            total += 2 * static_cast<uint64_t>(half);
        }
    }

    // Pause: 1 ms == 3500 T-states. StandardRom with pauseMs 0 inherits the
    // ROM loader's 1-second post-block pause (design §5.2: "0 fields = inherit
    // ROM constants"); an explicit pauseMs always wins. Parity with the engine's
    // totalBitstreamLength depends on this (DurationMatchesEngineForStandardRom).
    uint32_t pauseMs = profile.pauseMs;
    if (profile.profile == TapeSpeedProfileEnum::StandardRom && pauseMs == 0)
        pauseMs = ROM_PAUSE_MS;
    total += static_cast<uint64_t>(pauseMs) * (CPU_CLOCK_HZ / 1000);

    return static_cast<double>(total) / CPU_CLOCK_HZ;
}

/// endregion </Derived statistics>

/// region <Pairing>

void TapeCatalogParser::DerivePairing(std::vector<TapeBlockDescriptor>& descriptors)
{
    // Header <-> data pairing (design §5.5): a byte-payload block is
    // `pairedWithHeader` when the immediately preceding block is a *valid*
    // Header (of any speed profile). This is exactly the insult.tap
    // distinction: ROM-protocol blocks come in header+data pairs; custom
    // loader payloads are headerless by construction.
    for (size_t i = 0; i < descriptors.size(); i++)
    {
        TapeBlockDescriptor& descriptor = descriptors[i];

        if (descriptor.kind != TapeBlockKindEnum::Data && descriptor.kind != TapeBlockKindEnum::Custom)
        {
            continue;  // pairing is a byte-payload Data/Custom property; headers carry the reverse link
        }

        bool paired = i > 0 &&
                      descriptors[i - 1].kind == TapeBlockKindEnum::Header &&
                      descriptors[i - 1].headerValid;

        if (paired)
        {
            descriptor.pairedHeaderIndex = i - 1;
            descriptors[i - 1].pairedDataIndex = i;
        }

        descriptor.headerless = !paired;
    }
}

/// endregion </Pairing>

/// region <Fast-load eligibility>

TapeFastLoadPlan TapeFastLoadEligibility::Analyze(const TapeImage& image)
{
    return Analyze(image.descriptors, image.controlFlowLinearized);
}

TapeFastLoadPlan TapeFastLoadEligibility::Analyze(std::span<const TapeBlockDescriptor> descriptors, bool controlFlowLinearized)
{
    // Design §5.8 — a static evaluation of the runtime decline matrix over
    // descriptors only. Two walkers in one pass:
    //   - eligibleBlocks counts intrinsically trap-shaped blocks ANYWHERE
    //     (it exists to explain the horizon difference, never to promise);
    //   - the horizon walk skips Control entries and stops at the first real
    //     reject — stickiness (fast-tape §6.3) makes everything after it
    //     real-speed regardless of eligibility.
    TapeFastLoadPlan plan;
    plan.perBlock.reserve(descriptors.size());

    // Image-wide rule 6: an unlinearized image has no trustworthy block order
    // — its plan is None with every byte block inert.
    const bool linearized = controlFlowLinearized;

    bool sawByteBlock = false;

    for (size_t i = 0; i < descriptors.size(); i++)
    {
        const TapeBlockDescriptor& descriptor = descriptors[i];
        FastLoadRejectEnum reject = ClassifyBlock(descriptor);

        if (reject == FastLoadRejectEnum::None)
        {
            plan.eligibleBlocks++;
        }

        if (reject == FastLoadRejectEnum::ControlBlock)
        {
            // Structurally skipped: no payload for the ROM loader, invisible
            // to the trap — must not poison the horizon (tzx §8a.2).
            plan.perBlock.push_back(FastLoadRejectEnum::ControlBlock);
            continue;
        }

        sawByteBlock = true;

        if (!linearized)
        {
            reject = FastLoadRejectEnum::ControlFlowInert;
        }

        plan.perBlock.push_back(reject);

        if (reject != FastLoadRejectEnum::None && plan.firstRejectIndex == SIZE_MAX)
        {
            plan.firstRejectIndex = i;
            plan.firstRejectReason = reject;
        }
    }

    const size_t blockCount = descriptors.size();

    if (!sawByteBlock)
    {
        // No playable byte payload at all — empty tape or pause/structure-only image.
        plan.verdict = FastLoadVerdictEnum::Empty;
        plan.summary = "fast load: n/a — no loadable blocks";
        return plan;
    }

    // Durations: total = whole playable image at real speed; accelerated =
    // what the trap removes from the covered prefix (encoding time, not the
    // pauses the loader still sits through — PauseSeconds inherits the ROM
    // 1-second post-block pause exactly like the duration model).
    for (size_t i = 0; i < blockCount; i++)
    {
        if (descriptors[i].playable)
        {
            plan.totalSeconds += descriptors[i].estimatedSeconds;
        }
    }

    if (plan.firstRejectIndex == SIZE_MAX)
    {
        plan.stickinessHorizon = blockCount;
        plan.verdict = FastLoadVerdictEnum::Full;
    }
    else
    {
        plan.stickinessHorizon = plan.firstRejectIndex;
        plan.verdict = plan.stickinessHorizon == 0 ? FastLoadVerdictEnum::None : FastLoadVerdictEnum::Partial;
    }

    for (size_t i = 0; i < plan.stickinessHorizon; i++)
    {
        if (plan.perBlock[i] == FastLoadRejectEnum::None)
        {
            plan.acceleratedSeconds += descriptors[i].estimatedSeconds - PauseSeconds(descriptors[i]);
        }
    }

    // One-line summary, ready for CLI / API / UI badge verbatim.
    switch (plan.verdict)
    {
        case FastLoadVerdictEnum::Full:
            plan.summary = "fast load: full — all " + std::to_string(blockCount) + " block(s) ROM-standard";
            break;
        case FastLoadVerdictEnum::Partial:
            plan.summary = "fast load: partial — blocks 0-" + std::to_string(plan.stickinessHorizon - 1) +
                           " of " + std::to_string(blockCount) + "; block " +
                           std::to_string(plan.firstRejectIndex) + " " + ReasonPhrase(plan.firstRejectReason);
            break;
        case FastLoadVerdictEnum::None:
            plan.summary = "fast load: none — block " + std::to_string(plan.firstRejectIndex) + " " +
                           ReasonPhrase(plan.firstRejectReason) + " (" + std::to_string(plan.eligibleBlocks) +
                           " of " + std::to_string(blockCount) + " block(s) ROM-standard but sticky)";
            break;
        case FastLoadVerdictEnum::Empty:
            break;  // unreachable — handled above
    }

    return plan;
}

FastLoadRejectEnum TapeFastLoadEligibility::ClassifyBlock(const TapeBlockDescriptor& descriptor)
{
    // Rules 1-5 of design §5.8 — rows 6-8 of the runtime decline matrix,
    // evaluated statically. First failing rule wins. (Rule 1b — the r12
    // headerless row — mirrors the runtime reality that a custom loader
    // reads its payload through the port, not through the hooked ROM path.)
    switch (descriptor.kind)
    {
        case TapeBlockKindEnum::Control:
            return FastLoadRejectEnum::ControlBlock;  // skipped, never rejected
        case TapeBlockKindEnum::Tone:
        case TapeBlockKindEnum::PulseStream:
            return FastLoadRejectEnum::PulseStream;
        case TapeBlockKindEnum::Custom:
            return FastLoadRejectEnum::NonStandardFlag;  // that is what Custom MEANS
        case TapeBlockKindEnum::Header:
        case TapeBlockKindEnum::Data:
            break;  // byte payload — continue down the rule list
    }

    // Rule 1b (r12): a headerless data block has no ROM-protocol consumer —
    // the custom loader that owns it never calls the hooked LD-BYTES, so the
    // trap can never serve it. The reserved §5.8 row, wired after live
    // observation: DIZZY_X blocks 2+ (all headerless, all ROM-shaped) showed
    // ⚡ while actually falling back to the real-speed signal path.
    if (descriptor.kind == TapeBlockKindEnum::Data && descriptor.headerless)
    {
        return FastLoadRejectEnum::Headerless;
    }

    // Rule 2, descriptor-side encoding of `block.timing == nullopt`
    // (representation 1): the profile is StandardRom iff the block plays at
    // ROM timings. A ROM-timed $11 turbo still carries Custom and stays
    // ineligible (tzx-loader-design §8a.1).
    if (descriptor.timing.profile != TapeSpeedProfileEnum::StandardRom)
    {
        return FastLoadRejectEnum::NonStandardTiming;
    }

    // Rule 3 — safe for Header/Data kinds (their flags are $00/$FF by
    // definition) but kept so the function is total over descriptors.
    if (descriptor.rawFlag != 0x00 && descriptor.rawFlag != 0xFF)
    {
        return FastLoadRejectEnum::NonStandardFlag;
    }

    if (!descriptor.checksumValid)
    {
        return FastLoadRejectEnum::ChecksumInvalid;
    }

    if (!descriptor.playable)
    {
        return FastLoadRejectEnum::Unplayable;
    }

    return FastLoadRejectEnum::None;
}

double TapeFastLoadEligibility::PauseSeconds(const TapeBlockDescriptor& descriptor)
{
    uint32_t pauseMs = descriptor.timing.pauseMs;
    if (descriptor.timing.profile == TapeSpeedProfileEnum::StandardRom && pauseMs == 0)
    {
        pauseMs = ROM_PAUSE_MS;  // same inheritance rule as the duration model
    }
    return pauseMs / 1000.0;
}

const char* TapeFastLoadEligibility::ReasonPhrase(FastLoadRejectEnum reason)
{
    switch (reason)
    {
        case FastLoadRejectEnum::NonStandardTiming: return "is a turbo block (non-standard timing)";
        case FastLoadRejectEnum::PulseStream:       return "is a pulse stream (no byte payload)";
        case FastLoadRejectEnum::NonStandardFlag:   return "has a non-standard flag byte";
        case FastLoadRejectEnum::ChecksumInvalid:   return "has an invalid checksum";
        case FastLoadRejectEnum::Headerless:        return "is headerless";
        case FastLoadRejectEnum::Unplayable:        return "is not playable in this build";
        case FastLoadRejectEnum::ControlFlowInert:  return "has non-linearized control flow";
        case FastLoadRejectEnum::None:
        case FastLoadRejectEnum::ControlBlock:
            return "";  // never a first-reject reason
    }
    return "";
}

/// endregion </Fast-load eligibility>
