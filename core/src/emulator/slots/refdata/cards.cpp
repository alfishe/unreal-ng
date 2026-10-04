// ZX-bus slots reference data: the card catalog (compatibility-matrix.md §2, the first migration set).
// One CardDef per card; functions and claims follow the card's options through When conditions. Facts and their
// sources: docs/inprogress/2026-10-03-zx-bus-slots/research-cards.md (section per card).

#include "refdata.h"

namespace slots::refdata
{

namespace
{

constexpr SignalSet kNone = 0;

// region <ay / ts / tsfm: AY-socket boards (they inherit the host decode, research-cards.md §5.2-5.3)>

constexpr FunctionUse kAySocketFunctions[] = {
    { .function = Function::AySocket },
};

constexpr Src kAySources[] = { Src::Wos128kFaq, Src::Spectrum128ServiceManual };
constexpr Src kTsSources[] = { Src::ShiruTurboSound, Src::MameAySlot, Src::RepoSlotsResearchCards };
constexpr Src kTsfmSources[] = { Src::RepoTsfmHardware, Src::RepoSlotsResearchCards };

// endregion

// region <gs, gs-lw: classic General Sound (research-cards.md §5.7): full low-byte decode, IORQGE on reads only, no #33>

constexpr PortClaim kGsClaims[] = {
    { .mask = 0x00FF, .match = 0x00B3, .dir = Dir::InOut, .iorqge = Iorqge::ReadsOnly },
    { .mask = 0x00FF, .match = 0x00BB, .dir = Dir::InOut, .iorqge = Iorqge::ReadsOnly },
};

constexpr FunctionUse kGsFunctions[] = {
    { .function = Function::Gs },
};

constexpr OptionValue kGsRamValues[] = {
    { "128k", "128 K" }, { "256k", "256 K" }, { "512k", "512 K" }, { "1m", "1 M" }, { "2m", "2 M" },
};
constexpr OptionValue kGsRomValues[] = {
    { "1.04", "ROM 1.04" }, { "1.05", "ROM 1.05" },
};
constexpr OptionDef kGsOptions[] = {
    { .key = Opt::Ram, .kind = OptionKind::Enum, .values = kGsRamValues, .defaultBits = Bit(0),
      .description = "card RAM (128 K stock; 512 K extension; 2 M later boards)" },
    { .key = Opt::Rom, .kind = OptionKind::Enum, .values = kGsRomValues, .defaultBits = Bit(1),
      .description = "card ROM version" },
};

constexpr Src kGsSources[] = { Src::AlfisheGeneralSound, Src::BcIg4, Src::RepoSlotsResearchCards };
constexpr Src kGsLwSources[] = { Src::RepoGeneralSound, Src::AlfisheGeneralSound };

// endregion

// region <neogs (research-cards.md §5.8): IORQGE on the address match alone, #33 control>

constexpr PortClaim kNeogsClaims[] = {
    { .mask = 0x00FF, .match = 0x00B3, .dir = Dir::InOut, .iorqge = Iorqge::Yes },
    { .mask = 0x00FF, .match = 0x00BB, .dir = Dir::InOut, .iorqge = Iorqge::Yes },
    { .mask = 0x00FF, .match = 0x0033, .dir = Dir::InOut, .iorqge = Iorqge::Yes },
};

constexpr OptionValue kNeogsRamValues[] = {
    { "2m", "2 MB" }, { "4m", "4 MB" },
};
constexpr OptionDef kNeogsOptions[] = {
    { .key = Opt::Ram, .kind = OptionKind::Enum, .values = kNeogsRamValues, .defaultBits = Bit(0),
      .description = "card RAM" },
};

constexpr const char* kNeogsMedia[] = { "sd.ngs" };
constexpr Src kNeogsSources[] = { Src::AlfisheNeogs, Src::RepoGeneralSound, Src::RepoSlotsResearchCards };

// endregion

// region <moonsound (research-cards.md §5.10): low-byte decode, IORQGE; off in DOS unless JP1 "PentEvo" is fitted>

constexpr OptionValue kJp1Values[] = {
    { "open", "JP1 open" }, { "fitted", "JP1 fitted" },
};
constexpr When kJp1Open{ Opt::Jp1, Bit(0) };
constexpr When kJp1Fitted{ Opt::Jp1, Bit(1) };

// JP1 open: the card is off whenever /DOS is low. JP1 fitted: only /IODOS gates it, which no creatable machine
// drives (research-machines.md §1 item 6), so the card answers in DOS mode too.
constexpr PortClaim kMoonsoundClaims[] = {
    { .mask = 0x00FC, .match = 0x00C4, .dir = Dir::InOut, .iorqge = Iorqge::Yes, .gate = Gate::NonDos, .when = kJp1Open },
    { .mask = 0x00FE, .match = 0x007E, .dir = Dir::InOut, .iorqge = Iorqge::Yes, .gate = Gate::NonDos, .when = kJp1Open },
    { .mask = 0x00FC, .match = 0x00C4, .dir = Dir::InOut, .iorqge = Iorqge::Yes, .when = kJp1Fitted },
    { .mask = 0x00FE, .match = 0x007E, .dir = Dir::InOut, .iorqge = Iorqge::Yes, .when = kJp1Fitted },
};

constexpr FunctionUse kMoonsoundFunctions[] = {
    { .function = Function::Opl4 },
};

constexpr OptionDef kMoonsoundOptions[] = {
    { .key = Opt::Jp1, .kind = OptionKind::Enum, .values = kJp1Values, .defaultBits = Bit(0),
      .description = "JP1 \"PentEvo\": fitted = /DOS ignored, only /IODOS gates the card" },
};

constexpr Src kMoonsoundSources[] = { Src::AlfisheZxmMoonsound, Src::MicklabMoonsound, Src::RepoSlotsResearchCards };

// endregion

// region <covox-fb (research-cards.md §5.11): write-only, passive>

constexpr OptionValue kCovoxDecodeValues[] = {
    { "full", "full decode" }, { "a2", "A2-only decode" },
};

constexpr PortClaim kCovoxClaims[] = {
    { .mask = 0x00FF, .match = 0x00FB, .dir = Dir::Out, .when = { Opt::Decode, Bit(0) } },
    { .mask = 0x0004, .match = 0x0000, .dir = Dir::Out, .when = { Opt::Decode, Bit(1) }, .port = 0x00FB },
};

constexpr FunctionUse kCovoxFunctions[] = {
    { .function = Function::CovoxFb },
};

constexpr OptionDef kCovoxOptions[] = {
    { .key = Opt::Decode, .kind = OptionKind::Enum, .values = kCovoxDecodeValues, .defaultBits = Bit(0),
      .description = "port decode: A7-A0, or A2 = 0 only (ZX Format #5 homebrew)" },
};

constexpr Src kCovoxSources[] = { Src::ZxdnCovox, Src::DukeyusupovCovox, Src::RepoSlotsResearchCards };

// endregion

// region <soundrive (research-cards.md §5.12): S1 mode switch; the emulator decode (BC IG #4's looser one unconfirmed)>

// `both`: the decode emulators have always used (Unreal, Xpeccy: mode-1 and mode-2 ports at once), what the legacy
// [SOUND] SD=1 key fits; the real card answers one set at a time (research-cards.md §5.12)
constexpr OptionValue kSoundriveModeValues[] = {
    { "1", "mode 1" }, { "2", "mode 2" }, { "both", "modes 1 + 2 (emulator decode)" },
};
constexpr When kMode1{ Opt::Mode, Bit(0) | Bit(2) };
constexpr When kMode2{ Opt::Mode, Bit(1) | Bit(2) };

constexpr PortClaim kSoundriveClaims[] = {
    { .mask = 0x00AF, .match = 0x000F, .dir = Dir::Out, .when = kMode1 },
    { .mask = 0x00F5, .match = 0x00F1, .dir = Dir::Out, .when = kMode2 },
};

constexpr FunctionUse kSoundriveFunctions[] = {
    { .function = Function::Soundrive },
    { .function = Function::CovoxFb, .when = kMode2 },   // mode 2: channel RD is #FB
};

constexpr OptionDef kSoundriveOptions[] = {
    { .key = Opt::Mode, .kind = OptionKind::Enum, .values = kSoundriveModeValues, .defaultBits = Bit(0),
      .splitMatrixRows = true, .description = "S1 \"Soundrive / COVOX\" switch: one port set at a time; `both` = the emulator decode of both sets" },
};

constexpr Src kSoundriveSources[] = { Src::City20Soundrive, Src::VelesoftDa, Src::BcIg4, Src::RepoSlotsResearchCards };

// endregion

// region <multisound (ZX-MultiSound hardware reference §3.1, §5)>

constexpr OptionValue kDipValues[] = {
    { "ym", "`ym`" }, { "saa", "`saa`" }, { "gs", "`gs`" }, { "sd", "`sd`" },
};
constexpr When kDipYm{ Opt::Dip, Bit(0) };
constexpr When kDipSaa{ Opt::Dip, Bit(1) };
constexpr When kDipGs{ Opt::Dip, Bit(2) };
constexpr When kDipSd{ Opt::Dip, Bit(3) };

constexpr PortClaim kMultisoundClaims[] = {
    // YM register / control: A15-A14 = 11, A3-A0 = 1101; IORQGE only with A13 = 1 (#FFFD); #DFFD passive, co-written
    // with the machine's own #DFFD
    { .mask = 0xE00F, .match = 0xE00D, .dir = Dir::InOut, .iorqge = Iorqge::Yes, .when = kDipYm, .port = 0xFFFD },
    { .mask = 0xE00F, .match = 0xC00D, .dir = Dir::InOut, .when = kDipYm, .port = 0xDFFD },
    { .mask = 0xC00F, .match = 0x800D, .dir = Dir::Out, .iorqge = Iorqge::Yes, .when = kDipYm, .port = 0xBFFD },
    // SAA #FF data / #1FF address: passive since 2023-12, ROM-fetch lock
    { .mask = 0x00FF, .match = 0x00FF, .dir = Dir::Out, .romLock = true, .when = kDipSaa },
    { .mask = 0x00FF, .match = 0x00B3, .dir = Dir::InOut, .iorqge = Iorqge::Yes, .when = kDipGs },
    { .mask = 0x00FF, .match = 0x00BB, .dir = Dir::InOut, .iorqge = Iorqge::Yes, .when = kDipGs },
    // SounDrive mode-1 ports, passive, ROM-fetch lock
    { .mask = 0x00AF, .match = 0x000F, .dir = Dir::Out, .romLock = true, .when = kDipSd },
};

constexpr FunctionUse kMultisoundFunctions[] = {
    { .function = Function::AySocket, .role = Role::Takeover, .when = kDipYm },
    { .function = Function::Midi, .when = kDipYm },
    { .function = Function::Saa, .when = kDipSaa },
    { .function = Function::Gs, .when = kDipGs },
    { .function = Function::Soundrive, .when = kDipSd },
};

constexpr OptionValue kGsRam2Values[] = {
    { "1m", "1 M" }, { "2m", "2 M" },
};
constexpr OptionValue kCtrlMaskValues[] = {
    { "pro", "`pro`" }, { "classic", "`classic`" },
};
constexpr OptionDef kMultisoundOptions[] = {
    { .key = Opt::Dip, .kind = OptionKind::Set, .values = kDipValues, .defaultBits = Bit(0) | Bit(1) | Bit(2) | Bit(3),
      .description = "DIP switches: the functions enabled" },
    { .key = Opt::GsRam, .kind = OptionKind::Enum, .values = kGsRam2Values, .defaultBits = Bit(0),
      .description = "GS RAM (2 M with the rev_A1_2mb firmware)" },
    { .key = Opt::CtrlMask, .kind = OptionKind::Enum, .values = kCtrlMaskValues, .defaultBits = Bit(0),
      .description = "control-byte compare: pro (#F0-#FF) or classic (unofficial issue #11 patch)" },
};

constexpr Src kMultisoundSources[] = { Src::UzixMultisound, Src::RepoMultisoundHardware };

// endregion

// region <zxnetusb (research-cards.md §7.1): IORQGE from the address alone>

constexpr PortClaim kZxnetusbClaims[] = {
    { .mask = 0x00FF, .match = 0x00AB, .dir = Dir::InOut, .iorqge = Iorqge::Yes },
};

constexpr FunctionUse kZxnetusbFunctions[] = {
    { .function = Function::NetZxnetusb },
};

constexpr Src kZxnetusbSources[] = { Src::Lvd2ZxnetUsb, Src::RepoSlotsResearchCards };

// endregion

// region <zx-wifi (research-cards.md §7.2): 16550 on #EF or #EE (build option), IORQGE from board v1.2>

constexpr OptionValue kWifiPortValues[] = {
    { "ef", "`#EF` build" }, { "ee", "`#EE` build" },
};
constexpr When kPortEf{ Opt::Port, Bit(0) };
constexpr When kPortEe{ Opt::Port, Bit(1) };

constexpr PortClaim kWifiClaims[] = {
    { .mask = 0x00FF, .match = 0x00EF, .dir = Dir::InOut, .iorqge = Iorqge::Yes, .when = kPortEf },
    { .mask = 0x00FF, .match = 0x00EE, .dir = Dir::InOut, .iorqge = Iorqge::Yes, .when = kPortEe },
};

constexpr FunctionUse kWifiFunctions[] = {
    { .function = Function::SerialEf, .when = kPortEf },
    { .function = Function::SerialEe, .when = kPortEe },
};

constexpr OptionDef kWifiOptions[] = {
    { .key = Opt::Port, .kind = OptionKind::Enum, .values = kWifiPortValues, .defaultBits = Bit(0),
      .splitMatrixRows = true, .description = "port build (jumper v1.3-1.5, GAL v1.6)" },
};

constexpr Src kWifiSources[] = { Src::IzzxZxWifi, Src::IzzxZxWifiReadme, Src::RepoSlotsResearchCards };

// endregion

constexpr CardDef kCards[] = {
    { .id = "ay", .name = "machine's own AY (socket default)", .bus = BusKind::AySocket, .needs = kNone,
      .socketDefault = true, .portsNote = "host decode",
      .functions = kAySocketFunctions, .sources = kAySources },
    { .id = "ts", .name = "TurboSound (NedoPC, 2 x AY)", .bus = BusKind::AySocket, .needs = kNone,
      .portsNote = "host decode; chip select `#FC-#FF`",
      .functions = kAySocketFunctions, .sources = kTsSources },
    { .id = "tsfm", .name = "TurboSound FM (NedoPC)", .bus = BusKind::AySocket, .needs = kNone,
      .portsNote = "host decode; control `#F8-#FF`",
      .functions = kAySocketFunctions, .sources = kTsfmSources },
    { .id = "gs", .name = "General Sound (classic)", .bus = BusKind::ZxBus, .needs = Sig(BusSignal::Iorqge),
      .options = kGsOptions, .functions = kGsFunctions, .claims = kGsClaims, .sources = kGsSources },
    { .id = "gs-lw", .name = "General Sound, lightweight player (emulator-only personality of `gs`)",
      .bus = BusKind::ZxBus, .needs = Sig(BusSignal::Iorqge),
      .functions = kGsFunctions, .claims = kGsClaims, .sources = kGsLwSources },
    { .id = "neogs", .name = "NeoGS", .bus = BusKind::ZxBus,
      .needs = BusSignal::Iorqge | BusSignal::CsRom | BusSignal::RdRom | BusSignal::Wait,
      .options = kNeogsOptions, .functions = kGsFunctions, .claims = kNeogsClaims, .media = kNeogsMedia,
      .sources = kNeogsSources },
    { .id = "moonsound", .name = "ZXM-MoonSound", .bus = BusKind::ZxBus, .needs = Sig(BusSignal::Iorqge),
      .options = kMoonsoundOptions, .functions = kMoonsoundFunctions, .claims = kMoonsoundClaims,
      .sources = kMoonsoundSources },
    { .id = "covox-fb", .name = "Covox `#FB`", .bus = BusKind::ZxBus, .needs = kNone,
      .options = kCovoxOptions, .functions = kCovoxFunctions, .claims = kCovoxClaims, .sources = kCovoxSources },
    { .id = "soundrive", .name = "SounDrive 1.05", .bus = BusKind::ZxBus, .needs = kNone,
      .options = kSoundriveOptions, .functions = kSoundriveFunctions, .claims = kSoundriveClaims,
      .sources = kSoundriveSources },
    { .id = "multisound", .name = "ZX-MultiSound rev.A2", .bus = BusKind::ZxBus,
      .needs = BusSignal::Iorqge | BusSignal::Plus12V, .detection = CycleDetection::RdWr,
      .options = kMultisoundOptions, .functions = kMultisoundFunctions, .claims = kMultisoundClaims,
      .sources = kMultisoundSources },
    { .id = "zxnetusb", .name = "ZXNETUSB", .bus = BusKind::ZxBus, .needs = BusSignal::Iorqge | BusSignal::CsRom,
      .functions = kZxnetusbFunctions, .claims = kZxnetusbClaims, .sources = kZxnetusbSources },
    { .id = "zx-wifi", .name = "ZX-WiFi (izzx)", .bus = BusKind::ZxBus, .needs = Sig(BusSignal::Iorqge),
      .options = kWifiOptions, .functions = kWifiFunctions, .claims = kWifiClaims, .sources = kWifiSources },
};

} // namespace

std::span<const CardDef> Cards()
{
    return kCards;
}

} // namespace slots::refdata
