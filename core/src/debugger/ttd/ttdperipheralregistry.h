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

#include "debugger/ttd/engine/ttddevicetable.h"
#include <array>
#include <map>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <string>

#include "ttdserializable.h"
#include "ttdperipheralmask.h"

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
struct TTDDeviceRegion;

/// One registered device: its key (type + instance), its v1 blob id (None when v1 cannot record it)
struct TTDRegistryEntry
{
    TTDDeviceKey key;
    PeripheralId legacyId = PeripheralId::None;
    TTDSerializable* device = nullptr;
    /// Set when the device also offers its memory as engine regions and gives its state without that memory
    /// (ITTDRegionSource::TTDStateWithoutRegions): the same object, paired at registration
    ITTDRegionSource* regions = nullptr;
};

/// @brief Registry for managing connected peripherals and their TTD state.
///
/// A device is known by its TTDDeviceKey (D23, d23-device-keys.md): its type and its instance name. Several
/// instances of one type sit side by side. The v1 blob id (PeripheralId, frozen) is only for v1's checkpoints:
/// CaptureAll / RestoreAll; a device without one cannot be recorded by v1 (V1Recordable).
class TTDPeripheralRegistry
{
public:
    TTDPeripheralRegistry() = default;

    /// Register a peripheral for TTD serialization.
    /// Only connected devices should be registered.
    /// @param device Pointer to the device (must remain valid while registered); its key comes from TTDDescribe
    /// @param instance The engine's device instance name ("zxbus.1.neogs": a slot card's device named by its slot);
    ///        empty keeps the device's own (TTDDescribe)
    /// @return false when another device already holds the key: the first stays, the second is refused and named
    ///         by CheckDeviceTable, which then refuses recording. Registering the same device again only updates
    ///         its instance name
    bool Register(TTDSerializable* device, const std::string& instance = {});

    /// Unregister a peripheral (called when device disconnected); also drops a not-recorded mark
    void Unregister(const TTDDeviceKey& key);
    /// Every device of @p type
    void UnregisterType(TTDDeviceType type);

    /// Drop every registration. Used when a session ends: most registered
    /// devices are owned by the emulator, so there is no local list to walk.
    void Clear();

    /// Whether a device of @p type is registered
    bool IsRegistered(TTDDeviceType type) const { return FindType(type) != nullptr; }

    /// The device of @p key (nullptr if not registered)
    TTDSerializable* Find(const TTDDeviceKey& key) const;
    /// The first registered device of @p type (a machine with one device of that kind)
    TTDSerializable* FindType(TTDDeviceType type) const;
    /// The device v1 saves under @p id (v1's checkpoints and files)
    TTDSerializable* FindLegacy(uint8_t id) const;
    TTDSerializable* FindLegacy(PeripheralId id) const { return FindLegacy(static_cast<uint8_t>(id)); }

    /// The instance name of the first device of @p type ("ay-socket.ts"); "" when none is registered
    std::string InstanceOf(TTDDeviceType type) const;

    /// Every registered device, in registration order
    const std::vector<TTDRegistryEntry>& Entries() const { return _entries; }
    /// Count of registered peripherals.
    size_t Count() const { return _entries.size(); }

    /// v1's capture: every device with a v1 id, by that id
    void CaptureAll(std::unordered_map<uint8_t, std::vector<uint8_t>>& outBlobs) const;

    /// Restore all peripherals from a v1 blob map (by v1 id).
    ///
    /// Driven by the registered devices rather than by the blobs: a device the
    /// checkpoint has no entry for would otherwise keep whatever state the live
    /// machine last left there, which is a divergence with no error anywhere.
    ///
    /// @param blobs Map of v1 id → serialized state
    /// @return Counts describing what was and was not restored.
    TTDRestoreReport RestoreAll(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs) const;

    /// Whether v1 can record this device set: every device has a v1 id of its own. False with the device named
    bool V1Recordable(std::string& why) const;

    /// A v1 checkpoint's blobs as device states: each under the key of the live device with that v1 id (the
    /// registered machine's names), else {type of the id, ""}; a blob that does not decode is left out
    TTDDeviceStates StatesOfV1Blobs(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs) const;
    /// v1's not-recorded mask (a bit per v1 id) and the keys it stands for ({type of the id, ""})
    static std::vector<TTDDeviceKey> KeysOfV1Mask(const PeripheralMask& mask);
    static PeripheralMask V1MaskOf(const std::vector<TTDDeviceKey>& keys);   ///< keys of a type past the v1 ids: left out

    /// Every device's whole state, by entry: a live snapshot the same registry loads back (LoadLive) while the
    /// device set stays as it is
    void SaveLive(std::vector<std::vector<uint8_t>>& out) const;
    /// Load a SaveLive snapshot: in ascending v1 id (a device that completes another one's state follows it), the
    /// devices without one last. A snapshot of another device set loads nothing
    void LoadLive(const std::vector<std::vector<uint8_t>>& states) const;

    /// The engine's capture (Phase 5): every registered device's raw state, no blob encoding, kept per entry
    /// (LastCaptureState). A device paired with a region source gives its state without that memory
    void CaptureStates() const;

    /// The time-travel engine's device table entries for the registered
    /// devices with state; a device whose memory the engine keeps as regions
    /// (TTDStateWithoutRegions) is entered with its state without that memory
    std::vector<TTDDeviceEntry> DeviceEntries() const;

    /// Every registered device against its own descriptor (its state size),
    /// then the engine's device table built from them (dependencies present,
    /// no cycle, time fields inside the state). False with the reason:
    /// recording would otherwise start and the engine refuse the session silently
    bool CheckDeviceTable(std::string& error) const;

    /// Devices whose memory the time-travel engine records as regions
    /// (Phase 1, Step 6); v1 does not use them. A source that is a registered
    /// device itself is paired with it (TTDRegistryEntry::regions)
    void RegisterRegionSource(ITTDRegionSource* source);
    const std::vector<ITTDRegionSource*>& RegionSources() const { return _regionSources; }
    /// Every region source's regions, in registration order: each named by its device's instance
    /// (TTDRegionDesc::ownerInstance) when the source is a registered device; a kind's region met again (a second
    /// card of one kind) takes the id InstanceRegionFirst + n x 0x100 + the kind's id, so the same machine numbers
    /// its regions alike when recording and when loading
    void CollectRegions(std::vector<TTDDeviceRegion>& out) const;

    /// A device fitted on this machine that time travel deliberately does not
    /// record (the lightweight General Sound): no state, no restore, no missing
    /// report; the session file names it
    void MarkNotRecorded(const TTDDeviceKey& key);
    const std::vector<TTDDeviceKey>& NotRecorded() const { return _notRecorded; }

    /// Raw device-state bytes the last capture read, in all and for one entry
    /// (before compression: the work of reading the state)
    uint64_t LastCaptureStateBytes() const { return _lastStateTotal; }
    uint32_t LastCaptureStateBytes(size_t entry) const
    {
        return entry < _lastStates.size() ? static_cast<uint32_t>(_lastStates[entry].size()) : 0;
    }
    /// The raw state the last capture read for entry @p entry (empty when none)
    const std::vector<uint8_t>& LastCaptureState(size_t entry) const
    {
        static const std::vector<uint8_t> none;
        return entry < _lastStates.size() ? _lastStates[entry] : none;
    }

    /// Total state size of all registered peripherals (for metrics).
    size_t TotalStateSize() const;

    /// Unwrap a v1 blob back to raw device state. Returns empty on any
    /// inconsistency, including the header naming a different device than
    /// @p expectedId — the id is carried twice precisely so that can be caught.
    ///
    /// Public because a blob is the on-disk representation of a device's state:
    /// tests and offline tools need to read one without a live registry.
    static std::vector<uint8_t> DecodeBlob(uint8_t expectedId, const std::vector<uint8_t>& blob);

    /// Wrap a device's raw state in a PeripheralBlobHeader (v1), compressing the
    /// payload when that actually makes it smaller
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
    void PairRegionSources();

    std::vector<TTDRegistryEntry> _entries;
    std::vector<std::string> _duplicates;                  ///< devices refused for a key already held
    std::vector<ITTDRegionSource*> _regionSources;
    std::vector<TTDDeviceKey> _notRecorded;
    mutable std::vector<std::vector<uint8_t>> _lastStates;   ///< the raw states of the last capture, by entry
    mutable uint64_t _lastStateTotal = 0;
};

} // namespace ttd
