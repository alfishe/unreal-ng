#pragma once

/// @file ttdperipheralregistry.h
/// @brief Dynamic peripheral registration and serialization for TTD.
///
/// Per TDD §6.4.1: Peripherals register themselves at runtime, and only
/// connected devices contribute to checkpoint state. This avoids wasting
/// bytes on unconnected devices (e.g., GeneralSound not present) and enables
/// efficient xor-delta compression for devices with large state.
///
/// Design:
///   - Registry holds weak pointers to connected TTDSerializable devices
///   - CaptureAll() serializes only registered devices to peripheralBlobs
///   - RestoreAll() restores from blobs, ignoring unknown device IDs
///   - Each blob is self-describing: header carries the device id and sizes
///
/// Performance target: <1ms for capture, <2ms for restore (well within
/// the 5-6ms per-frame restoration budget).

#include <array>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

#include "ttdserializable.h"

namespace ttd {

/// @brief Header prefixing every peripheralBlobs entry.
struct PeripheralBlobHeader
{
    uint8_t  peripheralId;     ///< PeripheralId enum value
    uint8_t  flags;            ///< Reserved; always 0.
    uint16_t reserved;         ///< Padding for alignment
    uint32_t uncompressedSize; ///< Original state size (supports up to 4GB, e.g. GS 512KB SRAM)
    uint32_t compressedSize;   ///< Size after compression (0 = payload stored raw)
};

static_assert(sizeof(PeripheralBlobHeader) == 12, "Header must be 12 bytes");

/// @brief Outcome of a RestoreAll pass.
///
/// Restore is best-effort by design — a session must stay loadable on a build
/// with a different device set — so the counts are the only way a caller (or a
/// test) can tell "restored everything" from "silently restored nothing".
struct TTDRestoreReport
{
    size_t restored = 0;        ///< Devices whose state was loaded.
    size_t missingBlobs = 0;    ///< Registered devices the checkpoint had no blob for.
    size_t sizeMismatches = 0;  ///< Blobs whose size did not match the device.
    size_t unclaimedBlobs = 0;  ///< Blobs no registered device claimed.

    /// True when every registered device got its state back and nothing was
    /// left over — the expected outcome for a session restored on the build
    /// that recorded it.
    bool Complete() const
    {
        return missingBlobs == 0 && sizeMismatches == 0 && unclaimedBlobs == 0;
    }
};

/// @brief Registry for managing connected peripherals and their TTD state.
class ITTDRegionSource;

class TTDPeripheralRegistry
{
public:
    TTDPeripheralRegistry() = default;

    /// Register a peripheral for TTD serialization.
    /// Only connected devices should be registered.
    /// @param id Unique peripheral identifier
    /// @param device Pointer to the device (must remain valid while registered)
    void Register(PeripheralId id, TTDSerializable* device);

    /// Unregister a peripheral (called when device disconnected).
    void Unregister(PeripheralId id);   ///< also drops a not-recorded mark

    /// Drop every registration. Used when a session ends: most registered
    /// devices are owned by the emulator, so there is no local list to walk.
    void Clear();

    /// Check if a peripheral is registered.
    bool IsRegistered(PeripheralId id) const;

    /// Get registered device by ID (nullptr if not registered).
    TTDSerializable* GetDevice(PeripheralId id) const;

    /// Capture all registered peripherals to the blob map.
    /// @param outBlobs Output map of peripheral ID → serialized state
    void CaptureAll(std::unordered_map<uint8_t, std::vector<uint8_t>>& outBlobs) const;

    /// Restore all peripherals from blob map.
    ///
    /// Driven by the registered devices rather than by the blobs: a device the
    /// checkpoint has no entry for would otherwise keep whatever state the live
    /// machine last left there, which is a divergence with no error anywhere.
    ///
    /// @param blobs Map of peripheral ID → serialized state
    /// @return Counts describing what was and was not restored.
    TTDRestoreReport RestoreAll(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs) const;

    /// Count of registered peripherals.
    size_t Count() const { return _devices.size(); }
    /// Every registered device by its v1 id
    const std::unordered_map<uint8_t, TTDSerializable*>& Devices() const { return _devices; }

    /// Devices whose memory the time-travel engine records as regions
    /// (Phase 1, Step 6); v1 does not use them
    void RegisterRegionSource(ITTDRegionSource* source)
    {
        if (source)
            _regionSources.push_back(source);
    }
    const std::vector<ITTDRegionSource*>& RegionSources() const { return _regionSources; }

    /// A device fitted on this machine that time travel deliberately does not
    /// record (the lightweight General Sound): no blob, no restore, no missing
    /// report; the session file names it (kFlagsHasNotRecordedMask)
    void MarkNotRecorded(PeripheralId id)
    {
        Unregister(id);
        _notRecorded |= uint64_t(1) << static_cast<uint8_t>(id);
    }
    uint64_t NotRecordedMask() const { return _notRecorded; }

    /// Raw device-state bytes the last CaptureAll serialized, in all and for
    /// one device (before compression: the work of reading the state)
    uint64_t LastCaptureStateBytes() const { return _lastStateTotal; }
    uint32_t LastCaptureStateBytes(uint8_t id) const { return id < _lastStateBytes.size() ? _lastStateBytes[id] : 0; }
    /// The raw state the last CaptureAll serialized for device @p id (empty when none)
    const std::vector<uint8_t>& LastCaptureState(uint8_t id) const
    {
        static const std::vector<uint8_t> none;
        return id < _lastStates.size() ? _lastStates[id] : none;
    }

    /// Total state size of all registered peripherals (for metrics).
    size_t TotalStateSize() const;

    /// Unwrap a blob back to raw device state. Returns empty on any
    /// inconsistency, including the header naming a different device than
    /// @p expectedId — the id is carried twice precisely so that can be caught.
    ///
    /// Public because a blob is the on-disk representation of a device's state:
    /// tests and offline tools need to read one without a live registry.
    static std::vector<uint8_t> DecodeBlob(uint8_t expectedId, const std::vector<uint8_t>& blob);

    /// Wrap a device's raw state in a PeripheralBlobHeader, compressing the
    /// payload when that actually makes it smaller (also used by the
    /// time-travel engine for a device state without its region memory)
    static std::vector<uint8_t> EncodeBlob(uint8_t id, const uint8_t* state, size_t size);

    /// Compute combined hash contribution from all registered peripherals.
    /// The framework mixes this with the common chipset hash for divergence
    /// detection, so model-specific state participates without the framework
    /// knowing about machine specifics.
    uint64_t ComputePeripheralHash() const;

    /// Tell every registered device that recording started / stopped
    /// (TTDSerializable::TTDRecordingStarted / TTDRecordingStopped)
    void NotifyRecording(bool started) const;

private:
    std::unordered_map<uint8_t, TTDSerializable*> _devices;
    std::vector<ITTDRegionSource*> _regionSources;
    uint64_t _notRecorded = 0;
    mutable std::array<uint32_t, 64> _lastStateBytes{};   ///< raw state bytes of the last CaptureAll, by id
    mutable std::array<std::vector<uint8_t>, 64> _lastStates;   ///< the raw states of the last CaptureAll, by id
    mutable uint64_t _lastStateTotal = 0;



};

} // namespace ttd
