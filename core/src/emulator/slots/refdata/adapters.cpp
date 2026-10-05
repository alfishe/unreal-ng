// ZX-bus slots reference data: bus adapters (research-machines.md §12.2, §15.3, §16). A card behind an adapter fits
// as `adapter` when every signal it needs reaches it: (machine bus signals & passes) | adds.

#include "refdata.h"

namespace slots::refdata
{

namespace
{

constexpr SignalSet kZ80Lines = BusSignal::M1 | BusSignal::Rfsh | BusSignal::Int | BusSignal::Nmi | BusSignal::Busrq |
                                BusSignal::Busak | BusSignal::Wait | BusSignal::Reset;

constexpr Src kEdgeSources[] = { Src::VelesoftProtector, Src::SinclairWikiEdge, Src::SpectrumExpert02,
                                 Src::RepoSlotsResearchMachines };
constexpr Src kAtmSources[] = { Src::RepoSlotsResearchMachines };
constexpr Src kSprinterSources[] = { Src::MameIsaZxbusAdapter, Src::ZxpkSprinterAdapter, Src::RepoSlotsResearchMachines };

constexpr AdapterDef kAdapters[] = {
    // Position by position (research-machines.md §16.3): IORQGE passes through on the 48K / grey +2 (ULA scope only),
    // nothing can be done on the 128K / +2A / +3; /DOS and CSR/ do not exist on the edge; /ROMCS maps to RDR/;
    // +12 V is routed from upper 22
    { .id = "zxbus-to-sinclair-edge", .name = "ZX-bus card on the Sinclair edge", .cardSide = BusKind::ZxBus,
      .machineSide = BusKind::SinclairEdge,
      .passes = kZ80Lines | BusSignal::Iorqge | BusSignal::RdRom | BusSignal::Plus12V,
      .note = "no /DOS, no CSR/; IORQGE only where the edge has it (ULA scope)", .sources = kEdgeSources },
    // Third-party adapter in the Z80 socket: two Pentagon-pinout slots, IORQGE chain on a 555LL1 (card wins),
    // /DOS, clocks and +-12 V hand-wired (atmturbo SVN pcad/zxbus_adapter)
    { .id = "atm-cpu-socket-zxbus", .name = "ATM CPU-socket ZX-bus adapter", .cardSide = BusKind::ZxBus,
      .machineSide = BusKind::CpuSocket, .passes = kZ80Lines,
      .adds = BusSignal::Iorqge | BusSignal::Dos | BusSignal::CsRom | BusSignal::RdRom | BusSignal::Plus12V,
      .hasArbitration = true, .arbitration = Arbitration::CardWins,
      .note = "which data bus it taps is unconfirmed", .sources = kAtmSources },
    // Peters Plus "ISA -> Spectrum-BUS" (no schematic found). Behind the ISA window no device competes for a
    // card's port, so its IORQGE is satisfied without a chain (MAME models the adapter as transparent, no IORQGE);
    // memory, IRQ, NMI and WAIT are not passed
    { .id = "sprinter-isa-zxbus", .name = "Sprinter ISA to ZX-bus adapter", .cardSide = BusKind::ZxBus,
      .machineSide = BusKind::Isa8, .passes = BusSignal::Reset | BusSignal::Plus12V,
      .adds = Sig(BusSignal::Iorqge), .hasArbitration = true, .arbitration = Arbitration::None,
      .note = "software reaches the card through the ISA window, not as Spectrum ports", .sources = kSprinterSources },
};

} // namespace

std::span<const AdapterDef> Adapters()
{
    return kAdapters;
}

} // namespace slots::refdata
