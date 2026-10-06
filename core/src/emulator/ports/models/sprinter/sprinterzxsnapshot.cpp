#include "sprinterzxsnapshot.h"

#include <cstring>
#include <string>

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/state/devicestate.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "loaders/snapshot/szx/loaderszx.h"

namespace
{
using snapshot::Outcome;
using snapshot::Verdict;

PortDecoder_Sprinter* DecoderOf(EmulatorContext& context) { return dynamic_cast<PortDecoder_Sprinter*>(context.pPortDecoder); }

/// The ZX mode, as SprinterZxMode reports it: window 0 shows a vROM page (system ROM out, fast RAM off) and ALL_MODE
/// bit 0 = 0 (ZX screen shadow + ZX keyboard)
bool ZxActive(const SprinterPldState& pld)
{
    return pld.configState == SprinterConfigState::Configured && pld.romOff && !pld.cacheOn && (pld.allMode & 0x01) == 0;
}

/// The PLD cell that holds the physical page of Spectrum bank `bank`: #F0-#F7 for banks 0-7, #F8-#FF for 8-15
uint8_t CellOf(uint16_t bank) { return static_cast<uint8_t>(bank < 8 ? 0xF0 + bank : 0xF8 + (bank - 8)); }

std::string Hex2(unsigned value) { return StringHelper::Format("#%02X", value); }

const char* kStartMode = "start a 128K mode first, e.g. `spectrum p128.zx` from DSS, or the BIOS menu (ESC at the boot prompt)";
}  // namespace

SprinterZxSnapshot& SprinterZxSnapshot::Instance()
{
    static SprinterZxSnapshot instance;
    return instance;
}

Verdict SprinterZxSnapshot::Examine(const snapshot::Image& image, EmulatorContext& context) const
{
    PortDecoder_Sprinter* decoder = DecoderOf(context);
    if (!decoder || !dynamic_cast<SprinterMemory*>(context.pMemory))
        return Verdict::Refuse("the 'sprinter-zx' commit writes a Sprinter's Spectrum mode; this machine is not a Sprinter",
                               "model:SPRINTER");
    if (image.memoryModel == snapshot::MemoryModel::Physical)
        return Verdict::Refuse("this program runs on the TS-Conf machine only (a physically addressed SPG)", "model:TSL");

    const SprinterPldState& pld = decoder->GetPldState();
    if (!ZxActive(pld))
        return Verdict::Refuse(
            "the Sprinter is not in the Spectrum (ZX) mode: it is at the DSS prompt or in the BIOS, where there is no "
            "Spectrum memory to load into. Start a ZX mode first (the BIOS menu: ESC at the boot prompt; or `spectrum "
            "p128.zx` from DSS), then load the snapshot",
            "zx_mode");

    const bool paging7ffd = (pld.cnf & 0x20) == 0;   // CNF bit 5: the #7FFD latch is clean (off)
    const bool paging1ffd = (pld.cnf & 0x40) == 0;   // CNF bit 6: the #1FFD latch is clean (off)
    const bool mem512 = (pld.cnf & 0x80) != 0;

    if (image.memoryModel != snapshot::MemoryModel::Mem48k && !paging7ffd)
        return Verdict::Refuse(std::string("this snapshot needs the 128K memory (banks 0-7, #7FFD paging) and the running mode "
                                           "has #7FFD paging off (a 48K mode): ") + kStartMode,
                               "mode:128k");
    uint16_t highest = 0;
    for (const auto& bank : image.banks)
        highest = std::max(highest, bank.first);
    if (highest > 15)
        return Verdict::Refuse("the snapshot has Spectrum bank " + std::to_string(highest) +
                                   "; the Sprinter's cell table holds banks 0-15",
                               "mode:mem512");
    if (highest >= 8 && !mem512 && !paging1ffd)
        return Verdict::Refuse("the snapshot uses Spectrum banks 8-15; the running mode has neither the 512 KB paging nor "
                               "#1FFD paging that reach them",
                               "mode:mem512");

    // Every bank needs a RAM page behind its cell (the port table and the launcher's marker page are not one)
    for (const auto& bank : image.banks)
    {
        const uint8_t page = pld.Cell(CellOf(bank.first));
        if (page == SprinterMemory::kPortTablePage || page == 0x41)
            return Verdict::Refuse("the mode's page table gives Spectrum bank " + std::to_string(bank.first) + " (cell " +
                                       Hex2(CellOf(bank.first)) + ") page " + Hex2(page) + ", which is not RAM for programs",
                                   "mode:page_table");
    }
    return Verdict::Take("the Spectrum mode is running: banks go through the cell table");
}

bool SprinterZxSnapshot::Commit(const snapshot::Image& image, EmulatorContext& context, snapshot::Report& report)
{
    PortDecoder_Sprinter* decoder = DecoderOf(context);
    SprinterMemory* memory = dynamic_cast<SprinterMemory*>(context.pMemory);
    Z80* z80 = context.pCore ? context.pCore->GetZ80() : nullptr;
    if (!decoder || !memory || !z80)
    {
        report.Refuse("no Sprinter to commit into");
        return false;
    }
    SprinterPldState& pld = decoder->GetPldState();
    const bool paging7ffd = (pld.cnf & 0x20) == 0;
    const uint16_t pc = z80->pc;   // the PC the journal names for the latch writes

    // 1. The banks: each into the physical page its cell holds
    for (const auto& bank : image.banks)
    {
        const uint8_t cell = CellOf(bank.first);
        const uint8_t page = pld.Cell(cell);
        std::memcpy(memory->RAMPageAddress(page), bank.second.data(), PAGE_SIZE);
        memory->MarkRamPageEdited(page);
        report.Add("bank " + std::to_string(bank.first), Outcome::Applied, "physical page " + Hex2(page) + " (cell " + Hex2(cell) + ")");

        // Windows 1 and 2 hold Spectrum banks 5 and 2 through their own cells; they are the same pages in a consistent mode
        const uint8_t windowCell = bank.first == 5 ? SprinterCode::Page1 : bank.first == 2 ? SprinterCode::Page2 : 0;
        if (windowCell && pld.Cell(windowCell) != page)
        {
            const uint8_t windowPage = pld.Cell(windowCell);
            std::memcpy(memory->RAMPageAddress(windowPage), bank.second.data(), PAGE_SIZE);
            memory->MarkRamPageEdited(windowPage);
            report.Add("bank " + std::to_string(bank.first) + " (window)", Outcome::Applied,
                       "also physical page " + Hex2(windowPage) + " (cell " + Hex2(windowCell) + " differs from " + Hex2(cell) + ")");
        }
    }

    // 2. The Spectrum screen shadow in video RAM follows the banks the CPU would have written: bank 5 in window 1,
    // bank 7 in window 3 (so #7FFD selects 7 for the replay)
    if (image.banks.count(5))
        memory->RefreshZxShadow(1);
    if (image.banks.count(7) && paging7ffd)
    {
        decoder->SetPagingFromSnapshot(0x07, pc);
        memory->RefreshZxShadow(3);
    }

    // 3. The paging latch
    if (image.memoryModel != snapshot::MemoryModel::Mem48k)
    {
        const uint8_t value = image.paging.p7FFD.value_or(0x10);
        decoder->SetPagingFromSnapshot(value, pc);
        report.Add("7FFD", Outcome::Applied, Hex2(value) + " -> latch " + Hex2(pld.pn) + " (window 3 = bank " + std::to_string(pld.pn & 7) + ")");
    }
    else if (paging7ffd)
    {
        // A 48K program on a 128K mode: the state a 128K is in when it runs one - 48K ROM, bank 0, paging locked
        decoder->SetPagingFromSnapshot(0x30, pc);
        report.Add("7FFD", Outcome::Applied, "#30 for a 48K snapshot: 48K ROM, bank 0 on top, paging locked");
    }
    if (image.paging.p1FFD)
        report.Add("1FFD", Outcome::Ignored, "the Sprinter's #1FFD is the Scorpion's, not the +3's: left as it is");
    if (image.paging.pEFF7)
        report.Add("EFF7", Outcome::Ignored, "a Pentagon 1024 latch the Sprinter does not have");
    if (image.trdosPaged)
        report.Add("TR-DOS", Outcome::Ignored, "the Sprinter's TR-DOS follows its M1 trap rule: not forced from the file");

    // 4. The border through the port, so the PLD latches it as a program's OUT would
    decoder->DecodePortOut(0x00FE, image.border, pc);
    report.Add("border", Outcome::Applied, std::to_string(image.border));

    // 5. AY 0
    if (!image.ay.empty() && context.pSoundManager)
    {
        if (SoundChip_AY8910* psg = context.pSoundManager->getAYChip(0))
        {
            for (uint8_t reg = 0; reg < 16; ++reg)
                psg->writeRegister(reg, image.ay[0].registers[reg]);
            psg->setRegister(image.ay[0].selected & 0x0F);
            report.Add("AY", Outcome::Applied, "chip 0");
        }
    }

    // 6. The CPU
    const snapshot::Cpu& cpu = image.cpu;
    z80->af = cpu.af;
    z80->bc = cpu.bc;
    z80->de = cpu.de;
    z80->hl = cpu.hl;
    z80->ix = cpu.ix;
    z80->iy = cpu.iy;
    z80->alt.af = cpu.af2;
    z80->alt.bc = cpu.bc2;
    z80->alt.de = cpu.de2;
    z80->alt.hl = cpu.hl2;
    z80->sp = cpu.sp;
    z80->pc = cpu.pc;
    z80->i = cpu.i;
    z80->r_low = cpu.r & 0x7Fu;
    z80->r_hi = cpu.r & 0x80u;
    z80->iff1 = cpu.iff1 ? 1 : 0;
    z80->iff2 = cpu.iff2 ? 1 : 0;
    z80->im = cpu.im;
    z80->memptr = cpu.memptr.value_or(0);
    z80->q = 0;
    z80->halted = 0;
    const bool halted = cpu.halted ? *cpu.halted : memory->DirectReadFromZ80Memory(z80->pc) == 0x76;
    if (halted)
    {
        z80->halted = 1;
        z80->halt_cycle = 0;
        z80->haltpos = 0;
    }
    if (image.framePosition)
        z80->t = LoaderSZX::FramePositionFromIntCount(&context, *image.framePosition);
    if (cpu.eiShadow && *cpu.eiShadow)
        report.Add("EI shadow", Outcome::Ignored, "the interrupt shadow after EI is not restored on the Sprinter");
    report.Add("CPU", Outcome::Applied, "PC " + StringHelper::Format("#%04X", cpu.pc) + ", SP " + StringHelper::Format("#%04X", cpu.sp));

    // 7. The picture
    if (context.pScreen)
        context.pScreen->RenderOnlyMainScreen();
    return true;
}

SprinterZxCapture& SprinterZxCapture::Instance()
{
    static SprinterZxCapture instance;
    return instance;
}

SprinterZxCapture::Identity SprinterZxCapture::IdentityOf(const std::string& modeName, bool paging7ffd)
{
    Identity identity;
    const std::string lower = StringHelper::ToLower(modeName);
    if (!paging7ffd)
    {
        identity.model = MM_SPECTRUM48;
        identity.ramKb = 48;
        identity.machineHint = "48k";
        identity.timingHint = "48k";
        identity.layout48 = true;
        identity.bankCount = 3;
    }
    else if (lower.find("scorpion") != std::string::npos)
    {
        identity.model = MM_SCORP;
        identity.ramKb = 256;
        identity.machineHint = "scorpion256";
        identity.timingHint = "128k";
        identity.scorpion = true;
        identity.bankCount = 16;
    }
    else if (lower.find("pentagon") != std::string::npos)
    {
        identity.model = MM_PENTAGON;
        identity.ramKb = 128;
        identity.machineHint = "pentagon128";
        identity.timingHint = "pentagon";
    }
    return identity;
}

snapshot::MachineView SprinterZxCapture::Examine(EmulatorContext& context) const
{
    snapshot::MachineView view;
    auto refuse = [&](std::string reason, std::string needs) {
        view.available = false;
        view.reason = std::move(reason);
        view.needs = std::move(needs);
        return view;
    };

    PortDecoder_Sprinter* decoder = DecoderOf(context);
    SprinterMemory* memory = dynamic_cast<SprinterMemory*>(context.pMemory);
    if (!decoder || !memory)
        return refuse("no Sprinter to save", "model:SPRINTER");
    const SprinterPldState& pld = decoder->GetPldState();
    if (!ZxActive(pld))
        return refuse(
            "the Sprinter is not in a Spectrum (ZX) mode: it is at the DSS prompt or in the BIOS, where there is no Spectrum "
            "memory to save. Start a ZX mode first (the BIOS menu: ESC at the boot prompt; or `spectrum p128.zx` from DSS)",
            "zx_mode");
    if (pld.cnf & 0x80)
        return refuse("the running mode has the 512 KB paging (a Pentagon 512 mode): its memory is not a Spectrum 128K, and no snapshot "
                      "view exists for it yet; start a 128K mode",
                      "mode:128k");

    const bool paging7ffd = (pld.cnf & 0x20) == 0;
    const std::string modeName = DeviceState::SprinterZxModeBrief(&context, false).modeName;

    view.p7FFD = pld.pn;
    const Identity identity = IdentityOf(modeName, paging7ffd);
    view.model = identity.model;
    view.ramKb = identity.ramKb;
    view.machineHint = identity.machineHint;
    view.timingHint = identity.timingHint;
    view.layout48 = identity.layout48;
    if (identity.scorpion)
        view.p1FFD = pld.sc;
    std::vector<uint16_t> banks;
    if (identity.layout48)
        banks = {5, 2, 0};
    else
        for (uint16_t bank = 0; bank < identity.bankCount; bank++)
            banks.push_back(bank);

    for (uint16_t bank : banks)
    {
        const uint8_t cell = CellOf(bank);
        const uint8_t page = pld.Cell(cell);
        if (page == SprinterMemory::kPortTablePage || page == 0x41)
            return refuse("the mode's page table gives Spectrum bank " + std::to_string(bank) + " (cell " + Hex2(cell) + ") page " +
                              Hex2(page) + ", which is not RAM for programs: no snapshot view of this mode",
                          "mode:page_table");
        if (const uint8_t* bytes = memory->RAMPageAddress(page))
            view.banks[bank] = bytes;
    }
    view.note = "Sprinter ZX mode '" + (modeName.empty() ? std::string("unknown") : modeName) +
                "': Spectrum banks read through the PLD cell table (#F0-#FF)";
    return view;
}
