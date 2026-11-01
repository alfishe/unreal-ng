/// The init table detector.
///
/// Built tables: a run of bytes one routine wrote at most twice each and other code then read, its content checked
/// against the generators (MatchGenerator); a match raises the confidence and names the generator.
/// Loaded tables: a page table (LD H,n / LD L,A / LD A,(HL), L1's DataFlow) or a run read through a pointer whose
/// content follows a generator is a table as well, as loaded. All integer arithmetic: the sine check is on the shape
/// (quarter-wave monotone, half-wave antisymmetric), not on libm.

#include <algorithm>
#include <cstdlib>
#include <set>

#include "debugger/analyzers/analysis/detectors/detectors.h"

namespace analysis
{

namespace
{

constexpr size_t kMinTable = 8;

uint16_t RowOffset(uint32_t row)
{
    return static_cast<uint16_t>(((row & 7u) << 8) | ((row & 0x38u) << 2) | ((row & 0xC0u) << 5));
}

/// How many leading entries follow each generator (the longest wins in MatchGenerator)
size_t ScreenRows(const uint8_t* d, size_t n)
{
    if (n < 4)
        return 0;
    const uint16_t base = static_cast<uint16_t>(d[0] | (d[1] << 8));
    if ((base & 0x1FFF) != 0)
        return 0;
    size_t i = 0;
    for (; i * 2 + 1 < n && i < 192; ++i)
        if (static_cast<uint16_t>(d[2 * i] | (d[2 * i + 1] << 8)) != static_cast<uint16_t>(base + RowOffset(i)))
            break;
    return i * 2;
}

size_t ScreenRowsHalf(const uint8_t* d, size_t n, bool high)
{
    size_t best = 0;
    for (uint32_t base = 0; base < 0x10000; base += 0x2000)
    {
        size_t i = 0;
        for (; i < n && i < 192; ++i)
        {
            const auto value = static_cast<uint16_t>(base + RowOffset(static_cast<uint32_t>(i)));
            if (d[i] != (high ? value >> 8 : value & 0xFF))
                break;
        }
        best = std::max(best, i);
    }
    return best;
}

size_t Multiples(const uint8_t* d, size_t n, int& factor)
{
    if (n < 2)
        return 0;
    factor = static_cast<uint8_t>(d[1] - d[0]);
    if (factor == 0)
        return 0;
    size_t i = 0;
    for (; i < n; ++i)
        if (d[i] != static_cast<uint8_t>(d[0] + i * factor))
            break;
    return i;
}

size_t Shifts(const uint8_t* d, size_t n)
{
    size_t left = 0, right = 0;
    while (left < n && d[left] == static_cast<uint8_t>(1u << (left & 7)))
        ++left;
    while (right < n && d[right] == static_cast<uint8_t>(0x80u >> (right & 7)))
        ++right;
    return std::max(left, right);
}

size_t BitReverse(const uint8_t* d, size_t n)
{
    size_t i = 0;
    for (; i < n && i < 256; ++i)
    {
        uint8_t r = 0;
        for (int b = 0; b < 8; ++b)
            if ((i >> b) & 1)
                r = static_cast<uint8_t>(r | (0x80 >> b));
        if (d[i] != r)
            break;
    }
    return i;
}

/// One period of a sine: rises to the maximum at n/4, falls to the minimum at 3n/4, rises back; the second half
/// mirrors the first around the middle value (within 2)
bool Sine(const uint8_t* d, size_t n)
{
    if (n < 16 || n % 4 != 0)
        return false;
    const size_t q = n / 4;
    const auto* s = reinterpret_cast<const int8_t*>(d);
    // Signed or unsigned offset forms
    for (int form = 0; form < 2; ++form)
    {
        const auto at = [&](size_t i) { return form == 0 ? static_cast<int>(d[i]) : static_cast<int>(s[i]); };
        bool ok = true;
        for (size_t i = 0; i < q && ok; ++i)
            ok = at(i) <= at(i + 1);
        for (size_t i = q; i < 3 * q && ok; ++i)
            ok = at(i) >= at(i + 1 < n ? i + 1 : 0);
        for (size_t i = 3 * q; i + 1 < n && ok; ++i)
            ok = at(i) <= at(i + 1);
        const int mid2 = at(q) + at(3 * q);
        for (size_t i = 0; i < n / 2 && ok; ++i)
            ok = std::abs(at(i) + at(i + n / 2) - mid2) <= 2;
        if (ok && at(q) - at(3 * q) >= 16)
            return true;
    }
    return false;
}

}  // namespace

std::string MatchGenerator(const uint8_t* data, size_t size)
{
    if (data == nullptr || size < kMinTable)
        return {};
    if (ScreenRows(data, size) >= std::max<size_t>(kMinTable, size - 1))
        return "screen-rows";
    if (ScreenRowsHalf(data, size, false) == size && size >= 16)
        return "screen-rows-lo";
    if (ScreenRowsHalf(data, size, true) == size && size >= 16)
        return "screen-rows-hi";
    if (BitReverse(data, size) == size && size >= 16)
        return "bit-reverse";
    if (Shifts(data, size) == size)
        return "shift";
    if (Sine(data, size))
        return "sine8";
    int factor = 0;
    if (Multiples(data, size, factor) == size && factor > 1 && factor < 255)
        return "x*" + std::to_string(factor);
    return {};
}

void DetectInitTables(DetectorContext& context)
{
    const L1Result& l1 = context.l1;
    const std::vector<uint8_t> claimed = context.ClaimedMap();

    // Built tables: one writer routine, few writes, read later by others or itself
    uint32_t a = 0;
    while (a <= 0xFFFF)
    {
        const auto z80 = static_cast<uint16_t>(a);
        const ByteUse& u = context.Use(z80);
        const bool candidate = context.InScope(z80) && claimed[a] == 0 && (u.flags & use::kWrite) != 0 &&
                               (u.flags & use::kRead) != 0 && u.writes <= 2 && !u.multiWriter;
        if (!candidate)
        {
            ++a;
            continue;
        }
        const uint32_t writer = context.RoutineEntry(u.firstWriter);
        uint32_t b = a;
        while (b + 1 <= 0xFFFF && ((b + 1) >> 14) == (a >> 14))
        {
            const auto next = static_cast<uint16_t>(b + 1);
            const ByteUse& v = context.Use(next);
            if (!context.InScope(next) || claimed[b + 1] != 0 || (v.flags & use::kWrite) == 0 ||
                (v.flags & use::kRead) == 0 || v.writes > 2 || v.multiWriter ||
                context.RoutineEntry(v.firstWriter) != writer)
                break;
            ++b;
        }
        const size_t n = b - a + 1;
        if (n >= kMinTable && writer != 0xFFFFFFFFu)
        {
            std::vector<uint8_t> content(n);
            for (size_t i = 0; i < n; ++i)
                content[i] = context.End(static_cast<uint16_t>(a + i));
            Region r;
            r.kind = RegionKind::InitTable;
            r.detector = DetectorId::InitTables;
            r.status = RegionStatus::Observed;
            r.geometry.generator = MatchGenerator(content.data(), n);
            r.geometry.count = static_cast<uint16_t>(n);
            r.geometry.stride = static_cast<uint16_t>(r.geometry.generator == "screen-rows" ? 2 : 1);
            r.confidence = r.geometry.generator.empty() ? 600 : 900;
            r.evidence.push_back(Evidence{"writer", static_cast<uint16_t>(writer), n, "one routine wrote it"});
            if (!r.geometry.generator.empty())
                r.evidence.push_back(Evidence{"generator", z80, n, r.geometry.generator});
            r.users = context.UsersOf(z80, static_cast<uint16_t>(b));
            context.Claim(std::move(r), z80, static_cast<uint16_t>(b));
        }
        a = b + 1;
    }

    // Loaded tables: page tables and pointer-read runs whose content follows a generator
    std::set<uint16_t> starts;
    for (const PageTable& t : l1.pageTables)
        starts.insert(t.table);
    for (const PointerUse& p : l1.pointers)
        if (p.form == PointerForm::ImmediateHigh)
            starts.insert(static_cast<uint16_t>(p.value << 8));
    for (const uint16_t start : starts)
    {
        if (!context.InScope(start) || claimed[start] != 0)
            continue;
        std::vector<uint8_t> content(256);
        for (size_t i = 0; i < 256; ++i)
            content[i] = context.Start(static_cast<uint16_t>(start + i));
        // The longest prefix (in 8-byte steps) that follows a generator
        std::string generator;
        size_t length = 0;
        for (size_t n = 256; n >= kMinTable; n -= 8)
        {
            generator = MatchGenerator(content.data(), n);
            if (!generator.empty())
            {
                length = n;
                break;
            }
        }
        if (generator.empty())
            continue;
        Region r;
        r.kind = RegionKind::InitTable;
        r.detector = DetectorId::InitTables;
        r.status = RegionStatus::Observed;
        r.geometry.generator = generator;
        r.geometry.count = static_cast<uint16_t>(length);
        r.geometry.stride = static_cast<uint16_t>(generator == "screen-rows" ? 2 : 1);
        r.confidence = 850;
        r.evidence.push_back(Evidence{"page-table", start, 0, "read as a 256-byte aligned table"});
        r.evidence.push_back(Evidence{"generator", start, length, generator + " (as loaded)"});
        r.users = context.UsersOf(start, static_cast<uint16_t>(start + length - 1));
        context.Claim(std::move(r), start, static_cast<uint16_t>(start + length - 1));
    }
}

}  // namespace analysis
