// ZX-bus slots reference data: one MachineDef per creatable model (PortDecoder::IsModelSupported): its buses with
// signals, arbitration and the board ports it hides, and its built-in devices. Facts and their sources:
// docs/inprogress/2026-10-03-zx-bus-slots/research-machines.md (section per machine).
//
// Port claims of built-ins name their documented port (`.port`): a card shadows a built-in only when its IORQGE
// claim covers that port, not merely a mirror of it. Board ports are compared exactly (a card claim overlapping a
// board port is dead there for an Iorq card).

#include "refdata.h"

#include "emulator/platform.h"

namespace slots::refdata
{

namespace
{

// region <Signal sets (research-machines.md §3)>

constexpr SignalSet kZ80Lines = BusSignal::M1 | BusSignal::Rfsh | BusSignal::Int | BusSignal::Nmi | BusSignal::Busrq |
                                BusSignal::Busak | BusSignal::Wait | BusSignal::Reset;
// 48K edge: /IORQULA on lower 13, /ROMCS, +12 V
constexpr SignalSet kEdge48 = kZ80Lines | BusSignal::Iorqge | BusSignal::RdRom | BusSignal::Plus12V;
// 128K edge: lower 13 not connected
constexpr SignalSet kEdge128 = kZ80Lines | BusSignal::RdRom | BusSignal::Plus12V;
// +2A / +3 edge: no /ROMCS (/ROM1OE + /ROM2OE), no IORQGE
constexpr SignalSet kEdgePlus3 = kZ80Lines | BusSignal::Plus12V;
// NemoBus as on Pentagon-1024SL v2.2 (v1.0m: no /IODOS)
constexpr SignalSet kNemoBus = kZ80Lines | BusSignal::Iorqge | BusSignal::Dos | BusSignal::CsRom | BusSignal::RdRom |
                               BusSignal::Plus12V;
// Scorpion yellow board: ZX-bus pinout without +12 V on the system port
constexpr SignalSet kScorpionYellow = kZ80Lines | BusSignal::Iorqge | BusSignal::Dos | BusSignal::CsRom |
                                      BusSignal::RdRom;
// ATM I/O bus: data, CTS0-7, /IORD, /IOWR, reset, power only
constexpr SignalSet kAtmIoBus = BusSignal::Reset | BusSignal::Plus12V;
// Profi 64-pin bus: /OUTIORQ instead of IORQGE (board output), /ROMCS out, TR-DOS state out
constexpr SignalSet kProfiBus = kZ80Lines | BusSignal::Dos | BusSignal::CsRom | BusSignal::Plus12V;
// Sprinter ISA-8: IOCHRDY to /WAIT, RESET DRV, IRQ to the PIO, power
constexpr SignalSet kIsa8 = BusSignal::Wait | BusSignal::Reset | BusSignal::Int | BusSignal::Plus12V;

// endregion

// region <Shared built-in claims>

constexpr Function kAyFunctions[] = { Function::AySocket };
constexpr Function kBetaFunctions[] = { Function::Beta128 };
constexpr Function kKempstonFunctions[] = { Function::KempstonJoystick };
constexpr Function kMouseFunctions[] = { Function::KempstonMouse };

// AY decode of the 128K class: A15 = 1, A1 = 0, A14 selects (128K service manual §1.7)
constexpr PortClaim kAy128Claims[] = {
    { .mask = 0xC002, .match = 0xC000, .dir = Dir::InOut, .port = 0xFFFD },
    { .mask = 0xC002, .match = 0x8000, .dir = Dir::Out, .port = 0xBFFD },
};

// ULA #FE: A0 = 0
constexpr PortClaim kUlaClaims[] = {
    { .mask = 0x0001, .match = 0x0000, .dir = Dir::InOut, .port = 0x00FE },
};

// Beta-128 in DOS mode: the WD1793 registers and the system register (full low byte: MAME, FUSE)
constexpr PortClaim kBetaClaims[] = {
    { .mask = 0x00FF, .match = 0x001F, .dir = Dir::InOut, .gate = Gate::DosOnly },
    { .mask = 0x00FF, .match = 0x003F, .dir = Dir::InOut, .gate = Gate::DosOnly },
    { .mask = 0x00FF, .match = 0x005F, .dir = Dir::InOut, .gate = Gate::DosOnly },
    { .mask = 0x00FF, .match = 0x007F, .dir = Dir::InOut, .gate = Gate::DosOnly },
    { .mask = 0x00FF, .match = 0x00FF, .dir = Dir::InOut, .gate = Gate::DosOnly },
};

// Kempston joystick outside DOS, full low byte (ZX-Evo, unreal-ng)
constexpr PortClaim kKempstonClaims[] = {
    { .mask = 0x00FF, .match = 0x001F, .dir = Dir::In, .gate = Gate::NonDos },
};

constexpr PortClaim kCovoxFbClaims[] = {
    { .mask = 0x00FF, .match = 0x00FB, .dir = Dir::Out },
};

// endregion

// region <48K (research-machines.md §5)>

// The 48K has no AY: an AY / TurboSound board on the edge connector (Melodik-style interfaces) answers the 128K's
// #FFFD / #BFFD decode, which the emulator's 48K decoder routes to the AY socket. Modeled as a retrofitted AY socket
// (R-BUS-1a): empty by default, no physical socket, and every report says it is bolted on
constexpr BusDef kSpectrum48Buses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 0,
      .note = "no AY on the 48K board: an AY interface on the edge connector is retrofitted (128K decode)",
      .retrofit = true },
    { .id = "edge", .kind = BusKind::SinclairEdge, .signals = kEdge48, .physicalSlots = 1,
      .arbitration = Arbitration::UlaOnly, .readRule = ReadRule::CardOverUla },
};
constexpr BuiltInDef kSpectrum48BuiltIns[] = {
    { .id = "ula", .name = "ULA `#FE` (keyboard, beeper, border)", .claims = kUlaClaims },
};
constexpr Src kSpectrum48Sources[] = { Src::SinclairWiki48kEdge, Src::Spectrum48ServiceManual, Src::MameSpectrumExp };

// endregion

// region <128K, grey +2 (research-machines.md §6)>

constexpr BusDef kSpectrum128Buses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1 },
    { .id = "edge", .kind = BusKind::SinclairEdge, .signals = kEdge128, .physicalSlots = 1,
      .arbitration = Arbitration::None, .readRule = ReadRule::CardOverUla },
};
constexpr BusDef kPlus2Buses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1 },
    { .id = "edge", .kind = BusKind::SinclairEdge, .signals = kEdge48, .physicalSlots = 1,
      .arbitration = Arbitration::UlaOnly, .readRule = ReadRule::CardOverUla,
      .note = "lower 13 is /IORQGE; its scope beyond the ULA is unconfirmed" },
};
constexpr BuiltInDef kSpectrum128BuiltIns[] = {
    { .id = "ay", .name = "AY-3-8912", .socket = "ay-socket", .functions = kAyFunctions, .claims = kAy128Claims },
    { .id = "ula", .name = "ULA `#FE` (keyboard, beeper, border)", .claims = kUlaClaims },
};
constexpr Src kSpectrum128Sources[] = { Src::SinclairWiki128Edge, Src::Spectrum128ServiceManual, Src::Wos128kFaq };
constexpr Src kPlus2Sources[] = { Src::SinclairWiki128Edge, Src::SinclairWikiPlus2, Src::Wos128kFaq };

// endregion

// region <+2A, +3 (research-machines.md §7)>

constexpr BusDef kPlus3Buses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1 },
    { .id = "edge", .kind = BusKind::SinclairEdge, .signals = kEdgePlus3, .physicalSlots = 1,
      .arbitration = Arbitration::None, .readRule = ReadRule::WiredAnd,
      .note = "no /ROMCS (/ROM1OE + /ROM2OE), no IORQGE" },
};
constexpr Function kFdcFunctions[] = { Function::FdcUpd765 };
constexpr PortClaim kFdcClaims[] = {
    { .mask = 0xF002, .match = 0x2000, .dir = Dir::In, .port = 0x2FFD },
    { .mask = 0xF002, .match = 0x3000, .dir = Dir::InOut, .port = 0x3FFD },
};
constexpr BuiltInDef kPlus2ABuiltIns[] = {
    { .id = "ay", .name = "AY-3-8912", .socket = "ay-socket", .functions = kAyFunctions, .claims = kAy128Claims },
    { .id = "ula", .name = "gate array `#FE`", .claims = kUlaClaims },
};
constexpr BuiltInDef kPlus3BuiltIns[] = {
    { .id = "ay", .name = "AY-3-8912", .socket = "ay-socket", .functions = kAyFunctions, .claims = kAy128Claims },
    { .id = "ula", .name = "gate array `#FE`", .claims = kUlaClaims },
    { .id = "fdc", .name = "uPD765A floppy controller", .functions = kFdcFunctions, .claims = kFdcClaims },
};
constexpr Src kPlus3Sources[] = { Src::SinclairWikiPlus3Edge, Src::Wos128kFaq, Src::MameSpecpls3, Src::Plus3ServiceManual };

// endregion

// region <Pentagon (research-machines.md §8-9)>
// PENTAGON is the Pentagon 128K as chosen from the menu (owner, 2026-10-03): the 1991 board has no CPU bus
// connector. Cards are still allowed: the ZX-bus is "bolted on" (retrofit), has no physical slots, behaves as the
// NemoBus standard (card wins), and every report says in text that it is retrofitted.

constexpr BusDef kPentagonBuses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1 },
    { .id = "zxbus", .kind = BusKind::ZxBus, .signals = kNemoBus, .physicalSlots = 0,
      .arbitration = Arbitration::CardWins, .readRule = ReadRule::WiredAnd,
      .note = "no expansion connector on the Pentagon 128 board: the ZX-bus is retrofitted (NemoBus rules)",
      .retrofit = true },
};
// AY BDIR / BC1 from DD6: A15 = 1, A13 = 1, A1 = 0, A14 selects
constexpr PortClaim kPentagonAyClaims[] = {
    { .mask = 0xE002, .match = 0xE000, .dir = Dir::InOut, .port = 0xFFFD },
    { .mask = 0xE002, .match = 0xA000, .dir = Dir::Out, .port = 0xBFFD },
};
// Pentagon-1024SL built-in Kempston: A5 = 0, A0 = 1
constexpr PortClaim kPentagonKempstonClaims[] = {
    { .mask = 0x0021, .match = 0x0001, .dir = Dir::In, .gate = Gate::NonDos, .port = 0x001F },
};
constexpr BuiltInDef kPentagonBuiltIns[] = {
    { .id = "ay", .name = "YM2149 / AY-3-8910", .socket = "ay-socket", .functions = kAyFunctions,
      .claims = kPentagonAyClaims },
    { .id = "beta128", .name = "Beta-128 (VG93)", .functions = kBetaFunctions, .claims = kBetaClaims },
    { .id = "kempston-joystick", .name = "Kempston joystick", .functions = kKempstonFunctions,
      .claims = kPentagonKempstonClaims },
};
constexpr Src kPentagonSources[] = { Src::Pentagon22Schematic, Src::Pentagon22Cpld, Src::BcIg7, Src::MamePentagon };

// endregion

// region <Scorpion ZS-256 (research-machines.md §11): ZX-bus pinout variant, card wins>

constexpr BusDef kScorpionBuses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1 },
    { .id = "zxbus", .kind = BusKind::ZxBus, .signals = kScorpionYellow, .physicalSlots = 1,
      .arbitration = Arbitration::CardWins, .readRule = ReadRule::WiredAnd,
      .note = "+12 V only on the control port" },
};
constexpr BusDef kProfScorpionBuses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1 },
    { .id = "zxbus", .kind = BusKind::ZxBus, .signals = kNemoBus, .physicalSlots = 2,
      .arbitration = Arbitration::CardWins, .readRule = ReadRule::WiredAnd,
      .note = "Turbo+ board: +12 V on B22 (B29 through J6)" },
};
// The #FD group: A0 = 1, A1 = 0, A5 = 1, A15 / A14 select (Turbo+ netlist)
constexpr PortClaim kScorpionAyClaims[] = {
    { .mask = 0xC023, .match = 0xC021, .dir = Dir::InOut, .port = 0xFFFD },
    { .mask = 0xC023, .match = 0x8021, .dir = Dir::Out, .port = 0xBFFD },
};
constexpr BuiltInDef kScorpionBuiltIns[] = {
    { .id = "ay", .name = "AY-3-8912", .socket = "ay-socket", .functions = kAyFunctions, .claims = kScorpionAyClaims },
    { .id = "beta128", .name = "Beta-128 (WD1793)", .functions = kBetaFunctions, .claims = kBetaClaims },
    { .id = "kempston-joystick", .name = "Kempston joystick", .functions = kKempstonFunctions,
      .claims = kKempstonClaims },
};
constexpr Src kScorpionSources[] = { Src::ScorpionPortGuide, Src::ScorpionYellowReconstruction,
                                     Src::SpectrumExpert02, Src::MameScorpion };
constexpr Src kProfScorpionSources[] = { Src::ScorpionTurboPlusNetlist, Src::ScorpionProfRom, Src::ScorpionPortGuide,
                                         Src::MameScorpion };

// endregion

// region <ATM Turbo 2 / 2+ (research-machines.md §12): the I/O bus is not a ZX-bus; ZX-bus cards go through the
// third-party CPU-socket adapter>

constexpr BusDef kAtm450Buses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1 },
    { .id = "iobus", .kind = BusKind::AtmIoBus, .signals = kAtmIoBus, .physicalSlots = 1,
      .arbitration = Arbitration::None },
    { .id = "cpu-socket", .kind = BusKind::CpuSocket, .signals = kZ80Lines, .physicalSlots = 1,
      .arbitration = Arbitration::None },
};
constexpr BusDef kAtm710Buses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1 },
    { .id = "iobus", .kind = BusKind::AtmIoBus, .signals = kAtmIoBus, .physicalSlots = 2,
      .arbitration = Arbitration::None },
    { .id = "cpu-socket", .kind = BusKind::CpuSocket, .signals = kZ80Lines, .physicalSlots = 1,
      .arbitration = Arbitration::None },
};
constexpr PortClaim kAtmAdcClaims[] = {
    { .mask = 0xFFFF, .match = 0x7DFD, .dir = Dir::In },
};
constexpr Function kAtmIdeFunctions[] = { Function::IdeAtm };
constexpr PortClaim kAtmIdeClaims[] = {
    { .mask = 0x00FF, .match = 0x00EF, .dir = Dir::InOut, .gate = Gate::DosOnly },
};
constexpr BuiltInDef kAtm450BuiltIns[] = {
    { .id = "ay", .name = "AY-3-8912", .socket = "ay-socket", .functions = kAyFunctions, .claims = kAy128Claims },
    { .id = "beta128", .name = "Beta-128 (1818VG93)", .functions = kBetaFunctions, .claims = kBetaClaims },
    { .id = "covox", .name = "Covox / printer `#FB`", .claims = kCovoxFbClaims },
    { .id = "adc", .name = "ADC `#7DFD`", .claims = kAtmAdcClaims },
};
constexpr BuiltInDef kAtm710BuiltIns[] = {
    { .id = "ay", .name = "AY-3-8912", .kind = BuiltInKind::Socketed, .socket = "ay-socket", .chip = "AY-3-8912",
      .functions = kAyFunctions, .claims = kAy128Claims },
    { .id = "beta128", .name = "Beta-128 (VG93)", .functions = kBetaFunctions, .claims = kBetaClaims },
    { .id = "ide", .name = "ATM IDE", .functions = kAtmIdeFunctions, .claims = kAtmIdeClaims },
    { .id = "covox", .name = "Covox / printer `#FB`", .claims = kCovoxFbClaims },
    { .id = "adc", .name = "ADC `#7DFD`", .claims = kAtmAdcClaims },
};
constexpr Src kAtm450Sources[] = { Src::AtmMicroArtManual, Src::AtmSchematicSheet6 };
constexpr Src kAtm710Sources[] = { Src::RepoSlotsResearchMachines, Src::AtmMicroArtManual };

// endregion

// region <ZX-Evolution Baseconf and TS-Conf (research-machines.md §13): board wins (porthit)>

// Baseconf porthit set (zports.v L313-343), full low-byte decode. #FF is hidden only in shadow (DOS) mode; the Nemo
// IDE ports only in the IDE_HDD build (the emulated one).
constexpr PortClaim kBaseconfBoardPorts[] = {
    { .mask = 0x00FF, .match = 0x00FE },
    { .mask = 0x00FF, .match = 0x00F6 },
    { .mask = 0x00FF, .match = 0x00FC },
    { .mask = 0x00FF, .match = 0x00FD },    // every #xxFD
    { .mask = 0x00FF, .match = 0x00DF },
    { .mask = 0x00FF, .match = 0x001F },    // Kempston outside shadow mode, VG93 in it
    { .mask = 0x00FF, .match = 0x00F7 },
    { .mask = 0x00FF, .match = 0x0077 },
    { .mask = 0x00FF, .match = 0x0057 },
    { .mask = 0x00FF, .match = 0x00BF },
    { .mask = 0x00FF, .match = 0x00BE },
    { .mask = 0x00FF, .match = 0x00BD },
    { .mask = 0x00FF, .match = 0x00EF },    // COM port
    { .mask = 0x001F, .match = 0x0010 },    // Nemo IDE task file #10-#F0
    { .mask = 0x00FF, .match = 0x0011 },
    { .mask = 0x00FF, .match = 0x00C8 },
    { .mask = 0x00FF, .match = 0x00FF, .gate = Gate::DosOnly },
};
// TS-Conf porthit set (current zports.v L330-347): #AF and #FB hidden, #F6 / #FC / #BF-#BD passed
constexpr PortClaim kTsconfBoardPorts[] = {
    { .mask = 0x00FF, .match = 0x00FE },
    { .mask = 0x00FF, .match = 0x00AF },    // TS-Conf registers
    { .mask = 0x00FF, .match = 0x00FD },    // every #xxFD
    { .mask = 0x00FF, .match = 0x00FB },    // the board Covox
    { .mask = 0x00FF, .match = 0x00F7, .gate = Gate::NonDos },
    { .mask = 0x001F, .match = 0x0010 },    // Nemo IDE (IDE_HDD build)
    { .mask = 0x00FF, .match = 0x0011 },
    { .mask = 0x00FF, .match = 0x00C8 },
    { .mask = 0x00FF, .match = 0x001F },    // VG93 in DOS mode, Kempston otherwise
    { .mask = 0x00FF, .match = 0x003F, .gate = Gate::DosOnly },
    { .mask = 0x00FF, .match = 0x005F, .gate = Gate::DosOnly },
    { .mask = 0x00FF, .match = 0x007F, .gate = Gate::DosOnly },
    { .mask = 0x00FF, .match = 0x00FF, .gate = Gate::DosOnly },
    { .mask = 0x00FF, .match = 0x00DF },
    { .mask = 0x00FF, .match = 0x0077 },
    { .mask = 0x00FF, .match = 0x0057 },
    { .mask = 0x00FF, .match = 0x00EF },    // ZiFi
};

constexpr BusDef kAtm3Buses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1 },
    { .id = "zxbus", .kind = BusKind::ZxBus, .signals = kNemoBus, .physicalSlots = 2,
      .arbitration = Arbitration::BoardWins, .readRule = ReadRule::SlotOrder, .boardPorts = kBaseconfBoardPorts,
      .note = "+12 V only with jumper J4" },
};
constexpr BusDef kTsconfBuses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1 },
    { .id = "zxbus", .kind = BusKind::ZxBus, .signals = kNemoBus, .physicalSlots = 2,
      .arbitration = Arbitration::BoardWins, .readRule = ReadRule::SlotOrder, .boardPorts = kTsconfBoardPorts,
      .note = "+12 V only with jumper J4; the slots never see INTA" },
};

// The ZX-Evo AY is one YM2149 chip in a socket, decoded by the FPGA on the full low byte
constexpr PortClaim kEvoAyClaims[] = {
    { .mask = 0xC0FF, .match = 0xC0FD, .dir = Dir::InOut, .port = 0xFFFD },
    { .mask = 0xC0FF, .match = 0x80FD, .dir = Dir::Out, .port = 0xBFFD },
};
constexpr PortClaim kEvoMouseClaims[] = {
    { .mask = 0x00FF, .match = 0x00DF, .dir = Dir::In, .port = 0xFADF },
};
constexpr Function kSdZcFunctions[] = { Function::SdZc };
constexpr PortClaim kSdZcClaims[] = {
    { .mask = 0x00FF, .match = 0x0057, .dir = Dir::InOut },
    { .mask = 0x00FF, .match = 0x0077, .dir = Dir::InOut },
};
constexpr Function kNemoIdeFunctions[] = { Function::IdeNemo };
constexpr PortClaim kNemoIdeClaims[] = {
    { .mask = 0x001F, .match = 0x0010, .dir = Dir::InOut, .port = 0x0010 },
    { .mask = 0x00FF, .match = 0x0011, .dir = Dir::InOut },
    { .mask = 0x00FF, .match = 0x00C8, .dir = Dir::InOut },
};
constexpr Function kRtcFunctions[] = { Function::Rtc };
constexpr PortClaim kEvoRtcClaims[] = {
    { .mask = 0xFFFF, .match = 0xBFF7, .dir = Dir::Out },
    { .mask = 0xFFFF, .match = 0xDFF7, .dir = Dir::InOut },
    { .mask = 0xFFFF, .match = 0xEFF7, .dir = Dir::Out },
};
constexpr Function kSerialEfFunctions[] = { Function::SerialEf };
constexpr PortClaim kEvoComClaims[] = {
    { .mask = 0xF8FF, .match = 0xF8EF, .dir = Dir::InOut },
};
constexpr PortClaim kZifiClaims[] = {
    { .mask = 0x00FF, .match = 0x00EF, .dir = Dir::InOut },
};
constexpr PortClaim kTsconfRegisterClaims[] = {
    { .mask = 0x00FF, .match = 0x00AF, .dir = Dir::InOut },
};

constexpr BuiltInDef kAtm3BuiltIns[] = {
    { .id = "ay", .name = "YM2149 in a socket", .kind = BuiltInKind::Socketed, .socket = "ay-socket",
      .chip = "YM2149", .functions = kAyFunctions, .claims = kEvoAyClaims },
    { .id = "beta128", .name = "Beta-128 (VG93)", .functions = kBetaFunctions, .claims = kBetaClaims },
    { .id = "kempston-joystick", .name = "Kempston joystick (AVR)", .functions = kKempstonFunctions,
      .claims = kKempstonClaims },
    { .id = "kempston-mouse", .name = "Kempston mouse (AVR)", .functions = kMouseFunctions, .claims = kEvoMouseClaims },
    { .id = "sd-zc", .name = "Z-Controller SD", .functions = kSdZcFunctions, .claims = kSdZcClaims },
    { .id = "ide-nemo", .name = "Nemo IDE (FPGA build option)", .kind = BuiltInKind::Switchable,
      .functions = kNemoIdeFunctions, .claims = kNemoIdeClaims },
    { .id = "rtc", .name = "RTC (AVR)", .functions = kRtcFunctions, .claims = kEvoRtcClaims },
    // Written internally and passed on to the slots: a Covox card on #FB plays as well (no exclusive function)
    { .id = "covox", .name = "Covox `#FB` (passed to the slots)", .claims = kCovoxFbClaims },
    { .id = "com", .name = "COM port (AVR 16550)", .functions = kSerialEfFunctions, .claims = kEvoComClaims },
};
constexpr BuiltInDef kTsconfBuiltIns[] = {
    { .id = "ay", .name = "YM2149 in a socket", .kind = BuiltInKind::Socketed, .socket = "ay-socket",
      .chip = "YM2149", .functions = kAyFunctions, .claims = kEvoAyClaims },
    { .id = "beta128", .name = "Beta-128 (VG93)", .functions = kBetaFunctions, .claims = kBetaClaims },
    { .id = "kempston-joystick", .name = "Kempston joystick", .functions = kKempstonFunctions,
      .claims = kKempstonClaims },
    { .id = "kempston-mouse", .name = "Kempston mouse", .functions = kMouseFunctions, .claims = kEvoMouseClaims },
    { .id = "sd-zc", .name = "Z-Controller SD", .functions = kSdZcFunctions, .claims = kSdZcClaims },
    { .id = "ide-nemo", .name = "Nemo IDE (dropped in the VDAC2 build)", .kind = BuiltInKind::Switchable,
      .functions = kNemoIdeFunctions, .claims = kNemoIdeClaims },
    { .id = "ts-registers", .name = "TS-Conf registers `#xxAF`", .claims = kTsconfRegisterClaims },
    // Hidden from the slots by porthit: a Covox card's #FB is dead (no exclusive function needed for that)
    { .id = "covox", .name = "Covox `#FB` (hidden from the slots)", .claims = kCovoxFbClaims },
    { .id = "zifi", .name = "ZiFi (ESP8266 behind a 16550, `#00EF-#C9EF`, `#F8EF-#FFEF`)",
      .functions = kSerialEfFunctions, .claims = kZifiClaims },
};
constexpr Src kAtm3Sources[] = { Src::ZxevoSchematicRevC, Src::BaseconfZbus, Src::BaseconfZports };
constexpr Src kTsconfSources[] = { Src::ZxevoSchematicRevC, Src::TsconfZbus, Src::TsconfZports, Src::TsconfTune,
                                   Src::ZifiDoc };

// endregion

// region <Profi v5 / v3 (research-machines.md §14): own 64-pin bus, /OUTIORQ = board wins on the PROM group>

// The PROM-decoded group masked from /OUTIORQ: #1F, #3F, #5F, #7F are VG93 in DOS / CP/M and the 8255 outside, so
// always internal; the FDC system register #FF in TR-DOS. The extended CP/M map is not listed (CP/M mode only).
constexpr PortClaim kProfiBoardPorts[] = {
    { .mask = 0x009F, .match = 0x001F },
    { .mask = 0x00FF, .match = 0x00FF, .gate = Gate::DosOnly },
};
constexpr BusDef kProfiBuses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1 },
    { .id = "profi-bus", .kind = BusKind::ProfiBus, .signals = kProfiBus, .physicalSlots = 1,
      .arbitration = Arbitration::BoardWins, .readRule = ReadRule::WiredAnd, .boardPorts = kProfiBoardPorts,
      .note = "/OUTIORQ masks the PROM-decoded ports; `#FE`, `#7FFD`, `#DFFD`, AY, palette not shown masked" },
};
constexpr Function kPaletteFunctions[] = { Function::Palette };
// #xx7E: A0 = 0, A7 = 0 (Karabas-Pro, UnrealSpeccy, ZXMAK2, unreal-ng; the v5 album says "0FEH")
constexpr PortClaim kProfiPaletteClaims[] = {
    { .mask = 0x0081, .match = 0x0000, .dir = Dir::Out, .port = 0x007E },
};
constexpr Function kProfiIdeFunctions[] = { Function::IdeProfi };
constexpr BuiltInDef kProfiBuiltIns[] = {
    { .id = "ay", .name = "AY-3-8912 (optional socket)", .kind = BuiltInKind::Socketed, .socket = "ay-socket",
      .chip = "AY-3-8912", .functions = kAyFunctions, .claims = kAy128Claims },
    { .id = "beta128", .name = "VG93 FDC", .functions = kBetaFunctions, .claims = kBetaClaims },
    { .id = "ppi8255", .name = "8255 (Centronics, Kempston)", .functions = kKempstonFunctions,
      .claims = kKempstonClaims },
    { .id = "palette", .name = "palette `#7E`", .functions = kPaletteFunctions, .claims = kProfiPaletteClaims },
    { .id = "rtc", .name = "RTC", .functions = kRtcFunctions },
    { .id = "ide", .name = "IDE (extended map)", .functions = kProfiIdeFunctions },
};
constexpr BuiltInDef kProfi3BuiltIns[] = {
    { .id = "ay", .name = "AY-3-8912 (optional socket)", .kind = BuiltInKind::Socketed, .socket = "ay-socket",
      .chip = "AY-3-8912", .functions = kAyFunctions, .claims = kAy128Claims },
    { .id = "beta128", .name = "VG93 FDC", .functions = kBetaFunctions, .claims = kBetaClaims },
    { .id = "ppi8255", .name = "8255 (Centronics, Kempston)", .functions = kKempstonFunctions,
      .claims = kKempstonClaims },
};
constexpr Src kProfiSources[] = { Src::Insanity08Profi, Src::ZxReviewFedinProfi, Src::KarabasProPalette,
                                  Src::RepoProfi1024, Src::RepoSlotsResearchMachines };
constexpr Src kProfi3Sources[] = { Src::Insanity08Profi, Src::ProfiRetrace, Src::RepoSlotsResearchMachines };

// endregion

// region <Sprinter (research-machines.md §15): ISA-8 slots off the Z80 port path; every built-in behind the
// software-loaded decoder table>

constexpr BusDef kSprinterBuses[] = {
    { .id = "ay-socket", .kind = BusKind::AySocket, .physicalSlots = 1,
      .note = "the AY is in the FPGA; the socket is the emulator's TurboSound place" },
    { .id = "isa", .kind = BusKind::Isa8, .signals = kIsa8, .physicalSlots = 2, .arbitration = Arbitration::None,
      .note = "reached through a memory window; ISA cards never compete with a Z80 port" },
};
constexpr PortClaim kSprinterCovoxClaims[] = {
    { .mask = 0x00FF, .match = 0x00FB, .dir = Dir::Out },
    { .mask = 0x00FF, .match = 0x004F, .dir = Dir::Out },
    { .mask = 0x00FF, .match = 0x004E, .dir = Dir::InOut },
};
constexpr PortClaim kSprinterMouseClaims[] = {
    { .mask = 0xFFFF, .match = 0xFADF, .dir = Dir::In },
    { .mask = 0xFFFF, .match = 0xFBDF, .dir = Dir::In },
    { .mask = 0xFFFF, .match = 0xFFDF, .dir = Dir::In },
};
constexpr BuiltInDef kSprinterBuiltIns[] = {
    { .id = "ay", .name = "AY (FPGA)", .kind = BuiltInKind::Switchable, .socket = "ay-socket",
      .functions = kAyFunctions, .claims = kAy128Claims },
    { .id = "covox-blaster", .name = "Covox / Covox-Blaster (`#FB`, `#4F`, control `#4E`)", .kind = BuiltInKind::Switchable,
      .claims = kSprinterCovoxClaims },
    { .id = "beta128", .name = "WD1793", .kind = BuiltInKind::Switchable, .functions = kBetaFunctions,
      .claims = kBetaClaims },
    { .id = "kempston-mouse", .name = "Kempston mouse", .kind = BuiltInKind::Switchable, .functions = kMouseFunctions,
      .claims = kSprinterMouseClaims },
    { .id = "kempston-joystick", .name = "Kempston joystick", .kind = BuiltInKind::Switchable,
      .functions = kKempstonFunctions, .claims = kKempstonClaims },
};
constexpr Src kSprinterSources[] = { Src::SprinterSchematic, Src::SprinterHard, Src::RepoSprinterHardware,
                                     Src::RepoSprinterIsa };

// endregion

constexpr MachineDef kMachines[] = {
    { .model = MM_SPECTRUM48, .name = "48K", .variant = "Sinclair 16K / 48K", .buses = kSpectrum48Buses,
      .builtIns = kSpectrum48BuiltIns, .sources = kSpectrum48Sources },
    { .model = MM_SPECTRUM128, .name = "128K", .variant = "Sinclair 128K (UK / Spanish)", .buses = kSpectrum128Buses,
      .builtIns = kSpectrum128BuiltIns, .sources = kSpectrum128Sources },
    { .model = MM_PLUS2, .name = "PLUS2", .variant = "Amstrad grey +2", .buses = kPlus2Buses,
      .builtIns = kSpectrum128BuiltIns, .sources = kPlus2Sources },
    { .model = MM_PLUS2A, .name = "PLUS2A", .variant = "Amstrad +2A", .buses = kPlus3Buses,
      .builtIns = kPlus2ABuiltIns, .sources = kPlus3Sources },
    { .model = MM_PLUS3, .name = "PLUS3", .variant = "Amstrad +3", .buses = kPlus3Buses,
      .builtIns = kPlus3BuiltIns, .sources = kPlus3Sources },
    { .model = MM_PENTAGON, .name = "PENTAGON", .variant = "Pentagon 128 (1991): no expansion connector, ZX-bus retrofitted",
      .buses = kPentagonBuses, .builtIns = kPentagonBuiltIns, .sources = kPentagonSources },
    { .model = MM_SCORP, .name = "SCORPION", .variant = "Scorpion ZS-256 yellow board", .buses = kScorpionBuses,
      .builtIns = kScorpionBuiltIns, .sources = kScorpionSources },
    { .model = MM_PROFSCORP, .name = "PROFSCORP", .variant = "Scorpion ZS-256 Turbo+ with ProfROM",
      .buses = kProfScorpionBuses, .builtIns = kScorpionBuiltIns, .sources = kProfScorpionSources },
    { .model = MM_ATM450, .name = "ATM450", .variant = "ATM Turbo 2 v4.50", .buses = kAtm450Buses,
      .builtIns = kAtm450BuiltIns, .sources = kAtm450Sources },
    { .model = MM_ATM710, .name = "ATM710", .variant = "ATM Turbo 2+ v7.10", .buses = kAtm710Buses,
      .builtIns = kAtm710BuiltIns, .sources = kAtm710Sources },
    { .model = MM_ATM3, .name = "ATM3", .variant = "ZX-Evolution rev C, Baseconf", .buses = kAtm3Buses,
      .builtIns = kAtm3BuiltIns, .sources = kAtm3Sources },
    { .model = MM_TSL, .name = "TSL", .variant = "ZX-Evolution rev C, TS-Conf (FREE_IORQ off)", .buses = kTsconfBuses,
      .builtIns = kTsconfBuiltIns, .sources = kTsconfSources },
    { .model = MM_PROFI, .name = "PROFI", .variant = "Profi v5", .buses = kProfiBuses, .builtIns = kProfiBuiltIns,
      .sources = kProfiSources },
    { .model = MM_PROFI3, .name = "PROFI3", .variant = "Profi v3.2", .buses = kProfiBuses, .builtIns = kProfi3BuiltIns,
      .sources = kProfi3Sources },
    { .model = MM_SPRINTER, .name = "SPRINTER", .variant = "Peters Plus Sprinter Sp2000", .buses = kSprinterBuses,
      .builtIns = kSprinterBuiltIns, .sources = kSprinterSources },
};

} // namespace

std::span<const MachineDef> Machines()
{
    return kMachines;
}

} // namespace slots::refdata
