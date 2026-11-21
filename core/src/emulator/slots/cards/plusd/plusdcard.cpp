#include "plusdcard.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "common/filehelper.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/floppydriveslot.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/media/mediamanager.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/slots/slotplanner.h"

namespace
{

/// A ROM path resolved as ROM::LoadROM does: the working dir, then the executable's, then the resources path
std::string ResolveRomPath(const char* path)
{
    std::string resolved = FileHelper::NormalizePath(path);
    if (FileHelper::FileExists(resolved))
        return resolved;
    resolved = FileHelper::PathCombine(FileHelper::GetExecutablePath(), path);
    if (FileHelper::FileExists(resolved))
        return resolved;
    resolved = FileHelper::PathCombine(FileHelper::GetResourcesPath(), path);
    if (FileHelper::FileExists(resolved))
        return resolved;
    return {};
}

uint64_t Fnv1a(const uint8_t* data, size_t size)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= data[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

}  // namespace

static_assert(PlusDCard::kStateFdcSize == WD1793::kTtdStateSize, "the +D blob embeds the WD1772's blob");
static_assert(PlusDCard::kStateFdcContextSize == WD1793::kTransferContextSize, "the +D blob embeds the command context");
static_assert(PlusDCard::kDiscipleStateSize >= PlusDCard::kDiscipleStateOffset + 16, "the DISCiPLE tail fits");

/// The card's TTD device (SaveState / LoadState)
class PlusDTtd final : public ttd::TTDSerializable
{
public:
    explicit PlusDTtd(PlusDCard& card) : _card(card)
    {
    }

    size_t TTDStateSize() const override
    {
        return _card.StateSize();
    }
    void TTDSaveState(uint8_t* dst) const override
    {
        _card.SaveState(dst);
    }
    void TTDLoadState(const uint8_t* src) override
    {
        _card.LoadState(src);
    }
    std::string TTDDeviceName() const override
    {
        return _card.IsDisciple() ? "Disciple" : "PlusD";
    }
    ttd::TTDDeviceType TTDType() const override
    {
        return _card.IsDisciple() ? ttd::TTDDeviceType::Disciple : ttd::TTDDeviceType::PlusD;
    }
    uint64_t TTDHashState() const override
    {
        std::vector<uint8_t> blob(_card.StateSize());
        TTDSaveState(blob.data());
        return Fnv1a(blob.data(), blob.size());
    }
    ttd::TTDDeviceDescriptor TTDDescribe() const override
    {
        ttd::TTDDeviceDescriptor d = ttd::TTDSerializable::TTDDescribe();
        // The WD1772's clocks advance with emulated time, as in the Beta blob (WD1793::TTDDescribe)
        constexpr uint16_t fdc = PlusDCard::kStateFdcOffset;
        d.timeFields = { { fdc + 22, 8 }, { fdc + 30, 8 }, { fdc + 114, 8 } };
        if (_card.IsDisciple())
            d.timeFields.push_back({ static_cast<uint16_t>(PlusDCard::kDiscipleStateOffset + 8), 8 });
        return d;
    }

private:
    PlusDCard& _card;
};

std::unique_ptr<ICard> PlusDCard::Create(const CardContext& context)
{
    return std::make_unique<PlusDCard>(context, Model::PlusD);
}

std::unique_ptr<ICard> PlusDCard::CreateDisciple(const CardContext& context)
{
    return std::make_unique<PlusDCard>(context, Model::Disciple);
}

PlusDCard* PlusDCard::Find(const EmulatorContext* context)
{
    if (context == nullptr || context->pSlotManager == nullptr)
        return nullptr;
    for (const std::unique_ptr<ICard>& card : context->pSlotManager->Cards())
    {
        if (auto* plusd = dynamic_cast<PlusDCard*>(card.get()))
            return plusd;
    }
    return nullptr;
}

PlusDCard::PlusDCard(const CardContext& context, Model model)
    : ICard(context), _context(context.emulator), _model(model), _ttd(std::make_unique<PlusDTtd>(*this))
{
    if (context.def != nullptr)
        _inhibit = (slots::OptionBits(*context.def, context.options, slots::Opt::Inhibit) & slots::Bit(1)) != 0;
    windowStart = 0x0000;
    windowEnd = 0x4000;
    observesReads = true;

    if (_context == nullptr || _context->pCore == nullptr || _context->pCore->GetZ80() == nullptr)
        throw std::runtime_error("the machine has no CPU yet");

    LoadRom();

    for (size_t i = 0; i < kDrives; i++)
        _drives[i] = std::make_unique<FDD>(_context);
    _fdc = std::make_unique<WD1793>(_context, WdVariant::Wd1772,
                                    std::array<FDD*, 4>{ _drives[0].get(), _drives[1].get(), nullptr, nullptr },
                                    CardId());

    try
    {
        Attach();
    }
    catch (...)
    {
        Detach();
        throw;
    }
    BusReset();
}

PlusDCard::~PlusDCard()
{
    Detach();
}

void PlusDCard::LoadRom()
{
    const char* romPath = IsDisciple() ? kDiscipleRomPath : kRomPath;
    const std::string what = std::string("the ") + (IsDisciple() ? "DISCiPLE" : "+D") + " ROM " + romPath;
    const std::string path = ResolveRomPath(romPath);
    if (path.empty())
        throw std::runtime_error(what + " was not found");
    FILE* file = FileHelper::OpenFile(path, "rb");
    if (file == nullptr)
        throw std::runtime_error(what + " cannot be opened");
    const size_t size = std::fread(_rom.data(), 1, _rom.size(), file);
    const bool larger = size == _rom.size() && std::fgetc(file) != EOF;
    std::fclose(file);
    if (size != _rom.size() || larger)
        throw std::runtime_error(what + " is not 8K");
}

void PlusDCard::Attach()
{
    Z80* z80 = _context->pCore->GetZ80();
    if (z80->machineM1Hook != nullptr)
        throw std::runtime_error(std::string("the machine's M1 hook is taken (the ") + DisplayName() +
                                 " paging traps need it)");

    if (!_context->AddCardFdc(_fdc.get()))
        throw std::runtime_error("the machine has no room for another disk controller");
    _fdcAdded = true;

    // The overlay is installed only while the card is paged in (SetPagedLine): paged out, the machine's memory runs
    // the plain interfaces. One try here tells a machine with no room for it from the start
    if (!_context->pCore->AddBusOverlay(this))
        throw std::runtime_error("the host bus overlays are all taken");
    _overlayInstalled = true;

    _savedHookEnd = z80->machineM1HookEnd;
    z80->machineM1Hook = this;
    z80->machineM1HookEnd = kTrapEnd;
    _hookInstalled = true;
    SetPagedLine(_paged);

    if (MediaManager* media = _context->pMediaManager)
    {
        for (size_t i = 0; i < kDrives; i++)
        {
            FloppyDriveSlot::CardDrive spec;
            spec.fdd = _drives[i].get();
            spec.id = DriveSlotId(i);
            spec.label = std::string(IsDisciple() ? "DISCiPLE" : "+D") + " drive " + std::to_string(i + 1);
            spec.tags = { "wd1772", "gdos" };
            if (i == 0)
                spec.tags.emplace_back("boot");
            spec.aliases = { std::string(CardId()) + std::to_string(i + 1) };
            _driveSlots[i] = std::make_unique<FloppyDriveSlot>(_context, spec);
            media->RegisterSlot(*_driveSlots[i]);
        }
    }
}

void PlusDCard::Detach()
{
    if (MediaManager* media = _context != nullptr ? _context->pMediaManager : nullptr)
    {
        for (size_t i = 0; i < kDrives; i++)
        {
            if (_driveSlots[i])
                media->UnregisterSlot(DriveSlotId(i));
        }
    }
    for (auto& slot : _driveSlots)
        slot.reset();

    Core* core = _context != nullptr ? _context->pCore : nullptr;
    Z80* z80 = core != nullptr ? core->GetZ80() : nullptr;
    if (_hookInstalled && z80 != nullptr && z80->machineM1Hook == this)
    {
        z80->machineM1Hook = nullptr;
        z80->machineM1HookEnd = _savedHookEnd;
    }
    _hookInstalled = false;
    if (_overlayInstalled && core != nullptr)
        core->RemoveBusOverlay(this);
    _overlayInstalled = false;
    if (_fdcAdded)
        _context->RemoveCardFdc(_fdc.get());
    _fdcAdded = false;
}

/// region <Ports>

uint8_t PlusDCard::portDeviceInMethod(uint16_t port)
{
    bool drives = false;
    return portDeviceReadCycle(port, drives);
}

uint8_t PlusDCard::portDeviceReadCycle(uint16_t port, bool& drives)
{
    if (IsDisciple())
        return DiscipleRead(port, drives);
    drives = true;
    switch (static_cast<uint8_t>(port))
    {
        case kPortStatusCommand:
            return _fdc->ReadRegister(WD1793::REG_COMMAND_STATUS);
        case kPortTrack:
            return _fdc->ReadRegister(WD1793::REG_TRACK);
        case kPortSector:
            return _fdc->ReadRegister(WD1793::REG_SECTOR);
        case kPortData:
            return _fdc->ReadRegister(WD1793::REG_DATA);
        case kPortPaging:
            // A read pages the +D in; the card leaves the data bus alone (Fuse plusd_patch_read)
            SetPagedLine(true);
            drives = false;
            return 0xFF;
        default:
            return Peek(port, drives);
    }
}

void PlusDCard::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    if (IsDisciple())
    {
        DiscipleWrite(port, value);
        return;
    }
    switch (static_cast<uint8_t>(port))
    {
        case kPortStatusCommand:
            _fdc->WriteRegister(WD1793::REG_COMMAND_STATUS, value);
            break;
        case kPortTrack:
            _fdc->WriteRegister(WD1793::REG_TRACK, value);
            break;
        case kPortSector:
            _fdc->WriteRegister(WD1793::REG_SECTOR, value);
            break;
        case kPortData:
            _fdc->WriteRegister(WD1793::REG_DATA, value);
            break;
        case kPortControl:
            WriteControl(value);
            break;
        case kPortPaging:
            SetPagedLine(false);
            break;
        case kPortPrinter:
            _printerData = value;
            break;
        default:
            break;
    }
}

uint8_t PlusDCard::Peek(uint16_t port, bool& drives) const
{
    if (IsDisciple())
        return DisciplePeek(port, drives);
    drives = true;
    switch (static_cast<uint8_t>(port))
    {
        case kPortStatusCommand:
            return _fdc->PeekRegister(WD1793::REG_COMMAND_STATUS);
        case kPortTrack:
            return _fdc->PeekRegister(WD1793::REG_TRACK);
        case kPortSector:
            return _fdc->PeekRegister(WD1793::REG_SECTOR);
        case kPortData:
            return _fdc->PeekRegister(WD1793::REG_DATA);
        case kPortPrinter:
            // Bit 7 = busy; no printer is connected (Fuse: #FF without one, #7F with one)
            return 0xFF;
        default:
            // #E7 (pages in, does not drive), #EF (write only)
            drives = false;
            return 0xFF;
    }
}

void PlusDCard::WriteControl(uint8_t value)
{
    if (IsDisciple())
    {
        DiscipleWriteControl(value);
        return;
    }
    const bool strobeChanged = ((value ^ _control) & kControlStrobe) != 0;
    _control = value;
    if (strobeChanged)
        StrobeEdge();
    WriteControlLines();
}

void PlusDCard::WriteControlLines()
{
    const uint8_t value = _control;
    if (IsDisciple())
    {
        // Bit 0 picks one of the two drives (MAME m_floppy[~data & 1], Fuse disciple_cn_write): always one selected
        _fdc->selectDriveLine((value & kDiscipleControlDrive1) ? 0 : 1);
        _fdc->setSide((value & kDiscipleControlSide) != 0);
        _fdc->setDensityLine((value & kDiscipleControlSingleDensity) != 0);
        return;
    }
    // The drive select lines: drive 1 on bit 0, drive 2 on bit 1. Both set: the two drives would answer together;
    // drive 1 does here. None: no drive (no disk, no index)
    int line = -1;
    if (value & kControlDrive1)
        line = 0;
    else if (value & kControlDrive2)
        line = 1;
    _fdc->selectDriveLine(line);
    _fdc->setSide((value & kControlSide) != 0);
}

void PlusDCard::StrobeEdge()
{
    // Fuse printer_parallel_strobe_write: the ROMs disagree on the polarity, so a pulse is any two edges; the byte is
    // the one latched at the first (LPRINT changes the latch before the second)
    if (!_strobeFirstEdge)
    {
        _strobeByte = _printerData;
        _strobeFirstEdge = true;
        return;
    }
    _strobeFirstEdge = false;
    _lastPrinted = _strobeByte;
    _printedBytes++;
}

/// endregion </Ports>

void PlusDCard::BusReset()
{
    // Paged out: the machine starts from its own ROM, the +D comes in at its first trap (Fuse: plusd_reset() sets
    // plusd_active but clears ram.romcs, so nothing is mapped). Paged in, the +D ROM's #0000 entry would hand over to
    // #11CB, the 48K ROM's START: on a 128K that is ROM 0's code, and the boot hangs on whatever the RAM holds
    SetPagedLine(false);
    _strobeFirstEdge = false;
    _fdc->reset();
    if (IsDisciple())
    {
        // The latch (a 74LS374) has no reset input: it keeps the drive, side and density; the swap is cleared and
        // paging held off (MAME device_reset: m_map, m_romcs, m_reset_delay)
        _memSwap = false;
        _resetHold = true;
        _resetHoldStarted = false;
        _resetHoldStart = 0;
        WriteControlLines();
        return;
    }
    _control = 0;
    _fdc->selectDriveLine(-1);
    _fdc->setSide(false);
}

/// region <Paging>

uint8_t PlusDCard::onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged)
{
    (void)isExecution;
    (void)romPaged;
    if (!_paged || addr >= 0x4000)
        return normal;
    // The DISCiPLE's port #7B swaps the halves: RAM at #0000, ROM at #2000
    const bool lowIsRom = !_memSwap;
    if (addr < 0x2000)
        return lowIsRom ? _rom[addr] : _ram[addr];
    return lowIsRom ? _ram[addr - 0x2000] : _rom[addr - 0x2000];
}

void PlusDCard::onWrite(uint16_t addr, uint8_t value, bool romPaged)
{
    (void)romPaged;
    if (!_paged || addr >= 0x4000)
        return;
    if (!_memSwap && addr >= 0x2000)
        _ram[addr - 0x2000] = value;
    else if (_memSwap && addr < 0x2000)
        _ram[addr] = value;
}

void PlusDCard::BeforeMachineM1(uint16_t address)
{
    if (IsDisciple())
    {
        // Every fetch below #0300 moves the hold-off on: the first one after /RESET starts it
        HoldingOff();
        if (IsTrapAddress(_model, address) && !_paged && DisciplePagingAllowed())
            SetPagedLine(true);
        return;
    }
    if (IsTrapAddress(address) && !_paged)
        SetPagedLine(true);
}

void PlusDCard::SetPagedLine(bool paged)
{
    _paged = paged;
    // Before Attach and after Detach the card is not on the bus: the flag only
    if (!_hookInstalled)
        return;
    Core* core = _context->pCore;
    if (paged && !_overlayInstalled)
    {
        // Core::AddBusOverlay logs a refusal (all overlay slots taken since Attach): the card then reads as absent
        _overlayInstalled = core->AddBusOverlay(this);
    }
    else if (!paged && _overlayInstalled)
    {
        core->RemoveBusOverlay(this);
        _overlayInstalled = false;
    }
}

void PlusDCard::OnMachineM1(uint16_t address)
{
    (void)address;
}

bool PlusDCard::RepeatM1IsInert(uint16_t address) const
{
    // The DISCiPLE's hold-off ends on a fetch: a repeated fetch is not inert while it runs
    if (IsDisciple() && _resetHold)
        return false;
    return !IsTrapAddress(_model, address) || _paged;
}

/// endregion </Paging>

/// region <Snapshots>

PlusDCard::SnapshotRegisters PlusDCard::CaptureSnapshot() const
{
    SnapshotRegisters r;
    r.paged = _paged;
    r.stepIn = _fdc->isStepDirectionIn();
    r.control = _control;
    r.track = _fdc->getTrackRegister();
    r.sector = _fdc->getSectorRegister();
    r.data = _fdc->getDataRegister();
    r.status = _fdc->PeekRegister(WD1793::REG_COMMAND_STATUS);
    return r;
}

void PlusDCard::RestoreSnapshot(const SnapshotRegisters& registers, const uint8_t* ram)
{
    // The latch is taken as it was: its strobe bit is no printer pulse
    _control = registers.control;
    _strobeFirstEdge = false;
    WriteControlLines();
    _fdc->RestoreTaskRegisters(registers.track, registers.sector, registers.data, registers.status, registers.stepIn);
    SetPaged(registers.paged);
    if (ram != nullptr)
        std::memcpy(_ram.data(), ram, _ram.size());
}

/// endregion </Snapshots>

/// region <Time travel>

void PlusDCard::CollectTtdDevices(std::vector<CardTtdDevice>& out)
{
    out.push_back({ _ttd.get(), SlotId() + "." + CardId(), nullptr });
}

void PlusDCard::TtdFingerprint(std::vector<std::pair<std::string, uint64_t>>& out) const
{
    out.emplace_back("slots." + SlotId() + ".rom", Fnv1a(_rom.data(), _rom.size()));
}

void PlusDCard::SaveState(uint8_t* dst) const
{
    std::memset(dst, 0, kStateSize);
    dst[0] = kStateVersion;
    dst[1] = _paged ? 1 : 0;
    dst[2] = _control;
    dst[3] = _printerData;
    dst[4] = _strobeByte;
    dst[5] = _strobeFirstEdge ? 1 : 0;
    for (int i = 0; i < 4; i++)
        dst[6 + i] = static_cast<uint8_t>(_printedBytes >> (8 * i));
    dst[10] = _lastPrinted;
    dst[11] = _fdc->NoDriveSelected() ? 1 : 0;
    const uint64_t spinUp = static_cast<uint64_t>(_fdc->SpinUpDoneAt());
    for (int i = 0; i < 8; i++)
        dst[12 + i] = static_cast<uint8_t>(spinUp >> (8 * i));
    std::memcpy(dst + kStateRamOffset, _ram.data(), _ram.size());
    _fdc->TTDSaveState(dst + kStateFdcOffset);
    _fdc->SaveTransferContext(dst + kStateFdcOffset + kStateFdcSize);
    if (IsDisciple())
    {
        uint8_t* tail = dst + kDiscipleStateOffset;
        tail[0] = _memSwap ? 1 : 0;
        tail[1] = _resetHold ? 1 : 0;
        tail[2] = _resetHoldStarted ? 1 : 0;
        tail[3] = _inhibit ? 1 : 0;
        for (int i = 0; i < 8; i++)
            tail[8 + i] = static_cast<uint8_t>(_resetHoldStart >> (8 * i));
    }
}

bool PlusDCard::LoadState(const uint8_t* src)
{
    if (src == nullptr || src[0] != kStateVersion)
        return false;
    SetPagedLine(src[1] != 0);
    _control = src[2];
    _printerData = src[3];
    _strobeByte = src[4];
    _strobeFirstEdge = src[5] != 0;
    _printedBytes = 0;
    for (int i = 0; i < 4; i++)
        _printedBytes |= static_cast<uint32_t>(src[6 + i]) << (8 * i);
    _lastPrinted = src[10];
    uint64_t spinUp = 0;
    for (int i = 0; i < 8; i++)
        spinUp |= static_cast<uint64_t>(src[12 + i]) << (8 * i);
    std::memcpy(_ram.data(), src + kStateRamOffset, _ram.size());
    // The controller's blob restores its last drive index; the select lines may have none (the latch's bits 0-1)
    _fdc->TTDLoadState(src + kStateFdcOffset);
    _fdc->LoadTransferContext(src + kStateFdcOffset + kStateFdcSize);
    if (src[11] != 0)
        _fdc->RestoreNoDriveSelected();
    _fdc->RestoreSpinUpDoneAt(static_cast<size_t>(spinUp));
    if (IsDisciple())
    {
        const uint8_t* tail = src + kDiscipleStateOffset;
        _memSwap = tail[0] != 0;
        _resetHold = tail[1] != 0;
        _resetHoldStarted = tail[2] != 0;
        _inhibit = tail[3] != 0;
        _resetHoldStart = 0;
        for (int i = 0; i < 8; i++)
            _resetHoldStart |= static_cast<uint64_t>(tail[8 + i]) << (8 * i);
    }
    return true;
}

/// endregion </Time travel>

/// region <DISCiPLE>

uint64_t PlusDCard::Now() const
{
    return _context->emulatorState.t_states + _context->pCore->GetZ80()->t;
}

void PlusDCard::UpdateResetHold()
{
    // Measured from the first fetch after /RESET (the reset vector): the clock may be set back around a reset
    const uint64_t now = Now();
    if (!_resetHoldStarted)
    {
        _resetHoldStarted = true;
        _resetHoldStart = now;
        return;
    }
    if (now - _resetHoldStart >= kResetHoldTStates)
        _resetHold = false;
}

bool PlusDCard::HoldingOff()
{
    if (_resetHold)
        UpdateResetHold();
    return _resetHold;
}

bool PlusDCard::DisciplePagingAllowed()
{
    if (HoldingOff())
        return false;
    return !_inhibit || (_control & kDiscipleControlInhibitOverride) != 0;
}

uint8_t PlusDCard::DiscipleRead(uint16_t port, bool& drives)
{
    const uint8_t low = static_cast<uint8_t>(port);
    if ((low & 0x3F) == kDisciplePortFdc)
    {
        drives = true;
        return _fdc->ReadRegister(static_cast<uint8_t>(low >> 6));
    }
    switch (low)
    {
        case kDisciplePortBoot:
            // A read puts the ROM back at #0000 (MAME: not during the reset hold-off); the data bus is left alone
            if (!HoldingOff())
                _memSwap = false;
            drives = false;
            return 0xFF;
        case kDisciplePortPaging:
            if (DisciplePagingAllowed())
                SetPagedLine(true);
            drives = false;
            return 0xFF;
        default:
            return DisciplePeek(port, drives);
    }
}

void PlusDCard::DiscipleWrite(uint16_t port, uint8_t value)
{
    const uint8_t low = static_cast<uint8_t>(port);
    if ((low & 0x3F) == kDisciplePortFdc)
    {
        _fdc->WriteRegister(static_cast<uint8_t>(low >> 6), value);
        return;
    }
    switch (low)
    {
        case kDisciplePortControl:
            DiscipleWriteControl(value);
            break;
        case kDisciplePortBoot:
            if (!HoldingOff())
                _memSwap = true;
            break;
        case kDisciplePortPaging:
            if (!HoldingOff())
                SetPagedLine(false);
            break;
        case kDisciplePortPrinter:
            _printerData = value;
            break;
        case kDisciplePortNetwork:
        default:
            // The network (#3B, latch bit 7) is not emulated
            break;
    }
}

uint8_t PlusDCard::DisciplePeek(uint16_t port, bool& drives) const
{
    const uint8_t low = static_cast<uint8_t>(port);
    drives = true;
    if ((low & 0x3F) == kDisciplePortFdc)
        return _fdc->PeekRegister(static_cast<uint8_t>(low >> 6));
    if (low == kDisciplePortControl)
    {
        // Bits 0-4 the joystick socket (Kempston order, active high); bit 6 = printer not busy through IC10's 74LS240:
        // no printer is connected, so busy (0, Fuse #BF); bits 5 and 7 low (the network line idles low)
        const uint8_t stick = _context != nullptr && _context->pJoystick != nullptr ? _context->pJoystick->Read() : 0x00;
        return static_cast<uint8_t>(stick & 0x1F);
    }
    // #3B / #7B / #BB / #FB: write only, or the card leaves the data bus alone
    drives = false;
    return 0xFF;
}

void PlusDCard::DiscipleWriteControl(uint8_t value)
{
    const bool strobeChanged = ((value ^ _control) & kDiscipleControlStrobe) != 0;
    _control = value;
    if (strobeChanged)
        StrobeEdge();
    WriteControlLines();
    // With the inhibit button pressed the card stays out unless bit 4 lets it in (MAME iorq_w #1F)
    if (_inhibit && (value & kDiscipleControlInhibitOverride) == 0)
        SetPagedLine(false);
}

/// endregion </DISCiPLE>
