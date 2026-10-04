#pragma once

/// @file ttdconfigcapture.h
/// @brief The live machine's configuration fingerprint (Phase 3, Step 4).
///
/// Reads the settings a replay depends on from the emulator's configuration
/// and state into a TTDConfigFingerprint. The ROM signature is the caller's
/// (TimeTravelManager hashes the ROM region once per session, not per frame).

#include <cstdint>

#include "engine/ttdconfigfingerprint.h"

class EmulatorContext;

namespace ttd
{

/// Field name of the ROM set's signature (FNV-1a of the whole ROM region)
constexpr const char* kConfigRomSignature = "rom.signature";

/// The live settings of @p context's machine, with @p romSignature as the
/// ROM set's (0: not known, left out)
TTDConfigFingerprint CaptureConfigFingerprint(const EmulatorContext& context, uint64_t romSignature);

}  // namespace ttd
