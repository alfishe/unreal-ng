#pragma once

/// @file ttdserializable.h
/// @brief Minimal in-RAM state serialization interface for TTD peripherals.
///
/// Per parent TDD §6.4: implemented by AY/TurboSound, WD1793+FDD, Tape, Covox,
/// TSFM, GeneralSound, and any future sound/expansion peripherals.
///
/// The CPU and chipset do NOT implement this interface — they are stored by
/// value in TTDCheckpoint (see ttdcheckpoint.h) because their state layout
/// is fixed and known at compile time.
///
/// Design constraints (TDD §6.4):
///   - No heap allocation in TTDSaveState (runs every frame on the emulator
///     thread). Callers reserve a buffer of TTDStateSize() bytes up front.
///   - Per-payload versioning is not used. Session-level schema versioning
///     is handled by SerializeSession/DeserializeSession (ttddumpformat.h).
///   - The blob format is the implementer's choice (typically a memcpy of
///     the device's POD state) — there is no shared envelope or framing.
///
/// Blob encoding:
///   - TTDPeripheralRegistry wraps each device's state in a header and
///     compresses the payload when that makes it smaller.
///   - Every blob is self-describing: its header carries the device id, so a
///     blob filed under the wrong device is rejected rather than loaded.
///
/// The same implementations serve both TTD checkpoints and the in-RAM
/// snapshot serializer (TDD §6.1 "Serializer reuse") so the file-snapshot
/// feature and TTD can never disagree about peripheral state.

#include <cstdint>
#include <cstddef>
#include <string>

namespace ttd {

/// @brief Peripheral identifier for registry and checkpoint indexing.
/// New peripherals add entries here; the enum value is NOT stored on disk
/// (v1 checkpoints are memory-only), so insertion order can change.
enum class PeripheralId : uint8_t
{
    TurboSound = 0,
    BetaDisk   = 1,
    Tape       = 2,
    Covox      = 3,
    TSFM       = 4,
    GeneralSound = 5,
    ScorpionProfROM = 6,  // Scorpion ZS 256/1024 ProfROM state machine
    // Future: SAA1099, GS512, etc.
    Count
};

class TTDSerializable
{
public:
    virtual ~TTDSerializable() = default;

    /// Fixed-size payload length for this device. Stable for the lifetime of
    /// the device instance (i.e. does not change as the device is configured).
    /// Callers use this to size the destination buffer before calling
    /// TTDSaveState.
    /// @return Number of bytes needed for TTDSaveState's dst buffer.
    virtual size_t TTDStateSize() const = 0;

    /// Snapshot the device's complete runtime state into dst.
    ///
    /// Must be a plain write of exactly TTDStateSize() bytes — no allocation,
    /// no side effects on the device. Runs on the emulator thread at every
    /// captured frame boundary.
    ///
    /// @param dst Destination buffer, at least TTDStateSize() bytes. Caller
    ///            owns the buffer; implementer must not retain the pointer.
    virtual void TTDSaveState(uint8_t* dst) const = 0;

    /// Restore the device's runtime state from src.
    ///
    /// Must fully restore the device to the state captured by TTDSaveState.
    /// Runs on the control thread during SeekTo, with the emulator paused
    /// (TDD §7.2). The implementer must not assume src points at any
    /// particular alignment beyond uint8_t.
    ///
    /// @param src Source buffer of exactly TTDStateSize() bytes. Caller owns
    ///            the buffer; implementer must not retain the pointer.
    virtual void TTDLoadState(const uint8_t* src) = 0;

    /// Human-readable device name for logging/debugging.
    virtual std::string TTDDeviceName() const { return "unknown"; }

    /// Peripheral identifier for checkpoint indexing.
    virtual PeripheralId TTDPeripheralId() const { return PeripheralId::Count; }

    /// Compute a hash contribution for divergence detection.
    /// The framework mixes this with the common chipset hash so model-specific
    /// state participates in divergence detection without the framework
    /// knowing about machine specifics.
    /// Default returns 0 (no contribution). Override for devices with state
    /// that affects determinism (e.g., Scorpion ProfROM quadrant).
    virtual uint64_t TTDHashState() const { return 0; }
};

} // namespace ttd
