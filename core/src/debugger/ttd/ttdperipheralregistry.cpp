#include "ttdperipheralregistry.h"
#include "ttdcompression.h"
#include "debugger/ttd/engine/ttdregiontracker.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <utility>

namespace ttd {

namespace
{
std::string Describe(const TTDDeviceKey& key)
{
    return key.instance + " (type " + std::to_string(static_cast<uint16_t>(key.type)) + ")";
}

/// The divergence hash's rotation for a device: its v1 id (the values v1 always used), else its type
unsigned HashShift(const TTDRegistryEntry& e)
{
    const unsigned basis = e.legacyId != PeripheralId::None ? static_cast<unsigned>(e.legacyId)
                                                            : static_cast<unsigned>(e.key.type);
    return basis & 0x3F;
}
}  // namespace

bool TTDPeripheralRegistry::Register(TTDSerializable* device, const std::string& instance)
{
    if (!device)
        return true;
    const TTDDeviceDescriptor d = device->TTDDescribe();
    const TTDDeviceKey key{d.type, instance.empty() ? d.instance : instance};
    for (TTDRegistryEntry& e : _entries)
    {
        if (e.device == device)
        {
            // The same device again: only its name changes
            e.key = key;
            return true;
        }
        if (e.key == key)
        {
            _duplicates.push_back("two devices under one key " + Describe(key) +
                                  " (a session holds one state per device)");
            return false;
        }
        // Two keys under one v1 id are kept: the engine records both, v1 cannot (V1Recordable names them)
    }
    _entries.push_back({key, d.legacyId, device, nullptr});
    PairRegionSources();
    return true;
}

void TTDPeripheralRegistry::RegisterRegionSource(ITTDRegionSource* source)
{
    if (!source)
        return;
    _regionSources.push_back(source);
    PairRegionSources();
}

void TTDPeripheralRegistry::PairRegionSources()
{
    for (TTDRegistryEntry& e : _entries)
    {
        e.regions = nullptr;
        auto* asSource = dynamic_cast<ITTDRegionSource*>(e.device);
        if (!asSource)
            continue;
        for (ITTDRegionSource* source : _regionSources)
            if (source == asSource)
            {
                std::vector<uint8_t> probe;
                if (source->TTDStateWithoutRegions(probe))
                    e.regions = source;
            }
    }
}

void TTDPeripheralRegistry::CollectRegions(std::vector<TTDDeviceRegion>& out) const
{
    for (ITTDRegionSource* source : _regionSources)
    {
        const size_t first = out.size();
        source->TTDRegions(out);
        std::string instance;
        for (const TTDRegistryEntry& e : _entries)
            if (dynamic_cast<ITTDRegionSource*>(e.device) == source)
                instance = e.key.instance;
        for (size_t i = first; i < out.size(); ++i)
        {
            TTDRegionDesc& desc = out[i].desc;
            if (desc.ownerInstance.empty())
                desc.ownerInstance = instance;
            const auto base = static_cast<uint16_t>(desc.id);
            uint16_t copy = 0;
            for (size_t j = 0; j < i; ++j)
            {
                const auto other = static_cast<uint16_t>(out[j].desc.id);
                if (other == base || (other >= static_cast<uint16_t>(TTDRegionId::InstanceRegionFirst) &&
                                      other < static_cast<uint16_t>(TTDRegionId::DeviceStateFirst) && (other & 0xFF) == base))
                    ++copy;
            }
            if (copy != 0)
            {
                desc.id = static_cast<TTDRegionId>(static_cast<uint16_t>(TTDRegionId::InstanceRegionFirst) + copy * 0x100 + base);
                if (!instance.empty())
                    desc.name = instance + "." + desc.name;
            }
        }
    }
}

void TTDPeripheralRegistry::MarkNotRecorded(const TTDDeviceKey& key)
{
    Unregister(key);
    _notRecorded.push_back(key);
}

std::vector<TTDDeviceEntry> TTDPeripheralRegistry::DeviceEntries() const
{
    std::vector<TTDDeviceEntry> devices;
    for (const TTDRegistryEntry& e : _entries)
    {
        if (!e.device || e.device->TTDStateSize() == 0)
            continue;
        devices.push_back({e.device->TTDDescribe(), e.device, nullptr});
        TTDDeviceEntry& entry = devices.back();
        entry.descriptor.instance = e.key.instance;
        if (e.regions)
        {
            std::vector<uint8_t> probe;
            if (e.regions->TTDStateWithoutRegions(probe))
            {
                entry.withoutRegions = e.regions;
                entry.descriptor.stateSize = static_cast<uint32_t>(probe.size());
                entry.descriptor.variableSize = false;
            }
        }
    }
    return devices;
}

bool TTDPeripheralRegistry::CheckDeviceTable(std::string& error) const
{
    if (!_duplicates.empty())
    {
        error = _duplicates.front();
        return false;
    }
    for (const TTDRegistryEntry& e : _entries)
    {
        if (!e.device)
            continue;
        const std::string name = e.device->TTDDeviceName();
        const TTDDeviceDescriptor d = e.device->TTDDescribe();
        if (d.legacyId != e.device->TTDPeripheralId())
        {
            error = "device " + name + " describes itself as v1 id " + std::to_string(static_cast<unsigned>(d.legacyId)) +
                    " but names itself v1 id " + std::to_string(static_cast<unsigned>(e.device->TTDPeripheralId()));
            return false;
        }
        if (d.type != e.key.type)
        {
            error = "device " + name + " describes itself as type " + std::to_string(static_cast<unsigned>(d.type)) +
                    ", registered as " + Describe(e.key);
            return false;
        }
        if (!d.variableSize && d.stateSize != e.device->TTDStateSize())
        {
            error = "device " + name + " describes " + std::to_string(d.stateSize) + " bytes of state but saves " +
                    std::to_string(e.device->TTDStateSize());
            return false;
        }
    }
    TTDDeviceTable table;
    return table.Build(DeviceEntries(), error);
}

bool TTDPeripheralRegistry::V1Recordable(std::string& why) const
{
    for (size_t i = 0; i < _entries.size(); ++i)
    {
        const TTDRegistryEntry& e = _entries[i];
        if (!e.device || e.device->TTDStateSize() == 0)
            continue;
        if (e.legacyId == PeripheralId::None)
        {
            why = "device " + Describe(e.key) + " has no v1 id: record with the engine";
            return false;
        }
        for (size_t j = 0; j < i; ++j)
            if (_entries[j].device && _entries[j].legacyId == e.legacyId)
            {
                why = "devices " + Describe(_entries[j].key) + " and " + Describe(e.key) + " share v1 id " +
                      std::to_string(static_cast<unsigned>(e.legacyId)) + ": record with the engine";
                return false;
            }
    }
    return true;
}

TTDDeviceStates TTDPeripheralRegistry::StatesOfV1Blobs(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs) const
{
    TTDDeviceStates states;
    for (const auto& [id, blob] : blobs)
    {
        std::vector<uint8_t> state = DecodeBlob(id, blob);
        if (state.empty())
            continue;
        TTDDeviceKey key{static_cast<TTDDeviceType>(id), {}};
        for (const TTDRegistryEntry& e : _entries)
            if (static_cast<uint8_t>(e.legacyId) == id)
            {
                key = e.key;
                break;
            }
        states.emplace(std::move(key), std::move(state));
    }
    return states;
}

std::vector<TTDDeviceKey> TTDPeripheralRegistry::KeysOfV1Mask(const PeripheralMask& mask)
{
    std::vector<TTDDeviceKey> keys;
    for (unsigned id = 0; id < static_cast<unsigned>(PeripheralId::None); ++id)   // every u8 id a file may name
        if (mask.Test(static_cast<uint8_t>(id)))
            keys.push_back({static_cast<TTDDeviceType>(id), {}});
    return keys;
}

PeripheralMask TTDPeripheralRegistry::V1MaskOf(const std::vector<TTDDeviceKey>& keys)
{
    PeripheralMask mask;
    for (const TTDDeviceKey& key : keys)
        if (static_cast<uint16_t>(key.type) < static_cast<uint16_t>(PeripheralId::None))
            mask.Set(static_cast<uint8_t>(key.type));
    return mask;
}

void TTDPeripheralRegistry::SaveLive(std::vector<std::vector<uint8_t>>& out) const
{
    out.resize(_entries.size());
    for (size_t i = 0; i < _entries.size(); ++i)
    {
        out[i].clear();
        if (_entries[i].device && _entries[i].device->TTDStateSize() != 0)
            _entries[i].device->TTDSaveStateTo(out[i]);
    }
}

void TTDPeripheralRegistry::LoadLive(const std::vector<std::vector<uint8_t>>& states) const
{
    if (states.size() != _entries.size())
        return;
    std::vector<size_t> order(_entries.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return static_cast<uint16_t>(_entries[a].legacyId) < static_cast<uint16_t>(_entries[b].legacyId);
    });
    for (const size_t i : order)
    {
        TTDSerializable* device = _entries[i].device;
        const std::vector<uint8_t>& state = states[i];
        if (!device || state.empty())
            continue;
        if (device->TTDVariableSize() ? state.size() <= device->TTDStateSize() : state.size() == device->TTDStateSize())
            device->TTDLoadState(state.data());
    }
}

void TTDPeripheralRegistry::Unregister(const TTDDeviceKey& key)
{
    std::erase_if(_entries, [&](const TTDRegistryEntry& e) { return e.key == key; });
    std::erase(_notRecorded, key);
}

void TTDPeripheralRegistry::UnregisterType(TTDDeviceType type)
{
    std::erase_if(_entries, [&](const TTDRegistryEntry& e) { return e.key.type == type; });
    std::erase_if(_notRecorded, [&](const TTDDeviceKey& k) { return k.type == type; });
}

void TTDPeripheralRegistry::Clear()
{
    _entries.clear();
    _duplicates.clear();
    _regionSources.clear();
    _notRecorded.clear();
    _lastStates.clear();
    _lastStateTotal = 0;
}

TTDSerializable* TTDPeripheralRegistry::Find(const TTDDeviceKey& key) const
{
    for (const TTDRegistryEntry& e : _entries)
        if (e.key == key)
            return e.device;
    return nullptr;
}

TTDSerializable* TTDPeripheralRegistry::FindType(TTDDeviceType type) const
{
    for (const TTDRegistryEntry& e : _entries)
        if (e.key.type == type)
            return e.device;
    return nullptr;
}

TTDSerializable* TTDPeripheralRegistry::FindLegacy(uint8_t id) const
{
    if (id == static_cast<uint8_t>(PeripheralId::None))
        return nullptr;
    for (const TTDRegistryEntry& e : _entries)
        if (static_cast<uint8_t>(e.legacyId) == id)
            return e.device;
    return nullptr;
}

std::string TTDPeripheralRegistry::InstanceOf(TTDDeviceType type) const
{
    for (const TTDRegistryEntry& e : _entries)
        if (e.key.type == type)
            return e.key.instance;
    return {};
}

size_t TTDPeripheralRegistry::TotalStateSize() const
{
    size_t total = 0;
    for (const TTDRegistryEntry& e : _entries)
        if (e.device)
            total += e.device->TTDStateSize();
    return total;
}

uint64_t TTDPeripheralRegistry::ComputePeripheralHash() const
{
    uint64_t combined = 0;
    for (const TTDRegistryEntry& e : _entries)
    {
        if (!e.device)
            continue;
        // Rotate each contribution so two devices holding the same state do
        // not cancel under the XOR, then XOR so the result does not depend on
        // the registration order.
        //
        // The shift == 0 branch is required, not defensive: for a shift of 0
        // the naive form evaluates `contribution >> 64`, which is undefined for
        // a 64-bit type and observably yields different values at -O0 and -O2 —
        // a divergence hash that disagreed between debug and release builds
        // would make the oracle report phantom divergences.
        const unsigned shift = HashShift(e);
        const uint64_t contribution = e.device->TTDHashState();
        const uint64_t rotated =
            (shift == 0) ? contribution : ((contribution << shift) | (contribution >> (64 - shift)));
        combined ^= rotated;
    }
    return combined;
}

void TTDPeripheralRegistry::NotifyRecording(bool started) const
{
    for (const TTDRegistryEntry& e : _entries)
    {
        if (!e.device)
            continue;
        if (started)
            e.device->TTDRecordingStarted();
        else
            e.device->TTDRecordingStopped();
    }
}

void TTDPeripheralRegistry::CaptureAll(std::unordered_map<uint8_t, std::vector<uint8_t>>& outBlobs) const
{
    outBlobs.clear();
    _lastStates.resize(_entries.size());
    _lastStateTotal = 0;
    for (std::vector<uint8_t>& s : _lastStates)
        s.clear();   // keeps the capacity: no allocation per frame

    for (size_t i = 0; i < _entries.size(); ++i)
    {
        const TTDRegistryEntry& e = _entries[i];
        if (!e.device || e.legacyId == PeripheralId::None)
            continue;
        if (e.device->TTDStateSize() == 0)
            continue;

        // Capture current state (a variable-size device writes only what it needs)
        std::vector<uint8_t>& state = _lastStates[i];
        e.device->TTDSaveStateTo(state);
        if (state.empty())
            continue;

        const auto id = static_cast<uint8_t>(e.legacyId);
        outBlobs[id] = EncodeBlob(id, state.data(), state.size());
        _lastStateTotal += state.size();
    }
}

void TTDPeripheralRegistry::CaptureStates() const
{
    _lastStates.resize(_entries.size());
    _lastStateTotal = 0;
    for (size_t i = 0; i < _entries.size(); ++i)
    {
        const TTDRegistryEntry& e = _entries[i];
        std::vector<uint8_t>& state = _lastStates[i];
        state.clear();   // keeps the capacity: no allocation per frame
        if (!e.device || e.device->TTDStateSize() == 0)
            continue;
        if (!(e.regions && e.regions->TTDStateWithoutRegions(state)))
            e.device->TTDSaveStateTo(state);
        _lastStateTotal += state.size();
    }
}

TTDRestoreReport TTDPeripheralRegistry::RestoreAll(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs) const
{
    TTDRestoreReport result;

    // Iterate the registered devices, not the blobs. Driving from the blob map
    // would silently skip a device the checkpoint has no entry for, leaving it
    // holding live state from whatever the machine was doing before the seek —
    // a divergence with no error anywhere. Driving from the devices lets us at
    // least name the device that is about to be left stale.
    //
    // In ascending v1 id order, whatever the registration order: a serializer
    // that completes another device's state (Wd1793Context after BetaDisk)
    // takes a higher id and finds that device already loaded.
    std::vector<std::pair<uint8_t, TTDSerializable*>> ordered;
    for (const TTDRegistryEntry& e : _entries)
        if (e.device && e.legacyId != PeripheralId::None)
            ordered.emplace_back(static_cast<uint8_t>(e.legacyId), e.device);
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [id, device] : ordered)
    {
        auto blobIt = blobs.find(id);
        if (blobIt == blobs.end() || blobIt->second.empty())
        {
            // Recorded before this device existed, or captured while it was
            // disconnected. Its state is now whatever the live machine last
            // left there.
            ++result.missingBlobs;
            continue;
        }

        auto state = DecodeBlob(id, blobIt->second);
        const bool sizeOk = device->TTDVariableSize() ? (!state.empty() && state.size() <= device->TTDStateSize())
                                                      : state.size() == device->TTDStateSize();
        if (!sizeOk)
        {
            ++result.sizeMismatches;
            continue;
        }

        device->TTDLoadState(state.data());
        ++result.restored;
    }

    // Blobs whose id no device claims are intentionally ignored: a session
    // recorded on a build with more devices must still load here.
    result.unclaimedBlobs = blobs.size() - result.restored - result.sizeMismatches;

    return result;
}

std::vector<uint8_t> TTDPeripheralRegistry::EncodeBlob(uint8_t id,
                                                       const uint8_t* state,
                                                       size_t size)
{
    // Blob sizes are 32-bit on disk; the header addition must also fit size_t.
    // Reject invalid sizes before compression can read the source buffer.
    if (size == 0 || !state || size > std::numeric_limits<uint32_t>::max() ||
        size > std::numeric_limits<size_t>::max() - sizeof(PeripheralBlobHeader))
        return {};

    PeripheralBlobHeader header{};
    header.peripheralId = id;
    header.uncompressedSize = static_cast<uint32_t>(size);

    auto compressed = codec::Compress(state, size);
    const bool worthCompressing = !compressed.empty() && compressed.size() < size;

    const uint8_t* payload = worthCompressing ? compressed.data() : state;
    const size_t payloadSize = worthCompressing ? compressed.size() : size;

    // compressedSize == 0 signals a stored (uncompressed) payload.
    header.compressedSize = worthCompressing ? static_cast<uint32_t>(compressed.size()) : 0;

    std::vector<uint8_t> result(sizeof(header) + payloadSize);
    std::memcpy(result.data(), &header, sizeof(header));
    std::memcpy(result.data() + sizeof(header), payload, payloadSize);
    return result;
}

std::vector<uint8_t> TTDPeripheralRegistry::DecodeBlob(uint8_t expectedId,
                                                       const std::vector<uint8_t>& blob)
{
    if (blob.size() < sizeof(PeripheralBlobHeader))
        return {};

    PeripheralBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(header));

    // The id is carried both in the container key and in the blob itself. They
    // must agree: a mismatch means the blob was filed under the wrong device,
    // and loading it would feed one device another's bytes.
    if (header.peripheralId != expectedId)
        return {};

    const uint8_t* payload = blob.data() + sizeof(header);
    const size_t payloadSize = blob.size() - sizeof(header);
    const size_t rawSize = header.uncompressedSize;

    std::vector<uint8_t> state(rawSize);

    if (header.compressedSize > 0)
    {
        if (header.compressedSize != payloadSize)
            return {};

        std::vector<uint8_t> compressed(payload, payload + payloadSize);
        if (!codec::Decompress(compressed, rawSize, state.data()))
            return {};
    }
    else
    {
        if (payloadSize != rawSize)
            return {};
        std::memcpy(state.data(), payload, rawSize);
    }

    return state;
}

} // namespace ttd
