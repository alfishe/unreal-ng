#include "signaturecache.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "3rdparty/digestpp/digestpp.hpp"

/// region <Digest and cache access>

std::string SignatureCache::Sha256Hex(const uint8_t* buffer, size_t length)
{
    if (buffer == nullptr || length == 0)
        return std::string();

    // The key is built once and reused for both the lookup and the insert
    std::string key(reinterpret_cast<const char*>(buffer), length);
    State& state = GetState();

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (const Entry* entry = FindLocked(state, key, NowLocked(state)))
            return entry->signature;
    }

    // Digest outside the lock: concurrent initialisations of different ROMs do not serialise on it
    std::string signature = digestpp::sha256().absorb(buffer, length).hexdigest();

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        InsertLocked(state, std::move(key), signature, NowLocked(state));
    }

    return signature;
}

std::string SignatureCache::Lookup(const uint8_t* buffer, size_t length)
{
    if (buffer == nullptr || length == 0)
        return std::string();

    const std::string key(reinterpret_cast<const char*>(buffer), length);
    State& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);
    const Clock::time_point now = NowLocked(state);
    const Entry* entry = FindLocked(state, key, now);
    std::string result = entry ? entry->signature : std::string();
    SweepLocked(state, now, false);
    return result;
}

void SignatureCache::Store(const uint8_t* buffer, size_t length, const std::string& signature)
{
    if (buffer == nullptr || length == 0 || signature.empty())
        return;

    std::string key(reinterpret_cast<const char*>(buffer), length);
    State& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);
    InsertLocked(state, std::move(key), signature, NowLocked(state));
}

/// endregion </Digest and cache access>

/// region <Policy and eviction>

void SignatureCache::SetPolicy(const Policy& policy)
{
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.policy = policy;
}

SignatureCache::Policy SignatureCache::GetPolicy()
{
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.policy;
}

size_t SignatureCache::Sweep()
{
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return SweepLocked(state, NowLocked(state), true);
}

/// endregion </Policy and eviction>

/// region <Diagnostics and tests>

size_t SignatureCache::Size()
{
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.entries.size();
}

size_t SignatureCache::Bytes()
{
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.bytes;
}

SignatureCache::EntryInfo SignatureCache::Inspect(const uint8_t* buffer, size_t length)
{
    EntryInfo info;
    if (buffer == nullptr || length == 0)
        return info;

    const std::string key(reinterpret_cast<const char*>(buffer), length);
    State& state = GetState();

    std::lock_guard<std::mutex> lock(state.mutex);
    auto it = state.entries.find(key);
    if (it != state.entries.end())
    {
        info.found = true;
        info.createdAt = it->second.createdAt;
        info.lastUsedAt = it->second.lastUsedAt;
        info.hits = it->second.hits;
    }
    return info;
}

void SignatureCache::Clear()
{
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.entries.clear();
    state.bytes = 0;
    state.lastSweep = NowLocked(state);
}

void SignatureCache::SetClockForTesting(std::function<Clock::time_point()> now)
{
    State& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.now = std::move(now);
    state.lastSweep = NowLocked(state);
}

/// endregion </Diagnostics and tests>

/// region <Implementation>

SignatureCache::State& SignatureCache::GetState()
{
    static State state;
    return state;
}

SignatureCache::Clock::time_point SignatureCache::NowLocked(State& state)
{
    return state.now ? state.now() : Clock::now();
}

const SignatureCache::Entry* SignatureCache::FindLocked(State& state, const std::string& key, Clock::time_point now)
{
    auto it = state.entries.find(key);
    if (it == state.entries.end())
        return nullptr;

    it->second.lastUsedAt = now;
    it->second.hits++;
    return &it->second;
}

void SignatureCache::InsertLocked(State& state, std::string&& key, const std::string& signature, Clock::time_point now)
{
    const size_t keyBytes = key.size();

    // A block larger than the whole budget could never stay cached; admitting it would only flush
    // every other entry on the way out. Refresh it if somehow present, otherwise leave the cache alone
    if (keyBytes > state.policy.maxBytes)
    {
        auto existing = state.entries.find(key);
        if (existing != state.entries.end())
            existing->second.lastUsedAt = now;
        SweepLocked(state, now, false);
        return;
    }

    auto [it, inserted] = state.entries.try_emplace(std::move(key));
    if (inserted)
    {
        it->second.signature = signature;
        it->second.createdAt = now;
        state.bytes += keyBytes;
    }
    it->second.lastUsedAt = now;

    SweepLocked(state, now, false);
}

size_t SignatureCache::SweepLocked(State& state, Clock::time_point now, bool force)
{
    const Policy& policy = state.policy;
    const bool intervalElapsed = (now - state.lastSweep) >= policy.sweepInterval;
    const bool overBudget = state.bytes > policy.maxBytes;

    if (!force && !intervalElapsed && !overBudget)
        return 0;

    size_t evicted = 0;

    // 1. Idle entries - only on the regular cadence (or forced), not on every over-budget insert
    if (force || intervalElapsed)
    {
        for (auto it = state.entries.begin(); it != state.entries.end();)
        {
            if (now - it->second.lastUsedAt >= policy.maxIdle)
            {
                state.bytes -= it->first.size();
                it = state.entries.erase(it);
                evicted++;
            }
            else
            {
                ++it;
            }
        }
        state.lastSweep = now;
    }

    // 2. Byte budget - drop least recently used until the content fits
    if (state.bytes > policy.maxBytes)
    {
        std::vector<Map::iterator> byAge;
        byAge.reserve(state.entries.size());
        for (auto it = state.entries.begin(); it != state.entries.end(); ++it)
            byAge.push_back(it);

        std::sort(byAge.begin(), byAge.end(),
                  [](const Map::iterator& a, const Map::iterator& b) { return a->second.lastUsedAt < b->second.lastUsedAt; });

        for (auto& it : byAge)
        {
            if (state.bytes <= policy.maxBytes)
                break;
            state.bytes -= it->first.size();
            state.entries.erase(it);
            evicted++;
        }
    }

    return evicted;
}

/// endregion </Implementation>
