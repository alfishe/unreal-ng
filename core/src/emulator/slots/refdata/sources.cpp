// ZX-bus slots reference data: the bibliography. Every card, machine, adapter and exception cites ids from here.
// Titles and links come from the SL-0 research (docs/inprogress/2026-10-03-zx-bus-slots/research-machines.md,
// research-cards.md). Facts the research took from sources without a public link (local collection, SVN-only
// repositories) are cited through the research document that records them.

#include "refdata.h"

namespace slots::refdata
{

namespace
{

constexpr SourceRef kSources[] = {
    { Src::BcIg7, "Black_Cat, BC Info Guide #7: Standardization of ZX BUS interfaces and buses (R2020-05-27)",
      "https://zx.clan.su/forum/7-82-1" },
    { Src::BcIg4, "Black_Cat, BC Info Guide #4: full ZX port table",
      "https://wiki.speccy.org/_media/cursos/ensamblador/zx-ports-full-table.pdf" },
    { Src::SpectrumExpert02, "Spectrum Expert #02, ZX-BUS", "https://zxpress.ru/article.php?id=11759" },
    { Src::MameZxbusBus, "MAME zxbus/bus.h",
      "https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/zxbus/bus.h" },
    { Src::MameSpectrumExp, "MAME spectrum/exp.h",
      "https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/exp.h" },
    { Src::SinclairWikiEdge, "Sinclair Wiki: ZX Spectrum edge connector",
      "https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_edge_connector" },
    { Src::SinclairWiki48kEdge, "Sinclair Wiki: ZX Spectrum 16K/48K edge connector",
      "https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_16K/48K_edge_connector" },
    { Src::SinclairWiki128Edge, "Sinclair Wiki: ZX Spectrum 128 edge connector",
      "https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_128_edge_connector" },
    { Src::SinclairWikiPlus2, "Sinclair Wiki: ZX Spectrum +2", "https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_+2" },
    { Src::SinclairWikiPlus3Edge, "Sinclair Wiki: ZX Spectrum +2A/2B, +3/3B edge connector",
      "https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_+2A/2B,_+3/3B_edge_connector" },
    { Src::VelesoftProtector, "Velesoft, ZX Bus Protector", "https://velesoft.speccy.cz/protector.htm" },

    { Src::Spectrum48ServiceManual, "ZX Spectrum 48K service manual",
      "https://spectrumforeveryone.com/wp-content/uploads/2017/08/ZX-Spectrum-Service-Manual.pdf" },
    { Src::Spectrum128ServiceManual, "ZX Spectrum 128 service manual",
      "https://spectrumforeveryone.com/wp-content/uploads/2017/11/ZX-Spectrum-128-Service-Manual.pdf" },
    { Src::Wos128kFaq, "World of Spectrum: 128K reference", "https://worldofspectrum.org/faq/reference/128kreference.htm" },
    { Src::MameSpecpls3, "MAME specpls3.cpp", "https://github.com/mamedev/mame/blob/master/src/mame/sinclair/specpls3.cpp" },
    { Src::Plus3ServiceManual, "ZX Spectrum +3 service manual, p.18",
      "https://worldofspectrum.org/ZXSpectrum128+3ServiceManual/18.html" },

    { Src::Pentagon22Schematic, "Pentagon-1024SL v2.2 schematic (ver22.pdf)",
      "https://github.com/koe1234/pentagon_2.2/blob/main/ver22.pdf" },
    { Src::Pentagon22Cpld, "Pentagon-1024SL v2.2 CPLD p1024sl.tdf",
      "https://github.com/koe1234/pentagon_2.2/blob/main/CPLD/p1024sl.tdf" },
    { Src::MamePentagon, "MAME pentagon.cpp", "https://github.com/mamedev/mame/blob/master/src/mame/sinclair/pentagon.cpp" },

    { Src::ScorpionTurboPlusNetlist, "Scorpion ZS-256 Turbo+ reconstruction (KiCad netlist)",
      "https://github.com/romychs/Scorpion256TPlus/blob/main/KiCAD/Scorpion-256-Turbo.kicad_pcb" },
    { Src::ScorpionPortGuide, "Scorpion ZS-256 I/O ports reference guide",
      "https://zxpress.ru/eng/ezines/msd/03/scorpion-zs-256-i-o-ports-reference-guide-for-programmers-complete-description-of-port-allocation" },
    { Src::ScorpionYellowReconstruction, "YScorp: Scorpion yellow board reconstruction",
      "https://zxgit.org/romych/YScorp/src/branch/master/Sources/PCB_Scorpion-Yellow_v12.2.1.json" },
    { Src::ScorpionProfRom, "Scorpion ProfROM paging",
      "https://github.com/romychs/Scorpion256TPlus/blob/main/doc/files/Scorpion_ProfROM_Paging.md" },
    { Src::MameScorpion, "MAME scorpion.cpp", "https://github.com/mamedev/mame/blob/master/src/mame/sinclair/scorpion.cpp" },

    { Src::AtmMicroArtManual, "MicroArt ATM Turbo manual: peripherals and connectors",
      "https://zxpress.ru/ru/books/chapter/2353" },
    { Src::AtmSchematicSheet6, "ATM Turbo schematic, sheet 6", "https://zxpress.ru/chapters_images/atmturbo-6.jpg" },

    { Src::ZxevoSchematicRevC, "ZX-Evolution rev C schematic",
      "https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/revC/zxevo_sch_revc.pdf" },
    { Src::BaseconfZbus, "ZX-Evo Baseconf z80/zbus.v",
      "https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/base/z80/zbus.v" },
    { Src::BaseconfZports, "ZX-Evo Baseconf z80/zports.v",
      "https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/base/z80/zports.v" },
    { Src::TsconfZbus, "TS-Conf z80/zbus.v", "https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/z80/zbus.v" },
    { Src::TsconfZports, "TS-Conf z80/zports.v",
      "https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/z80/zports.v" },
    { Src::TsconfTune, "TS-Conf quartus/tune.v",
      "https://github.com/tslabs/zx-evo/blob/master/pentevo/fpga/current/quartus/tune.v" },
    { Src::ZifiDoc, "TS-Conf ZiFi", "https://github.com/tslabs/zx-evo/blob/master/pentevo/docs/ZiFi/zifi.md" },

    { Src::Insanity08Profi, "Insanity #08: connecting peripherals to the Profi (2001)",
      "https://zxpress.ru/ru/ezines/insanity/08/podklyuchenie-periferii-k-kompyuteru-profi-general-sound-ustroystva-na-zx-bus-sovmestimost-s-tr-dos" },
    { Src::ZxReviewFedinProfi, "ZX Review #3-4 (1997): P. Fedin's Profi modification",
      "https://zxpress.ru/ru/ezines/zx-review/3-4/dorabotka-kompyutera-profi-dlya-beskonfliktnogo-podklyucheniya-periferii-modema-myshi-i-drugih" },
    { Src::KarabasProPalette, "Karabas-Pro karabas_pro.vhd (Profi palette decode)",
      "https://github.com/andykarpov/karabas-pro/blob/c210d6c/firmware/src/fpga/profi/rtl/karabas_pro.vhd#L1389" },
    { Src::ProfiRetrace, "alemorf/retro_computers Profi 3.2 re-trace",
      "https://github.com/alemorf/retro_computers/tree/master/Profi_3_2" },
    { Src::RepoProfi1024, "unreal-ng: Profi 1024 hardware notes", "docs/hardware/profi-1024.md" },

    { Src::SprinterSchematic, "Peters Plus Sprinter Sp2000 schematic v1.62 (archive)",
      "https://web.archive.org/web/20031117033312/http://www.petersplus.com/download/sp2k_sch.pdf" },
    { Src::SprinterHard, "Sprinter hardware sources (ACEX SP2_ACEX.TDF)", "https://gitlab.com/sprinter-computer/hard" },
    { Src::MameIsaZxbusAdapter, "MAME isa/zxbus_adapter.cpp",
      "https://github.com/mamedev/mame/blob/master/src/devices/bus/isa/zxbus_adapter.cpp" },
    { Src::ZxpkSprinterAdapter, "zx-pk.com: zxbus-mod (Sprinter ISA to ZX-bus adapter re-creation)",
      "https://zx-pk.com/forum/viewtopic.php?t=21431" },
    { Src::RepoSprinterHardware, "unreal-ng: Sprinter hardware reference",
      "docs/inprogress/2026-09-28-sprinter/hardware-reference.md" },
    { Src::RepoSprinterIsa, "unreal-ng: Sprinter ISA research", "docs/inprogress/2026-10-02-sprinter-isa/research.md" },

    { Src::ShiruTurboSound, "Shiru, Programming Turbo Sound (Info Guide #8)", "https://zxpress.ru/article.php?id=8612" },
    { Src::MameAySlot, "MAME ay/slot.cpp", "https://github.com/mamedev/mame/blob/master/src/devices/bus/spectrum/ay/slot.cpp" },
    { Src::RepoTsfmHardware, "unreal-ng: TurboSound FM hardware reference",
      "docs/inprogress/2026-09-10-turbosound-fm/hardware-reference.md" },
    { Src::AlfisheGeneralSound, "alfishe/GeneralSound (schematic v1.4, manuals)", "https://github.com/alfishe/GeneralSound" },
    { Src::RepoGeneralSound, "unreal-ng: General Sound design folder", "docs/inprogress/2026-09-19-general-sound" },
    { Src::AlfisheNeogs, "alfishe/neogs (zxbus.v, GS_info)", "https://github.com/alfishe/neogs" },
    { Src::AlfisheZxmMoonsound, "alfishe/zxm-moonsound (CPLD, schematic rev 01)", "https://github.com/alfishe/zxm-moonsound" },
    { Src::MicklabMoonsound, "micklab: ZXM-MoonSound", "http://micklab.ru/My%20Soundcard/ZXMMoonSound.htm" },
    { Src::ZxdnCovox, "zxdn: LPT Covox (ZX Format #5)", "http://zxdn.narod.ru/hardware/zf5covox.htm" },
    { Src::DukeyusupovCovox, "dukeyusupov: ZX Covox (2025)", "https://dukeyusupov.ru/2025/02/17/zx-covox.html" },
    { Src::City20Soundrive, "City #20, V. Kazakov: SounDrive", "http://zxpress.ru/article.php?id=13608" },
    { Src::VelesoftDa, "Velesoft: D/A for ZX", "https://velesoft.speccy.cz/da_for_zx-cz.htm" },
    { Src::UzixMultisound, "UzixLS/zx-multisound (CPLD top.v, schematic rev.A2)", "https://github.com/UzixLS/zx-multisound" },
    { Src::RepoMultisoundHardware, "unreal-ng: ZX-MultiSound hardware reference",
      "docs/inprogress/2026-10-03-zx-multisound/hardware-reference.md" },
    { Src::Lvd2ZxnetUsb, "lvd2/zxnet_usb (CPLD zbus.v, PRM rev C)", "https://github.com/lvd2/zxnet_usb" },
    { Src::IzzxZxWifi, "izzx-git/ZX-WiFi", "https://github.com/izzx-git/ZX-WiFi" },
    { Src::IzzxZxWifiReadme, "izzx-git/ZX-WiFi ReadMe", "https://github.com/izzx-git/ZX-WiFi/blob/main/ReadMe.txt" },

    { Src::RepoSlotsResearchMachines, "unreal-ng: ZX-bus slots research, machines",
      "docs/inprogress/2026-10-03-zx-bus-slots/research-machines.md" },
    { Src::RepoSlotsResearchCards, "unreal-ng: ZX-bus slots research, cards",
      "docs/inprogress/2026-10-03-zx-bus-slots/research-cards.md" },
};

} // namespace

std::span<const SourceRef> Sources()
{
    return kSources;
}

} // namespace slots::refdata
