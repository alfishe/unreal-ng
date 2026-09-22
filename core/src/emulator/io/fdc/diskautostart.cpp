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
    _name.clear();
    _commandLine.clear();
}

bool DiskAutostart::HandleCommandLoopHook(Z80& cpu)
{
    if (!_armed)
        return false;

    // Only inside the TR-DOS ROM
    if (!(_context->emulatorState.flags & CF_TRDOS))
        return false;

    const uint16_t eLine = static_cast<uint16_t>(cpu.rd(SYSVAR_E_LINE) | (cpu.rd(SYSVAR_E_LINE + 1) << 8));
    for (size_t i = 0; i < sizeof(BOOT_LINE); i++)
    {
        if (cpu.rd(static_cast<uint16_t>(eLine + i)) != BOOT_LINE[i])
        {
            // First arrival is not the cold-start boot line: the hook does not apply to this run
            _context->pModuleLogger->Warning(PlatformModulesEnum::MODULE_DISK, PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC,
                                              "DiskAutostart: E_LINE does not hold RUN \"boot\" - name hook skipped");
            Disarm();
            return false;
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
