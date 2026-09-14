#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

/// @brief Process-wide cache of content digests, keyed by the content itself.
///
/// Digesting a block of bytes (a 16 KB ROM page, a tape block, a snapshot) costs a full pass of the hash
/// function, and the same blocks come back again and again within one process: every Emulator::Init
/// names its ROM pages by SHA-256, software identification will digest the same files on every load,
/// and the test suite alone creates several hundred emulators. Each distinct block is therefore digested
/// once and served from here afterwards.
///
/// The key is the content itself, which makes the cache exact: a lookup is one hash of the key plus one
/// memcmp against the stored bytes, never a probabilistic fingerprint. The price is one copy of every
/// cached block, so the cache is bounded by an eviction policy (see Policy).
///
/// Eviction is fully synchronous - there is no background thread. Every entry records when it was
/// created and when it was last served. The public calls run a sweep themselves when one is due:
/// - entries not used for Policy::maxIdle are dropped;
/// - if the cached content still exceeds Policy::maxBytes, the least recently used entries are dropped
///   until it fits.
/// A sweep is due at most once per Policy::sweepInterval, and immediately whenever a store pushes the
/// content over Policy::maxBytes. A block larger than Policy::maxBytes on its own is never admitted.
/// An evicted block is simply digested again on its next use.
///
/// Thread-safe: emulator instances may initialise concurrently on different threads. The digest itself
/// is computed outside the lock.
///
/// Usage: call Sha256Hex() and forget about the cache. Lookup()/Store() are for callers that compute a
/// different digest themselves and want the same caching; the remaining methods are for tuning and tests.
class SignatureCache
{
    /// region <Types>
public:
    using Clock = std::chrono::steady_clock;

    /// @brief Eviction policy
    struct Policy
    {
        /// @brief Drop entries not served for this long
        Clock::duration maxIdle = std::chrono::minutes(10);

        /// @brief Upper bound for the cached content in bytes (keys only - digests are negligible)
        size_t maxBytes = 64u * 1024u * 1024u;

        /// @brief Minimum time between two idle sweeps
        Clock::duration sweepInterval = std::chrono::seconds(30);
    };

    /// @brief Snapshot of one entry's bookkeeping (tests, diagnostics)
    struct EntryInfo
    {
        bool found = false;
        Clock::time_point createdAt{};
        Clock::time_point lastUsedAt{};
        uint64_t hits = 0;
    };
    /// endregion </Types>

    /// region <Digest and cache access>
public:
    /// @brief Hex SHA-256 digest of a block, served from the cache when the block was seen before
    /// @param buffer Block content
    /// @param length Block length in bytes
    /// @return Lower-case hex digest (64 characters), or an empty string for a null/empty buffer
    static std::string Sha256Hex(const uint8_t* buffer, size_t length);

    /// @brief Return the cached digest for a block and mark it as used, or an empty string on a miss
    /// @param buffer Block content
    /// @param length Block length in bytes
    /// @return Cached digest, or "" on a miss
    static std::string Lookup(const uint8_t* buffer, size_t length);

    /// @brief Remember the digest of a block. Storing a block that is already cached only marks it as used;
    ///        null/empty buffers and empty signatures are ignored
    /// @param buffer Block content
    /// @param length Block length in bytes
    /// @param signature Digest of exactly that content
    static void Store(const uint8_t* buffer, size_t length, const std::string& signature);
    /// endregion </Digest and cache access>

    /// region <Policy and eviction>
public:
    /// @brief Replace the eviction policy. Takes effect on the next sweep; call Sweep() to apply it now
    static void SetPolicy(const Policy& policy);

    /// @brief Current eviction policy
    static Policy GetPolicy();

    /// @brief Run an eviction sweep now, regardless of Policy::sweepInterval
    /// @return Number of entries evicted
    static size_t Sweep();
    /// endregion </Policy and eviction>

    /// region <Diagnostics and tests>
public:
    /// @brief Number of distinct blocks currently cached
    static size_t Size();

    /// @brief Total cached content in bytes
    static size_t Bytes();

    /// @brief Bookkeeping of the entry for a block (found == false when not cached). Does not mark it as used
    static EntryInfo Inspect(const uint8_t* buffer, size_t length);

    /// @brief Drop every cached digest. Policy and clock are kept
    static void Clear();

    /// @brief Replace the time source (tests). An empty function restores Clock::now
    static void SetClockForTesting(std::function<Clock::time_point()> now);
    /// endregion </Diagnostics and tests>

    /// region <Implementation>
private:
    struct Entry
    {
        std::string signature;
        Clock::time_point createdAt;
        Clock::time_point lastUsedAt;
        uint64_t hits = 0;
    };

    using Map = std::unordered_map<std::string, Entry>;

    struct State
    {
        std::mutex mutex;
        Map entries;
        size_t bytes = 0;
        Policy policy;
        Clock::time_point lastSweep{};
        std::function<Clock::time_point()> now;
    };

    static State& GetState();

    /// @brief Current time from the configured source. Caller holds the lock
    static Clock::time_point NowLocked(State& state);

    /// @brief Find by a pre-built key and mark as used. Caller holds the lock
    static const Entry* FindLocked(State& state, const std::string& key, Clock::time_point now);

    /// @brief Insert (or refresh) by a moved key, then sweep if due. Caller holds the lock
    static void InsertLocked(State& state, std::string&& key, const std::string& signature, Clock::time_point now);

    /// @brief Sweep when forced, when the interval elapsed, or when over the byte budget. Caller holds the lock
    /// @return Number of entries evicted
    static size_t SweepLocked(State& state, Clock::time_point now, bool force);
    /// endregion </Implementation>
};
