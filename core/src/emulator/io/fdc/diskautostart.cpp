#include "diskautostart.h"

#include <cstring>

#include "common/modulelogger.h"
#include "emulator/cpu/z80.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/trdosbootinjector.h"
#include "emulator/io/fdc/trdoscatalog.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{
// $027B-$02AE of every TR-DOS ROM: builds RUN "boot" in E_LINE and jumps to the command loop
const uint8_t BOOT_LINE_BUILDER_BYTES[] = {
    0x2A, 0x59, 0x5C, 0x3E, 0xFE, 0x32, 0x0E, 0x5D, 0x36, 0xF7, 0x23, 0x36, 0x22, 0x23, 0x36, 0x62, 0x23,
    0x36, 0x6F, 0x23, 0x36, 0x6F, 0x23, 0x36, 0x74, 0x23, 0x36, 0x22, 0x23, 0x22, 0x5B, 0x5C, 0x36, 0x0D,
    0x23, 0x36, 0x80, 0x23, 0x22, 0x61, 0x5C, 0x22, 0x63, 0x5C, 0x22, 0x65, 0x5C, 0xFD, 0xCB, 0x01, 0xDE, 0x18, 0x3F};

// What the ROM leaves in E_LINE: RUN "boot" <ENTER> <end marker>
const uint8_t BOOT_LINE[] = {0xF7, 0x22, 'b', 'o', 'o', 't', 0x22, 0x0D, 0x80};

constexpr uint16_t SYSVAR_E_LINE = 0x5C59;
constexpr uint16_t SYSVAR_K_CUR = 0x5C5B;
constexpr uint16_t SYSVAR_WORKSP = 0x5C61;
constexpr uint16_t SYSVAR_STKBOT = 0x5C63;
constexpr uint16_t SYSVAR_STKEND = 0x5C65;

/// An iS-DOS floppy that TR-DOS starts with RUN: track 0 has 1024-byte sectors 1, 2, 3, 4, 9 (256-byte blocks 0-19);
/// block 0 (sector 1) is the device descriptor ("DSK" at +10 on Classic, +13 on Chic, 1024-byte sectors, the main
/// catalogue's block at +20, the sector table at +64), and the main catalogue lists a live boot.sys at block 16, the
/// start of sector 9. TR-DOS reads sector 9 first on RUN: its 1024 bytes overrun the 256-byte buffer and the iS-DOS
/// loader in boot.sys takes over, so the sector carries no TR-DOS signature. Every iS-DOS system and data disk checked
/// (88 images, Classic and Chic) has this boot.sys
bool IsIsdosBootDisk(DiskImage& image)
{
    constexpr size_t BLOCK = 256;
    constexpr size_t BLOCKS_PER_SECTOR = 4;
    constexpr size_t ENTRY = 32;
    constexpr uint16_t BOOT_SYS_BLOCK = 16;
    static const uint8_t BOOT_SYS_NAME[11] = {'b', 'o', 'o', 't', ' ', ' ', ' ', ' ', 's', 'y', 's'};

    DiskImage::Track* track0 = image.getTrack(0);
    if (track0 == nullptr)
        return false;
    for (uint8_t id : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4), uint8_t(9)})
    {
        const DiskImage::Sector* sector = track0->findSector(id);
        if (sector == nullptr || !sector->hasData || sector->dataSize != BLOCK * BLOCKS_PER_SECTOR)
            return false;
    }

    const uint8_t* desc = track0->findSector(1)->data;
    const bool chic = std::memcmp(desc + 13, "DSK", 3) == 0;
    const bool classic = std::memcmp(desc + 10, "DSK", 3) == 0;
    if ((!chic && !classic) || desc[24] != BLOCKS_PER_SECTOR || desc[25] != 5)
        return false;

    // Block -> sector of track 0 through the descriptor's sector table (numbered from 0)
    auto block = [&](uint16_t b) -> const uint8_t* {
        if (b >= 5 * BLOCKS_PER_SECTOR)
            return nullptr;
        const DiskImage::Sector* sector = track0->findSector(static_cast<uint8_t>(desc[64 + b / BLOCKS_PER_SECTOR] + 1));
        if (sector == nullptr || !sector->hasData || sector->dataSize != BLOCK * BLOCKS_PER_SECTOR)
            return nullptr;
        return sector->data + (b % BLOCKS_PER_SECTOR) * BLOCK;
    };

    // Entry 0 of a catalogue describes the catalogue itself; boot.sys is one of the first files of the main one
    const uint8_t* catalogue = block(static_cast<uint16_t>(desc[20] | (desc[21] << 8)));
    if (catalogue == nullptr)
        return false;
    for (size_t entry = 1; entry < BLOCK / ENTRY; entry++)
    {
        const uint8_t* file = catalogue + entry * ENTRY;
        const bool live = (file[11] & 0x01) != 0;
        const uint16_t first = static_cast<uint16_t>(file[17] | (file[18] << 8));
        if (live && std::memcmp(file, BOOT_SYS_NAME, sizeof(BOOT_SYS_NAME)) == 0 && file[19] == 0xFF)
            return first == BOOT_SYS_BLOCK && block(BOOT_SYS_BLOCK) == track0->findSector(9)->data;
    }
    return false;
}
}  // namespace

DiskAutostart::DiskAutostart(EmulatorContext* context) : _context(context) {}

bool DiskAutostart::IsTrdosCapable(std::string* reason) const
{
    auto fail = [&](const char* text) {
        if (reason)
            *reason = text;
        return false;
    };

    if (!_context || !_context->pMemory || !_context->pBetaDisk)
        return fail("No Beta disk interface");
    if (!_context->config.trdos_present)
        return fail("Beta 128 interface is disabled for this machine");
    if (_context->pMemory->base_dos_rom == nullptr || _context->pMemory->base_sys_rom == nullptr)
        return fail("This machine has no TR-DOS");

    // ZX-Evo (ATM3/BaseConf) runs EVO-DOS: a reset into it goes through the BaseConf boot menu (service ROM),
    // it does not cold-start TR-DOS with RUN "boot". Direct entry is not possible there
    if (_context->config.mem_model == MM_ATM3)
        return fail("ZX-Evo boots disks through the BaseConf menu - autostart is not supported on this machine");
    return true;
}

bool DiskAutostart::IsNameHookSupported() const
{
    if (!_context || !_context->pMemory || !_context->pMemory->base_dos_rom)
        return false;

    return std::memcmp(_context->pMemory->base_dos_rom + BOOT_LINE_BUILDER, BOOT_LINE_BUILDER_BYTES,
                       sizeof(BOOT_LINE_BUILDER_BYTES)) == 0;
}

DiskAutostart::Plan DiskAutostart::MakePlan(DiskImage& image) const
{
    Plan plan;

    std::string reason;
    if (!IsTrdosCapable(&reason))
    {
        plan.action = Action::Unsupported;
        plan.message = reason;
        return plan;
    }

    TrdosCatalog catalog;
    if (!catalog.Parse(image))
    {
        if (IsIsdosBootDisk(image))
        {
            plan.action = Action::Boot;
            plan.message = "Autostart: iS-DOS boot.sys";
            return plan;
        }
        plan.message = "Disk is not TR-DOS formatted - mounted only";
        return plan;
    }

    if (catalog.FindBoot() != nullptr)
    {
        plan.action = Action::Boot;
        plan.message = "Autostart: boot";
        return plan;
    }

    std::vector<const TrdosFile*> basics = catalog.BasicFiles();
    if (basics.empty())
    {
        plan.message = "No BASIC programs on disk - mounted only";
        return plan;
    }

    if (basics.size() == 1)
    {
        const std::string name = basics[0]->TrimmedName();
        if (name.empty() || name.find('"') != std::string::npos)
        {
            plan.message = "BASIC program name cannot be started automatically - mounted only";
            return plan;
        }

        plan.action = IsNameHookSupported() ? Action::BootNamed : Action::BootGenerated;
        plan.bootName = name;
        plan.message = "Autostart: " + name;
        return plan;
    }

    plan.action = Action::BootCommander;
    plan.message = "Autostart: disk commander";
    return plan;
}

DiskAutostart::Plan DiskAutostart::Prepare(DiskImage& image)
{
    Plan plan = MakePlan(image);

    if (plan.action == Action::BootGenerated)
    {
        if (!TrdosBootInjector::InjectNamedBoot(image, plan.bootName))
        {
            plan.action = Action::MountOnly;
            plan.message = "Cannot inject boot file (disk full) - mounted only";
        }
    }
    else if (plan.action == Action::BootCommander)
    {
        std::vector<uint8_t> commander = TrdosBootInjector::LoadBundledCommander();
        if (commander.empty() || !TrdosBootInjector::InjectHobeta(image, commander))
        {
            // Fall back to the first BASIC program
            TrdosCatalog catalog;
            catalog.Parse(image);
            std::vector<const TrdosFile*> basics = catalog.BasicFiles();
            const std::string name = basics.empty() ? std::string() : basics[0]->TrimmedName();
            if (!name.empty() && name.find('"') == std::string::npos)
            {
                plan.bootName = name;
                plan.action = IsNameHookSupported() ? Action::BootNamed : Action::BootGenerated;
                plan.message = "Autostart: " + name + " (commander unavailable)";
                if (plan.action == Action::BootGenerated && !TrdosBootInjector::InjectNamedBoot(image, name))
                {
                    plan.action = Action::MountOnly;
                    plan.message = "Cannot inject boot file - mounted only";
                }
            }
            else
            {
                plan.action = Action::MountOnly;
                plan.message = "Cannot inject the disk commander - mounted only";
            }
        }
    }

    return plan;
}

void DiskAutostart::Arm(const std::string& name)
{
    // RUN "<name>"
    std::vector<uint8_t> line;
    line.push_back(0xF7);
    line.push_back(0x22);
    for (char c : name)
        line.push_back(static_cast<uint8_t>(c));
    line.push_back(0x22);

    _name = name;
    _commandLine = std::move(line);
    _armed = !name.empty();
}

void DiskAutostart::ArmCommand(const std::vector<uint8_t>& commandLine, const std::string& label)
{
    _name = label;
    _commandLine = commandLine;
    _armed = !commandLine.empty();
}

void DiskAutostart::Disarm()
{
    _armed = false;
    _coldStartFinishPending = false;
    _name.clear();
    _commandLine.clear();
}

bool DiskAutostart::HandleCommandLoopHook(Z80& cpu)
{
    if (!IsHookPending())
        return false;

    // Only inside the TR-DOS ROM
    if (!(_context->emulatorState.flags & CF_TRDOS))
        return false;

    bool changed = false;
    if (_coldStartFinishPending)
    {
        _coldStartFinishPending = false;
        changed = _context->pPortDecoder && _context->pPortDecoder->FinishAutostartColdStart(cpu.pc);
    }

    if (!_armed)
        return changed;

    const uint8_t eLineLow = cpu.rd(SYSVAR_E_LINE);   // one read per statement: the order is the CPU's
    const uint16_t eLine = static_cast<uint16_t>(eLineLow | (cpu.rd(SYSVAR_E_LINE + 1) << 8));
    for (size_t i = 0; i < sizeof(BOOT_LINE); i++)
    {
        if (cpu.rd(static_cast<uint16_t>(eLine + i)) != BOOT_LINE[i])
        {
            // First arrival is not the cold-start boot line: the hook does not apply to this run
            _context->pModuleLogger->Warning(PlatformModulesEnum::MODULE_DISK, PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC,
                                              "DiskAutostart: E_LINE does not hold RUN \"boot\" - name hook skipped");
            Disarm();
            return changed;
        }
    }

    // <command line> <ENTER> <end marker>, pointers exactly as the ROM code at $0298 leaves them
    uint16_t addr = eLine;
    for (uint8_t b : _commandLine)
        cpu.wd(addr++, b);

    const uint16_t enter = addr;
    cpu.wd(SYSVAR_K_CUR, static_cast<uint8_t>(enter & 0xFF));
    cpu.wd(SYSVAR_K_CUR + 1, static_cast<uint8_t>(enter >> 8));
    cpu.wd(addr++, 0x0D);
    cpu.wd(addr++, 0x80);

    for (uint16_t var : {SYSVAR_WORKSP, SYSVAR_STKBOT, SYSVAR_STKEND})
    {
        cpu.wd(var, static_cast<uint8_t>(addr & 0xFF));
        cpu.wd(var + 1, static_cast<uint8_t>(addr >> 8));
    }

    _context->pModuleLogger->Info(PlatformModulesEnum::MODULE_DISK, PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC,
                                  "DiskAutostart: cold-start line rewritten (%s)", _name.c_str());
    Disarm();
    return true;
}
