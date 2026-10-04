#include "slotvocabulary.h"

#include <cstddef>

namespace slots
{

namespace
{

// Indexed by the enum value; the Count entries are checked by a test (RefData_Test.CollectionIsConsistent)
constexpr FunctionInfo kFunctions[] = {
    { "ay-socket", "the AY role (AY, TurboSound, TSFM, or a bus card taking it over)", "`#FFFD`, `#BFFD` (`#C002` decodes)" },
    { "gs", "General Sound host interface", "`#B3`, `#BB` (+ `#33` on NeoGS / ZXM-GS)" },
    { "saa", "SAA1099", "`#FF` data, `#1FF` address" },
    { "soundrive", "4-channel DAC, SounDrive layout", "mode 1 `#0F #1F #4F #5F`; mode 2 `#F1 #F3 #F9 #FB`" },
    { "covox-fb", "8-bit DAC on `#FB`", "`#FB`" },
    { "covox-dd", "Scorpion Covox", "`#DD`" },
    { "opl4", "OPL4 (MoonSound)", "`#C4-#C7`, `#7E`, `#7F`" },
    { "midi", "General MIDI synthesizer fed from the AY / YM I/O port", "(AY register 14)" },
    { "net.zxnetusb", "ZXNETUSB", "`#AB`" },
    { "serial.ef", "16550 serial (ZX-WiFi, ZX-Evo COM, TS-Conf ZiFi)", "`#xxEF`" },
    { "serial.ee", "16550 serial, ZX-WiFi `#EE` build", "`#xxEE`" },
    { "beta128", "TR-DOS disk interface", "`#1F #3F #5F #7F #FF` (DOS)" },
    { "ide.nemo", "Nemo IDE", "`#10-#F0`, `#11`, `#C8`" },
    { "ide.smuc", "SMUC IDE (Scorpion)", "`#xxBE` family (DOS)" },
    { "ide.divide", "DivIDE", "`#A3-#BF`, `#E3`" },
    { "ide.atm", "ATM Turbo IDE", "`#xxEF` (DOS)" },
    { "ide.profi", "Profi v5 IDE", "`#8B #AB #CB #EB` (extended map)" },
    { "kempston-joystick", "Kempston joystick", "`#1F`" },
    { "kempston-mouse", "Kempston mouse", "`#FADF #FBDF #FFDF`" },
    { "sd.zc", "Z-Controller SD card", "`#57`, `#77`" },
    { "rtc", "real-time clock", "per machine (ZX-Evo `#BFF7 #DFF7 #EFF7`)" },
    { "palette", "Profi v5 palette", "`#xx7E`" },
    { "fdc.upd765", "+3 floppy controller", "`#2FFD`, `#3FFD`" },
};
static_assert(sizeof(kFunctions) / sizeof(kFunctions[0]) == static_cast<std::size_t>(Function::Count));

constexpr EnumInfo kSignals[] = {
    { "IORQGE", "card claims the I/O cycle (ZX-bus 13A; 48K /IORQULA, +2 /IORQGE)" },
    { "/IODOS", "NemoBus v1.1: shadow ports open" },
    { "/DOS", "TR-DOS ROM paged (board output)" },
    { "/WAIT", "wait states" },
    { "+12V", "+12 V supply" },
    { "/RESET", "bus reset" },
    { "/CSROM", "board ROM select (output)" },
    { "/RDROM", "ROM override (input; Sinclair /ROMCS)" },
    { "/M1", "opcode fetch" },
    { "/RFSH", "refresh" },
    { "/INT", "interrupt request" },
    { "/NMI", "non-maskable interrupt" },
    { "/BUSRQ", "bus request" },
    { "/BUSAK", "bus acknowledge" },
};
static_assert(sizeof(kSignals) / sizeof(kSignals[0]) == static_cast<std::size_t>(kBusSignalCount));

constexpr EnumInfo kBusKinds[] = {
    { "ay-socket", "the AY chip socket: the machine's own AY or a TurboSound board in its place" },
    { "zxbus", "ZX-bus / NemoBus slots (62-pin SL-62; Scorpion pinout variant)" },
    { "sinclair-edge", "Sinclair / Amstrad rear edge connector" },
    { "atm-iobus", "ATM Turbo 2 / 2+ 2x12 I/O bus (data, address latched by `#FB`, strobes from `#FA`)" },
    { "cpu-socket", "the Z80 socket; a ZX-bus card needs a CPU-socket adapter" },
    { "profi-bus", "Profi 64-pin system bus (own pin order)" },
    { "isa8", "Sprinter ISA-8 slots (reached through a memory window)" },
};
static_assert(sizeof(kBusKinds) / sizeof(kBusKinds[0]) == static_cast<std::size_t>(BusKind::Count));

constexpr EnumInfo kArbitrations[] = {
    { "CardWins", "a card driving IORQGE hides the cycle from lower slots and the whole board decoder" },
    { "BoardWins", "the board hides its own ports from the slots; IORQGE only orders the slots" },
    { "UlaOnly", "IORQGE silences only the ULA's `#FE`" },
    { "None", "no suppression input" },
};
static_assert(sizeof(kArbitrations) / sizeof(kArbitrations[0]) == static_cast<std::size_t>(Arbitration::Count));

constexpr EnumInfo kDetections[] = {
    { "Iorq", "sees a cycle only when its /IORQ is active" },
    { "RdWr", "RD or WR without MREQ and M1: sees the cycles the board hides" },
};
static_assert(sizeof(kDetections) / sizeof(kDetections[0]) == static_cast<std::size_t>(CycleDetection::Count));

constexpr EnumInfo kReadRules[] = {
    { "WiredAnd", "two drivers combine by AND (modeling choice, reported as a bus fight)" },
    { "CardOverUla", "the ULA sits behind series resistors; a card wins against it" },
    { "SlotOrder", "slot 1 beats slot 2; the board drives `#FF` when nobody claims" },
};
static_assert(sizeof(kReadRules) / sizeof(kReadRules[0]) == static_cast<std::size_t>(ReadRule::Count));

constexpr EnumInfo kIorqges[] = {
    { "no", "passive: never drives IORQGE" },
    { "yes", "drives IORQGE on reads and writes" },
    { "reads only", "drives IORQGE on reads only (classic General Sound)" },
};
static_assert(sizeof(kIorqges) / sizeof(kIorqges[0]) == static_cast<std::size_t>(Iorqge::Count));

constexpr EnumInfo kGates[] = {
    { "always", "live in every state" },
    { "DOS", "live only while the TR-DOS ROM is paged" },
    { "non-DOS", "live only outside DOS" },
};
static_assert(sizeof(kGates) / sizeof(kGates[0]) == static_cast<std::size_t>(Gate::Count));

constexpr EnumInfo kRoles[] = {
    { "own", "the card is the device" },
    { "takeover", "a bus card takes the role over from the socket by IORQGE" },
};
static_assert(sizeof(kRoles) / sizeof(kRoles[0]) == static_cast<std::size_t>(Role::Count));

constexpr EnumInfo kBuiltInKinds[] = {
    { "fixed", "cannot be switched off" },
    { "switchable", "a machine setting can switch it off" },
    { "socketed", "a chip in a socket: a plan can take it out" },
};
static_assert(sizeof(kBuiltInKinds) / sizeof(kBuiltInKinds[0]) == static_cast<std::size_t>(BuiltInKind::Count));

constexpr EnumInfo kOpts[] = {
    { "", "no option (the entry always applies)" },
    { "dip", "DIP switches: the functions enabled" },
    { "gsRam", "General Sound RAM size" },
    { "ctrlMask", "TurboSound control-byte compare" },
    { "mode", "mode switch" },
    { "port", "port build" },
    { "ram", "RAM size" },
    { "rom", "ROM / firmware version" },
    { "jp1", "jumper JP1" },
    { "decode", "port decode width" },
};
static_assert(sizeof(kOpts) / sizeof(kOpts[0]) == static_cast<std::size_t>(Opt::Count));

constexpr EnumInfo kOptionKinds[] = {
    { "enum", "exactly one value" },
    { "set", "any subset of the values" },
};
static_assert(sizeof(kOptionKinds) / sizeof(kOptionKinds[0]) == static_cast<std::size_t>(OptionKind::Count));

constexpr EnumInfo kOutcomes[] = {
    { "✓", "coexist" },
    { "**D**", "the installed card is displaced (shared function)" },
    { "**⊘**", "pointless pair: the socket card would be shadowed; it is displaced and the socket returns to the machine's chip" },
    { "**S**", "the built-in is shadowed (silent, still fitted)" },
    { "**X**", "refused: a fixed built-in holds the function, or the card's ports are dead on this board" },
    { "**R**", "a socketed built-in chip is taken out (bus fight otherwise)" },
    { "**P**", "both stay; the later card is disabled for an accidental port clash" },
    { "A", "needs an adapter (or the override, then `unrealistic`)" },
    { "replaces", "a card put into an occupied slot replaces its content" },
    { "partly dead", "some of the card's ports are board ports hidden from the slots" },
};
static_assert(sizeof(kOutcomes) / sizeof(kOutcomes[0]) == static_cast<std::size_t>(Outcome::Count));

template <typename E, std::size_t N>
EnumInfo Lookup(const EnumInfo (&table)[N], E value)
{
    const auto index = static_cast<std::size_t>(value);
    return index < N ? table[index] : EnumInfo{};
}

} // namespace

const FunctionInfo& Describe(Function value)
{
    static const FunctionInfo none{};
    const auto index = static_cast<std::size_t>(value);
    return index < static_cast<std::size_t>(Function::Count) ? kFunctions[index] : none;
}

EnumInfo Describe(BusSignal value)
{
    const auto bits = static_cast<uint32_t>(value);
    for (int i = 0; i < kBusSignalCount; i++)
    {
        if (bits == (1u << i))
        {
            return kSignals[i];
        }
    }
    return {};
}

EnumInfo Describe(BusKind value) { return Lookup(kBusKinds, value); }
EnumInfo Describe(Arbitration value) { return Lookup(kArbitrations, value); }
EnumInfo Describe(CycleDetection value) { return Lookup(kDetections, value); }
EnumInfo Describe(ReadRule value) { return Lookup(kReadRules, value); }
EnumInfo Describe(Iorqge value) { return Lookup(kIorqges, value); }
EnumInfo Describe(Gate value) { return Lookup(kGates, value); }
EnumInfo Describe(Role value) { return Lookup(kRoles, value); }
EnumInfo Describe(BuiltInKind value) { return Lookup(kBuiltInKinds, value); }
EnumInfo Describe(Opt value) { return Lookup(kOpts, value); }
EnumInfo Describe(OptionKind value) { return Lookup(kOptionKinds, value); }
EnumInfo Describe(Outcome value) { return Lookup(kOutcomes, value); }

} // namespace slots
