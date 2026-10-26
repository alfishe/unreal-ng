#pragma once

/// @file detectors.h
/// @brief The L2 structural detectors (docs/emulator/analysis/analysis-layers.md, "L2: structure"), in their fixed
/// order: signatures, code map, text, init tables, pointer / handler tables, records, variables. Each reads L1 (the
/// replay's code behaviour, per-byte uses, block copies, the memory at the start and the end) and the claims made
/// before it, and adds its own to the region store. One file per detector; RunDetectors runs them in order.
///
/// @code
///   analysis::L2Result l2 = analysis::RunDetectors(l1);
///   for (const analysis::Region& r : l2.store.Regions())
///       printf("%04X %s\n", r.z80, analysis::RegionKindName(r.kind));
/// @endcode

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l1model.h"
#include "debugger/analyzers/analysis/l2model.h"
#include "debugger/analyzers/analysis/signatures/signatures.h"

namespace analysis
{

struct DetectorOptions
{
    /// The Z80 range the detectors claim in (inclusive); bytes outside are left alone. Default: all
    uint16_t start = 0x0000;
    uint16_t end = 0xFFFF;
    /// The signature catalog (empty: the built-in one, signatures::Builtin())
    const std::vector<Signature>* catalog = nullptr;
};

/// What every detector reads: L1, the claims so far, and the memory seen through the start's paging
class DetectorContext
{
public:
    DetectorContext(const L1Result& l1, L2Result& out, const DetectorOptions& options);

    const L1Result& l1;
    L2Result& out;
    const DetectorOptions& options;

    bool InScope(uint16_t z80) const { return z80 >= options.start && z80 <= options.end; }
    /// The physical key of a Z80 address through the start's paging
    PhysKey Key(uint16_t z80) const { return l1.StartKey(z80); }
    /// The Z80 address of a key through the start's paging (false: the page was not mapped at the start)
    bool Z80Of(PhysKey key, uint16_t& z80) const;
    /// The byte at the start of the replay (0 when not known)
    uint8_t Start(uint16_t z80) const;
    uint8_t End(uint16_t z80) const;
    /// The use of the byte (an empty use when never touched)
    const ByteUse& Use(uint16_t z80) const;
    /// The Z80 entry of the routine owning the instruction @p key (0xFFFFFFFF: none)
    uint32_t RoutineEntry(PhysKey instruction) const;
    /// A data access of any kind (not the stack, not a fetch)
    static bool Data(const ByteUse& u) { return (u.flags & (use::kRead | use::kWrite)) != 0; }
    static bool Code(const ByteUse& u) { return (u.flags & (use::kOpcode | use::kOperand)) != 0; }
    /// Touched at all, the stack aside
    static bool Touched(const ByteUse& u) { return Data(u) || Code(u); }

    /// Add a claim in the Z80 view [lo, hi] (one page); false when out of scope or refused by the store
    bool Claim(Region region, uint16_t lo, uint16_t hi);

    /// Per Z80 address (start's paging): 1 when a claim covers it (the code map's plain data and untouched
    /// fallbacks aside)
    std::vector<uint8_t> ClaimedMap() const;

    /// The routines whose instructions read / wrote any byte of [lo, hi] (Z80 entries, sorted, at most 16)
    std::vector<uint16_t> UsersOf(uint16_t lo, uint16_t hi) const;

private:
    ByteUse _none;
};

/// Run the detectors over @p l1 in the fixed order
L2Result RunDetectors(const L1Result& l1, const DetectorOptions& options = {});

void DetectSignatures(DetectorContext& context);
void DetectCodeMap(DetectorContext& context);
void DetectText(DetectorContext& context);
void DetectInitTables(DetectorContext& context);
void DetectRecords(DetectorContext& context);
void DetectPointerTables(DetectorContext& context);
void DetectVariables(DetectorContext& context);
/// After the seven: each buffer's field list from the claims made inside it (records.cpp)
void FinishBufferFields(DetectorContext& context);

/// Init-table generators the init table detector checks (exposed for the tests): the name of the generator the
/// @p size bytes at @p data follow ("screen-rows", "screen-rows-lo", "screen-rows-hi", "x*N", "shift", "sine8",
/// "bit-reverse"), empty when none
std::string MatchGenerator(const uint8_t* data, size_t size);

}  // namespace analysis
