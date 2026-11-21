#include "divmmccard.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "common/filehelper.h"
#include "debugger/ttd/engine/ttdregiontracker.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/mediaslot.h"
#include "emulator/media/medium.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/slots/slotplanner.h"
#include "emulator/state/statenode.h"

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

uint64_t Fnv1a(const uint8_t* data, size_t size, uint64_t hash = 0xcbf29ce484222325ull)
{
    for (size_t i = 0; i < size; i++)
    {
        hash ^= data[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

const char* AutomapName(DivMmcPaging::AutomapMode mode)
{
    switch (mode)
    {
        case DivMmcPaging::AutomapMode::Off:
            return "off";
        case DivMmcPaging::AutomapMode::Rom3:
            return "rom3";
        default:
            return "on";
    }
}

}  // namespace

/// One SD socket of the card as a media slot: the media manager inserts, the board's SdCardSpi reads it
class DivMmcSdSlot final : public IMediaSlot
{
public:
    DivMmcSdSlot(SdCardSpi& card, size_t index) : _card(card)
    {
        _descriptor.id = DivMmcCard::kSdSlotIds[index];
        _descriptor.kind = MediaKind::Block;
        _descriptor.label = index == 0 ? "DivMMC SD card 0" : "DivMMC SD card 1";
        _descriptor.removable = true;
        _descriptor.swapDelayMs = 500;
        _descriptor.acceptsFolder = true;
        _descriptor.defaultAccess = AccessMode::Session;
        // esxDOS and UnoDOS read FAT16 and FAT32 from an MBR partition
        _descriptor.defaultFs = FatType::Fat16;
        _descriptor.fsCompatibility = { FatType::Fat16, FatType::Fat32 };
        _descriptor.folderMbr = true;
        _descriptor.tags = index == 0 ? std::vector<std::string>{ "sd", "divmmc", "primary", "boot" }
                                      : std::vector<std::string>{ "sd", "divmmc", "secondary" };
        _descriptor.aliases = index == 0 ? std::vector<std::string>{ "divmmc", "divmmc0" }
                                         : std::vector<std::string>{ "divmmc1" };
    }

    const SlotDescriptor& Descriptor() const override
    {
        return _descriptor;
    }
    void Attach(Medium& medium) override
    {
        // The card type follows the medium's size (up to 2 GB SDSC); esxDOS and UnoDOS take both
        if (IBlockDevice* block = medium.Block())
            _card.attach(*block);
    }
    void Detach() override
    {
        _card.detach();
    }
    bool IsBusy() const override
    {
        return _card.busy();
    }

private:
    SdCardSpi& _card;
    SlotDescriptor _descriptor;
};

/// The card's TTD device: the board's blob, its RAM as an engine region
class DivMmcTtd final : public ttd::TTDSerializable, public ttd::ITTDRegionSource
{
public:
    explicit DivMmcTtd(DivMmcCard& card) : _card(card)
    {
        // Bound from the start: the board's write path may mark it as soon as the engine arms it
        _tracker.Bind(_card.Paging().RamData(), DivMmcPaging::kRamSize);
    }

    size_t TTDStateSize() const override
    {
        return DivMmcCard::kTtdStateSize;
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
        return "DivMmc";
    }
    ttd::TTDDeviceType TTDType() const override
    {
        return ttd::TTDDeviceType::DivMmc;
    }
    uint64_t TTDHashState() const override
    {
        std::vector<uint8_t> blob(DivMmcPaging::kStateSize);
        _card.Paging().SaveState(blob.data());
        return Fnv1a(_card.Paging().RamData(), DivMmcPaging::kRamSize, Fnv1a(blob.data(), blob.size()));
    }

    /// region <ITTDRegionSource>
    void TTDRegions(std::vector<ttd::TTDDeviceRegion>& out) override
    {
        uint8_t* ram = _card.Paging().RamData();
        _tracker.Bind(ram, DivMmcPaging::kRamSize);
        ttd::TTDDeviceRegion r;
        r.desc.id = ttd::TTDRegionId::DivMmcRam;
        r.desc.name = "divmmc.ram";
        r.desc.ownerType = static_cast<uint16_t>(ttd::TTDDeviceType::DivMmc);
        r.desc.memory = ram;
        r.desc.bytes = DivMmcPaging::kRamSize;
        r.desc.pieces = _tracker.Pieces();
        r.tracker = &_tracker;
        out.push_back(r);
    }
    void TTDArmRegions(bool on) override
    {
        _card.Paging().SetRamTracker(on ? &_tracker : nullptr);
    }
    bool TTDStateWithoutRegions(std::vector<uint8_t>& state) const override
    {
        state.resize(DivMmcPaging::kStateSize);
        _card.Paging().SaveState(state.data());
        return true;
    }
    bool TTDLoadStateWithoutRegions(const uint8_t* state, size_t size) override
    {
        return size == DivMmcPaging::kStateSize && _card.Paging().LoadState(state);
    }
    /// endregion </ITTDRegionSource>

private:
    DivMmcCard& _card;
    ttd::TTDRegionTracker _tracker;
};

std::unique_ptr<ICard> DivMmcCard::Create(const CardContext& context)
{
    return std::make_unique<DivMmcCard>(context);
}

DivMmcCard* DivMmcCard::Find(const EmulatorContext* context)
{
    if (context == nullptr || context->pSlotManager == nullptr)
        return nullptr;
    for (const std::unique_ptr<ICard>& card : context->pSlotManager->Cards())
    {
        if (auto* divmmc = dynamic_cast<DivMmcCard*>(card.get()))
            return divmmc;
    }
    return nullptr;
}

DivMmcCard::DivMmcCard(const CardContext& context) : ICard(context), _context(context.emulator)
{
    if (_context == nullptr || _context->pCore == nullptr || _context->pCore->GetZ80() == nullptr)
        throw std::runtime_error("the machine has no CPU yet");
    if (_context->pCore->GetZ80()->machineM1Hook != nullptr)
        throw std::runtime_error("the machine's M1 hook is taken (the DivMMC automap needs it)");

    _paging = std::make_unique<DivMmcPaging>(_context);
    LoadFirmware(context);
    if (context.def != nullptr)
    {
        const uint32_t automap = slots::OptionBits(*context.def, context.options, slots::Opt::Automap);
        _paging->SetAutomapMode((automap & slots::Bit(1))   ? DivMmcPaging::AutomapMode::Off
                                : (automap & slots::Bit(2)) ? DivMmcPaging::AutomapMode::Rom3
                                                            : DivMmcPaging::AutomapMode::On);
    }
    // The ports come through the card's claims; the board takes the overlay and the M1 hook
    if (!_paging->Attach(false))
        throw std::runtime_error("the DivMMC cannot go on the bus (the M1 hook or the host bus overlays are taken)");
    _ttd = std::make_unique<DivMmcTtd>(*this);
    RegisterMedia();
    _paging->PowerOn();
}

DivMmcCard::~DivMmcCard()
{
    UnregisterMedia();
    if (_paging)
        _paging->Detach();
}

void DivMmcCard::LoadFirmware(const CardContext& context)
{
    const bool esxdos = context.def != nullptr &&
                        (slots::OptionBits(*context.def, context.options, slots::Opt::Rom) & slots::Bit(1)) != 0;
    _firmware = esxdos ? "esxdos" : "unodos";
    const char* romPath = esxdos ? kEsxDosRomPath : kUnoDosRomPath;
    const std::string what = std::string("the DivMMC firmware ") + romPath;
    _romPath = ResolveRomPath(romPath);
    if (_romPath.empty())
        throw std::runtime_error(what + " was not found");
    if (!_paging->LoadRomFile(_romPath))
        throw std::runtime_error(what + " cannot be read or is not 8K");
}

void DivMmcCard::RegisterMedia()
{
    MediaManager* media = _context->pMediaManager;
    for (size_t i = 0; i < kSdSlots; i++)
    {
        SdCardSpi& sd = _paging->Card(static_cast<unsigned>(i));
        const std::string id = kSdSlotIds[i];
        // A guest write is a barrier of the media manager (and a TTD external event)
        sd.setWriteListener([context = _context, id](uint64_t) {
            if (context->pMediaManager)
                context->pMediaManager->NoteWrite(id);
        });
        _sdSlots[i] = std::make_unique<DivMmcSdSlot>(sd, i);
        if (media != nullptr)
            media->RegisterSlot(*_sdSlots[i]);
    }
}

void DivMmcCard::UnregisterMedia()
{
    MediaManager* media = _context != nullptr ? _context->pMediaManager : nullptr;
    for (size_t i = 0; i < kSdSlots; i++)
    {
        if (_sdSlots[i] && media != nullptr)
            media->UnregisterSlot(kSdSlotIds[i]);
        _sdSlots[i].reset();
        if (_paging)
            _paging->Card(static_cast<unsigned>(i)).setWriteListener(nullptr);
    }
}

/// region <Ports>

uint8_t DivMmcCard::portDeviceInMethod(uint16_t port)
{
    bool drives = false;
    return portDeviceReadCycle(port, drives);
}

uint8_t DivMmcCard::portDeviceReadCycle(uint16_t port, bool& drives)
{
    // #E3 and #E7 are write only: the CPLD decodes them with /WR low
    drives = static_cast<uint8_t>(port) == DivMmcPaging::kPortSpiData;
    return drives ? _paging->portDeviceInMethod(port) : 0xFF;
}

void DivMmcCard::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    _paging->portDeviceOutMethod(port, value);
}

uint8_t DivMmcCard::Peek(uint16_t port, bool& drives) const
{
    drives = static_cast<uint8_t>(port) == DivMmcPaging::kPortSpiData;
    return drives ? _paging->SpiReceive() : 0xFF;
}

/// endregion </Ports>

/// region <Reset>

void DivMmcCard::BusReset()
{
    _paging->Reset();
}

void DivMmcCard::PowerCycle()
{
    _paging->PowerOn();
}

void DivMmcCard::AfterSnapshotLoad()
{
    _paging->UnmapForProgram();
}

/// endregion </Reset>

/// region <Automation>

bool DivMmcCard::ReportLiveState(StateNode& out) const
{
    const DivMmcPaging& board = *_paging;
    out["firmware"] = _firmware;
    out["automap_mode"] = AutomapName(board.GetAutomapMode());
    out["control_register"] = int(board.Control());
    out["conmem"] = board.Conmem();
    out["mapram"] = board.Mapram();
    out["bank"] = int(board.Bank());
    out["automapped"] = board.Automapped();
    out["map_condition"] = board.MapCondition();
    out["mapped"] = board.Mapped();
    out["selected_card"] = board.SelectedCard();
    StateNode cards = StateNode::Array();
    for (size_t i = 0; i < kSdSlots; i++)
    {
        const SdCardSpi& sd = board.Card(static_cast<unsigned>(i));
        StateNode card = StateNode::Object();
        card["media_slot"] = kSdSlotIds[i];
        card["present"] = sd.present();
        card["initialized"] = sd.initialized();
        card["sdhc"] = sd.isSdhc();
        card["blocks_read"] = sd.blocksRead();
        card["blocks_written"] = sd.blocksWritten();
        cards.push(std::move(card));
    }
    out["sd_cards"] = std::move(cards);
    return true;
}

/// endregion </Automation>

/// region <Time travel>

void DivMmcCard::CollectTtdDevices(std::vector<CardTtdDevice>& out)
{
    out.push_back({ _ttd.get(), SlotId() + "." + kCardId, _ttd.get() });
}

void DivMmcCard::TtdFingerprint(std::vector<std::pair<std::string, uint64_t>>& out) const
{
    out.emplace_back("slots." + SlotId() + ".rom", Fnv1a(_paging->Rom(), DivMmcPaging::kRomSize));
}

void DivMmcCard::SaveState(uint8_t* dst) const
{
    _paging->SaveState(dst);
    std::memcpy(dst + DivMmcPaging::kStateSize, _paging->RamData(), DivMmcPaging::kRamSize);
}

bool DivMmcCard::LoadState(const uint8_t* src)
{
    if (!_paging->LoadState(src))
        return false;
    std::memcpy(_paging->RamData(), src + DivMmcPaging::kStateSize, DivMmcPaging::kRamSize);
    return true;
}

/// endregion </Time travel>
