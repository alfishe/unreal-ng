#include "ttdperipheralregistry.h"
#include "ttdcompression.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace ttd {

void TTDPeripheralRegistry::Register(PeripheralId id, TTDSerializable* device)
{
    if (device)
    {
        _devices[static_cast<uint8_t>(id)] = device;
    }
}

void TTDPeripheralRegistry::Unregister(PeripheralId id)
{
    _devices.erase(static_cast<uint8_t>(id));
}

void TTDPeripheralRegistry::Clear()
{
    _devices.clear();
}

bool TTDPeripheralRegistry::IsRegistered(PeripheralId id) const
{
    return _devices.find(static_cast<uint8_t>(id)) != _devices.end();
}

TTDSerializable* TTDPeripheralRegistry::GetDevice(PeripheralId id) const
{
    auto it = _devices.find(static_cast<uint8_t>(id));
    return it != _devices.end() ? it->second : nullptr;
}

size_t TTDPeripheralRegistry::TotalStateSize() const
{
    size_t total = 0;
    for (const auto& [id, device] : _devices)
    {
        if (device)
        {
            total += device->TTDStateSize();
        }
    }
    return total;
}

uint64_t TTDPeripheralRegistry::ComputePeripheralHash() const
{
    uint64_t combined = 0;
    for (const auto& [id, device] : _devices)
    {
        if (device)
        {
            // Rotate each contribution by its id so two devices holding the
            // same state do not cancel under the XOR, then XOR so the result
            // does not depend on _devices' (unordered) iteration order.
            //
            // The shift == 0 branch is required, not defensive: for id 0
            // (PeripheralId::TurboSound) the naive form evaluates
            // `contribution >> 64`, which is undefined for a 64-bit type and
            // observably yields different values at -O0 and -O2 — a divergence
            // hash that disagreed between debug and release builds would make
            // the oracle report phantom divergences.
            const unsigned shift = id & 0x3F;
            const uint64_t contribution = device->TTDHashState();
            const uint64_t rotated = (shift == 0)
                                     ? contribution
                                     : ((contribution << shift) | (contribution >> (64 - shift)));
            combined ^= rotated;
        }
    }
    return combined;
}

void TTDPeripheralRegistry::CaptureAll(
    std::unordered_map<uint8_t, std::vector<uint8_t>>& outBlobs) const
{
    outBlobs.clear();

    for (const auto& [id, device] : _devices)
    {
        if (!device)
            continue;

        const size_t stateSize = device->TTDStateSize();
        if (stateSize == 0)
            continue;

        // Capture current state
        std::vector<uint8_t> currentState(stateSize);
        device->TTDSaveState(currentState.data());

        outBlobs[id] = EncodeBlob(id, currentState.data(), stateSize);
    }
}

TTDRestoreReport TTDPeripheralRegistry::RestoreAll(
    const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs) const
{
    TTDRestoreReport result;

    // Iterate the registered devices, not the blobs. Driving from the blob map
    // would silently skip a device the checkpoint has no entry for, leaving it
    // holding live state from whatever the machine was doing before the seek —
    // a divergence with no error anywhere. Driving from the devices lets us at
    // least name the device that is about to be left stale.
    for (const auto& [id, device] : _devices)
    {
        if (!device)
            continue;

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
        if (state.size() != device->TTDStateSize())
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
    if (size == 0 || !state)
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
