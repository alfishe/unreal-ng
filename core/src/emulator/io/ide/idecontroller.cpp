#include "stdafx.h"

#include "idecontroller.h"

#include "common/modulelogger.h"
#include "emulator/config.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/ata/atadisk.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/media/mediamanager.h"
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

    // The Sprinter's AT board pulls DD7 down as the ATA standard asks, so an empty channel reads
    // BSY = 0 (#7F) and the BIOS (3.04 and the 3.06 / 3.07 four-unit scan) reports "None" at once.
    // The other boards keep the floating #FF (IDE design §6.2)
    for (AtaChannel& channel : _channels)
        channel.SetEmptyBus(_scheme == IDE_SPRINTER ? AtaChannel::kEmptyBusDd7PullDown : AtaChannel::kEmptyBusFloating);

    const int units = ChannelCount() * AtaChannel::kUnits;
    for (int unit = 0; unit < units; unit++)
        BuildUnit(unit);

    if (_context->pMediaManager)
    {
        for (auto& slot : _slots)
        {
            if (slot)
                _context->pMediaManager->RegisterSlot(*slot);
        }
        _registered = true;
    }
}

bool IdeController::FirstOfKind(int unit) const
{
    // "hd" / "cd" name the first unit of each kind on the board
    const bool cd = _context->config.ide[unit].cd != 0;
    for (int lower = 0; lower < unit; lower++)
    {
        if ((_context->config.ide[lower].cd != 0) == cd)
            return false;
    }
    return true;
}

void IdeController::BuildUnit(int unit)
{
    const IDE_CONFIG& ide = _context->config.ide[unit];
    const int channel = unit / AtaChannel::kUnits;
    const int position = unit % AtaChannel::kUnits;
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
    _channels[channel].SetUnit(position, std::move(device));

    auto slot = std::make_unique<IdeUnitSlot>(_context, IdeUnitSlot::IdFor(channel, position), unitDevice, config);
    SlotDescriptor& d = slot->MutableDescriptor();
    const char* place = position == 0 ? "master" : "slave";
    // A two-channel board names the channel as its firmware does: primary (ide0), secondary (ide1)
    const bool twoChannels = ChannelCount() == 2;
    const char* channelName = channel == 0 ? "primary" : "secondary";
    d.label = std::string("IDE ") + (twoChannels ? std::string(channelName) + " " : std::string()) + place +
              (ide.cd ? " (CD-ROM)" : " (hard disk)");
    d.tags = {"ide", place, SchemeTag(_scheme), ide.cd ? "cdrom" : "hdd"};
    if (twoChannels)
        d.tags.push_back(channelName);
    if (FirstOfKind(unit))
        d.aliases.push_back(ide.cd ? "cd" : "hd");
    _slots[unit] = std::move(slot);
}

int IdeController::UnitForSlot(const std::string& slotId)
{
    for (int unit = 0; unit < kMaxUnits; unit++)
    {
        if (slotId == IdeUnitSlot::IdFor(unit / AtaChannel::kUnits, unit % AtaChannel::kUnits))
            return unit;
    }
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
    if (unit < 0 || unit >= ChannelCount() * AtaChannel::kUnits)
        return fail("no such IDE unit");
    IDE_CONFIG& ide = _context->config.ide[unit];
    if ((ide.cd != 0) == cdrom)
        return true;
    const std::string id = IdeUnitSlot::IdFor(unit / AtaChannel::kUnits, unit % AtaChannel::kUnits);
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
    // The other units keep their drives and media; only their aliases may move
    // ("hd" / "cd" name the first unit of each kind)
    for (int other = 0; other < kMaxUnits; other++)
    {
        if (other == unit || !_slots[other])
            continue;
        std::vector<std::string>& aliases = _slots[other]->MutableDescriptor().aliases;
        aliases.clear();
        if (FirstOfKind(other))
            aliases.push_back(_context->config.ide[other].cd ? "cd" : "hd");
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
    for (AtaChannel& channel : _channels)
        channel.HardReset();
    if (_context && _context->pPortDecoder)
        _context->pPortDecoder->GetIdeAdapter().Reset();
}

bool IdeController::SchemeFits(IDE_SCHEME scheme, MEM_MODEL model)
{
    const bool profi = model == MM_PROFI;
    const bool scorpion = model == MM_SCORP || model == MM_PROFSCORP;
    const bool atm = model == MM_ATM710 || model == MM_ATM450 || model == MM_ATM3;
    const bool sprinter = model == MM_SPRINTER;
    switch (scheme)
    {
        case IDE_NONE: return true;
        case IDE_PROFI: return profi;
        case IDE_SMUC: return scorpion;
        case IDE_ATM: return atm;
        case IDE_SPRINTER: return sprinter;
        case IDE_NEMO:
        case IDE_NEMO_A8:
        case IDE_NEMO_DIVIDE:
        case IDE_DIVIDE: return !profi && !sprinter;  // the Profi's and the Sprinter's own decode own every port
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
        case IDE_SPRINTER: return "sprinter";
        case IDE_NONE: break;
    }
    return "none";
}
