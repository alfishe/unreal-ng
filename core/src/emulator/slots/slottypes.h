#pragma once

/// @file slottypes.h
/// @brief Aggregates of the ZX-bus slot reference data (docs/inprogress/2026-10-03-zx-bus-slots/reference-data.md
/// §4): port claims, card options, function uses, cards, buses, built-in devices, machines, adapters, documented
/// exceptions and sources. Plain constant data: every array is a constexpr table in refdata/*.cpp, referenced by
/// std::span, so the collection needs no heap and no loader.

// Qt defines `slots` and `signals` as macros; these headers reach Qt translation units through portdecoder.h
#pragma push_macro("slots")
#pragma push_macro("signals")
#undef slots
#undef signals

#include <cstdint>
#include <span>

#include "slotvocabulary.h"

// The machine model enum lives in emulator/platform.h (a heavy header); the slot data only needs its name.
enum MEM_MODEL : uint8_t;

namespace slots
{

/// Bibliography ids. Every CardDef / MachineDef / AdapterDef / ExceptionDef cites at least one; refdata/sources.cpp
/// maps each to a title and a direct URL (or a repository-relative path for the repo's own design documents).
enum class Src : uint16_t
{
    // Standards, port tables, bus pinouts
    BcIg7,
    BcIg4,
    SpectrumExpert02,
    MameZxbusBus,
    MameSpectrumExp,
    SinclairWikiEdge,
    SinclairWiki48kEdge,
    SinclairWiki128Edge,
    SinclairWikiPlus2,
    SinclairWikiPlus3Edge,
    VelesoftProtector,
    // Sinclair / Amstrad machines
    Spectrum48ServiceManual,
    Spectrum128ServiceManual,
    Wos128kFaq,
    MameSpecpls3,
    Plus3ServiceManual,
    // Pentagon
    Pentagon22Schematic,
    Pentagon22Cpld,
    MamePentagon,
    // Scorpion
    ScorpionTurboPlusNetlist,
    ScorpionPortGuide,
    ScorpionYellowReconstruction,
    ScorpionProfRom,
    MameScorpion,
    // ATM Turbo
    AtmMicroArtManual,
    AtmSchematicSheet6,
    // ZX-Evolution
    ZxevoSchematicRevC,
    BaseconfZbus,
    BaseconfZports,
    TsconfZbus,
    TsconfZports,
    TsconfTune,
    ZifiDoc,
    // Profi
    Insanity08Profi,
    ZxReviewFedinProfi,
    KarabasProPalette,
    ProfiRetrace,
    RepoProfi1024,
    // Sprinter
    SprinterSchematic,
    SprinterHard,
    MameIsaZxbusAdapter,
    ZxpkSprinterAdapter,
    RepoSprinterHardware,
    RepoSprinterIsa,
    // Cards
    ShiruTurboSound,
    MameAySlot,
    RepoTsfmHardware,
    AlfisheGeneralSound,
    RepoGeneralSound,
    AlfisheNeogs,
    AlfisheZxmMoonsound,
    MicklabMoonsound,
    ZxdnCovox,
    DukeyusupovCovox,
    City20Soundrive,
    VelesoftDa,
    UzixMultisound,
    RepoMultisoundHardware,
    Lvd2ZxnetUsb,
    IzzxZxWifi,
    IzzxZxWifiReadme,
    // The SL-0 research of this design (facts collected from sources without a direct URL, e.g. SVN-only)
    RepoSlotsResearchMachines,
    RepoSlotsResearchCards,
    Count
};

struct SourceRef
{
    Src id = Src::Count;
    const char* title = "";
    const char* url = "";   ///< https://... or a repository-relative path ("docs/...")
};

/// "This entry applies when ...": option `option` has a value in `anyOf` (bit i = value i of the card's OptionDef).
/// Opt::None = always.
struct When
{
    Opt option = Opt::None;
    uint32_t anyOf = 0;
};

/// One port range a device answers: port p is claimed when (p & mask) == match
struct PortClaim
{
    uint16_t mask = 0;
    uint16_t match = 0;
    Dir dir = Dir::InOut;
    Iorqge iorqge = Iorqge::No;
    bool romLock = false;           ///< ignored while the last opcode fetch came from #0000-#3FFF (MultiSound SAA / SD)
    Gate gate = Gate::Always;
    When when{};
    uint16_t port = 0;              ///< the documented port of the claim (#FFFD, #001F); 0 = the match with every
                                    ///< undecoded line high. Used to name the claim and to test shadowing (a built-in
                                    ///< is shadowed when a card's IORQGE claim covers this port, not merely a mirror)
};

/// One value of a card option
struct OptionValue
{
    const char* id = "";      ///< INI / API spelling: "ym", "2", "ef"
    const char* label = "";   ///< matrix wording: "`ym`", "mode 2", "`#EF` build"
};

struct OptionDef
{
    Opt key = Opt::None;
    OptionKind kind = OptionKind::Enum;
    std::span<const OptionValue> values{};
    uint32_t defaultBits = 0;           ///< enum: one bit; set: any subset
    bool splitMatrixRows = false;       ///< the card × machine table shows one row per value
    const char* description = "";
};

struct FunctionUse
{
    Function function = Function::Count;
    Role role = Role::Own;
    When when{};
};

struct CardDef
{
    const char* id = "";                ///< "multisound"
    const char* name = "";              ///< "ZX-MultiSound rev.A2"
    BusKind bus = BusKind::ZxBus;       ///< the card's native bus
    SignalSet needs = 0;                ///< required signals
    CycleDetection detection = CycleDetection::Iorq;
    bool socketDefault = false;         ///< the machine's own chip in a socket (`ay`): not a catalog card of its own
    const char* portsNote = "";         ///< socket boards: "host decode; ..." (they have no claims of their own)
    std::span<const OptionDef> options{};
    std::span<const FunctionUse> functions{};
    std::span<const PortClaim> claims{};
    std::span<const char* const> media{}; ///< media slots the card owns ("sd.ngs")
    std::span<const Src> sources{};
};

/// One bus of a machine. Slots are named "<id>.<n>" ("zxbus.1"); the AY socket is the single slot "ay-socket".
struct BusDef
{
    const char* id = "";
    BusKind kind = BusKind::ZxBus;
    SignalSet signals = 0;
    uint8_t physicalSlots = 0;          ///< informational (the real board); the emulator does not limit slots
    Arbitration arbitration = Arbitration::None;
    ReadRule readRule = ReadRule::WiredAnd;
    std::span<const PortClaim> boardPorts{};   ///< BoardWins: the ports the board hides from the slots
    const char* note = "";              ///< "+12 V only with jumper J4"
    bool retrofit = false;              ///< the board has no such connector: the bus is bolted on, and every report says so
};

struct BuiltInDef
{
    const char* id = "";                ///< "ay", "beta128", "palette"
    const char* name = "";
    BuiltInKind kind = BuiltInKind::Fixed;
    const char* socket = nullptr;       ///< "ay-socket": the default content of that socket (a socket board replaces it)
    const char* chip = "";              ///< "YM2149" (socketed chips)
    std::span<const Function> functions{};
    std::span<const PortClaim> claims{};
};

struct MachineDef
{
    MEM_MODEL model{};
    const char* name = "";              ///< the model's short name ("PENTAGON", "ATM3")
    const char* variant = "";           ///< the real board the declaration follows ("Pentagon-1024SL v2.2 class")
    std::span<const BusDef> buses{};
    std::span<const BuiltInDef> builtIns{};
    std::span<const Src> sources{};
};

/// A bus adapter: a card of bus `cardSide` in a slot of bus `machineSide`. Signals on the card side =
/// (machine bus signals & passes) | adds.
struct AdapterDef
{
    const char* id = "";
    const char* name = "";
    BusKind cardSide = BusKind::ZxBus;
    BusKind machineSide = BusKind::SinclairEdge;
    SignalSet passes = 0;
    SignalSet adds = 0;
    bool hasArbitration = false;        ///< the adapter brings its own IORQGE chain
    Arbitration arbitration = Arbitration::None;
    const char* note = "";
    std::span<const Src> sources{};
};

/// A documented real-world pair rule the functions and claims alone do not express (rule D9). The consistency test
/// refuses an exception the claims already explain.
struct ExceptionDef
{
    const char* card = "";              ///< the card plugged in
    bool anyMachine = false;
    MEM_MODEL model{};                  ///< the machine (when !anyMachine)
    const char* installedCard = nullptr;///< another installed card the rule needs (nullptr = none)
    Outcome outcome = Outcome::Refused;
    bool hard = true;                   ///< refused even with replaceIfIncompatible
    const char* reason = "";
    const char* brief = "";             ///< matrix cell wording
    std::span<const Src> sources{};
};

/// The whole collection the plan engine reads (refdata::All(), or a test's own)
struct Collection
{
    std::span<const CardDef> cards{};
    std::span<const MachineDef> machines{};
    std::span<const AdapterDef> adapters{};
    std::span<const ExceptionDef> exceptions{};
    std::span<const SourceRef> sources{};
};

/// The bit for value index `index` of an option (When::anyOf, OptionDef::defaultBits)
constexpr uint32_t Bit(int index)
{
    return 1u << index;
}

} // namespace slots

#pragma pop_macro("signals")
#pragma pop_macro("slots")
