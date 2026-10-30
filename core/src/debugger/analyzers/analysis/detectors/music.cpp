/// The music detector. A player signature (signatures/players.cpp) gives the player's entries and its module: for
/// the Vortex Tracker II PT3 player the LD HL,nn at its init is the module; the claim runs from there to the last
/// module byte the player's routines (init, play and everything they call but what draws) read. Without a signature,
/// a routine that wrote both AY ports (register select $FFFD, data $BFFD: (port & $C002) = $C000 / $8000) is a
/// player: the bytes it and its callees (not through one that writes the display: a player does not draw) read and
/// nothing wrote (outside code and the display), in runs of at least 16 bytes (gaps up to 16 joined), are its music
/// data. A writer that read none (a register output routine: it sends a copy of the registers or a pair it is given)
/// is fed by the player: its callers and the code that wrote the bytes it sent, those that do not draw, are the
/// player then; one of those that read none either (a helper storing one register into the copy) adds its callers. Beeper players are not covered (no beeper player's code is in the repository to take a signature from, and a
/// border OUT is no evidence).

#include <algorithm>
#include <cstdio>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/detectors/media.h"

namespace analysis
{

namespace
{

constexpr uint32_t kMinRun = 16;
constexpr uint32_t kJoin = 16;

std::string Hex(uint16_t v)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", v);
    return text;
}

/// The routine wrote display memory
bool Draws(const Routine& routine, const media::DisplayInfo& display)
{
    for (const Interval& w : routine.profile.writes)
        for (uint32_t a = w.z80; a <= std::min<uint32_t>(0xFFFF, w.z80 + (w.hi - w.lo)); ++a)
            if (display.At(a) != media::kNotDisplay)
                return true;
    return false;
}

/// The routines reachable from @p roots over calls, tail jumps and fall-throughs (not interrupts); not through a
/// callee that draws: a player does not, a routine that both plays and runs the game (an interrupt handler, a main
/// loop) reaches the printing and the sprites, whose reads are no music
std::set<uint32_t> Closure(const L1Result& l1, const std::vector<uint32_t>& roots, const media::DisplayInfo& display)
{
    std::set<uint32_t> seen(roots.begin(), roots.end());
    std::deque<uint32_t> work(roots.begin(), roots.end());
    while (!work.empty())
    {
        const uint32_t r = work.front();
        work.pop_front();
        for (const CallEdge& c : l1.calls)
            if (c.caller == r && c.kind != CallKind::Interrupt && !seen.count(c.callee) &&
                !Draws(l1.routines[c.callee], display))
            {
                seen.insert(c.callee);
                work.push_back(c.callee);
            }
    }
    return seen;
}

/// Bytes the routines read that nothing wrote and that are not code or display (Z80 view)
std::vector<uint8_t> ReadOnly(const DetectorContext& context, const std::set<uint32_t>& routines,
                              const media::DisplayInfo& display)
{
    const L1Result& l1 = context.l1;
    std::vector<uint8_t> marks(0x10000, 0);
    for (uint32_t a = 0; a <= 0xFFFF; ++a)
    {
        const ByteUse& u = context.Use(static_cast<uint16_t>(a));
        if ((u.flags & use::kRead) == 0 || (u.flags & (use::kWrite | use::kOpcode | use::kOperand)) != 0 ||
            display.At(a) != media::kNotDisplay)
            continue;
        for (const PhysKey k : {u.firstReader, u.lastReader})
        {
            const Routine* r = l1.RoutineOf(k);
            if (r != nullptr && routines.count(l1.IndexOf(*r)) != 0)
                marks[a] = 1;
        }
    }
    return marks;
}

struct Claimed
{
    Region region;
    uint16_t lo = 0;
    uint16_t hi = 0;
};

/// The runs of @p marks (at least 16 bytes, gaps up to 16 joined, one bank) as music data of the AY writer @p routine
std::vector<Claimed> Runs(const DetectorContext& context, const std::vector<uint8_t>& marks, const Routine& routine)
{
    std::vector<Claimed> out;
    uint32_t a = 0;
    while (a <= 0xFFFF)
    {
        if (!marks[a])
        {
            ++a;
            continue;
        }
        uint32_t b = a, gapEnd = a;
        uint32_t bytes = 0;
        while (gapEnd <= 0xFFFF && gapEnd - b <= kJoin)
        {
            if (marks[gapEnd])
            {
                b = gapEnd;
                ++bytes;
            }
            ++gapEnd;
        }
        if (b - a + 1 >= kMinRun && (a >> 14) == (b >> 14))
        {
            Region r;
            r.kind = RegionKind::MusicData;
            r.detector = DetectorId::Music;
            r.status = RegionStatus::Observed;
            r.confidence = confidence::FromCount(600, 800, bytes, 64);
            r.title = "Music data read by the AY player " + Hex(routine.z80);
            r.evidence.push_back(Evidence{"ay", routine.z80, routine.profile.entries,
                                          "writes the AY register select and data ports"});
            r.evidence.push_back(Evidence{"reads", static_cast<uint16_t>(a), bytes,
                                          "bytes the player's routines read, nothing wrote"});
            r.users = context.UsersOf(static_cast<uint16_t>(a), static_cast<uint16_t>(b));
            out.push_back(Claimed{std::move(r), static_cast<uint16_t>(a), static_cast<uint16_t>(b)});
        }
        a = b + 1;
    }
    return out;
}

}  // namespace

void DetectMusic(DetectorContext& context)
{
    const L1Result& l1 = context.l1;
    const media::DisplayInfo display = media::FindDisplay(context);
    std::vector<Claimed> claims;
    std::set<uint32_t> covered;  // routines of a player found by its signature

    for (const Region& sig : context.out.store.Regions())
    {
        if (sig.detector != DetectorId::Signatures || sig.evidence.empty() ||
            sig.evidence.front().detail != "players.vtii-pt3-r7")
            continue;
        const uint16_t init = sig.z80;
        const uint16_t play = static_cast<uint16_t>(init + 5);
        const uint16_t module = static_cast<uint16_t>(context.Start(static_cast<uint16_t>(init + 1)) |
                                                      (context.Start(static_cast<uint16_t>(init + 2)) << 8));
        std::vector<uint32_t> roots;
        for (const uint16_t at : {init, play})
        {
            const Routine* r = l1.RoutineOf(context.Key(at));
            if (r != nullptr)
                roots.push_back(l1.IndexOf(*r));
        }
        if (context.l1.RoutineOf(context.Key(play)) != nullptr && context.InScope(play))
            context.out.entries.emplace_back(play, "VTII_PLAY");
        const std::set<uint32_t> player = Closure(l1, roots, display);
        covered.insert(player.begin(), player.end());
        const std::vector<uint8_t> marks = ReadOnly(context, player, display);
        uint32_t last = 0, reads = 0;
        for (uint32_t a = module; a <= (module | 0x3FFFu); ++a)
            if (marks[a])
            {
                last = a;
                ++reads;
            }
        if (reads == 0)
            continue;
        Region r;
        r.kind = RegionKind::MusicData;
        r.detector = DetectorId::Music;
        r.status = RegionStatus::Observed;
        r.label = "VTII_MODULE";
        r.confidence = 900;
        r.title = "PT3 module (Vortex Tracker II), played by " + Hex(play) + "; the replay read " +
                  std::to_string(reads) + " of its bytes up to " + Hex(static_cast<uint16_t>(last));
        r.evidence.push_back(Evidence{"pattern", init, 0, "players.vtii-pt3-r7: init's LD HL," + Hex(module)});
        r.evidence.push_back(Evidence{"reads", module, reads, "module bytes the player's routines read"});
        r.users = context.UsersOf(module, static_cast<uint16_t>(last));
        claims.push_back(Claimed{std::move(r), module, static_cast<uint16_t>(last)});
    }

    // AY players found by what they did
    for (const Routine& routine : l1.routines)
    {
        const uint32_t index = l1.IndexOf(routine);
        if (covered.count(index) != 0)
            continue;
        bool select = false, data = false;
        for (const uint16_t p : routine.profile.portsOut)
        {
            select = select || (p & 0xC002) == 0xC000;
            data = data || (p & 0xC002) == 0x8000;
        }
        if (!select || !data)
            continue;
        // A register output routine reads no music itself: the player is its callers and the code that wrote the
        // bytes it sent (the AY register copy the player fills); a root that reads no music either is a register store
        // helper, and its callers are the player
        std::set<uint32_t> player = Closure(l1, {index}, display);
        std::vector<Claimed> found = Runs(context, ReadOnly(context, player, display), routine);
        if (found.empty())
        {
            std::set<uint32_t> roots;
            const auto add = [&](uint32_t r) {
                if (r != index && covered.count(r) == 0 && !Draws(l1.routines[r], display))
                    roots.insert(r);
            };
            const auto callers = [&](uint32_t callee) {
                for (const CallEdge& c : l1.calls)
                    if (c.callee == callee && c.kind != CallKind::Interrupt)
                        add(c.caller);
            };
            callers(index);
            for (const Interval& read : routine.profile.reads)
                for (uint32_t z = read.z80; z <= std::min<uint32_t>(0xFFFF, read.z80 + (read.hi - read.lo)); ++z)
                {
                    const ByteUse& u = context.Use(static_cast<uint16_t>(z));
                    if ((u.flags & use::kWrite) == 0)
                        continue;
                    for (const PhysKey w : {u.firstWriter, u.lastWriter})
                        if (const Routine* r = w != kNoKey ? l1.RoutineOf(w) : nullptr)
                            add(l1.IndexOf(*r));
                }
            const std::set<uint32_t> first = roots;
            for (const uint32_t r : first)
                if (Runs(context, ReadOnly(context, Closure(l1, {r}, display), display), routine).empty())
                    callers(r);
            if (!roots.empty())
            {
                player = Closure(l1, std::vector<uint32_t>(roots.begin(), roots.end()), display);
                player.insert(index);
                found = Runs(context, ReadOnly(context, player, display), routine);
            }
        }
        covered.insert(player.begin(), player.end());
        claims.insert(claims.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
    }
    for (Claimed& c : claims)
    {
        // One bank a claim
        uint32_t lo = c.lo;
        while (lo <= c.hi)
        {
            const uint32_t end = std::min<uint32_t>(c.hi, lo | 0x3FFFu);
            context.Claim(c.region, static_cast<uint16_t>(lo), static_cast<uint16_t>(end));
            lo = end + 1;
        }
    }
}

}  // namespace analysis
