#pragma once

/// @file ttd_serializable.h
/// @brief Minimal in-RAM state serialization interface for TTD peripherals.
///
/// Per parent TDD §6.4: implemented by AY/TurboSound, WD1793+FDD, Tape, Covox.
/// The CPU and chipset do NOT implement this interface — they are stored by
/// value in TTDCheckpoint (see ttd_checkpoint.h) because their state layout
/// is fixed and known at compile time.
///
/// Design constraints (TDD §6.4):
///   - No heap allocation in TTDSaveState (runs every frame on the emulator
///     thread). Callers reserve a buffer of TTDStateSize() bytes up front.
///   - No versioning. Checkpoints never persist across process runs in v1
///     (on-disk sessions are a post-v1 open question). If versioning is
///     added later, it will be a session-level magic, not per-payload.
///   - The blob format is the implementer's choice (typically a memcpy of
///     the device's POD state) — there is no shared envelope or framing.
///
/// The same implementations serve both TTD checkpoints and the in-RAM
/// snapshot serializer (TDD §6.1 "Serializer reuse") so the file-snapshot
/// feature and TTD can never disagree about peripheral state.

#include <cstdint>
#include <cstddef>

namespace ttd {

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
};

} // namespace ttd
