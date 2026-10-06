#pragma once

/// @file slotvocabulary.h
/// @brief The vocabulary of the ZX-bus slot reference data: functions, bus signals, bus kinds, arbitration modes,
/// cycle detection, read rules, card options and the outcome codes of the compatibility matrix.
///
/// Design: docs/inprogress/2026-10-03-zx-bus-slots/reference-data.md (§3-§4) and architecture.md (§3-§5).
/// Every enum value has an id (the string used in reports, the INI and the docs) and a one-line description; the
/// tables live in slotvocabulary.cpp and a test checks that none is missing.

// Qt defines `slots` and `signals` as macros; these headers reach Qt translation units through portdecoder.h
#pragma push_macro("slots")
#pragma push_macro("signals")
#undef slots
#undef signals

#include <cstdint>

namespace slots
{

/// A role only one device in a machine can have at a time (reference-data.md §2). Two cards that hold one function
/// are incompatible (rule D1); a card holding a function a fixed built-in holds is refused (D4).
enum class Function : uint8_t
{
    AySocket,           ///< "ay-socket": the AY role (AY, TurboSound, TSFM, or a bus card taking it over)
    Gs,                 ///< "gs": General Sound host interface
    Saa,                ///< "saa": SAA1099
    Soundrive,          ///< "soundrive": 4-channel DAC, SounDrive layout
    CovoxFb,            ///< "covox-fb": 8-bit DAC on #FB
    CovoxDd,            ///< "covox-dd": Scorpion Covox on #DD
    Opl4,               ///< "opl4": OPL4 (MoonSound)
    Midi,               ///< "midi": General MIDI synthesizer fed from the AY / YM I/O port
    NetZxnetusb,        ///< "net.zxnetusb"
    SerialEf,           ///< "serial.ef": 16550 serial on #xxEF
    SerialEe,           ///< "serial.ee": 16550 serial on #xxEE
    Beta128,            ///< "beta128": TR-DOS disk interface
    IdeNemo,            ///< "ide.nemo"
    IdeSmuc,            ///< "ide.smuc"
    IdeDivide,          ///< "ide.divide"
    IdeAtm,             ///< "ide.atm"
    IdeProfi,           ///< "ide.profi"
    KempstonJoystick,   ///< "kempston-joystick"
    KempstonMouse,      ///< "kempston-mouse"
    SdZc,               ///< "sd.zc": Z-Controller SD card
    Rtc,                ///< "rtc": real-time clock
    Palette,            ///< "palette": Profi v5 palette
    FdcUpd765,          ///< "fdc.upd765": +3 floppy controller
    Count
};

/// One expansion-bus signal; a BusDef / AdapterDef carries a bit set of them, a CardDef the set it needs
/// (research-machines.md §3). Values are bits.
enum class BusSignal : uint32_t
{
    Iorqge  = 1u << 0,   ///< card claims the cycle (ZX-bus 13A; 48K /IORQULA)
    IoDos   = 1u << 1,   ///< NemoBus v1.1 /IODOS (shadow ports open)
    Dos     = 1u << 2,   ///< /DOS output (TR-DOS ROM paged)
    Wait    = 1u << 3,
    Plus12V = 1u << 4,
    Reset   = 1u << 5,
    CsRom   = 1u << 6,   ///< board ROM select, an output (ZX-bus CSR/)
    RdRom   = 1u << 7,   ///< ROM override input (ZX-bus RDR/, Sinclair /ROMCS)
    M1      = 1u << 8,
    Rfsh    = 1u << 9,
    Int     = 1u << 10,
    Nmi     = 1u << 11,
    Busrq   = 1u << 12,
    Busak   = 1u << 13,
};
constexpr int kBusSignalCount = 14;

/// Bit set of BusSignal values
using SignalSet = uint32_t;

constexpr SignalSet operator|(BusSignal a, BusSignal b)
{
    return static_cast<SignalSet>(a) | static_cast<SignalSet>(b);
}
constexpr SignalSet operator|(SignalSet a, BusSignal b)
{
    return a | static_cast<SignalSet>(b);
}
constexpr SignalSet Sig(BusSignal s)
{
    return static_cast<SignalSet>(s);
}

/// Physical kind of a bus. Scorpion slots are ZxBus with their own signal set (research-machines.md §11).
enum class BusKind : uint8_t
{
    AySocket,       ///< "ay-socket": the AY chip socket (one slot)
    ZxBus,          ///< "zxbus": ZX-bus / NemoBus slots
    SinclairEdge,   ///< "sinclair-edge": the Sinclair / Amstrad edge connector
    AtmIoBus,       ///< "atm-iobus": ATM Turbo 2 / 2+ 2x12 I/O bus
    CpuSocket,      ///< "cpu-socket": the Z80 socket, reachable only through an adapter
    ProfiBus,       ///< "profi-bus": Profi's own 64-pin system bus
    Isa8,           ///< "isa8": Sprinter ISA-8 slots
    Count
};

/// How a card's IORQGE interacts with the board's own decode (research-machines.md §1, architecture.md §4.1)
enum class Arbitration : uint8_t
{
    CardWins,   ///< a card driving IORQGE hides the cycle from lower slots and from the whole board decoder
    BoardWins,  ///< the board hides its own ports from the slots; IORQGE only orders the slots
    UlaOnly,    ///< IORQGE silences only the ULA's #FE
    None,       ///< no suppression input at all
    Count
};

/// How a card detects an I/O cycle (research-cards.md review correction)
enum class CycleDetection : uint8_t
{
    Iorq,   ///< the card sees a cycle only when its /IORQ is active (almost all cards)
    RdWr,   ///< "RD or WR without MREQ and M1": sees the cycles the board hides by masking /IORQ (ZX-MultiSound)
    Count
};

/// What two drivers on one read produce (research.md §1 item 4: no machine documents it)
enum class ReadRule : uint8_t
{
    WiredAnd,       ///< modeling choice: the values combine by AND; every such port is reported as a bus fight
    CardOverUla,    ///< the ULA sits behind series resistors, a card on the CPU side wins against it (48K / 128K)
    SlotOrder,      ///< ZX-Evo: slot 1 beats slot 2, the FPGA drives #FF when nobody claims
    Count
};

/// Port claim direction (bit set)
enum class Dir : uint8_t
{
    In = 1,
    Out = 2,
    InOut = 3,
};

/// Whether a claim drives IORQGE
enum class Iorqge : uint8_t
{
    No,
    Yes,
    ReadsOnly,  ///< classic General Sound: IORQGE on reads only
    Count
};

/// When a claim is live (the DOS state: TR-DOS ROM paged / Beta-128 shadow ports open)
enum class Gate : uint8_t
{
    Always,
    DosOnly,    ///< live only in DOS mode (Beta-128 ports, ZX-Evo #FF in shadow mode)
    NonDos,     ///< live only outside DOS mode (built-in Kempston, MoonSound with JP1 open)
    Count
};

/// How a card holds a function
enum class Role : uint8_t
{
    Own,        ///< the card is the device (a GS card holds `gs`)
    Takeover,   ///< a bus card takes the role over from the socket by IORQGE (ZX-MultiSound `ym` -> `ay-socket`):
                ///< no displacement of the socket's content by function; shadowing / socket rules apply (D2, D3, D12)
    Count
};

/// What a machine allows done to a built-in device (requirements.md R-COMP-5, open-questions Q7)
enum class BuiltInKind : uint8_t
{
    Fixed,      ///< cannot be switched off: a card needing its function is refused (D4)
    Switchable, ///< a machine setting can switch it off: reported as switched off (D5)
    Socketed,   ///< a chip in a socket: a plan can take it out (D12)
    Count
};

/// Card option keys. The values each card offers are in its OptionDef (refdata/cards.cpp).
enum class Opt : uint8_t
{
    None,
    Dip,        ///< ZX-MultiSound DIP switches: the functions enabled (set)
    GsRam,      ///< ZX-MultiSound GS RAM size
    CtrlMask,   ///< ZX-MultiSound TurboSound control-byte compare (pro / classic)
    Mode,       ///< SounDrive S1 mode switch
    Port,       ///< ZX-WiFi port build (#EF / #EE)
    Ram,        ///< card RAM size
    Rom,        ///< card ROM / firmware version
    Jp1,        ///< ZXM-MoonSound JP1 "PentEvo"
    Decode,     ///< Covox port decode width
    Count
};
constexpr int kOptCount = static_cast<int>(Opt::Count);

/// Whether an option holds one value or a set of values
enum class OptionKind : uint8_t
{
    Enum,       ///< exactly one value
    Set,        ///< any subset of the values (DIP switches)
    Count
};

/// Outcome codes of the compatibility matrix (compatibility-matrix.md §0)
enum class Outcome : uint8_t
{
    Coexist,            ///< ✓
    Displaced,          ///< D (D1)
    Pointless,          ///< ⊘ (D3)
    Shadowed,           ///< S (D2, D6)
    Refused,            ///< X (D4, D9)
    RemovedFromSocket,  ///< R (D12, Q7)
    PortClash,          ///< P (D7)
    NeedsAdapter,       ///< A (D8)
    Replaces,           ///< "replaces": a card put into an occupied slot replaces its content
    PartlyDead,         ///< "partly dead": some of the card's ports are board ports hidden from the slots
    Count
};

/// One row of a vocabulary table
struct EnumInfo
{
    const char* id = "";
    const char* description = "";
};

/// Function: id + meaning + ports involved (compatibility-matrix.md §1)
struct FunctionInfo
{
    const char* id = "";
    const char* meaning = "";
    const char* ports = "";
};

const FunctionInfo& Describe(Function value);
EnumInfo Describe(BusSignal value);
EnumInfo Describe(BusKind value);
EnumInfo Describe(Arbitration value);
EnumInfo Describe(CycleDetection value);
EnumInfo Describe(ReadRule value);
EnumInfo Describe(Iorqge value);
EnumInfo Describe(Gate value);
EnumInfo Describe(Role value);
EnumInfo Describe(BuiltInKind value);
EnumInfo Describe(Opt value);
EnumInfo Describe(OptionKind value);
/// Outcome: id = the matrix code ("✓", "D", ...), description = its meaning
EnumInfo Describe(Outcome value);

/// The BusSignal with bit index `index` (0 .. kBusSignalCount - 1)
constexpr BusSignal SignalAt(int index)
{
    return static_cast<BusSignal>(1u << index);
}

} // namespace slots

#pragma pop_macro("signals")
#pragma pop_macro("slots")
