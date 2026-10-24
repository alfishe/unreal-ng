#pragma once

/// @file match.h
/// @brief The signature match rules of swsignature (input-and-profiles.md §4), header-only and free of the core: the
/// controllers library (core/controllers, which must not use core symbols) and the profile index use them on the
/// strings a profile stores, the library itself on what it computed.
///
/// A TR-DOS file set travels as tokens, two per catalogue entry, the hex the first 64 bits of a SHA-256:
/// - `n:<16 hex>`: the entry itself (name as stored, type, start, length), not its data;
/// - `c:<16 hex>` (BASIC `B`, code `C`) or `d:<16 hex>` (any other type): the entry with its data.
///
/// The catalog rule (POC 034): the candidate holds every entry of the reference (`n:` tokens), at most one reference
/// file has other data (its `c:` / `d:` token missing) but not all of them, and at most max(2, n) entries were added.
/// A disk the game saved to (a high-score file added or rewritten in place), a re-release with one file patched, still
/// match; another title that only shares a loader of the same name and length does not (its one file differs). A
/// reference without `n:` tokens (a hand-written set) falls back to the Jaccard rule: Jaccard >= kFuzzyJaccard over
/// the `c:` / `d:` tokens and no code file of the reference missing.
///
/// @code
///   FileSetScore score = CompareFileSets(profile.fileSet, loaded.FileSet());
///   if (score.matches) ... // score.reason
/// @endcode

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace unrealng::swsignature
{

/// Jaccard similarity of two file sets at and above which they are the same title (§4: "fuzzy")
constexpr double kFuzzyJaccard = 0.8;
/// Share of equal 256-byte RAM pages at and above which two snapshots hold the same program (§4: "memory")
constexpr double kMemoryShare = 0.9;
/// Pages both snapshots must hold for a memory match: uniform pages (zero-filled RAM, one byte value) are left out
/// of the signature, so two snapshots near the boot state have too few pages in common to match (POC 034)
constexpr size_t kMemoryMinPages = 16;

/// Signature prefixes: what a profile's `match.signatures` and the index keys hold
constexpr const char* kFilePrefix = "file:sha256:";      ///< the whole file
constexpr const char* kTapePrefix = "tape:sha256:";      ///< TAP / TZX data payloads
constexpr const char* kTrdosPrefix = "trdset:sha256:";   ///< TR-DOS file set
constexpr const char* kMemoryPrefix = "mem:sha256:";     ///< snapshot RAM outside the screen and the stack page

/// Entries a candidate may add to the reference's catalogue: max(kCatalogAdded, the reference's entry count)
constexpr size_t kCatalogAdded = 2;

/// A content token of a code file (`c:`)
inline bool IsCodeToken(const std::string& token)
{
    return token.size() > 2 && token[0] == 'c' && token[1] == ':';
}

/// A catalogue entry token (`n:`)
inline bool IsCatalogToken(const std::string& token)
{
    return token.size() > 2 && token[0] == 'n' && token[1] == ':';
}

/// A well formed token: `n:`, `c:` or `d:` and 16 lower-case hex digits
inline bool IsFileSetToken(const std::string& token)
{
    if (token.size() != 18 || (token[0] != 'c' && token[0] != 'd' && token[0] != 'n') || token[1] != ':')
        return false;
    return std::all_of(token.begin() + 2, token.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

struct FileSetScore
{
    bool catalogRule = false;  ///< the reference has `n:` tokens: the catalog rule decided, else the Jaccard rule
    size_t entries = 0;        ///< the reference's entries (catalog rule) or content tokens (Jaccard rule)
    size_t entriesMissing = 0; ///< reference entries not in the candidate's catalogue
    size_t changed = 0;        ///< reference content tokens not in the candidate (a file with other data)
    size_t added = 0;          ///< candidate entries beyond the reference's
    double jaccard = 0.0;      ///< |A ∩ B| / |A ∪ B| over the distinct content tokens
    size_t common = 0;         ///< content tokens in both
    size_t codeMissing = 0;    ///< code content tokens of the reference not in the candidate
    bool matches = false;

    /// "every entry kept, 1 of 5 files changed, 1 added" / "Jaccard 0.83, ..." (without the "file set: " prefix)
    std::string Reason() const
    {
        char text[160];
        if (catalogRule)
        {
            if (entriesMissing > 0)
                std::snprintf(text, sizeof text, "%zu of %zu entries missing", entriesMissing, entries);
            else
                std::snprintf(text, sizeof text, "every entry kept, %zu of %zu files changed, %zu added", changed,
                              entries, added);
        }
        else
            std::snprintf(text, sizeof text, "Jaccard %.2f, %zu common, %zu code file(s) missing", jaccard, common,
                          codeMissing);
        return text;
    }
};

/// The fuzzy rule for @p reference (a profile's, the known title) against @p candidate (the loaded disk): the catalog
/// rule when the reference has `n:` tokens, else the Jaccard rule with @p threshold. Two empty sets do not match
inline FileSetScore CompareFileSets(const std::vector<std::string>& referenceTokens,
                                    const std::vector<std::string>& candidateTokens, double threshold = kFuzzyJaccard)
{
    auto split = [](const std::vector<std::string>& tokens, std::vector<std::string>& names,
                    std::vector<std::string>& contents) {
        for (const std::string& token : tokens)
            (IsCatalogToken(token) ? names : contents).push_back(token);
        for (std::vector<std::string>* list : {&names, &contents})
        {
            std::sort(list->begin(), list->end());
            list->erase(std::unique(list->begin(), list->end()), list->end());
        }
    };
    std::vector<std::string> referenceNames, reference, candidateNames, candidate;
    split(referenceTokens, referenceNames, reference);
    split(candidateTokens, candidateNames, candidate);
    FileSetScore score;
    if (reference.empty() || candidate.empty())
        return score;
    std::vector<std::string> common;
    std::set_intersection(reference.begin(), reference.end(), candidate.begin(), candidate.end(),
                          std::back_inserter(common));
    score.common = common.size();
    score.changed = reference.size() - common.size();
    const size_t united = reference.size() + candidate.size() - common.size();
    score.jaccard = static_cast<double>(common.size()) / static_cast<double>(united);
    for (const std::string& token : reference)
        if (IsCodeToken(token) && !std::binary_search(candidate.begin(), candidate.end(), token))
            ++score.codeMissing;
    if (!referenceNames.empty())
    {
        score.catalogRule = true;
        score.entries = referenceNames.size();
        for (const std::string& token : referenceNames)
            if (!std::binary_search(candidateNames.begin(), candidateNames.end(), token))
                ++score.entriesMissing;
        score.added = candidateNames.size() > referenceNames.size() ? candidateNames.size() - referenceNames.size() : 0;
        score.matches = score.entriesMissing == 0 && score.changed <= 1 && score.common > 0 &&
                        score.added <= std::max(kCatalogAdded, referenceNames.size());
        return score;
    }
    score.entries = reference.size();
    score.matches = score.jaccard >= threshold && score.codeMissing == 0;
    return score;
}

}  // namespace unrealng::swsignature
