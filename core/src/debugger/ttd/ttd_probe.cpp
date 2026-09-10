/// @file ttd_probe.cpp
/// @brief TTD access probe — implementation.
///
/// See ttd_probe.h for the threading model and design rationale.

#include "ttd_probe.h"

#include <cstring>

namespace ttd {

// ---------------------------------------------------------------------------
// TTDAccessType strings (automation contract — lowercase, stable)
// ---------------------------------------------------------------------------

const char* TTDAccessTypeToString(TTDAccessType k)
{
    switch (k)
    {
        case TTDAccessType::Write:   return "write";
        case TTDAccessType::Read:    return "read";
        case TTDAccessType::Execute: return "execute";
        case TTDAccessType::Io:      return "io";
    }
    return "unknown";
}

TTDAccessType TTDAccessTypeFromString(const char* s)
{
    if (!s) return TTDAccessType::Write;
    if (std::strcmp(s, "read") == 0)    return TTDAccessType::Read;
    if (std::strcmp(s, "execute") == 0) return TTDAccessType::Execute;
    if (std::strcmp(s, "io") == 0)      return TTDAccessType::Io;
    // Default fallback: "write" (also matches "w"/"write")
    return TTDAccessType::Write;
}

// ---------------------------------------------------------------------------
// TTDAccessProbe
// ---------------------------------------------------------------------------

void TTDAccessProbe::Arm(const TTDSearchQuery& q)
{
    _query = q;
    _hits.clear();
    _armed.store(true, std::memory_order_release);
}

void TTDAccessProbe::Disarm()
{
    _armed.store(false, std::memory_order_release);
}

void TTDAccessProbe::Reset()
{
    Disarm();
    _query = TTDSearchQuery{};
    _hits.clear();
}

std::vector<TTDSearchResult> TTDAccessProbe::ExtractHits()
{
    std::vector<TTDSearchResult> out = std::move(_hits);
    _hits.clear();
    return out;
}

} // namespace ttd
