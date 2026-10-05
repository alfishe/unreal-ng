#include "emulator/media/mediatargets.h"

#include "emulator/media/composedescriptor.h"

#include <algorithm>
#include <cstring>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/cd/audiofolderdisc.h"
#include "emulator/io/storage/cd/cdimageformats.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/media/blockadvisory.h"
#include "emulator/media/floppyformats.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/mediamanager.h"

namespace
{
    /// Enough for sector 0 and the ISO 9660 primary volume descriptor (sector 16 of 2048 bytes)
    constexpr size_t kHeadBytes = 0x8000 + 2048;
    constexpr size_t kIsoMarkOffset = 0x8001;

    bool Contains(const std::vector<std::string>& list, const std::string& value)
    {
        return std::find(list.begin(), list.end(), value) != list.end();
    }

    bool HasTag(const SlotInfo& info, const char* tag)
    {
        return Contains(info.tags, tag);
    }

    /// A block image's two kinds, in the order its extension suggests: a hard-disk
    /// extension (.hdf, .vhd, ...) is a hard disk first, anything else a card first
    void AddBlockKinds(FileClass& file, const std::string& ext)
    {
        if (HddImageFormats::IsHardDiskExtension(ext))
            file.kinds = {FileKind::Hdd, FileKind::SdCard};
        else
            file.kinds = {FileKind::SdCard, FileKind::Hdd};
    }

    /// Does a slot take a file of this kind
    bool Takes(const SlotInfo& info, FileKind kind)
    {
        const MediaKind slotKind = info.descriptor.kind;
        // A block slot that says neither "ide" nor "sd" takes either image
        const bool untaggedBlock = slotKind == MediaKind::Block && !HasTag(info, "ide") && !HasTag(info, "hdd") &&
                                   !HasTag(info, "sd");
        switch (kind)
        {
            case FileKind::Floppy: return slotKind == MediaKind::Floppy;
            case FileKind::Tape: return slotKind == MediaKind::Tape;
            case FileKind::Optical: return slotKind == MediaKind::Optical;
            case FileKind::Hdd: return slotKind == MediaKind::Block && (HasTag(info, "ide") || HasTag(info, "hdd") || untaggedBlock);
            case FileKind::SdCard: return slotKind == MediaKind::Block && (HasTag(info, "sd") || untaggedBlock);
            default: return false;
        }
    }

    /// The chooser's order within a kind: the slot the machine boots from or calls
    /// primary first, an add-on's slot last. Whether a slot is occupied does not
    /// matter: the tiles keep their place while the media change
    int Rank(const SlotInfo& info)
    {
        if (HasTag(info, "primary") || HasTag(info, "boot"))
            return 0;
        if (HasTag(info, "addon"))
            return 2;
        return 1;
    }

    std::string NoSlotReason(const FileClass& file, const std::vector<SlotInfo>& slots)
    {
        if (file.folder)
            return "no slot on this machine takes a folder";
        const bool hdd = file.Is(FileKind::Hdd);
        const bool card = file.Is(FileKind::SdCard);
        if (hdd && card)
            return "no SD card slot or hard-disk unit on this machine";
        if (file.Is(FileKind::Optical))
        {
            const bool ide = std::any_of(slots.begin(), slots.end(), [](const SlotInfo& s) { return HasTag(s, "ide"); });
            return ide ? "no CD-ROM drive on this machine (its IDE units are hard disks)"
                       : "no CD-ROM drive: this machine has no IDE board";
        }
        if (hdd)
            return "no hard-disk unit on this machine";
        if (card)
            return "no SD card slot on this machine";
        if (file.Is(FileKind::Floppy))
            return "no floppy drive on this machine";
        if (file.Is(FileKind::Tape))
            return "no tape deck on this machine";
        return "no slot on this machine takes it";
    }

    std::string NoMachineReason(const FileClass& file)
    {
        if (file.Is(FileKind::Optical))
            return "start a machine with a CD-ROM drive first (ZX-Evo): a CD image does not say which machine it is for";
        if (file.Is(FileKind::Hdd) || file.Is(FileKind::SdCard))
            return "start a machine with an IDE board or an SD card slot first (ZX-Evo, Profi, Pentagon): "
                   "the image does not say which machine it is for";
        if (file.Is(FileKind::Symbols))
            return "labels need a running machine";
        return "start a machine first";
    }
}  // namespace

const char* FileKindName(FileKind kind)
{
    switch (kind)
    {
        case FileKind::Floppy: return "floppy";
        case FileKind::Tape: return "tape";
        case FileKind::Hdd: return "hdd";
        case FileKind::SdCard: return "sdcard";
        case FileKind::Optical: return "optical";
        case FileKind::Snapshot: return "snapshot";
        case FileKind::Rzx: return "rzx";
        case FileKind::ZxPoly: return "zxpoly";
        case FileKind::Symbols: return "symbols";
        case FileKind::Rom: return "rom";
    }
    return "unknown";
}

bool MediaTargets::IsMedium(FileKind kind)
{
    return kind == FileKind::Floppy || kind == FileKind::Tape || kind == FileKind::Hdd || kind == FileKind::SdCard ||
           kind == FileKind::Optical;
}

bool FileClass::Is(FileKind kind) const
{
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

std::string MediaPlan::SlotList() const
{
    std::string list;
    for (const MediaTarget& target : targets)
    {
        if (target.action != MediaTarget::Action::Insert)
            continue;
        list += (list.empty() ? "" : ", ") + target.slotId;
    }
    return list;
}

FileClass MediaTargets::Classify(const std::string& path)
{
    FileClass file;
    file.path = path;
    const std::string ext = StringHelper::ToLower(FileHelper::GetFileExtension(path));

    // A folder becomes a TR-DOS disk, a FAT volume or a tape: whichever slot takes one. A folder
    // with MP3 / FLAC / WAV files is offered to a CD drive first, as an audio CD
    if (FileHelper::IsFolder(path))
    {
        file.folder = true;
        file.kinds = {FileKind::Floppy, FileKind::SdCard, FileKind::Hdd, FileKind::Tape};
        file.format = "folder";
        file.evidence.push_back("a folder");
        if (AudioFolderDisc::HasAudioFiles(path))
        {
            file.kinds.insert(file.kinds.begin(), FileKind::Optical);
            file.format = "audio-folder";
            file.evidence.push_back("MP3 / FLAC / WAV files in it (an audio CD for a CD drive)");
        }
        return file;
    }
    if (!FileHelper::FileExists(path))
    {
        file.evidence.push_back("file not found");
        return file;
    }
    if (ComposeDescriptor::IsDescriptorName(path))
    {
        // A composite medium: a FAT volume built from the layers it names
        file.kinds = {FileKind::SdCard, FileKind::Hdd};
        file.format = "compose";
        file.evidence.push_back("a composition descriptor (*.ucompose)");
        return file;
    }

    const uint64_t size = FileHelper::GetFileSize(path);
    std::vector<uint8_t> head(static_cast<size_t>(std::min<uint64_t>(size, kHeadBytes)));
    if (!head.empty() && FileHelper::ReadFileToBuffer(path, head.data(), head.size()) != head.size())
        head.clear();

    auto set = [&file](std::vector<FileKind> kinds, std::string format, std::string why) {
        file.kinds = std::move(kinds);
        file.format = std::move(format);
        file.evidence.push_back(std::move(why));
        return file;
    };

    // Signatures that cannot be anything else
    if (head.size() >= 4 && std::memcmp(head.data(), "ZXST", 4) == 0)
        return set({FileKind::Snapshot}, "szx", "ZXST signature");
    if (head.size() >= 4 && std::memcmp(head.data(), "RZX!", 4) == 0)
        return set({FileKind::Rzx}, "rzx", "RZX! signature");

    // Files only their extension tells apart (no medium has these extensions)
    if (ext == "sna" || ext == "z80" || ext == "szx" || ext == "spg")
        return set({FileKind::Snapshot}, ext, "extension ." + ext);
    if (ext == "rzx")
        return set({FileKind::Rzx}, ext, "extension .rzx");
    if (ext == "zxp" || ext == "prom")
        return set({FileKind::ZxPoly}, ext, "extension ." + ext);
    if (ext == "map" || ext == "sym")
        return set({FileKind::Symbols}, ext, "extension ." + ext);
    if (ext == "rom")
        return set({FileKind::Rom}, ext, "extension .rom");

    // A CD before every floppy rule: its first 32 KB are zero or a boot area, and a
    // byte there may well look like a TR-DOS id
    if (head.size() > kIsoMarkOffset + 5 && std::memcmp(head.data() + kIsoMarkOffset, "CD001", 5) == 0)
        return set({FileKind::Optical}, "iso", "CD001 at #8001 (ISO 9660)");

    // A CUE sheet, or a raw image of 2352-byte frames (a sync pattern at 0): a CD
    if (ext == "cue")
        return set({FileKind::Optical}, "cue", "extension .cue (CUE sheet)");
    if (head.size() >= sizeof(cd::kSync) && std::memcmp(head.data(), cd::kSync, sizeof(cd::kSync)) == 0 && size % cd::kFrameBytes == 0)
        return set({FileKind::Optical}, "bin", "CD sync pattern at 0 (raw 2352-byte frames)");

    const std::string hdd = HddImageFormats::Probe(path);
    if (hdd == "chd" && CdImageFormats::IsCdChd(path))
        return set({FileKind::Optical}, "chd", "MComprHD signature with CD track metadata (MAME CD-ROM CHD)");
    if (hdd == "chd")
    {
        // MAME keeps every hard disk and SD card as a CHD: either slot kind, a hard disk first
        file.kinds = {FileKind::Hdd, FileKind::SdCard};
        file.format = "chd";
        file.evidence.push_back("MComprHD signature (MAME CHD)");
        return file;
    }
    if (hdd == "hdf" || hdd == "vhd")
        return set({FileKind::Hdd}, hdd, hdd + " header");
    if (hdd == "hdi")
        return set({FileKind::Hdd}, hdd, "extension .hdi");

    // A tape by its extension: tape images have no common signature
    if (Contains(MediaFormatRegistry::Extensions(MediaKind::Tape), ext))
        return set({FileKind::Tape}, ext, "extension ." + ext);

    if (const std::string floppy = FloppyFormats::Probe(path); !floppy.empty())
        return set({FileKind::Floppy}, floppy, "floppy image (" + floppy + ")");

    // A block image: sector 0 says FAT or a partition table, else the extension
    const SectorZeroLayout layout = ClassifySectorZero(head.data(), head.size());
    if (layout == SectorZeroLayout::FatVolume)
    {
        AddBlockKinds(file, ext);
        file.format = "fat";
        file.evidence.push_back("FAT boot sector at sector 0");
        return file;
    }
    if (layout == SectorZeroLayout::PartitionTable)
    {
        AddBlockKinds(file, ext);
        file.format = "mbr";
        file.evidence.push_back("MBR partition table at sector 0");
        return file;
    }
    if (Contains(MediaFormatRegistry::Extensions(MediaKind::Block), ext))
    {
        AddBlockKinds(file, ext);
        file.format = "raw";
        file.evidence.push_back("extension ." + ext);
        return file;
    }
    file.evidence.push_back("no signature, no known extension");
    return file;
}

MediaPlan MediaTargets::Plan(EmulatorContext* context, const FileClass& file)
{
    MediaPlan plan;
    plan.file = file;
    const size_t slash = file.path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? file.path : file.path.substr(slash + 1);

    if (file.kinds.empty())
    {
        plan.refusal = file.evidence == std::vector<std::string>{"file not found"}
                           ? "file not found: '" + file.path + "'"
                           : "'" + name + "' is not a medium, snapshot or recording this emulator knows";
        return plan;
    }

    // Files the caller loads itself: they say which machine they are for
    const FileKind first = file.kinds.front();
    if (first == FileKind::Snapshot || first == FileKind::Rzx || first == FileKind::ZxPoly ||
        (first == FileKind::Symbols && context))
    {
        MediaTarget load;
        load.action = MediaTarget::Action::Load;
        load.as = first;
        load.label = std::string("Load (") + FileKindName(first) + ")";
        plan.targets.push_back(load);
        plan.defaultTarget = 0;
        return plan;
    }
    if (first == FileKind::Rom)
    {
        plan.refusal = "a ROM image is set in the machine configuration, not loaded";
        return plan;
    }

    MediaManager* manager = context ? context->pMediaManager : nullptr;
    if (!manager)
    {
        // No machine: only an image that has always started one (a floppy boots a
        // Pentagon 128, a tape the default machine); a CD, hard-disk or card image
        // does not say which machine it is for
        if (!file.folder && (first == FileKind::Floppy || first == FileKind::Tape))
        {
            MediaTarget start;
            start.action = MediaTarget::Action::NewMachine;
            start.as = first;
            start.model = first == FileKind::Floppy ? "PENTAGON" : "";
            start.label = first == FileKind::Floppy ? "New Pentagon 128, drive A" : "New machine, tape";
            start.autostart = first == FileKind::Floppy;
            plan.targets.push_back(start);
            plan.defaultTarget = 0;
            return plan;
        }
        plan.refusal = NoMachineReason(file);
        return plan;
    }

    std::vector<SlotInfo> slots = manager->List();
    slots.erase(std::remove_if(slots.begin(), slots.end(), [](const SlotInfo& s) { return s.detached; }), slots.end());

    for (FileKind kind : file.kinds)
    {
        if (!MediaTargets::IsMedium(kind))
            continue;
        std::vector<const SlotInfo*> matches;
        for (const SlotInfo& info : slots)
        {
            if (!Takes(info, kind) || (file.folder && !info.descriptor.acceptsFolder))
                continue;
            const bool listed = std::any_of(plan.targets.begin(), plan.targets.end(),
                                            [&info](const MediaTarget& t) { return t.slotId == info.descriptor.id; });
            if (!listed)
                matches.push_back(&info);
        }
        std::stable_sort(matches.begin(), matches.end(),
                         [](const SlotInfo* a, const SlotInfo* b) { return Rank(*a) < Rank(*b); });
        for (const SlotInfo* info : matches)
        {
            MediaTarget target;
            target.action = MediaTarget::Action::Insert;
            target.as = kind;
            target.slotId = info->descriptor.id;
            target.label = info->descriptor.label.empty() ? info->descriptor.id : info->descriptor.label;
            if (info->present || info->pending)
                target.occupiedBy = info->source.empty() ? "a medium" : info->source;
            target.dirty = info->dirty;
            target.autostart = kind == FileKind::Floppy && HasTag(*info, "boot") && HasTag(*info, "trdos");
            plan.targets.push_back(target);
        }
    }

    if (plan.targets.empty())
    {
        plan.refusal = NoSlotReason(file, slots);
        return plan;
    }
    if (plan.targets.size() == 1)
    {
        plan.defaultTarget = 0;
    }
    else if (first == FileKind::Floppy && !file.folder)
    {
        // The drop shortcut: a floppy image goes to the drive the machine boots from
        for (size_t i = 0; i < plan.targets.size(); i++)
        {
            const auto info = std::find_if(slots.begin(), slots.end(), [&](const SlotInfo& s) {
                return s.descriptor.id == plan.targets[i].slotId;
            });
            if (plan.targets[i].as == FileKind::Floppy && info != slots.end() && HasTag(*info, "boot"))
            {
                plan.defaultTarget = static_cast<int>(i);
                break;
            }
        }
    }
    return plan;
}

MediaReply MediaTargets::Apply(EmulatorContext* context, const MediaPlan& plan, size_t index,
                               const std::map<std::string, std::string>& options)
{
    MediaReply reply;
    if (index >= plan.targets.size())
    {
        reply.result = MediaResult::Fail(MediaError::BadRequest,
                                         plan.Refused() ? plan.refusal : "no target " + std::to_string(index));
        return reply;
    }
    const MediaTarget& target = plan.targets[index];
    if (target.action != MediaTarget::Action::Insert)
    {
        reply.result = MediaResult::Fail(MediaError::NotSupported, "the caller loads snapshots and starts machines");
        return reply;
    }
    if (!context || !context->pMediaManager)
    {
        reply.result = MediaResult::Fail(MediaError::BadRequest, "no machine");
        return reply;
    }
    MediaRequest request;
    request.verb = "insert";
    request.selector = target.slotId;
    request.path = plan.file.path;
    request.options = options;
    return MediaControl(context).Execute(request);
}
