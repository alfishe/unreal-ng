#include "machinestatetransfer.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "common/stringhelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/medium.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#ifdef UNREALNG_HAVE_OPL4
#include "emulator/sound/chips/soundchip_moonsound.h"
#endif
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "emulator/zxpoly/zxpolygroup.h"
#include "stdafx.h"

using ttd::PeripheralId;

namespace
{
    /// How a model pages its memory. Models of one family share the paging ports and their meaning
    enum class PagingFamily : uint8_t
    {
        Spectrum48,   ///< 48K: no paging
        Spectrum128,  ///< 128K, +2: #7FFD
        Plus3,        ///< +2A, +3: #7FFD + #1FFD (ROM high bit, all-RAM modes)
        Pentagon,     ///< Pentagon 128 / 512 / 1024: #7FFD with extension bits, #EFF7 on 1024
        Scorpion,     ///< Scorpion ZS-256 (and 1024): #7FFD + Scorpion #1FFD
        Other         ///< Machine-specific paging (ATM, Profi, TSConf, ProfROM, ...)
    };

    PagingFamily FamilyOf(const CONFIG& config)
    {
        switch (config.mem_model)
        {
            case MM_SPECTRUM48:
                return PagingFamily::Spectrum48;
            case MM_SPECTRUM128:
            case MM_PLUS2:
                return PagingFamily::Spectrum128;
            case MM_PLUS3:
            case MM_PLUS2A:
                return PagingFamily::Plus3;
            case MM_PENTAGON:
                return PagingFamily::Pentagon;
            case MM_SCORP:
                return PagingFamily::Scorpion;
            default:
                // ProfROM Scorpion carries a ROM-plane state machine no other model has
                return PagingFamily::Other;
        }
    }

    const char* FamilyName(PagingFamily family)
    {
        switch (family)
        {
            case PagingFamily::Spectrum48:
                return "48K";
            case PagingFamily::Spectrum128:
                return "128K";
            case PagingFamily::Plus3:
                return "+2A/+3";
            case PagingFamily::Pentagon:
                return "Pentagon";
            case PagingFamily::Scorpion:
                return "Scorpion";
            default:
                return "model-specific";
        }
    }

    /// What the source's state needs from a target
    enum class Need : uint8_t
    {
        Mode48,      ///< Pages 5, 2, 0 and the 48 BASIC ROM: any Spectrum-compatible family
        Mode128,     ///< Pages 0-7, #7FFD without extensions: any 128K-class family
        SameFamily,  ///< Extended paging of its own family (at least the same RAM size)
        SameModel    ///< Only the same model and RAM size
    };

    /// Which ROM the source runs, independent of how its model numbers ROMs
    enum class RomRole : uint8_t
    {
        Editor,    ///< 128K editor ROM (128K ROM 0, +3 ROM 0)
        Basic48,   ///< 48 BASIC (48K ROM, 128K ROM 1, +3 ROM 3)
        Specific   ///< A ROM only this family has (+3 syntax / DOS ROMs)
    };

    constexpr uint8_t P7FFD_PAGE_MASK = 0x07;
    constexpr uint8_t P7FFD_SCREEN = 0x08;
    constexpr uint8_t P7FFD_ROM = 0x10;
    constexpr uint8_t P7FFD_LOCK = 0x20;
    constexpr uint8_t P7FFD_128_BITS = 0x3F;       // page, screen, ROM, lock
    constexpr uint8_t P1FFD_PLUS3_SPECIAL = 0x01;  // all-RAM mode
    constexpr uint8_t P1FFD_PLUS3_ROM_HIGH = 0x04;
    constexpr uint8_t PEFF7_P1024_COMPAT = 0x04;   // #7FFD bit 5 is the lock, pages 0-7 only

    uint16_t RamPages(const CONFIG& config)
    {
        return static_cast<uint16_t>(std::min<uint32_t>(config.ramsize / 16, MAX_RAM_PAGES));
    }

    /// The pages a clone copies: every page the model can map. A 48K maps banks 5, 2 and 0 (as a 128K does)
    /// although its ramsize says three pages, so never fewer than the eight 128K banks
    uint16_t ClonePages(const CONFIG& config)
    {
        return std::max<uint16_t>(RamPages(config), 8);
    }

    bool PageIsZero(EmulatorContext& context, uint16_t page)
    {
        const uint8_t* data = context.pMemory->RAMPageAddress(page);
        if (!data)
            return true;
        return std::all_of(data, data + PAGE_SIZE, [](uint8_t b) { return b == 0; });
    }

    /// Any non-zero page at or above 'from'
    bool UsesPagesFrom(EmulatorContext& context, uint16_t from)
    {
        const uint16_t pages = RamPages(context.config);
        for (uint16_t page = from; page < pages; page++)
        {
            if (!PageIsZero(context, page))
                return true;
        }
        return false;
    }

    struct SourceAnalysis
    {
        PagingFamily family = PagingFamily::Other;
        Need need = Need::SameModel;
        RomRole rom = RomRole::Editor;
        bool fitsIn48 = false;  ///< A 128K-class source locked in 48K mode
        std::string why;        ///< Why the need is what it is
    };

    SourceAnalysis Analyze(EmulatorContext& source)
    {
        SourceAnalysis a;
        const CONFIG& config = source.config;
        const EmulatorState& state = source.emulatorState;
        a.family = FamilyOf(config);

        const uint8_t p7FFD = state.p7FFD;
        a.rom = (p7FFD & P7FFD_ROM) ? RomRole::Basic48 : RomRole::Editor;

        switch (a.family)
        {
            case PagingFamily::Spectrum48:
                a.need = Need::Mode48;
                a.rom = RomRole::Basic48;
                a.fitsIn48 = true;
                return a;

            case PagingFamily::Other:
                a.need = Need::SameModel;
                a.why = "the source's memory map is specific to its model";
                return a;

            case PagingFamily::Spectrum128:
                a.need = Need::Mode128;
                break;

            case PagingFamily::Plus3:
            {
                const uint8_t p1FFD = state.p1FFD;
                const unsigned rom = ((p1FFD & P1FFD_PLUS3_ROM_HIGH) ? 2u : 0u) | ((p7FFD & P7FFD_ROM) ? 1u : 0u);
                if (p1FFD & P1FFD_PLUS3_SPECIAL)
                {
                    a.need = Need::SameFamily;
                    a.why = "the source runs a +2A/+3 all-RAM configuration (#1FFD bit 0)";
                }
                else if (rom == 1 || rom == 2)
                {
                    a.need = Need::SameFamily;
                    a.rom = RomRole::Specific;
                    a.why = "the source runs +3 ROM " + std::to_string(rom) + " (syntax / DOS), which only +2A/+3 have";
                }
                else
                {
                    a.need = Need::Mode128;
                    a.rom = (rom == 3) ? RomRole::Basic48 : RomRole::Editor;
                }
                break;
            }

            case PagingFamily::Pentagon:
            {
                const bool is1024 = config.ramsize >= 1024;
                const bool extended = config.ramsize > 128 &&
                                      (state.pEFF7 != 0 || (p7FFD & 0xC0) != 0 ||
                                       (is1024 && (p7FFD & P7FFD_LOCK)) || UsesPagesFrom(source, 8));
                a.need = extended ? Need::SameFamily : Need::Mode128;
                if (extended)
                    a.why = "the source uses Pentagon " + std::to_string(config.ramsize) + "K extended paging";
                break;
            }

            case PagingFamily::Scorpion:
            {
                const bool extended = state.p1FFD != 0 || UsesPagesFrom(source, 8);
                a.need = extended ? Need::SameFamily : Need::Mode128;
                if (extended)
                    a.why = "the source uses Scorpion #1FFD or pages above 7";
                break;
            }
        }

        // A 128K-class machine locked in 48K mode (48 BASIC, page 0, normal screen, not in TR-DOS) is a 48K
        a.fitsIn48 = a.need == Need::Mode128 && (p7FFD & P7FFD_LOCK) && a.rom == RomRole::Basic48 &&
                     (p7FFD & (P7FFD_PAGE_MASK | P7FFD_SCREEN)) == 0 && !(state.flags & CF_TRDOS);
        return a;
    }

    /// Whether the target can hold what the source needs; the reason when not
    bool TargetAccepts(const SourceAnalysis& a, EmulatorContext& source, EmulatorContext& target, std::string& why)
    {
        const PagingFamily targetFamily = FamilyOf(target.config);
        const bool target128Class = targetFamily == PagingFamily::Spectrum128 || targetFamily == PagingFamily::Plus3 ||
                                    targetFamily == PagingFamily::Pentagon || targetFamily == PagingFamily::Scorpion;

        switch (a.need)
        {
            case Need::Mode48:
                if (targetFamily == PagingFamily::Spectrum48 || target128Class)
                    return true;
                why = std::string("a ") + FamilyName(targetFamily) + " target has no 48K-compatible memory map here";
                return false;

            case Need::Mode128:
                if (target128Class)
                    return true;
                if (targetFamily == PagingFamily::Spectrum48 && a.fitsIn48)
                    return true;
                why = std::string("the source uses 128K paging; a ") + FamilyName(targetFamily) +
                      " target cannot hold it" +
                      (targetFamily == PagingFamily::Spectrum48 ? " (it is not locked in 48K mode)" : "");
                return false;

            case Need::SameFamily:
                if (targetFamily == a.family && target.config.ramsize >= source.config.ramsize)
                    return true;
                why = a.why + "; the target must be " + FamilyName(a.family) + " with at least " +
                      std::to_string(source.config.ramsize) + "K";
                return false;

            case Need::SameModel:
            default:
                if (target.config.mem_model == source.config.mem_model &&
                    target.config.ramsize == source.config.ramsize)
                    return true;
                why = a.why + "; the target must be the same model with the same RAM size";
                return false;
        }
    }

    // --- Devices -------------------------------------------------------------------------------------------------

    struct DeviceSet
    {
        ttd::TTDPeripheralRegistry registry;
        std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
        bool ok = false;
        std::string error;

        explicit DeviceSet(EmulatorContext& context)
        {
            ok = ttd::RegisterMachinePeripherals(&context, registry, owned, &error);
        }
    };

    /// Devices whose state does not depend on the machine they are plugged into
    bool IsPortable(PeripheralId id)
    {
        switch (id)
        {
            case PeripheralId::TurboSound:
            case PeripheralId::TSFM:
            case PeripheralId::Covox:
            case PeripheralId::GeneralSound:
            case PeripheralId::GeneralSoundLightweight:
            case PeripheralId::NeoGS:
            case PeripheralId::MoonSound:
            case PeripheralId::KempstonMouse:
            case PeripheralId::KempstonJoystick:
                return true;
            default:
                return false;
        }
    }

    std::string DeviceLabel(PeripheralId id, const ttd::TTDSerializable* device)
    {
        std::string name = device ? device->TTDDeviceName() : std::string();
        if (name.empty() || name == "unknown")
            name = "device " + std::to_string(static_cast<unsigned>(id));
        return name;
    }

    /// Memory a device's TTD blob does not carry. Checked (and, when apply is true, copied) before the blob loads
    /// @return false when the target's memory cannot hold the source's (the device is then not transferred)
    bool CopyDeviceMemory(PeripheralId id, EmulatorContext& source, EmulatorContext& target, bool apply,
                          std::vector<MachineStateTransfer::Item>& items)
    {
        using Status = MachineStateTransfer::ItemStatus;

        if (id == PeripheralId::NeoGS)
        {
            auto* src = dynamic_cast<SoundChip_NeoGS*>(source.pSoundManager->getGeneralSound());
            auto* dst = dynamic_cast<SoundChip_NeoGS*>(target.pSoundManager->getGeneralSound());
            if (!src || !dst)
                return false;
            if (src->memory().ramSize() != dst->memory().ramSize())
            {
                items.push_back({"NeoGS RAM", Status::Dropped,
                                 StringHelper::Format("source %zu KB, target %zu KB", src->memory().ramSize() / 1024,
                                                      dst->memory().ramSize() / 1024)});
                return false;
            }
            if (apply)
            {
                std::memcpy(dst->memory().ram(), src->memory().ram(), src->memory().ramSize());
                // Contents only: load() leaves the target's "modified" flag clear, so a transferred copy never
                // overwrites the flash image file on disk; the flash state machine comes with the blob
                dst->flash().load(src->flash().data(), Flash29F040B::SIZE);
            }
            items.push_back({"NeoGS RAM and flash", Status::Copied,
                             StringHelper::Format("%zu KB RAM + 512 KB flash", src->memory().ramSize() / 1024)});
            return true;
        }

#ifdef UNREALNG_HAVE_OPL4
        if (id == PeripheralId::MoonSound)
        {
            SoundChip_Moonsound* src = source.pSoundManager->getMoonSound();
            SoundChip_Moonsound* dst = target.pSoundManager->getMoonSound();
            if (!src || !dst)
                return false;
            const opl4::WaveMemory& sw = src->waveMemory();
            opl4::WaveMemory& dw = dst->waveMemory();
            if (sw.RomEnd() != dw.RomEnd() || sw.RamEnd() != dw.RamEnd())
            {
                items.push_back({"MoonSound wave memory", Status::Dropped,
                                 StringHelper::Format("source ROM %u + RAM %u KB, target ROM %u + RAM %u KB",
                                                      sw.RomEnd() / 1024, (sw.RamEnd() - sw.RomEnd()) / 1024,
                                                      dw.RomEnd() / 1024, (dw.RamEnd() - dw.RomEnd()) / 1024)});
                return false;
            }
            if (sw.RomEnd() > 0 && std::memcmp(sw.Data(), dw.Data(), sw.RomEnd()) != 0)
                items.push_back({"MoonSound wave ROM", Status::Note, "the target's wave ROM image differs"});
            if (apply && sw.RamEnd() > sw.RomEnd())
                dw.WriteSram(sw.RomEnd(), sw.Data() + sw.RomEnd(), sw.RamEnd() - sw.RomEnd());
            items.push_back({"MoonSound wave SRAM", Status::Copied,
                             StringHelper::Format("%u KB", (sw.RamEnd() - sw.RomEnd()) / 1024)});
            return true;
        }
#endif

        (void)source;
        (void)target;
        (void)apply;
        return true;
    }


    // --- Media ---------------------------------------------------------------------------------------------------

    /// What the media step left behind: the controllers whose media followed may take the source's state
    struct MediaOutcome
    {
        bool refused = false;
        std::string reason;
        bool floppiesFollowed = true;  ///< every source floppy is in the same drive on the target (or both empty)
        bool tapeFollowed = true;      ///< the tape too
    };

    /// Kinds that move: floppies and tapes live entirely in memory, detached from their files until saved.
    /// SD cards, hard disks and CDs are NOT moved (by design, for now): the target keeps its own
    bool MovesWithState(MediaKind kind)
    {
        return kind == MediaKind::Floppy || kind == MediaKind::Tape;
    }

    /// The target's copy stands for its own file next to the source's: game.trd -> game.pentagon-1a2b3c4d.trd.
    /// A save on the target never overwrites the source's image, even by accident
    std::string TransferredPath(const std::string& path, const std::string& tag)
    {
        const size_t slash = path.find_last_of("/\\");
        const size_t nameStart = slash == std::string::npos ? 0 : slash + 1;
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || dot <= nameStart)
            return path + "." + tag;
        return path.substr(0, dot) + "." + tag + path.substr(dot);
    }

    std::string TargetTag(EmulatorContext& target)
    {
        std::string model = "transfer";
        std::string id;
        if (target.pEmulator)
        {
            const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*target.pEmulator);
            if (identity.Valid && !identity.Model.empty())
                model = identity.Model;
            id = target.pEmulator->GetId().substr(0, 8);
        }
        std::transform(model.begin(), model.end(), model.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return id.empty() ? model : model + "-" + id;
    }

    /// The target's own copy of a source medium: same contents (unsaved writes included), clean, session access,
    /// standing for the postfixed file. nullptr for kinds that do not move
    std::unique_ptr<Medium> CopyMedium(const Medium& medium, const std::string& tag)
    {
        MediaSource source = medium.Source();
        if (source.type == MediaSourceType::File || source.type == MediaSourceType::Upload)
        {
            source.type = MediaSourceType::File;
            source.path = TransferredPath(source.path, tag);
        }
        else
        {
            // A disk built from a host folder, or a blank one: the copy is a blank-sourced disk (a save needs a path)
            source.type = MediaSourceType::Blank;
            source.path.clear();
        }

        if (const DiskImage* disk = medium.Floppy())
            return std::make_unique<Medium>(source, AccessMode::Session, medium.Format(), disk->Clone(source.path));
        if (const TapeImage* tape = medium.Tape())
            return std::make_unique<Medium>(source, medium.Access(), medium.Format(), std::make_unique<TapeImage>(*tape));
        return nullptr;
    }

    /// Check (apply = false) or copy (apply = true) the floppies and the tape into the same slots of the target.
    /// A target slot that would lose unsaved writes refuses the transfer; nothing of the target's is lost silently
    MediaOutcome TransferMedia(EmulatorContext& source, EmulatorContext& target, bool apply,
                               std::vector<MachineStateTransfer::Item>& items)
    {
        using Status = MachineStateTransfer::ItemStatus;
        MediaOutcome outcome;

        items.push_back({"SD / HDD / CD", Status::Note,
                         "not moved (by design, for now): the target keeps its own cards, hard disks and CDs, and their "
                         "controllers keep the target's state"});

        MediaManager* from = source.pMediaManager;
        MediaManager* to = target.pMediaManager;
        if (!from || !to)
            return outcome;

        const std::string tag = TargetTag(target);
        auto follow = [&outcome](MediaKind kind, bool followed) {
            if (followed)
                return;
            if (kind == MediaKind::Floppy)
                outcome.floppiesFollowed = false;
            else
                outcome.tapeFollowed = false;
        };

        // Source slots: their medium goes into the target's slot with the same id
        for (const SlotInfo& slot : from->List())
        {
            const MediaKind kind = slot.descriptor.kind;
            if (!MovesWithState(kind))
                continue;
            const std::string& id = slot.descriptor.id;
            const std::optional<SlotInfo> there = to->Info(id);

            if (!slot.present)
            {
                // An empty drive stays empty on the target: its controller state says so
                if (there && there->present)
                {
                    if (there->dirty)
                    {
                        outcome.refused = true;
                        outcome.reason = "the target's " + id + " has unsaved writes (" + there->changes +
                                         "); save or discard them first";
                        items.push_back({id, Status::Refused, outcome.reason});
                        return outcome;
                    }
                    if (apply)
                    {
                        const MediaResult ejected = to->Eject(id);
                        to->WaitApplied(id, 1000);
                        if (!ejected.Ok())
                        {
                            items.push_back({id, Status::Dropped, "could not empty the target's slot: " + ejected.message});
                            follow(kind, false);
                            continue;
                        }
                    }
                    items.push_back({id, Status::Copied, "empty, as on the source (the target's medium ejected)"});
                }
                continue;
            }

            if (!there)
            {
                items.push_back({id, Status::Dropped, "the target has no such slot; its controller keeps the target's state"});
                follow(kind, false);
                continue;
            }
            if (there->present && there->dirty)
            {
                outcome.refused = true;
                outcome.reason =
                    "the target's " + id + " has unsaved writes (" + there->changes + "); save or discard them first";
                items.push_back({id, Status::Refused, outcome.reason});
                return outcome;
            }

            const Medium* medium = from->GetMedium(id);
            if (!medium)
            {
                items.push_back({id, Status::Dropped, "the source's medium is still being inserted"});
                follow(kind, false);
                continue;
            }

            std::string detail = medium->Describe();
            if (medium->Source().type == MediaSourceType::File || medium->Source().type == MediaSourceType::Upload)
                detail += " -> " + TransferredPath(medium->Source().path, tag);
            detail += " (in-memory copy, clean; written only by an explicit save";
            if (medium->IsDirty())
                detail += "; carries the source's unsaved writes";
            detail += ")";

            if (apply)
            {
                std::unique_ptr<Medium> copy = CopyMedium(*medium, tag);
                InsertOptions options;
                options.immediate = true;  // no swap delay: the controller state expects the disk in place
                options.writeProtect = slot.writeProtect;
                const MediaResult inserted = copy ? to->Insert(id, std::move(copy), options)
                                                  : MediaResult::Fail(MediaError::KindMismatch, "no copy for this kind");
                to->WaitApplied(id, 1000);
                if (!inserted.Ok())
                {
                    items.push_back({id, Status::Dropped, "the target refused the copy: " + inserted.message});
                    follow(kind, false);
                    continue;
                }
            }
            items.push_back({id, Status::Copied, detail});
        }
        return outcome;
    }

    /// Controllers whose state belongs to their media: portable only when the media followed
    enum class Binding : uint8_t
    {
        Machine,   ///< model paging and the like: clone only
        Portable,  ///< independent of the machine
        Floppy,    ///< floppy controllers: when the disks followed
        Tape,      ///< the tape deck: when the tape followed
        Storage,   ///< SD / HDD / CD controllers: never (their media are not moved)
    };

    Binding BindingOf(PeripheralId id)
    {
        switch (id)
        {
            case PeripheralId::BetaDisk:
            case PeripheralId::Upd765:
                return Binding::Floppy;
            case PeripheralId::Tape:
                return Binding::Tape;
            case PeripheralId::AtaChannel:
            case PeripheralId::CdDrive:
            case PeripheralId::EvoSdCard:
                return Binding::Storage;
            default:
                return IsPortable(id) ? Binding::Portable : Binding::Machine;
        }
    }

    /// Check (apply = false) or transfer (apply = true) every device state
    void TransferDevices(EmulatorContext& source, EmulatorContext& target, bool clone, bool apply,
                         const MediaOutcome& media, std::vector<MachineStateTransfer::Item>& items)
    {
        using Status = MachineStateTransfer::ItemStatus;

        DeviceSet src(source);
        DeviceSet dst(target);
        if (!src.ok || !dst.ok)
        {
            items.push_back({"devices", Status::Dropped, !src.ok ? src.error : dst.error});
            return;
        }

        std::vector<uint8_t> buffer;
        for (uint8_t raw = 0; raw < static_cast<uint8_t>(PeripheralId::Count); raw++)
        {
            const auto id = static_cast<PeripheralId>(raw);
            ttd::TTDSerializable* from = src.registry.GetDevice(id);
            // The RZX playback position is time travel's, not the machine's: a playback stays on its machine
            if (!from || id == PeripheralId::RzxPlayback)
                continue;

            const std::string name = DeviceLabel(id, from);
            const Binding binding = BindingOf(id);
            if (binding == Binding::Machine && !clone)
            {
                items.push_back({name, Status::Dropped, "machine-bound state; not moved between models (the target keeps its own)"});
                continue;
            }
            if (binding == Binding::Storage)
            {
                items.push_back({name, Status::Dropped,
                                 "SD / HDD / CD controller: its media are not moved, so it keeps the target's state"});
                continue;
            }
            if ((binding == Binding::Floppy && !media.floppiesFollowed) || (binding == Binding::Tape && !media.tapeFollowed))
            {
                items.push_back({name, Status::Dropped, "its media did not follow; it keeps the target's state"});
                continue;
            }

            ttd::TTDSerializable* to = dst.registry.GetDevice(id);
            if (!to)
            {
                items.push_back({name, Status::Dropped, "the target has no such device (or another kind in its slot)"});
                continue;
            }

            const size_t size = from->TTDStateSize();
            if (to->TTDStateSize() != size)
            {
                items.push_back({name, Status::Dropped,
                                 StringHelper::Format("device configuration differs (state %zu vs %zu bytes)", size,
                                                      to->TTDStateSize())});
                continue;
            }

            if (!CopyDeviceMemory(id, source, target, apply, items))
            {
                items.push_back({name, Status::Dropped, "its memory does not fit the target's"});
                continue;
            }

            if (apply)
            {
                // The FDC clock policy is the machine's (Pentagon fixed 1 MHz, ZX-Evo latched, ...): the target keeps it
                const bool betaDisk = id == PeripheralId::BetaDisk && target.pBetaDisk;
                const FdcClockPolicy policy = betaDisk ? target.pBetaDisk->GetClockPolicy() : FdcClockPolicy::Fixed1MHz;
                buffer.resize(size);
                from->TTDSaveState(buffer.data());
                to->TTDLoadState(buffer.data());
                if (betaDisk && target.pBetaDisk->GetClockPolicy() != policy)
                    target.pBetaDisk->SetClockPolicy(policy);
                if (id == PeripheralId::BetaDisk)
                    std::memcpy(target.emulatorState.wd_shadow, source.emulatorState.wd_shadow,
                                sizeof(target.emulatorState.wd_shadow));
            }
            items.push_back({name, Status::Copied, StringHelper::Format("%zu bytes of state", size)});
        }

        // Devices the target has and the source lacks keep the target's (reset) state
        for (uint8_t raw = 0; raw < static_cast<uint8_t>(PeripheralId::Count); raw++)
        {
            const auto id = static_cast<PeripheralId>(raw);
            ttd::TTDSerializable* to = dst.registry.GetDevice(id);
            if (to && id != PeripheralId::RzxPlayback && !src.registry.GetDevice(id) &&
                (clone || BindingOf(id) != Binding::Machine))
                items.push_back({DeviceLabel(id, to), Status::Note, "only the target has it; it starts from reset"});
        }
    }

    // --- Machine state ---------------------------------------------------------------------------------------

    void ResyncScreen(EmulatorContext& target)
    {
        Screen* screen = target.pScreen;
        if (!screen)
            return;
        screen->InitRaster();
        screen->SetActiveScreen((target.emulatorState.p7FFD & P7FFD_SCREEN) ? SCREEN_SHADOW : SCREEN_NORMAL);
        screen->SetBorderColor(target.emulatorState.pFE & 0x07);
        screen->ResetPrevTstate();
        screen->InitFrame();
    }

    void CopyPage(EmulatorContext& source, EmulatorContext& target, uint16_t page)
    {
        const uint8_t* from = source.pMemory->RAMPageAddress(page);
        uint8_t* to = target.pMemory->RAMPageAddress(page);
        if (from && to)
            std::memcpy(to, from, PAGE_SIZE);
    }

    bool SameFrameGeometry(const CONFIG& a, const CONFIG& b)
    {
        return a.frame == b.frame && a.intstart == b.intstart && a.intlen == b.intlen;
    }

    /// Paging of a cross-model target, through its own port decoder
    void ApplyCrossPaging(const SourceAnalysis& a, EmulatorContext& source, EmulatorContext& target)
    {
        Memory& memory = *target.pMemory;
        PortDecoder& ports = *target.pPortDecoder;
        EmulatorState& state = target.emulatorState;
        const EmulatorState& from = source.emulatorState;
        const PagingFamily targetFamily = FamilyOf(target.config);
        const uint16_t pc = source.pCore->GetZ80()->pc;

        ports.UnlockPaging();

        if (targetFamily == PagingFamily::Spectrum48)
        {
            memory.SetRAMPageToBank1(5);
            memory.SetRAMPageToBank2(2);
            memory.SetRAMPageToBank3(0);
            memory.SetROM48k();
            return;
        }

        const bool mode48 = a.need == Need::Mode48;
        const bool basic48 = mode48 || a.rom == RomRole::Basic48;

        if (a.need == Need::SameFamily)
        {
            // The family's own latches, extensions first: #7FFD may lock them
            if (targetFamily == PagingFamily::Plus3 || targetFamily == PagingFamily::Scorpion)
            {
                ports.DecodePortOut(0x1FFD, from.p1FFD, pc);
                state.p1FFD = from.p1FFD;
            }
            if (targetFamily == PagingFamily::Pentagon && target.config.ramsize >= 1024)
            {
                ports.DecodePortOut(0xEFF7, from.pEFF7, pc);
                state.pEFF7 = from.pEFF7;
            }
            ports.DecodePortOut(0x7FFD, from.p7FFD, pc);
            state.p7FFD = from.p7FFD;
            memory.UpdateZ80Banks();
            return;
        }

        uint8_t p7FFD = mode48 ? static_cast<uint8_t>(P7FFD_LOCK | P7FFD_ROM) : static_cast<uint8_t>(from.p7FFD & P7FFD_128_BITS);
        p7FFD = static_cast<uint8_t>((p7FFD & ~P7FFD_ROM) | (basic48 ? P7FFD_ROM : 0));

        if (targetFamily == PagingFamily::Plus3)
        {
            // +3 ROMs: 0 editor, 3 48 BASIC - ROM high bit and #7FFD bit 4 together
            const uint8_t p1FFD = basic48 ? P1FFD_PLUS3_ROM_HIGH : 0x00;
            ports.DecodePortOut(0x1FFD, p1FFD, pc);
            state.p1FFD = p1FFD;
        }
        else if (targetFamily == PagingFamily::Scorpion)
        {
            ports.DecodePortOut(0x1FFD, 0x00, pc);
            state.p1FFD = 0x00;
        }
        else if (targetFamily == PagingFamily::Pentagon && target.config.ramsize >= 1024)
        {
            // 128K compatibility: #7FFD bit 5 is the lock, as the source meant it
            ports.DecodePortOut(0xEFF7, PEFF7_P1024_COMPAT, pc);
            state.pEFF7 = PEFF7_P1024_COMPAT;
        }

        ports.DecodePortOut(0x7FFD, p7FFD, pc);
        state.p7FFD = p7FFD;
        memory.UpdateZ80Banks();
    }

    void ApplyTrdos(EmulatorContext& source, EmulatorContext& target)
    {
        if (source.emulatorState.flags & CF_TRDOS)
        {
            target.emulatorState.flags |= CF_TRDOS;
            target.pMemory->SetROMDOS();
        }
        else
        {
            target.emulatorState.flags &= ~CF_TRDOS;
        }
    }

    void ApplyCpu(EmulatorContext& source, EmulatorContext& target, bool keepPosition)
    {
        Z80* from = source.pCore->GetZ80();
        Z80* to = target.pCore->GetZ80();
        ttd::RestoreCpuState(ttd::CaptureCpuState(*static_cast<Z80State*>(from)), static_cast<Z80State*>(to));
        to->t = keepPosition ? from->t : 0;
        to->RecomputeFrameTiming();
    }
} // namespace

// --- Report ----------------------------------------------------------------------------------------------------

size_t MachineStateTransfer::Report::Count(ItemStatus status) const
{
    return static_cast<size_t>(std::count_if(items.begin(), items.end(),
                                             [status](const Item& item) { return item.status == status; }));
}

std::string MachineStateTransfer::Report::ToString() const
{
    static const char* const kStatus[] = {"copied", "dropped", "refused", "note"};
    std::string out = ok ? (clone ? "transfer: ok (clone)\n" : "transfer: ok (cross-model)\n")
                         : "transfer: refused - " + reason + "\n";
    for (const Item& item : items)
    {
        out += "  [" + std::string(kStatus[static_cast<size_t>(item.status)]) + "] " + item.name;
        if (!item.detail.empty())
            out += ": " + item.detail;
        out += "\n";
    }
    return out;
}

// --- Check / Apply ---------------------------------------------------------------------------------------------

namespace
{
    /// The machine part of the plan (memory map, CPU, TR-DOS); devices are added separately
    bool PlanMachine(EmulatorContext& source, EmulatorContext& target, MachineStateTransfer::Report& report,
                     SourceAnalysis& a)
    {
        using Status = MachineStateTransfer::ItemStatus;

        if (&source == &target)
        {
            report.reason = "source and target are the same machine";
            return false;
        }
        if (!source.pCore || !source.pMemory || !target.pCore || !target.pMemory || !target.pPortDecoder)
        {
            report.reason = "an instance is not initialized";
            return false;
        }

        report.clone = source.config.mem_model == target.config.mem_model &&
                       source.config.ramsize == target.config.ramsize;

        a = Analyze(source);
        if (!report.clone)
        {
            std::string why;
            if (!TargetAccepts(a, source, target, why))
            {
                report.reason = why;
                report.items.push_back({"memory map", Status::Refused, why});
                return false;
            }
        }

        if ((source.emulatorState.flags & CF_TRDOS) && !(target.config.trdos_present && target.pBetaDisk))
        {
            report.reason = "the source is in TR-DOS; the target has no Beta 128 interface";
            report.items.push_back({"TR-DOS", Status::Refused, report.reason});
            return false;
        }

        if (report.clone)
        {
            report.items.push_back({"RAM", Status::Copied, std::to_string(ClonePages(source.config)) + " pages"});
            report.items.push_back({"chipset latches and frame position", Status::Copied, ""});
        }
        else
        {
            const bool to48 = FamilyOf(target.config) == PagingFamily::Spectrum48;
            const std::string pages = (a.need == Need::Mode48 || to48) ? "pages 5, 2, 0"
                                      : a.need == Need::Mode128     ? "pages 0-7"
                                                                    : std::to_string(RamPages(source.config)) + " pages";
            report.items.push_back({"RAM", Status::Copied, pages});
            report.items.push_back({"paging", Status::Copied,
                                    std::string(FamilyName(a.family)) + " state replayed through the " +
                                        FamilyName(FamilyOf(target.config)) + " port decoder"});
            if (!SameFrameGeometry(source.config, target.config))
                report.items.push_back({"frame position", Status::Note,
                                        "frame geometry differs; the target starts at its own frame start"});
            if (source.pCore->GetZ80()->pc < 0x4000 && FamilyOf(source.config) != FamilyOf(target.config))
                report.items.push_back({"ROM", Status::Note,
                                        "the CPU is in ROM code; the target's ROM set differs from the source's"});
        }
        report.items.push_back({"CPU", Status::Copied, "registers, MEMPTR, Q, interrupt state"});
        if (source.emulatorState.flags & CF_TRDOS)
            report.items.push_back({"TR-DOS", Status::Copied, "DOS ROM paged in"});
        return true;
    }
} // namespace

MachineStateTransfer::Report MachineStateTransfer::Check(EmulatorContext& source, EmulatorContext& target)
{
    Report report;
    SourceAnalysis a;
    if (!PlanMachine(source, target, report, a))
        return report;

    const MediaOutcome media = TransferMedia(source, target, false, report.items);
    if (media.refused)
    {
        report.reason = media.reason;
        return report;
    }
    TransferDevices(source, target, report.clone, false, media, report.items);
    report.ok = true;
    return report;
}

MachineStateTransfer::Report MachineStateTransfer::Apply(EmulatorContext& source, EmulatorContext& target,
                                                         const Options& options)
{
    Report report;
    SourceAnalysis a;
    if (!PlanMachine(source, target, report, a))
        return report;

    const bool keepPosition = options.keepFramePositionWhenTimingMatches &&
                              SameFrameGeometry(source.config, target.config);

    // Media first, checked before anything changes: a target disk with unsaved writes refuses the transfer
    {
        std::vector<Item> probe;
        const MediaOutcome check = TransferMedia(source, target, false, probe);
        if (check.refused)
        {
            report.reason = check.reason;
            report.items = std::move(probe);
            return report;
        }
    }

    // A clean machine first: devices the source lacks start from reset (as a snapshot load does)
    target.pCore->Reset();

    // The floppies and the tape go in before the controllers take the source's state
    const MediaOutcome media = TransferMedia(source, target, true, report.items);

    if (report.clone)
    {
        // As a TTD checkpoint restore: chipset latches, CPU and position, devices (model paging state included,
        // before the banks are rebuilt from it), banks, RAM
        const Z80* from = source.pCore->GetZ80();
        ttd::RestoreChipsetState(ttd::CaptureChipsetState(source.emulatorState, from->t), &target.emulatorState);
        if (!keepPosition)
            target.emulatorState.t_states += from->t;  // same instant on both machines (see the cross-model path)
        ApplyCpu(source, target, keepPosition);
        TransferDevices(source, target, true, true, media, report.items);
        target.pMemory->UpdateZ80Banks();
        for (uint16_t page = 0; page < ClonePages(source.config); page++)
            CopyPage(source, target, page);
    }
    else
    {
        ApplyCrossPaging(a, source, target);

        const bool to48 = FamilyOf(target.config) == PagingFamily::Spectrum48;
        if (a.need == Need::Mode48 || to48)
        {
            for (uint16_t page : {uint16_t(5), uint16_t(2), uint16_t(0)})
                CopyPage(source, target, page);
        }
        else
        {
            const uint16_t pages = a.need == Need::Mode128 ? uint16_t(8) : RamPages(source.config);
            for (uint16_t page = 0; page < pages; page++)
                CopyPage(source, target, page);
        }

        EmulatorState& state = target.emulatorState;
        state.pFE = source.emulatorState.pFE;
        state.border_attr = source.emulatorState.border_attr;

        // The target takes the source's time axis: the floppy controller and the tape deck keep absolute t-state
        // stamps (the disk's rotation phase, the pulse in flight), so "now" must be the same instant on both.
        // When the target restarts its frame, the source's in-frame position moves into the counter
        state.t_states = source.emulatorState.t_states + (keepPosition ? 0 : source.pCore->GetZ80()->t);
        state.frame_counter = source.emulatorState.frame_counter;

        ApplyCpu(source, target, keepPosition);
        TransferDevices(source, target, false, true, media, report.items);

        // The banks from the latches, then the DOS ROM over them when the source is in TR-DOS
        target.pMemory->UpdateZ80Banks();
        ApplyTrdos(source, target);
    }

    ResyncScreen(target);
    report.ok = true;
    return report;
}

// --- Emulator level --------------------------------------------------------------------------------------------

MachineStateTransfer::Report MachineStateTransfer::Transfer(Emulator& source, Emulator& target, const Options& options)
{
    Report report;
    if (&source == &target)
    {
        report.reason = "source and target are the same instance";
        return report;
    }

    // A ZX-Poly module runs in lockstep with three others: moving one module's state in or out
    // would desynchronize the group
    EmulatorManager* manager = EmulatorManager::GetInstance();
    if (manager->GetZXPolyGroup(source.GetId()) || manager->GetZXPolyGroup(target.GetId()))
    {
        report.reason = "ZX-Poly modules are not transferable: the group runs four machines in lockstep";
        return report;
    }

    const std::string guard = target.RecordingGuard(ttd::TTDGuardedAction::SwitchModel);
    if (!guard.empty())
    {
        report.reason = guard;
        return report;
    }

    EmulatorContext* sourceContext = source.GetContext();
    EmulatorContext* targetContext = target.GetContext();
    if (!sourceContext || !targetContext)
    {
        report.reason = "an instance has no context";
        return report;
    }

    // Both machines stand still while their state moves
    const bool sourceWasRunning = source.IsRunning() && !source.IsPaused();
    const bool targetWasRunning = target.IsRunning() && !target.IsPaused();
    if (sourceWasRunning)
    {
        source.Pause(false);
        source.WaitForPauseConfirmation(1000);
    }
    if (targetWasRunning)
    {
        target.Pause(false);
        target.WaitForPauseConfirmation(1000);
    }

    report = Check(*sourceContext, *targetContext);
    if (report.ok)
    {
        // The target leaves its own history: a transfer teleports its state, like a snapshot load
        if (targetContext->pTimeTravelHooks)
            targetContext->pTimeTravelHooks->OnModelTransfer("state-transfer");

        report = Apply(*sourceContext, *targetContext, options);
        if (report.ok)
            target.RestartFrame();
    }

    if (targetWasRunning)
        target.Resume(false);
    if (sourceWasRunning)
        source.Resume(false);
    return report;
}

void MachineStateTransfer::FitSourceDevices(const CONFIG& source, CONFIG& target)
{
    target.sound.turboSoundKind = source.sound.turboSoundKind;
    target.sound.gsTypeKind = source.sound.gsTypeKind;
    target.sound.gsRamKB = source.sound.gsRamKB;
    target.sound.moonsound = source.sound.moonsound;
    target.sound.covoxFB = source.sound.covoxFB;
    target.sound.covoxDD = source.sound.covoxDD;
    target.sound.sd = source.sound.sd;
    target.sound.sdMode = source.sound.sdMode;
    target.moonsound = source.moonsound;
    target.ngs = source.ngs;
    target.trdos_present = source.trdos_present || target.trdos_present;
}

std::shared_ptr<Emulator> MachineStateTransfer::TransferToNewInstance(Emulator& source, const std::string& modelName,
                                                                      uint32_t ramSizeKB, Report& report,
                                                                      const Options& options,
                                                                      const std::string& symbolicId)
{
    report = Report{};
    EmulatorContext* sourceContext = source.GetContext();
    if (ZXPolyGroup::FindConfiguration(modelName))
    {
        report.reason = "ZX-Poly configuration '" + modelName + "' is not a transfer target: its four modules run in lockstep";
        return nullptr;
    }

    const TMemModel* model = Config::FindModelByShortName(modelName);
    if (!sourceContext || !model)
    {
        report.reason = !model ? "unknown model '" + modelName + "'" : "the source has no context";
        return nullptr;
    }

    const uint32_t ram = ramSizeKB ? ramSizeKB : model->defaultRAM;
    const CONFIG sourceConfig = sourceContext->config;
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string createError;
    std::shared_ptr<Emulator> target = manager->CreateEmulatorWithModelAndRAM(
        symbolicId.empty() ? "transfer-" + modelName : symbolicId, modelName, ram, LoggerLevel::LogWarning,
        &createError, [sourceConfig](CONFIG& config) { FitSourceDevices(sourceConfig, config); });
    if (!target)
    {
        report.reason = createError.empty() ? "the target instance could not be created" : createError;
        return nullptr;
    }

    report = Transfer(source, *target, options);
    if (!report.ok)
    {
        manager->RemoveEmulator(target->GetId());
        return nullptr;
    }
    return target;
}
