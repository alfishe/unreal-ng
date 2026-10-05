# ZX-bus slots: compatibility and displacement as reference data

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Status** | Draft (phase SL-0); the values come from [research.md](research.md) |
| **Owner direction** | the compatibility / displacement matrix is described in the design and then encoded as a **reference data collection in the code**, easy to manage; moving it into configuration files is a later option, only if new hardware starts appearing again (2026-10-03) |
| **Related** | [requirements.md](requirements.md) §3.3, [architecture.md](architecture.md) §3-5, [compatibility-matrix.md](compatibility-matrix.md) (the readable, generated view) |

## 1. Why one collection

The matrix grows with every card and every machine. If the rules are spread over `switch` statements in decoders,
`SoundManager` and `NetworkManager`, each new card touches shared code and the docs drift from the behavior. Instead:

- **One collection of reference data in C++** holds the vocabulary (functions, signals, bus kinds, arbitration
  modes), the card types, the machines' buses and built-in devices, the adapters, the documented real-world
  exceptions and the sources. Plain constant tables (`constexpr` arrays of aggregates), no file parsing, no loader:
  adding a card is adding one table entry.
- **The plan engine reads only this collection.** Each card's behavior (its `ICard` implementation) stays in its own
  C++ module; the collection says *where it fits and what it occupies*.
- **The readable matrix is generated** from the same collection (a `slots matrix --markdown` report on the CLI writes
  the tables in [compatibility-matrix.md](compatibility-matrix.md)); a test fails if the committed tables differ.
- **Later, if needed:** the same aggregates can be filled from a configuration file without changing the engine
  (the types are plain data). Not planned now.

## 2. Glossary

| Term | Meaning |
|---|---|
| **Function** | A role that only one device in a machine can have at a time (`gs`, `saa`, `ay-socket`, `opl4`, `beta128`, ...). |
| **Claim** | One port range a device answers: `mask`, `match`, direction, IORQGE or passive, ROM-fetch lock. |
| **Displace** | Remove an installed card because the new one needs a function the old one holds. |
| **Shadow** | Keep a device fitted but silent, because a card's IORQGE claims cover its ports (`CardWins` buses). |
| **Pointless pair** | A socket card that the new bus card would shadow completely (TSFM in the AY socket under a MultiSound): treated as displaced, and the socket returns to the machine's own chip. |
| **Fixed / switchable / socketed built-in** | A built-in device that cannot be switched off / can be switched off by a machine setting / is a chip in a socket that a plan can take out. |
| **Fit** | Whether a card's required signals exist on the slot's bus: `real`, `adapter`, `unrealistic` (override). |
| **Exception** | A documented real-world pair rule that the functions and claims alone do not express. |

## 3. Layout in the code

```
core/src/emulator/slots/
  slotvocabulary.{h,cpp}   enums: Function, BusSignal, BusKind, Arbitration, CycleDetection, ReadRule, Iorqge, Gate,
                           Role, BuiltInKind, Opt, OptionKind, Outcome; one id + description per value (the .cpp)
  slottypes.h              aggregates: When, PortClaim, OptionValue, OptionDef, FunctionUse, CardDef, BusDef,
                           BuiltInDef, MachineDef, AdapterDef, ExceptionDef, SourceRef (+ Src ids), Collection
  refdata/
    sources.cpp            bibliography: id -> title + URL (every entry below cites ids from here)
    cards.cpp              the card catalog (one CardDef per card)
    machines.cpp           one MachineDef per creatable model: buses, arbitration, board ports, built-ins
    adapters.cpp           bus adapters
    exceptions.cpp         documented pair rules
    refdata.{h,cpp}        span accessors: Cards(), Machines(), Adapters(), Exceptions(), Sources(); All() = Collection
  slotplanner.{h,cpp}      the plan engine (pure functions over a Collection + the current slot set)
  slotmatrix.{h,cpp}       renders compatibility-matrix.md §1-§4 from the collection and the plan engine
```

As built in SL-1: the machine declarations live here, keyed by `MEM_MODEL`; the decoders get no `DescribeBuses()`
(architecture.md §3.1). The planner takes a `Collection` (default `refdata::All()`), so tests run it over their own
cards and machines.

`refdata/` holds data only: no logic, no includes beyond the two headers. A reviewer reads one card as one block.

## 4. Shape of the entries (sketch)

### 4.1 Option-dependent claims

A card's functions and claims depend on its options (DIP switches, mode switches). Instead of a parsed expression
language, each entry carries a small typed condition:

```cpp
struct When                    // "this entry applies when ..."
{
    OptionId option = OptionId::None;   // None = always
    uint32_t anyOf = 0;                 // bit set of option values (set options: members; enum options: values)
};
```

### 4.2 A card

```cpp
// refdata/cards.cpp (sketch)
constexpr PortClaim kMultiSoundClaims[] = {
    { 0xE00F, 0xE00D, Dir::InOut, Iorqge::Yes, RomLock::No,  { Opt::Dip, Dip::Ym  } },   // #FFFD family
    { 0xE00F, 0xC00D, Dir::InOut, Iorqge::No,  RomLock::No,  { Opt::Dip, Dip::Ym  } },   // #DFFD family
    { 0xC00F, 0x800D, Dir::InOut, Iorqge::Yes, RomLock::No,  { Opt::Dip, Dip::Ym  } },   // #BFFD family (a read: IORQGE, not driven)
    { 0x00FF, 0x00FF, Dir::Out,   Iorqge::No,  RomLock::Yes, { Opt::Dip, Dip::Saa } },   // SAA #FF / #1FF
    { 0x00FF, 0x00B3, Dir::InOut, Iorqge::Yes, RomLock::No,  { Opt::Dip, Dip::Gs  } },
    { 0x00FF, 0x00BB, Dir::InOut, Iorqge::Yes, RomLock::No,  { Opt::Dip, Dip::Gs  } },
    { 0x00AF, 0x000F, Dir::Out,   Iorqge::No,  RomLock::Yes, { Opt::Dip, Dip::Sd  } },   // SounDrive mode 1
};

constexpr FunctionUse kMultiSoundFunctions[] = {
    { Function::AySocket,  Role::Takeover, { Opt::Dip, Dip::Ym  } },
    { Function::Midi,      Role::Own,      { Opt::Dip, Dip::Ym  } },
    { Function::Saa,       Role::Own,      { Opt::Dip, Dip::Saa } },
    { Function::Gs,        Role::Own,      { Opt::Dip, Dip::Gs  } },
    { Function::Soundrive, Role::Own,      { Opt::Dip, Dip::Sd  } },
};

constexpr CardDef kMultiSound = {
    .id = "multisound", .name = "ZX-MultiSound (UzixLS)",
    .bus = BusKind::ZxBus, .requires = Sig::Iorqge | Sig::Plus12V, .detection = CycleDetection::RdWr,
    .options = kMultiSoundOptions, .functions = kMultiSoundFunctions, .claims = kMultiSoundClaims,
    .sources = { Src::MsRepo, Src::MsRtl, Src::MsSch },
};
```

### 4.3 A machine

```cpp
// refdata/machines.cpp (sketch)
constexpr PortClaim kEvoBoardPorts[] = {
    { 0x00FF, 0x00FE }, { 0x00FF, 0x00FD /* every #xxFD */ }, { 0x00FF, 0x001F }, { 0x00FF, 0x00DF },
    { 0x00FF, 0x00EF }, { 0x00FF, 0x0057 }, { 0x00FF, 0x0077 }, { 0x00FF, 0x00F7 }, /* ... research §13.2 */
};

constexpr MachineDef kAtm3 = {
    .model = MM_ATM3,
    .buses = { { "ay-socket", BusKind::AySocket },
               { "zxbus", BusKind::ZxBus, Sig::Iorqge | Sig::Plus12V /* jumper J4 */ | ...,
                 .physicalSlots = 2, .arbitration = Arbitration::BoardWins, .readRule = ReadRule::WiredAnd,
                 .boardPorts = kEvoBoardPorts } },
    .builtIn = { { "ay", Function::AySocket, .socketed = true, .chip = "YM2149" }, /* beta128, kempston, ... */ },
    .sources = { Src::EvoZbus, Src::EvoZports, Src::EvoManual },
};
```

Designated initializers need C++20, which the core already uses.

### 4.4 Adapters and exceptions

```cpp
constexpr AdapterDef kZxBusToSinclairEdge = {
    .id = "zxbus-to-sinclair-edge", .cardSide = BusKind::ZxBus, .machineSide = BusKind::SinclairEdge,
    .passes = Sig::M1 | Sig::Rfsh | Sig::Int | Sig::Nmi | Sig::Busrq | Sig::Reset | Sig::Wait,
    .missing = Sig::IoDos | Sig::Dos | Sig::Iorqge,
    .sources = { Src::VelesoftBusProtector },
};

constexpr ExceptionDef kExceptions[] = {
    // Only for facts the claims cannot express; a pair already explained by claims is flagged as redundant by the test.
};
```

### 4.5 Sources

Every `CardDef`, `MachineDef`, `AdapterDef` and `ExceptionDef` names at least one `Src` id; the test refuses an entry
without one (owner rule: hardware facts from sources, by reference consensus).

## 5. Displacement rules (what the collection drives)

For a request "card A into slot S" (and for "options of A change"), the plan engine
([architecture.md](architecture.md) §5) computes, from the collection alone:

| # | Situation | Outcome for the installed device B | Reported as |
|---|---|---|---|
| D1 | A and card B share a function | B displaced | `removed` (+ B's full options) |
| D2 | `CardWins` bus: A takes over `ay-socket` by IORQGE, B is the socket's default chip | B shadowed | `shadowed` |
| D3 | `CardWins` bus: A takes over `ay-socket`, B is a TS / TSFM in the socket | B displaced, socket back to default, default shadowed | `removed` + `shadowed` |
| D4 | A shares a function with a **fixed** built-in, or **every** claim of an `Iorq` card is a hidden board port (`BoardWins`); when only some are, the card is allowed and those ports are reported | request refused (even with the flag); partly: allowed | `refused: <reason>`; `deadPorts` (`partly dead`) |
| D5 | A shares a function with a **switchable** built-in | built-in switched off | `switchedOff` |
| D6 | `CardWins` bus: A's IORQGE claims cover a built-in's documented port (`PortClaim::port`; a mirror is not enough) | built-in shadowed on those ports | `shadowed` |
| D7 | A's **read** claims overlap card B's read claims with no shared function and no IORQGE on either (a write both receive is co-reception, as on the real bus) | the later card by slot order plugged in but disabled | `disabled: port clash with <slot>` |
| D8 | A needs a signal the bus lacks | adapter in the slot -> `fit: adapter`; else refused, or `fit: unrealistic` with the override | `fit` |
| D9 | an exception matches | its outcome | `exception: <reason>` |
| D10 | B displaced held functions A does not offer | nothing more | `lostFunctions` |
| D11 | B displaced owns media | media follow the stranded-media rules | `media` |
| D12 | A's read claim covers a built-in's documented read port and nothing silences either side (`BoardWins` with an `RdWr` card, or `None` / `UlaOnly`) | a **socketed** chip is taken out of its socket (Q7); a socket board there is displaced as pointless; otherwise a bus fight (fit `unrealistic` with the override) | `removedFromSocket`, `busFights` |

**Order and determinism.** The engine evaluates D9 first (exceptions), then D4 (refusals), D8 (fit), then D1-D3, D5-D7,
D12 in slot order, and finally D10-D11. The result does not depend on the order in which cards were added (the
slot set is sorted by the machine's bus order and slot number first). As built (SL-1): the fit (D8) is computed
before D4's board-port check, because an adapter decides which arbitration is in effect; the order only changes the
order of the reasons, never the outcome. Without the fit, a card works "as if the bus carried every signal it needs"
(Q5): on a `None` / `UlaOnly` bus an IORQGE card is planned as `CardWins`.

**One step.** Displacement never chains: removing B never displaces anything else, and a plan only removes.

**Multiple displacement.** D1 / D3 collect *every* matching installed device at once; the reply lists all of them
(matrix example A).

## 6. Generated views and tests

- `RenderMatrixTable()` (`slotmatrix.h`) renders [compatibility-matrix.md](compatibility-matrix.md) §1-§4 from the
  collection; the document carries each table between `<!-- slots:generated:<name>:begin -->` / `end` markers.
  `slots matrix --markdown` (CLI, from `SlotControl`) prints the same from SL-7.
- `RefData_Test.CollectionIsConsistent`: every enum value has a description, every entry has a source (and every
  source a link or an existing repository document), no claim outside 16 bits, every creatable machine model has
  exactly one `MachineDef`, option conditions name options the card has, and no exception is already explained by
  the claims.
- `SlotMatrix_Test.MatrixMatchesDocs`: the plan engine, run over every card pair and every machine, reproduces the
  tables committed in the docs (the docs, the collection and the engine cannot drift apart).
- `SlotPlanner_Test.WorkedPlans`: the worked plans of compatibility-matrix.md §6 as table-driven cases.
