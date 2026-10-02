#include "stdafx.h"

#include "idecontroller.h"

#include "common/modulelogger.h"
#include "emulator/config.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/ata/atadisk.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/media/mediamanager.h"
#include "emulator/ports/models/profiboard.h"
#include "emulator/ports/portdecoder.h"
#include "debugger/ttd/timetravelmanager.h"

IdeController::IdeController(EmulatorContext* context) : _context(context)
{
    if (!_context)
        return;
    _scheme = _context->config.ide_scheme;
    if (_scheme != IDE_NONE && !SchemeFits(_scheme, _context->config.mem_model))
    {
        if (_context->pModuleLogger)
            _context->pModuleLogger->Warning(PlatformModulesEnum::MODULE_DISK, PlatformDiskSubmodulesEnum::SUBMODULE_DISK_HDD,
                                             "[HDD] Scheme=%s does not fit this machine: no IDE",
                                             Config::IdeSchemeName(_scheme));
        _scheme = IDE_NONE;
    }
    if (_scheme == IDE_NONE)
        return;

    for (int unit = 0; unit < 2; unit++)
        BuildUnit(unit);

    if (_context->pMediaManager)
    {
        for (auto& slot : _slots)
            _context->pMediaManager->RegisterSlot(*slot);
        _registered = true;
    }
}

void IdeController::BuildUnit(int unit)
{
    const IDE_CONFIG& ide = _context->config.ide[unit];
    DriveConfig config;
    config.geometry.cylinders = ide.c;
    config.geometry.heads = ide.h;
    config.geometry.sectors = ide.s;
    config.profiGeometry = _scheme == IDE_PROFI;

    std::unique_ptr<AtaDevice> device;
    if (ide.cd)
        device = std::make_unique<AtapiCdrom>();
    else
        device = std::make_unique<AtaDisk>();
    AtaDevice& unitDevice = *device;
    unitDevice.SetActivityCounter(&_activity);
    _channel.SetUnit(unit, std::move(device));

    auto slot = std::make_unique<IdeUnitSlot>(_context, IdeUnitSlot::IdFor(0, unit), unitDevice, config);
    SlotDescriptor& d = slot->MutableDescriptor();
    const char* position = unit == 0 ? "master" : "slave";
    d.label = std::string("IDE ") + position + (ide.cd ? " (CD-ROM)" : " (hard disk)");
    d.tags = {"ide", position, SchemeTag(_scheme), ide.cd ? "cdrom" : "hdd"};
    // "hd" / "cd": the first unit of each kind
    const IDE_CONFIG& other = _context->config.ide[unit ^ 1];
    const bool firstOfKind = unit == 0 || other.cd != ide.cd;
    if (firstOfKind)
        d.aliases.push_back(ide.cd ? "cd" : "hd");
    _slots[unit] = std::move(slot);
}

int IdeController::UnitForSlot(const std::string& slotId)
{
    if (slotId == IdeUnitSlot::IdFor(0, 0))
        return 0;
    if (slotId == IdeUnitSlot::IdFor(0, 1))
        return 1;
    return -1;
}

bool IdeController::SetUnitKind(int unit, bool cdrom, std::string* error)
{
    auto fail = [error](const std::string& reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (!Enabled())
        return fail("this machine has no IDE board");
    if (unit < 0 || unit > 1)
        return fail("no such IDE unit");
    IDE_CONFIG& ide = _context->config.ide[unit];
    if ((ide.cd != 0) == cdrom)
        return true;
    const std::string id = IdeUnitSlot::IdFor(0, unit);
    MediaManager* manager = _context->pMediaManager;
    if (manager)
    {
        const auto info = manager->Info(id);
        if (info && (info->present || info->pending))
            return fail("slot '" + id + "' holds a medium: eject it before changing the drive");
    }
    if (_context->pTimeTravelManager && _context->pTimeTravelManager->IsRecording())
        return fail("a TTD recording runs: the machine's hardware is fixed until it stops");

    if (manager && _registered)
        manager->UnregisterSlot(id);
    ide.cd = cdrom ? 1 : 0;
    BuildUnit(unit);
    // The other unit keeps its drive and medium; only its alias may move
    // ("hd" / "cd" name the first unit of each kind)
    const int other = unit ^ 1;
    if (_slots[other])
    {
        std::vector<std::string>& aliases = _slots[other]->MutableDescriptor().aliases;
        aliases.clear();
        const bool otherCd = _context->config.ide[other].cd != 0;
        if (other == 0 || otherCd != cdrom)
            aliases.push_back(otherCd ? "cd" : "hd");
    }
    if (manager && _registered)
        manager->RegisterSlot(*_slots[unit]);
    return true;
}

IdeController::~IdeController()
{
    if (_registered && _context && _context->pMediaManager)
    {
        for (auto& slot : _slots)
        {
            if (slot)
                _context->pMediaManager->UnregisterSlot(slot->Descriptor().id);
        }
    }
}

void IdeController::Reset()
{
    // The reset line reaches the units and the board's latches (IDE design §3.3)
    _channel.HardReset();
    if (_context && _context->pPortDecoder)
        _context->pPortDecoder->GetIdeAdapter().Reset();
}

bool IdeController::SchemeFits(IDE_SCHEME scheme, MEM_MODEL model)
{
    // Only the v5 board decodes the Profi IDE (its extended port map); the v3 board has none (ProfiBoard)
    const bool profi = IsProfiModel(model);
    const bool profiIde = ProfiBoard::For(model).extendedPorts;
    const bool scorpion = model == MM_SCORP || model == MM_PROFSCORP;
    const bool atm = model == MM_ATM710 || model == MM_ATM450 || model == MM_ATM3;
    switch (scheme)
    {
        case IDE_NONE: return true;
        case IDE_PROFI: return profiIde;
        case IDE_SMUC: return scorpion;
        case IDE_ATM: return atm;
        case IDE_NEMO:
        case IDE_NEMO_A8:
        case IDE_NEMO_DIVIDE:
        case IDE_DIVIDE: return !profi;  // the Profi's own decode owns its IDE family
    }
    return false;
}

const char* IdeController::SchemeTag(IDE_SCHEME scheme)
{
    switch (scheme)
    {
        case IDE_ATM: return "atm";
        case IDE_NEMO: return "nemo";
        case IDE_NEMO_A8: return "nemo-a8";
        case IDE_NEMO_DIVIDE: return "nemo-divide";
        case IDE_SMUC: return "smuc";
        case IDE_PROFI: return "profi";
        case IDE_DIVIDE: return "divide";
        case IDE_NONE: break;
    }
    return "none";
}
