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
///   - Delta encoding: xor against previous checkpoint + LZ4 compression
///
/// Performance target: <1ms for capture, <2ms for restore (well within
/// the 5-6ms per-frame restoration budget).

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

#include "ttdserializable.h"

namespace ttd {

/// @brief Blob header for delta-encoded peripheral state.
/// Stored at the start of each peripheralBlobs entry.
struct PeripheralBlobHeader
{
    uint8_t  peripheralId;     ///< PeripheralId enum value
    uint8_t  flags;            ///< Bit 0: isDelta (xor-encoded)
    uint16_t reserved;         ///< Padding for alignment
    uint32_t uncompressedSize; ///< Original state size (supports up to 4GB, e.g. GS 512KB SRAM)
    uint32_t compressedSize;   ///< Size after compression (0 = uncompressed)
};

static_assert(sizeof(PeripheralBlobHeader) == 12, "Header must be 12 bytes");

/// @brief Registry for managing connected peripherals and their TTD state.
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
    void Unregister(PeripheralId id);

    /// Check if a peripheral is registered.
    bool IsRegistered(PeripheralId id) const;

    /// Get registered device by ID (nullptr if not registered).
    TTDSerializable* GetDevice(PeripheralId id) const;

    /// Capture all registered peripherals to the blob map.
    /// @param prevBlobs Previous checkpoint's blobs for delta encoding (optional)
    /// @param outBlobs Output map of peripheral ID → serialized state
    void CaptureAll(
        const std::unordered_map<uint8_t, std::vector<uint8_t>>* prevBlobs,
        std::unordered_map<uint8_t, std::vector<uint8_t>>& outBlobs) const;

    /// Restore all peripherals from blob map.
    /// @param blobs Map of peripheral ID → serialized state
    /// @param prevBlobs Previous checkpoint's blobs for delta decoding (optional)
    void RestoreAll(
        const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs,
        const std::unordered_map<uint8_t, std::vector<uint8_t>>* prevBlobs) const;

    /// Count of registered peripherals.
    size_t Count() const { return _devices.size(); }

    /// Total state size of all registered peripherals (for metrics).
    size_t TotalStateSize() const;

private:
    std::unordered_map<uint8_t, TTDSerializable*> _devices;

    /// Compress state with xor-delta against previous.
    /// Returns compressed blob with header.
    static std::vector<uint8_t> CompressWithDelta(
        const uint8_t* current, size_t size,
        const uint8_t* previous, size_t prevSize,
        bool supportsDelta);

    /// Decompress blob, applying xor-delta if needed.
    /// Returns decompressed state.
    static std::vector<uint8_t> DecompressWithDelta(
        const std::vector<uint8_t>& blob,
        const uint8_t* previous, size_t prevSize);
};

} // namespace ttd
