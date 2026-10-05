#include "stdafx.h"

#include "idecontroller.h"

#include "common/modulelogger.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
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
    config.compactFlash = !ide.cd && ide.cf;

    std::unique_ptr<AtaDevice> device;
    if (ide.cd)
    {
        auto cd = std::make_unique<AtapiCdrom>();
        EmulatorContext* context = _context;
        cd->Audio().SetClock([context]() { return FrameElapsedBaseT(context); });
        device = std::move(cd);
    }
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
              (ide.cd ? " (CD-ROM)" : config.compactFlash ? " (CompactFlash)" : " (hard disk)");
    d.tags = {"ide", place, SchemeTag(_scheme), ide.cd ? "cdrom" : "hdd"};
    if (config.compactFlash)
        d.tags.push_back("cf");
    if (twoChannels)
        d.tags.push_back(channelName);
    if (FirstOfKind(unit))
        d.aliases.push_back(ide.cd ? "cd" : "hd");
    // Estex DSS reads FAT12 / FAT16 only (the Sprinter hardware reference §9.3): a FAT32
    // folder volume or composite on a Sprinter disk is refused, never built
    if (_scheme == IDE_SPRINTER && !ide.cd)
        d.fsCompatibility = {FatType::Fat16};
    _slots[unit] = std::move(slot);
}

CdAudioPlayer* IdeController::CdAudio(int unit)
{
    if (unit < 0 || unit >= ChannelCount() * AtaChannel::kUnits)
        return nullptr;
    AtaDevice* device = _channels[unit / AtaChannel::kUnits].Unit(unit % AtaChannel::kUnits);
    if (!device || device->Kind() != AtaDeviceKind::Cdrom)
        return nullptr;
    return &static_cast<AtapiCdrom*>(device)->Audio();
}

uint8_t IdeController::CdUnitMask() const
{
    uint8_t mask = 0;
    const int units = ChannelCount() * AtaChannel::kUnits;
    for (int unit = 0; unit < units; unit++)
    {
        const AtaDevice* device = _channels[unit / AtaChannel::kUnits].Unit(unit % AtaChannel::kUnits);
        if (device && device->Kind() == AtaDeviceKind::Cdrom)
            mask = static_cast<uint8_t>(mask | (1u << unit));
    }
    return mask;
}

std::string IdeController::CdAudioName(int unit)
{
    return "CD " + IdeUnitSlot::IdFor(unit / AtaChannel::kUnits, unit % AtaChannel::kUnits);
}

uint32_t IdeController::FrameElapsedBaseT(EmulatorContext* context)
{
    if (!context || !context->pCore)
        return 0;
    Z80* z80 = context->pCore->GetZ80();
    if (!z80)
        return 0;
    const EmulatorState& state = context->emulatorState;
    uint32_t t = state.AudioTstate(static_cast<uint32_t>(z80->t));
    const uint8_t host = state.HostSpeedMultiplier();
    if (host > 1)
        t /= host;
    return t;
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

IdeController::UnitKind IdeController::KindOf(int unit) const
{
    if (unit < 0 || unit >= kMaxUnits)
        return UnitKind::Disk;
    const IDE_CONFIG& ide = _context->config.ide[unit];
    return ide.cd ? UnitKind::Cdrom : ide.cf ? UnitKind::CompactFlash : UnitKind::Disk;
}

const char* IdeController::UnitKindName(UnitKind kind)
{
    switch (kind)
    {
        case UnitKind::Cdrom:
            return "cdrom";
        case UnitKind::CompactFlash:
            return "cf";
        default:
            return "disk";
    }
}

bool IdeController::SetUnitKind(int unit, UnitKind kind, std::string* error)
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
    if (KindOf(unit) == kind)
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
    ide.cd = kind == UnitKind::Cdrom ? 1 : 0;
    ide.cf = kind == UnitKind::CompactFlash ? 1 : 0;
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
    // The CD drives' clock reads the CPU through the context: nothing reads it while the board goes
    for (int unit = 0; unit < kMaxUnits; unit++)
    {
        if (CdAudioPlayer* player = CdAudio(unit))
            player->SetClock({});
    }
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
    // Only the v5 board decodes the Profi IDE (its extended port map); the v3 board has none (ProfiBoard)
    const bool profi = IsProfiModel(model);
    const bool profiIde = ProfiBoard::For(model).extendedPorts;
    const bool scorpion = model == MM_SCORP || model == MM_PROFSCORP;
    const bool atm = model == MM_ATM710 || model == MM_ATM450 || model == MM_ATM3;
    const bool sprinter = model == MM_SPRINTER;
    switch (scheme)
    {
        case IDE_NONE: return true;
        case IDE_PROFI: return profiIde;
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
