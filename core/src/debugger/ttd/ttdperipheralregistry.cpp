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

void TTDPeripheralRegistry::CaptureAll(
    const std::unordered_map<uint8_t, std::vector<uint8_t>>* prevBlobs,
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

        // Find previous state for delta encoding
        std::vector<uint8_t> prevState;  // Keep in scope for CompressWithDelta
        if (prevBlobs)
        {
            auto it = prevBlobs->find(id);
            if (it != prevBlobs->end() && !it->second.empty())
            {
                prevState = DecompressWithDelta(it->second, nullptr, 0);
            }
        }

        // Compress with delta (if supported and previous state valid)
        const uint8_t* prevData = prevState.size() == stateSize ? prevState.data() : nullptr;
        outBlobs[id] = CompressWithDelta(
            currentState.data(), stateSize,
            prevData, prevState.size(),
            device->TTDSupportsDelta());
    }
}

void TTDPeripheralRegistry::RestoreAll(
    const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs,
    const std::unordered_map<uint8_t, std::vector<uint8_t>>* prevBlobs) const
{
    for (const auto& [id, blob] : blobs)
    {
        if (blob.empty())
            continue;

        auto it = _devices.find(id);
        if (it == _devices.end() || !it->second)
            continue;

        TTDSerializable* device = it->second;

        // Find previous state for delta decoding.
        // IMPORTANT: prevState must remain in scope until DecompressWithDelta
        // completes, as prevData points into its storage.
        std::vector<uint8_t> prevState;
        const uint8_t* prevData = nullptr;
        size_t prevSize = 0;
        if (prevBlobs)
        {
            auto prevIt = prevBlobs->find(id);
            if (prevIt != prevBlobs->end() && !prevIt->second.empty())
            {
                prevState = DecompressWithDelta(prevIt->second, nullptr, 0);
                prevData = prevState.data();
                prevSize = prevState.size();
            }
        }

        // Decompress and restore
        auto state = DecompressWithDelta(blob, prevData, prevSize);
        if (state.size() == device->TTDStateSize())
        {
            device->TTDLoadState(state.data());
        }
    }
}

std::vector<uint8_t> TTDPeripheralRegistry::CompressWithDelta(
    const uint8_t* current, size_t size,
    const uint8_t* previous, size_t prevSize,
    bool supportsDelta)
{
    if (size == 0 || !current)
        return {};

    PeripheralBlobHeader header{};
    header.uncompressedSize = static_cast<uint32_t>(size);

    // Decide whether to use delta encoding
    const bool useDelta = supportsDelta && previous && prevSize == size;

    if (useDelta)
    {
        // XOR delta against previous
        std::vector<uint8_t> delta(size);
        for (size_t i = 0; i < size; ++i)
        {
            delta[i] = current[i] ^ previous[i];
        }

        // Compress the delta
        auto compressed = codec::Compress(delta.data(), size);

        // Only use delta if it's smaller
        if (!compressed.empty() && compressed.size() < size)
        {
            header.flags = 1;  // isDelta
            header.compressedSize = static_cast<uint32_t>(compressed.size());

            std::vector<uint8_t> result(sizeof(header) + compressed.size());
            std::memcpy(result.data(), &header, sizeof(header));
            std::memcpy(result.data() + sizeof(header), compressed.data(), compressed.size());
            return result;
        }
    }

    // Fall back to full state compression
    auto compressed = codec::Compress(current, size);
    if (!compressed.empty() && compressed.size() < size)
    {
        header.flags = 0;  // not delta
        header.compressedSize = static_cast<uint32_t>(compressed.size());

        std::vector<uint8_t> result(sizeof(header) + compressed.size());
        std::memcpy(result.data(), &header, sizeof(header));
        std::memcpy(result.data() + sizeof(header), compressed.data(), compressed.size());
        return result;
    }

    // Store uncompressed if compression didn't help
    header.flags = 0;
    header.compressedSize = 0;  // signals uncompressed

    std::vector<uint8_t> result(sizeof(header) + size);
    std::memcpy(result.data(), &header, sizeof(header));
    std::memcpy(result.data() + sizeof(header), current, size);
    return result;
}

std::vector<uint8_t> TTDPeripheralRegistry::DecompressWithDelta(
    const std::vector<uint8_t>& blob,
    const uint8_t* previous, size_t prevSize)
{
    if (blob.size() < sizeof(PeripheralBlobHeader))
        return {};

    PeripheralBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(header));

    const uint8_t* payload = blob.data() + sizeof(header);
    const size_t payloadSize = blob.size() - sizeof(header);
    const size_t rawSize = header.uncompressedSize;

    std::vector<uint8_t> state(rawSize);

    if (header.compressedSize > 0)
    {
        // Decompress
        std::vector<uint8_t> compressed(payload, payload + payloadSize);
        if (!codec::Decompress(compressed, rawSize, state.data()))
            return {};
    }
    else
    {
        // Uncompressed
        if (payloadSize != rawSize)
            return {};
        std::memcpy(state.data(), payload, rawSize);
    }

    // Apply XOR delta if flagged
    if (header.flags & 1)
    {
        // Delta blob REQUIRES previous state - without it, the data is garbage.
        // This is a hard error, not a silent skip.
        if (!previous || prevSize != rawSize)
            return {};  // Fail: delta blob cannot be decoded without predecessor

        for (size_t i = 0; i < rawSize; ++i)
        {
            state[i] ^= previous[i];
        }
    }

    return state;
}

} // namespace ttd
