#pragma once

// The breakpoint model of conditional-breakpoints design §4 / §5.1 (without conditions), the set generators
// every experiment uses, and the reference matcher that decides what "correct" is.

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "trace.h"

namespace poc
{

/// Which accesses a breakpoint watches (one or more bits)
enum Watch : uint8_t
{
    WExec = 1,   // instruction start (PC)
    WRead = 2,   // data read and opcode / operand fetch (the debug read path checks both)
    WWrite = 4,
    WIn = 8,
    WOut = 16
};

/// What address space the range is in
enum class Space : uint8_t
{
    Z80,       // CPU addresses start..end, any page
    Z80Slot,   // CPU addresses start..end, only while `page` is mapped in that slot (design F3)
    Phys,      // page `page`, offsets start..end (0..#3FFF), through whatever slot shows it (design F4)
    Port       // port `start`, matched as (port & mask) == (start & mask) (design F7)
};

struct Breakpoint
{
    uint16_t id = 0;
    uint8_t watch = WExec;
    Space space = Space::Z80;
    uint16_t start = 0;
    uint16_t end = 0;         // inclusive; == start for one address
    PhysPage page = 0;        // Z80Slot / Phys
    uint16_t mask = 0xFFFF;   // Port
};

/// What a matcher answers for one access: the id of a matching breakpoint, or kNone
constexpr int kNone = -1;

/// The watch bit an event kind is checked against
inline uint8_t WatchOf(uint8_t kind)
{
    switch (kind)
    {
        case KExec: return WExec;
        case KFetch:
        case KRead: return WRead;
        case KWrite: return WWrite;
        case KIn: return WIn;
        case KOut: return WOut;
        default: return 0;
    }
}

/// The definition of a hit, by brute force over every breakpoint. Returns the lowest matching id, so every
/// matcher can be compared hit by hit (a matcher may return any matching id; the harness compares "hit or
/// not" per event and the hit counts).
class ReferenceMatcher
{
public:
    explicit ReferenceMatcher(std::vector<Breakpoint> set) : _set(std::move(set)) {}

    int Match(uint8_t kind, uint16_t addr, const PhysPage slots[4]) const
    {
        const uint8_t want = WatchOf(kind);
        int best = kNone;
        for (const Breakpoint& b : _set)
        {
            if (!(b.watch & want))
                continue;
            bool hit = false;
            switch (b.space)
            {
                case Space::Z80: hit = addr >= b.start && addr <= b.end; break;
                case Space::Z80Slot: hit = addr >= b.start && addr <= b.end && slots[addr >> 14] == b.page; break;
                case Space::Phys:
                {
                    const uint16_t offset = addr & 0x3FFF;
                    hit = slots[addr >> 14] == b.page && offset >= b.start && offset <= b.end;
                    break;
                }
                case Space::Port: hit = (addr & b.mask) == (b.start & b.mask); break;
            }
            if (hit && (best == kNone || b.id < best))
                best = b.id;
        }
        return best;
    }

private:
    std::vector<Breakpoint> _set;
};

/// What addresses / pages / ports a trace touches, so a generated set can be placed where it hits ("warm")
/// or where it never does ("cold": the pure filtering cost, the common case in a debugging session)
struct TraceProfile
{
    std::vector<uint16_t> execAddrs, readAddrs, writeAddrs, ports;
    std::vector<uint16_t> coldAddrs;  // Z80 addresses no event of the trace touches
    std::vector<PhysPage> pages;

    explicit TraceProfile(const Trace& t)
    {
        std::vector<uint8_t> touched(0x10000, 0);
        std::vector<uint8_t> seenPort(0x10000, 0);
        std::vector<uint8_t> seenPage(0x300, 0);
        for (int s = 0; s < 4; s++)
            seenPage[t.initialSlots[s]] = 1;
        for (const Event& e : t.events)
        {
            switch (e.kind)
            {
                case KExec: if (!(touched[e.addr] & 1)) execAddrs.push_back(e.addr); touched[e.addr] |= 1; break;
                case KRead: if (!(touched[e.addr] & 2)) readAddrs.push_back(e.addr); touched[e.addr] |= 2; break;
                case KFetch: touched[e.addr] |= 8; break;
                case KWrite: if (!(touched[e.addr] & 4)) writeAddrs.push_back(e.addr); touched[e.addr] |= 4; break;
                case KIn:
                case KOut: if (!seenPort[e.addr]) ports.push_back(e.addr); seenPort[e.addr] = 1; break;
                case KRemap: seenPage[e.value] = 1; break;
                default: break;
            }
        }
        for (uint32_t a = 0; a < 0x10000; a++)
            if (!touched[a])
                coldAddrs.push_back(static_cast<uint16_t>(a));
        for (uint32_t p = 0; p < seenPage.size(); p++)
            if (seenPage[p])
                pages.push_back(static_cast<PhysPage>(p));
    }
};

/// A named breakpoint set recipe: `count` breakpoints of one shape
struct SetSpec
{
    std::string name;
    uint32_t count = 0;
    enum Shape : uint8_t { ExecPoints, ReadWritePoints, Ranges, PhysRanges, SlotRanges, Ports, Mixed } shape = ExecPoints;
    bool warm = false;          // placed on touched addresses (they hit) instead of untouched ones
    uint16_t rangeLen = 256;    // Ranges / PhysRanges / SlotRanges: the length (bytes)
};

/// Builds a deterministic set from a recipe and a trace profile
inline std::vector<Breakpoint> MakeSet(const SetSpec& spec, const TraceProfile& prof, uint32_t seed = 22)
{
    std::mt19937 rng(seed);
    std::vector<Breakpoint> set;
    auto pick = [&](const std::vector<uint16_t>& from, uint16_t fallback) -> uint16_t {
        if (from.empty())
            return fallback;
        return from[rng() % from.size()];
    };
    auto pickPage = [&]() -> PhysPage { return prof.pages.empty() ? MakePhys(kRam, 0) : prof.pages[rng() % prof.pages.size()]; };
    const std::vector<uint16_t>& cold = prof.coldAddrs;
    for (uint32_t i = 0; i < spec.count; i++)
    {
        Breakpoint b;
        b.id = static_cast<uint16_t>(i + 1);
        SetSpec::Shape shapeOf = spec.shape;
        if (spec.shape == SetSpec::Mixed)
        {
            // A debugging session: mostly code points, some watchpoints and ranges, a few ports
            const uint32_t r = rng() % 100;
            shapeOf = r < 55 ? SetSpec::ExecPoints : r < 75 ? SetSpec::ReadWritePoints : r < 88 ? SetSpec::Ranges
                    : r < 95 ? SetSpec::PhysRanges : SetSpec::Ports;
        }
        switch (shapeOf)
        {
            case SetSpec::ExecPoints:
                b.watch = WExec;
                b.start = b.end = spec.warm ? pick(prof.execAddrs, 0) : pick(cold, 0);
                break;
            case SetSpec::ReadWritePoints:
                b.watch = (rng() & 1) ? WWrite : WRead;
                b.start = b.end = spec.warm ? pick(b.watch == WWrite ? prof.writeAddrs : prof.readAddrs, 0) : pick(cold, 0);
                break;
            case SetSpec::Ranges:
            {
                b.watch = (rng() % 3 == 0) ? WRead : WWrite;
                const uint16_t at = spec.warm ? pick(b.watch == WWrite ? prof.writeAddrs : prof.readAddrs, 0) : pick(cold, 0);
                b.start = at;
                b.end = static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, at + spec.rangeLen - 1u));
                break;
            }
            case SetSpec::PhysRanges:
            case SetSpec::SlotRanges:
            {
                b.watch = (rng() & 1) ? WWrite : WRead;
                b.space = shapeOf == SetSpec::PhysRanges ? Space::Phys : Space::Z80Slot;
                b.page = pickPage();
                const uint16_t at = static_cast<uint16_t>((spec.warm ? pick(prof.writeAddrs, 0) : pick(cold, 0)) & 0x3FFF);
                b.start = at;
                b.end = static_cast<uint16_t>(std::min<uint32_t>(0x3FFF, at + spec.rangeLen - 1u));
                if (b.space == Space::Z80Slot)
                {
                    // CPU addresses in a slot where the page can appear
                    const uint16_t slotBase = static_cast<uint16_t>((rng() % 4) << 14);
                    b.start = static_cast<uint16_t>(slotBase | b.start);
                    b.end = static_cast<uint16_t>(slotBase | b.end);
                }
                break;
            }
            case SetSpec::Ports:
                b.watch = (rng() & 1) ? WIn : WOut;
                b.space = Space::Port;
                b.start = spec.warm ? pick(prof.ports, 0xFE) : static_cast<uint16_t>(rng() | 0x0101);  // odd: no ULA
                b.mask = (rng() & 1) ? 0x00FF : 0xFFFF;
                break;
            default:
                break;
        }
        set.push_back(b);
    }
    return set;
}

}  // namespace poc
