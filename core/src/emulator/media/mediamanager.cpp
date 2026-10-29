#include "stdafx.h"

#include "mediamanager.h"

#include "emulator/media/composedescriptor.h"
#include "emulator/media/compositemediumfactory.h"
#include "emulator/media/sessiondelta.h"
#include "emulator/media/mediachanges.h"
#include "emulator/media/writeback.h"
#include "emulator/io/storage/commitjournal.h"
#include "emulator/io/storage/compose/graftvolume.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/io/storage/partitioneddisk.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include <set>
#include <atomic>
#include <fstream>
#include <random>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <system_error>

#include "common/logger.h"
#include "emulator/io/storage/hostwritehold.h"
#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/io/storage/mediareadtap.h"
#include "emulator/media/mediawritegate.h"
#include "emulator/media/guestprogramwatch.h"
#include "common/modulelogger.h"
#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/blockadvisory.h"
#include "emulator/media/floppyformats.h"
#include "emulator/notifications.h"
#include "loaders/tape/writer_tap.h"
#include "loaders/tape/writer_tzx.h"

namespace
{
    const char* FatTypeName(FatType fs)
    {
        return fs == FatType::Fat32 ? "fat32" : "fat16";
    }

    std::string FatTypeNames(const std::vector<FatType>& list)
    {
        std::string names;
        for (FatType fs : list)
        {
            if (!names.empty())
                names += " or ";
            names += FatTypeName(fs);
        }
        return names.empty() ? "no folder volume" : names;
    }

    /// FR-45: writes.commit none (the default) keeps the sources read-only; a request may choose all / partial only
    /// where the descriptor allows a commit at all
    std::optional<std::string> CommitRefusal(const CompositeInfo& composite)
    {
        if (composite.writesCommit != "none")
            return std::nullopt;
        return "writes.commit is none (the default): the sources of this composite are never written; set writes.commit: "
               "all or partial in its descriptor, or save a delta or a flat image";
    }

    /// The sectors one volume of a commit writes, as LBAs of that volume, in order: a graft's re-encoded sectors and
    /// grafted files, and the guest's writes in [start, start + sectors) of the session (C8d: walked, not listed)
    struct CommitWalk
    {
        std::vector<uint64_t> patches;
        std::vector<std::pair<uint64_t, uint64_t>> runs;
        const SessionWriteMap* session = nullptr;
        uint64_t start = 0;
        uint64_t sectors = 0;

        std::function<std::optional<uint64_t>()> Make() const
        {
            const auto guestAt = [this](uint64_t from) -> std::optional<uint64_t> {
                const std::optional<uint64_t> lba = session->NextChanged(start + from);
                if (!lba || *lba >= start + sectors)
                    return std::nullopt;
                return *lba - start;
            };
            return [this, guestAt, p = size_t(0), r = size_t(0), offset = uint64_t(0), guest = guestAt(0),
                    last = std::optional<uint64_t>()]() mutable -> std::optional<uint64_t> {
                for (;;)
                {
                    std::optional<uint64_t> next;
                    auto offer = [&next](uint64_t lba) { next = next ? std::min(*next, lba) : lba; };
                    if (p < patches.size())
                        offer(patches[p]);
                    if (r < runs.size())
                        offer(runs[r].first + offset);
                    if (guest)
                        offer(*guest);
                    if (!next)
                        return std::nullopt;
                    if (p < patches.size() && patches[p] == *next)
                        p++;
                    if (r < runs.size() && runs[r].first + offset == *next && ++offset == runs[r].second)
                    {
                        r++;
                        offset = 0;
                    }
                    if (guest && *guest == *next)
                        guest = guestAt(*guest + 1);
                    if (last && *last == *next)
                        continue;
                    last = next;
                    return next;
                }
            };
        }
    };

    /// The save a disposition runs: the policy with the D-8 fallback (SaveOptions::disposition)
    SaveOptions DispositionSave(const std::string& strategy, bool keepBoth, bool strict)
    {
        SaveOptions options;
        options.allowRetarget = true;
        options.disposition = true;
        options.strategy = strategy;
        options.keepBoth = keepBoth;
        options.strict = strict;
        return options;
    }
}  // namespace

/// One media operation as the coordinator carries it (MachineOp::payload points here)
struct MediaManager::MediaRequest
{
    enum class Kind : uint8_t
    {
        Insert,
        Eject,
        Discard
    };
    Kind kind = Kind::Insert;
    std::string slotId;
    std::unique_ptr<Medium> medium;            ///< Insert
    const InsertOptions* insert = nullptr;     ///< Insert
    const EjectOptions* eject = nullptr;       ///< Eject
    MediaResult result = MediaResult::Success();
    bool nothingToDo = false;                  ///< Prepare found nothing to change (no session effect)
    std::unique_lock<std::recursive_mutex> lock;   ///< the manager's lock, from Prepare to the end of Commit
};

/// The media operations' owner (coordinator::IOpOwner): Prepare checks and settles the outgoing medium's writes,
/// Commit marks the commit point and swaps. _mutex is held from Prepare to Commit, as one lock held the whole
/// operation before
class MediaManager::MediaOpOwner : public coordinator::IOpOwner
{
public:
    explicit MediaOpOwner(MediaManager& manager) : _manager(manager) {}

    /// TTD v1 keeps no media change events (NoteMediaChange): while it records, a change is refused as before M3.
    /// Goes with v1
    std::string Refusal(const coordinator::MachineOp& op) const override
    {
        EmulatorContext* context = _manager._context;
        ttd::ITimeTravelHooks* ttd = context ? context->pTimeTravelHooks : nullptr;
        if (op.endRecording || !ttd || ttd->RecordsMediaChanges())
            return {};
        const ttd::TTDGuardedAction action = op.kind == coordinator::OpKind::DiskCreate ? ttd::TTDGuardedAction::CreateDisk
                                             : op.media == coordinator::MediaClass::Tape ? ttd::TTDGuardedAction::LoadTape
                                                                                         : ttd::TTDGuardedAction::LoadDisk;
        std::string refusal = ttd->RecordingGuard(action);
        if (!refusal.empty() && op.media == coordinator::MediaClass::Any && op.kind != coordinator::OpKind::DiskCreate)
            refusal = "the media set is fixed while a TTD recording runs; end the recording first";
        return refusal;
    }

    bool Prepare(const coordinator::MachineOp& op, coordinator::OpStage& stage) override
    {
        MediaRequest& request = *std::any_cast<MediaRequest*>(op.payload);
        request.lock = std::unique_lock<std::recursive_mutex>(_manager._mutex);
        request.result = _manager.PrepareMediaOp(request);
        if (request.result.Ok())
            return true;
        stage.error = request.result.message;
        return false;
    }

    bool Commit(const coordinator::MachineOp& op, coordinator::OpStage& stage) override
    {
        MediaRequest& request = *std::any_cast<MediaRequest*>(op.payload);
        const bool ok = _manager.CommitMediaOp(request, stage);
        if (!ok)
            stage.error = request.result.message;
        request.lock.unlock();
        return ok;
    }

    void Abort(const coordinator::MachineOp& op, coordinator::OpStage&) override
    {
        // Prepare failed, or Commit threw before it let go
        MediaRequest& request = *std::any_cast<MediaRequest*>(op.payload);
        if (request.lock.owns_lock())
            request.lock.unlock();
    }

private:
    MediaManager& _manager;
};

std::atomic<bool> MediaManager::_saveOnRelease{true};

MediaManager::MediaManager(EmulatorContext* context) : _context(context), _opOwner(std::make_unique<MediaOpOwner>(*this)) {}

void MediaManager::SetMachineOps(coordinator::IMachineOps* ops)
{
    _machineOps = ops;
}

coordinator::IOpOwner& MediaManager::OpOwner()
{
    return *_opOwner;
}

MediaResult MediaManager::RunMediaOp(coordinator::MachineOp op, MediaRequest& request)
{
    op.payload = &request;
    if (_machineOps == nullptr)
    {
        // No emulator around this manager: no TTD, no rules - the owner runs the operation itself
        coordinator::OpStage stage;
        if (_opOwner->Prepare(op, stage))
            _opOwner->Commit(op, stage);
        else
            _opOwner->Abort(op, stage);
        return request.result;
    }
    const coordinator::OpResult done = _machineOps->Run(std::move(op));
    if (done.status == coordinator::OpStatus::Refused)
        return MediaResult::Fail(MediaError::Recording, done.reason);
    if (done.status == coordinator::OpStatus::Failed && request.result.Ok())
        return MediaResult::Fail(MediaError::IoError, done.reason);   // the owner threw
    return request.result;
}

MediaManager::~MediaManager()
{
    // Composites keep their writes as their policy says (D-8); then peripherals unregister before the manager
    // goes and anything left is plain data. Staged uploads die with their medium
    if (_saveOnRelease)
        SaveByPolicyOnRelease();
    for (auto& [id, state] : _slots)
    {
        const bool dirty = state.attached && state.attached->IsDirty();
        Retire(std::move(state.attached), dirty);
        Retire(std::move(state.incoming));
    }
    for (auto& [id, medium] : _parked)
    {
        const bool dirty = medium && medium->IsDirty();
        Retire(std::move(medium), dirty);
    }
}

std::vector<std::string> MediaManager::SaveByPolicyOnRelease()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    std::vector<std::string> lines;
    const bool releasing = _releasing;
    _releasing = true;  // nothing executes any more: the saves run here
    auto save = [&](const std::string& id, Medium* medium, IMediaSlot* slot) {
        if (!medium || medium->Source().type != MediaSourceType::Composite || !medium->Session() || !medium->Composite() ||
            !medium->IsDirty())
            return;
        const std::string policy = medium->Composite()->writesSave;  // a write-back rebuilds the slot: medium goes
        const MediaResult saved = SaveMedium(id, *medium, slot, DispositionSave({}, false, false), nullptr);
        std::string line = id + ": " + (saved.Ok() ? "saved by writes.save " + policy : "not saved, " + saved.message);
        for (const std::string& r : saved.report)
            line += "; " + r;
        LOGINFO("MediaManager: on release: %s", line.c_str());
        lines.push_back(std::move(line));
    };
    for (auto& [id, state] : _slots)
        save(id, state.attached.get(), state.slot);
    for (auto& [id, medium] : _parked)
        save(id, medium.get(), nullptr);
    _releasing = releasing;
    return lines;
}

/// region <Slots>

void MediaManager::RegisterSlot(IMediaSlot& slot)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    const std::string id = slot.Descriptor().id;

    SlotState& state = _slots[id];
    state.slot = &slot;
    _revision++;

    // A card that comes back finds its medium where it left it
    auto parked = _parked.find(id);
    if (parked != _parked.end())
    {
        state.attached = std::move(parked->second);
        _parked.erase(parked);
        slot.Attach(*state.attached);
        slot.SetWriteProtectSwitch(state.writeProtect);
        Post(NC_MEDIA_INSERTED, id, state.attached.get());
        return;
    }

    // A card fitted after creation (a sound card switch) gets what the config states for it
    if (_configured)
    {
        auto entry = std::find_if(_configured->begin(), _configured->end(),
                                  [&id](const MediaSetEntry& e) { return e.slotId == id; });
        if (entry != _configured->end())
        {
            std::vector<std::string> problems;
            ApplyConfiguredEntry(*entry, problems);
            for (const std::string& line : problems)
                LOGWARNING("MediaManager: configured media: %s", line.c_str());
        }
    }
}

void MediaManager::UnregisterSlot(const std::string& slotId)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return;

    SlotState& state = it->second;
    if (state.attached)
    {
        state.slot->Detach();
        _parked[slotId] = std::move(state.attached);
    }
    else if (state.incoming)
    {
        _parked[slotId] = std::move(state.incoming);
    }
    _slots.erase(it);
    _revision++;
}

bool MediaManager::HasSlot(const std::string& slotId) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _slots.count(slotId) != 0;
}

/// endregion </Slots>

/// region <Operations>

std::vector<SlotInfo> MediaManager::List() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    std::vector<SlotInfo> result;
    for (const auto& [id, state] : _slots)
        result.push_back(Describe(id, state));
    return result;
}

std::optional<SlotInfo> MediaManager::Info(const std::string& slotId) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return std::nullopt;
    return Describe(slotId, it->second);
}

std::vector<SlotInfo> MediaManager::Detached() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    std::vector<SlotInfo> result;
    for (const auto& [id, medium] : _parked)
    {
        SlotInfo info;
        info.descriptor.id = id;
        info.descriptor.kind = medium->Kind();
        info.tags = {MediaKindName(medium->Kind())};
        info.detached = true;
        info.source = medium->Source().path.empty() ? medium->Describe() : medium->Source().path;
        info.format = medium->Format();
        info.access = medium->Access();
        info.changedUnits = medium->ChangedUnits();  // nobody writes to a detached medium
        info.changes = medium->DescribeChanges();
        info.dirty = info.changedUnits > 0;
        if (info.dirty)
            info.onRelease = OnReleaseOf(*medium);
        info.volumeId = medium->VolumeId();
        result.push_back(std::move(info));
    }
    return result;
}

MediaResult MediaManager::Insert(const std::string& slotId, const MediaSource& source, const InsertOptions& options)
{
    // A change the rules refuse now is refused (and logged) before the file is opened: the same operation, without
    // a medium, runs into the refusal
    if (_machineOps)
    {
        coordinator::MachineOp op = InsertOp(slotId, source.path, options);
        if (_machineOps->WouldRefuse(op).status == coordinator::OpStatus::Refused)
        {
            MediaRequest request;
            request.slotId = slotId;
            request.insert = &options;
            MediaResult refused = RunMediaOp(std::move(op), request);
            if (refused.error == MediaError::Recording)
                return refused;
            // The mode changed in between: the operation reached the owner without its medium; open it after all
        }
    }
    std::unique_ptr<Medium> medium;
    MediaResult opened = OpenForSlot(slotId, source, options, medium);
    if (!opened.Ok())
        return opened;
    MediaResult inserted = Insert(slotId, std::move(medium), options);
    inserted.report.insert(inserted.report.begin(), opened.report.begin(), opened.report.end());
    return inserted;
}

static uint64_t ContentIdentity(const MediaSource& source);

MediaResult MediaManager::OpenForSlot(const std::string& slotId, const MediaSource& source, const InsertOptions& options,
                                      std::unique_ptr<Medium>& medium)
{
    SlotDescriptor descriptor;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto it = _slots.find(slotId);
        if (it == _slots.end())
            return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
        descriptor = it->second.slot->Descriptor();
    }

    if ((source.type == MediaSourceType::Folder || FileHelper::IsFolder(source.path)) && !descriptor.acceptsFolder)
        return MediaResult::Fail(MediaError::KindMismatch, "slot '" + slotId + "' does not take a folder");

    // File I/O and folder scans happen here, on the caller's thread
    OpenRequest request;
    request.context = _context;
    request.source = source;
    request.kind = descriptor.kind;
    request.access = options.access.value_or(descriptor.defaultAccess);
    // A folder is never written: a slot whose images default to write-through
    // (an IDE hard disk) keeps a folder's writes for the session
    // A composite reads folders and images and is never written in place either
    const bool folder = source.type == MediaSourceType::Folder || FileHelper::IsFolder(source.path) ||
                        source.type == MediaSourceType::Composite || !source.inlineBody.empty() ||
                        ComposeDescriptor::IsDescriptorName(source.path);
    if (!options.access && folder && request.access == AccessMode::WriteThrough)
        request.access = AccessMode::Session;
    request.fs = options.fs.value_or(descriptor.defaultFs);
    request.explicitFs = options.fs.has_value();
    request.allowedFs = descriptor.fsCompatibility;
    request.mbr = descriptor.folderMbr;
    request.codePage = options.codePage;
    request.freeBytes = options.freeBytes;
    request.cancelRequested = options.cancelRequested;
    request.onProgress = options.onProgress;

    // The slot's FAT compatibility matrix (BUGS.md #1): a folder volume is
    // built in a flavour the controller reads. The default is clamped into
    // the matrix; an explicit request for a flavour it cannot read is a
    // caller error, not silently reinterpreted
    if (!descriptor.fsCompatibility.empty())
    {
        const bool allowed = std::find(descriptor.fsCompatibility.begin(), descriptor.fsCompatibility.end(),
                                       request.fs) != descriptor.fsCompatibility.end();
        if (!allowed && options.fs)
            return MediaResult::Fail(MediaError::BadRequest, "slot '" + slotId + "' reads " +
                                                                  FatTypeNames(descriptor.fsCompatibility) +
                                                                  " volumes, not " + FatTypeName(request.fs));
        if (!allowed)
            request.fs = descriptor.fsCompatibility.front();
    }

    MediaResult opened = MediaFormatRegistry::Open(request, medium);
    if (!opened.Ok())
        return opened;
    medium->SetIdentity(ContentIdentity(source));

    // The same matrix for an inserted image: a FAT volume of a flavour the
    // controller cannot read is refused. A folder cannot land here wrong - it
    // was built into an allowed flavour above; a non-FAT image is none of the
    // matrix's business (a blank disk the guest formats itself is legitimate)
    // Every FAT partition is looked at: the image is refused only when the slot reads none of them; a partition
    // it cannot read is a report line (the guest will not see it)
    if (!descriptor.fsCompatibility.empty() && !folder && medium->Kind() == MediaKind::Block && medium->Block())
    {
        const std::vector<FatVolumeProbe> volumes = ProbeFatVolumes(*medium->Block());
        const auto reads = [&descriptor](const FatVolumeProbe& v) {
            return std::find(descriptor.fsCompatibility.begin(), descriptor.fsCompatibility.end(), v.fs) !=
                   descriptor.fsCompatibility.end();
        };
        const bool none = !volumes.empty() && std::none_of(volumes.begin(), volumes.end(), reads);
        if (none && volumes.size() == 1)
            return MediaResult::Fail(MediaError::BadRequest,
                                     "slot '" + slotId + "' reads " + FatTypeNames(descriptor.fsCompatibility) +
                                         " volumes: '" + source.path + "' is a " + FatTypeName(volumes.front().fs) +
                                         " volume");
        std::string list;
        for (const FatVolumeProbe& v : volumes)
            if (!reads(v))
                list += (list.empty() ? "" : ", ") + std::string("partition ") + std::to_string(v.partition) + " " +
                        FatTypeName(v.fs);
        if (none)
            return MediaResult::Fail(MediaError::BadRequest, "slot '" + slotId + "' reads " +
                                                                 FatTypeNames(descriptor.fsCompatibility) + " volumes: '" +
                                                                 source.path + "' has none (" + list + ")");
        if (!list.empty())
            medium->Report().push_back("slot '" + slotId + "' reads " + FatTypeNames(descriptor.fsCompatibility) +
                                       " volumes: the guest will not see " + list);
    }

    // Advisory only: does sector 0 look like the layout this slot's boot path
    // actually reads (MBR-partitioned vs. raw FAT)? Never refuses the insert
    if ((medium->Kind() == MediaKind::Block) && medium->Block())
    {
        std::string mismatch = DescribeBlockLayoutMismatch(*medium->Block(), descriptor.tags);
        if (!mismatch.empty())
            medium->Report().push_back(std::move(mismatch));
    }

    AttachJournal(*medium, source, options.journal);
    return opened;
}

MediaResult MediaManager::ValidateInsert(const std::string& slotId, const Medium* medium) const
{
    if (!medium)
        return MediaResult::Fail(MediaError::UnreadableSource, "no medium");
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
    const SlotDescriptor& descriptor = it->second.slot->Descriptor();
    if (medium->Kind() != descriptor.kind)
        return MediaResult::Fail(MediaError::KindMismatch, std::string("slot '") + slotId + "' takes " +
                                                               MediaKindName(descriptor.kind) + " media, not " +
                                                               MediaKindName(medium->Kind()));
    return CheckInUse(slotId, *medium);
}

MediaResult MediaManager::Insert(const std::string& slotId, std::unique_ptr<Medium> medium, const InsertOptions& options)
{
    if (MediaResult valid = ValidateInsert(slotId, medium.get()); !valid.Ok())
        return valid;

    MediaRequest request;
    request.kind = MediaRequest::Kind::Insert;
    request.slotId = slotId;
    request.insert = &options;
    coordinator::MachineOp op =
        InsertOp(slotId, medium->Source().path.empty() ? medium->Describe() : medium->Source().path, options);
    request.medium = std::move(medium);
    return RunMediaOp(std::move(op), request);
}

coordinator::MachineOp MediaManager::InsertOp(const std::string& slotId, const std::string& what,
                                              const InsertOptions& options)
{
    coordinator::MachineOp op;
    op.kind = options.opKind;
    op.source = options.origin;
    op.summary = slotId + " " + what;
    op.sessionReason = options.ttdReason ? options.ttdReason : "media-change";
    op.media = options.refusalWording;
    op.endRecording = options.endRecording;
    return op;
}

MediaResult MediaManager::PrepareMediaOp(MediaRequest& request)
{
    const std::string& slotId = request.slotId;
    switch (request.kind)
    {
        case MediaRequest::Kind::Insert:
        {
            // Checked again: the slot may have changed since the caller's check
            if (MediaResult valid = ValidateInsert(slotId, request.medium.get()); !valid.Ok())
                return valid;
            const InsertOptions& options = *request.insert;
            SlotState& state = _slots.find(slotId)->second;
            // The medium in the slot leaves: its unsaved writes need a decision
            MediaResult result = MediaResult::Success();
            if (state.attached && !state.ejectRequested)
            {
                MediaResult kept = ApplyDisposition(slotId, *state.attached, options.disposition, options.exportPath,
                                                    DispositionSave(options.strategy, options.keepBoth, options.strict));
                if (!kept.Ok())
                    return kept;
                result.report = kept.report;
            }
            return result;
        }
        case MediaRequest::Kind::Eject:
        {
            auto it = _slots.find(slotId);
            if (it == _slots.end())
                return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
            SlotState& state = it->second;
            if (!state.attached && !state.incoming)
            {
                request.nothingToDo = true;
                return MediaResult::Success();
            }
            const EjectOptions& options = *request.eject;
            MediaResult result = MediaResult::Success();
            if (state.attached && !state.ejectRequested)
            {
                MediaResult kept = ApplyDisposition(slotId, *state.attached, options.disposition, options.exportPath,
                                                    DispositionSave(options.strategy, options.keepBoth, options.strict));
                if (!kept.Ok())
                    return kept;
                result.report = kept.report;
            }
            return result;
        }
        case MediaRequest::Kind::Discard:
        {
            auto it = _slots.find(slotId);
            if (it == _slots.end())
                return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
            SlotState& state = it->second;
            request.nothingToDo = !state.attached || (!state.attached->Floppy() && !state.attached->Session());
            if (state.attached && state.attached->Floppy())
            {
                const Medium& disk = *state.attached;
                if (disk.Source().type != MediaSourceType::File && disk.Source().type != MediaSourceType::Folder)
                    return MediaResult::Fail(MediaError::NotSupported, "slot '" + slotId +
                                                                           "' holds a disk without a source to go back "
                                                                           "to: eject it with discard");
            }
            return MediaResult::Success();
        }
    }
    return MediaResult::Fail(MediaError::BadRequest, "unknown media operation");
}

bool MediaManager::CommitMediaOp(MediaRequest& request, coordinator::OpStage& stage)
{
    const std::string& slotId = request.slotId;
    if (request.nothingToDo)
    {
        stage.NoChange();
        return true;
    }
    SlotState& state = _slots.find(slotId)->second;
    switch (request.kind)
    {
        case MediaRequest::Kind::Insert:
        {
            const InsertOptions& options = *request.insert;
            // The change goes ahead: the TTD session the old media described ends here
            stage.CommitPoint();
            std::unique_ptr<Medium> medium = std::move(request.medium);
            request.result.report.insert(request.result.report.end(), medium->Report().begin(), medium->Report().end());

            // Replace any insert still waiting; the medium in the slot goes out first
            if (state.incoming)
                Retire(std::move(state.incoming));
            state.incoming = std::move(medium);
            state.writeProtect = options.writeProtect;
            if (state.attached)
                state.ejectRequested = true;
            const SlotDescriptor& descriptor = state.slot->Descriptor();
            state.emptyFramesLeft = (state.attached && !options.immediate)
                                        ? DelayFrames(state.swapDelayMs.value_or(descriptor.swapDelayMs))
                                        : 0;

            if (CanApplyNow() && !stage.AtFrameBoundary())
            {
                std::vector<std::unique_ptr<Medium>> retired;
                state.emptyFramesLeft = 0;  // nobody runs to see the empty slot
                ApplySlot(slotId, state, retired);
                for (auto& old : retired)
                    Retire(std::move(old));
            }
            return true;
        }
        case MediaRequest::Kind::Eject:
        {
            stage.CommitPoint();
            if (state.incoming)
                Retire(std::move(state.incoming));
            state.ejectRequested = state.attached != nullptr;

            if (CanApplyNow() && !stage.AtFrameBoundary())
            {
                std::vector<std::unique_ptr<Medium>> retired;
                ApplySlot(slotId, state, retired);
                for (auto& old : retired)
                    Retire(std::move(old));
            }
            return true;
        }
        case MediaRequest::Kind::Discard:
        {
            if (state.attached->Floppy())
            {
                // The writes live in the disk image itself: open the source again. The insert is an operation of
                // its own (its rules, its commit point, its log entry); this one changed nothing itself
                const Medium& disk = *state.attached;
                MediaSource source = disk.Source();
                InsertOptions again;
                again.access = disk.Access();
                again.fs = disk.Options().fs;
                again.codePage = disk.Options().codePage;
                again.freeBytes = disk.Options().freeBytes;
                again.writeProtect = state.writeProtect;
                again.immediate = true;
                again.disposition = Disposition::Discard;
                request.result = Insert(slotId, source, again);
                stage.NoChange();
                return request.result.Ok();
            }
            stage.CommitPoint();
            state.discardRequested = true;
            if (CanApplyNow() && !stage.AtFrameBoundary())
            {
                std::vector<std::unique_ptr<Medium>> retired;
                ApplySlot(slotId, state, retired);
            }
            return true;
        }
    }
    return false;
}

MediaTransfer MediaManager::TakeMediaSet()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    MediaTransfer transfer;
    for (auto& [id, state] : _slots)
    {
        // A change still waiting is applied first: what the user asked for is what moves
        if (state.incoming || state.ejectRequested || state.discardRequested)
        {
            std::vector<std::unique_ptr<Medium>> retired;
            state.emptyFramesLeft = 0;
            ApplySlot(id, state, retired);
            for (auto& old : retired)
                Retire(std::move(old));
        }
        if (!state.attached)
            continue;
        state.slot->Detach();
        Post(NC_MEDIA_EJECTED, id, state.attached.get());
        transfer.entries.push_back({id, std::move(state.attached), state.writeProtect});
        state.changedUnits = 0;
        state.changes.clear();
    }
    for (auto& [id, medium] : _parked)
        transfer.entries.push_back({id, std::move(medium), false});
    _parked.clear();
    _revision++;
    return transfer;
}

MediaTransferReport MediaManager::AdoptMediaSet(MediaTransfer transfer)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    MediaTransferReport report;

    // A medium the config put into another slot of the new machine may be the
    // same file as one arriving: the arriving one wins (it may carry writes)
    for (const MediaTransfer::Entry& entry : transfer.entries)
    {
        for (auto& [id, state] : _slots)
        {
            if (id != entry.slotId && state.attached && state.attached->SourceKey() == entry.medium->SourceKey() &&
                !entry.medium->SourceKey().empty())
            {
                state.slot->Detach();
                Post(NC_MEDIA_EJECTED, id, state.attached.get());
                Retire(std::move(state.attached));
            }
        }
    }

    for (MediaTransfer::Entry& entry : transfer.entries)
    {
        const std::string what = entry.medium->Source().path.empty() ? entry.medium->Describe() : entry.medium->Source().path;
        auto it = _slots.find(entry.slotId);
        if (it != _slots.end() && it->second.slot->Descriptor().kind == entry.medium->Kind())
        {
            SlotState& state = it->second;
            if (state.incoming)
                Retire(std::move(state.incoming));
            if (state.attached)
            {
                state.slot->Detach();
                Post(NC_MEDIA_EJECTED, entry.slotId, state.attached.get());
                Retire(std::move(state.attached));
            }
            state.ejectRequested = false;
            state.discardRequested = false;
            state.emptyFramesLeft = 0;
            state.attached = std::move(entry.medium);
            state.writeProtect = entry.writeProtect;
            ForgetVersions(state);
            if (HostWriteHold* hold = state.attached->Hold())
                hold->SetHolding(_holdHostWrites);
            if (MediaReadTap* tap = state.attached->ReadTap())
                tap->Bind(&_readJournal, entry.slotId, this, &_guestProgramWatch);
            _guestProgramWatch.Reset(entry.slotId);
            if (CdImage* cd = state.attached->Cd())
                cd->BindReadJournal(&_readJournal, entry.slotId);
            state.slot->Attach(*state.attached);
            state.slot->SetWriteProtectSwitch(state.writeProtect);
            state.changedUnits = state.attached->ChangedUnits();
            state.changes = state.attached->DescribeChanges();
            Post(NC_MEDIA_INSERTED, entry.slotId, state.attached.get());
            report.attached.push_back(entry.slotId);
            report.lines.push_back(entry.slotId + ": " + what);
        }
        else if (entry.medium->IsDirty())
        {
            const std::string changes = entry.medium->DescribeChanges();
            _parked[entry.slotId] = std::move(entry.medium);
            report.detached.push_back(entry.slotId);
            report.lines.push_back(entry.slotId + ": " + what + " - not on this model, kept detached with its unsaved changes (" +
                                   changes + ")");
        }
        else
        {
            Retire(std::move(entry.medium));
            report.closed.push_back(entry.slotId);
            report.lines.push_back(entry.slotId + ": " + what + " - not on this model, closed");
        }
    }
    _revision++;
    return report;
}

MediaResult MediaManager::Eject(const std::string& slotId, const EjectOptions& options)
{
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto it = _slots.find(slotId);
        if (it == _slots.end())
            return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
        // An empty slot: nothing leaves, nothing to rule on
        if (!it->second.attached && !it->second.incoming)
            return MediaResult::Success();
    }

    MediaRequest request;
    request.kind = MediaRequest::Kind::Eject;
    request.slotId = slotId;
    request.eject = &options;
    coordinator::MachineOp op;
    op.kind = coordinator::OpKind::MediaEject;
    op.source = options.origin;
    op.summary = slotId;
    op.sessionReason = "media-change";
    op.endRecording = options.endRecording;
    return RunMediaOp(std::move(op), request);
}

MediaResult MediaManager::Discard(const std::string& slotId)
{
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        auto parked = _parked.find(slotId);
        if (parked != _parked.end() && !_slots.count(slotId))
        {
            // A detached medium is dropped with its writes: it is in no slot, the machine does not see it
            std::unique_ptr<Medium> gone = std::move(parked->second);
            _parked.erase(parked);
            Post(NC_MEDIA_EJECTED, slotId, gone.get());
            Retire(std::move(gone));
            return MediaResult::Success();
        }
        if (!_slots.count(slotId))
            return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
    }

    MediaRequest request;
    request.kind = MediaRequest::Kind::Discard;
    request.slotId = slotId;
    coordinator::MachineOp op;
    op.kind = coordinator::OpKind::MediaDiscard;
    op.summary = slotId;
    op.sessionReason = "media-change";
    return RunMediaOp(std::move(op), request);
}

MediaResult MediaManager::Export(const std::string& slotId, const std::string& path, const BlockWriteOptions& options)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    SlotState* state = nullptr;
    Medium* medium = FindMedium(slotId, &state);
    if (!medium)
        return MediaResult::Fail(_slots.count(slotId) ? MediaError::UnreadableSource : MediaError::UnknownSlot,
                                 "slot '" + slotId + "' is empty");
    return ExportMedium(slotId, *medium, path, options);
}

MediaResult MediaManager::Save(const std::string& slotId, const SaveOptions& options, SaveOutcome* outcome)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    SlotState* state = nullptr;
    Medium* medium = FindMedium(slotId, &state);
    if (!medium)
        return MediaResult::Fail(_slots.count(slotId) ? MediaError::UnreadableSource : MediaError::UnknownSlot,
                                 "slot '" + slotId + "' is empty");
    const bool wasDirty = medium->IsDirty();
    MediaResult result = SaveMedium(slotId, *medium, state ? state->slot : nullptr, options, outcome);
    if (!result.Ok())
        return result;
    // A write-back rebuilds the slot: the medium saved may be gone, the slot holds its successor
    medium = FindMedium(slotId, &state);
    if (medium && state)
    {
        // A save runs with the guest parked: the medium is read as it is (a discard policy leaves it dirty)
        state->changedUnits = medium->ChangedUnits();
        state->changes = state->changedUnits ? medium->DescribeChanges() : std::string();
    }
    if (medium && wasDirty && !medium->IsDirty())
        Post(NC_MEDIA_CLEAN, slotId, medium);
    return result;
}

MediaResult MediaManager::SetWriteProtect(const std::string& slotId, bool on)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
    SlotState& state = it->second;
    // While the user's recording runs the toggle is an event of the recording: it lands at the next frame boundary,
    // where a replay meets it again (M3, gap 12)
    const ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    if (ttd && ttd->RecordsMediaChanges() && ttd->IsRecording())
    {
        state.pendingProtect = on;
        _revision++;
        return MediaResult::Success();
    }
    state.pendingProtect.reset();
    ApplyProtect(slotId, state, on);
    _revision++;
    return MediaResult::Success();
}

MediaResult MediaManager::Rescan(const std::string& slotId, const RescanOptions& options)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
    SlotState& state = it->second;
    if (!state.attached || (state.attached->Source().type != MediaSourceType::Folder &&
                            state.attached->Source().type != MediaSourceType::Composite))
        return MediaResult::Fail(MediaError::NotSupported, "slot '" + slotId + "' does not hold a folder or a composite");

    Medium& current = *state.attached;
    const MediaSource source = current.Source();
    InsertOptions again;
    again.access = current.Access();
    again.fs = current.Options().fs;
    again.codePage = current.Options().codePage;
    again.freeBytes = current.Options().freeBytes;
    again.writeProtect = state.writeProtect;

    // DT-16: the sources as they are now, compared with what the medium was built from (its writes aside)
    std::unique_ptr<Medium> fresh;
    MediaResult opened = OpenForSlot(slotId, source, again, fresh);
    if (!opened.Ok())
        return opened;
    const SessionWriteMap* session = current.Session();
    const uint64_t built = session ? session->Base().ContentId() : current.ContentId();
    const SessionWriteMap* freshSession = fresh->Session();  // it may have restored a session delta
    if (!options.force && built != 0 && (freshSession ? freshSession->Base().ContentId() : fresh->ContentId()) == built)
    {
        Retire(std::move(fresh));
        MediaResult unchanged = MediaResult::Success();
        unchanged.report.push_back("unchanged: the sources give the same volume, the medium stays as it is");
        return unchanged;
    }

    const uint64_t changed = CanApplyNow() ? current.ChangedUnits() : state.changedUnits;
    if (changed > 0)
    {
        if (options.disposition == Disposition::None)
        {
            Retire(std::move(fresh));
            return MediaResult::Fail(MediaError::Dirty, "slot '" + slotId + "' has " + std::to_string(changed) +
                                                            " unsaved changes and its sources changed: rescan with save, "
                                                            "export <path> or discard (the writes cannot follow a rebuild)");
        }
        MediaResult kept = ApplyDisposition(slotId, current, options.disposition, options.exportPath,
                                            DispositionSave(options.strategy, options.keepBoth, options.strict));
        if (!kept.Ok())
        {
            Retire(std::move(fresh));
            return kept;
        }
        if (options.disposition == Disposition::Save)
        {
            // A commit or a write-back changed the sources (a delta is checked against them at the next build)
            Retire(std::move(fresh));
            opened = OpenForSlot(slotId, source, again, fresh);
            if (!opened.Ok())
                return opened;
        }
        opened.report.insert(opened.report.begin(), kept.report.begin(), kept.report.end());
        again.disposition = Disposition::Discard;  // decided above
    }

    MediaResult inserted = Insert(slotId, std::move(fresh), again);
    inserted.report.insert(inserted.report.begin(), opened.report.begin(), opened.report.end());
    return inserted;
}

bool MediaManager::WaitApplied(const std::string& slotId, uint32_t timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::recursive_mutex> lock(_mutex);
    while (true)
    {
        auto it = _slots.find(slotId);
        if (it == _slots.end())
            return true;
        SlotState& state = it->second;
        if (!state.incoming && !state.ejectRequested && !state.discardRequested)
            return true;
        if (CanApplyNow())
        {
            // Nobody runs frames: apply here, the swap delay has nobody to show it to
            std::vector<std::unique_ptr<Medium>> retired;
            state.emptyFramesLeft = 0;
            ApplySlot(slotId, state, retired);
            for (auto& old : retired)
                Retire(std::move(old));
            continue;
        }
        if (_applied.wait_until(lock, deadline) == std::cv_status::timeout)
        {
            auto again = _slots.find(slotId);
            return again == _slots.end() ||
                   (!again->second.incoming && !again->second.ejectRequested && !again->second.discardRequested);
        }
    }
}

std::vector<std::string> MediaManager::ApplyConfiguredMedia(const std::vector<MediaSetEntry>& mediaSet)
{
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _configured = mediaSet;
    }
    std::vector<std::string> problems;
    for (const MediaSetEntry& entry : mediaSet)
    {
        if (!HasSlot(entry.slotId))
        {
            if (!entry.legacy)
                problems.push_back("[MEDIA] " + entry.slotId + ": this machine has no such slot");
            continue;
        }
        ApplyConfiguredEntry(entry, problems);
    }
    return problems;
}

void MediaManager::ApplyConfiguredEntry(const MediaSetEntry& entry, std::vector<std::string>& problems)
{
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        SlotState& state = _slots[entry.slotId];
        if (entry.swapDelayMs)
            state.swapDelayMs = entry.swapDelayMs;
        if (entry.writeProtect)
        {
            state.writeProtect = *entry.writeProtect;
            if (state.attached)
                state.slot->SetWriteProtectSwitch(state.writeProtect);
        }
    }
    if (entry.source.path.empty())
        return;

    InsertOptions options;
    options.access = entry.access;
    options.fs = entry.fs;
    options.codePage = entry.codePage;
    options.freeBytes = entry.freeBytes;
    options.writeProtect = entry.writeProtect.value_or(false);
    options.immediate = true;
    const MediaResult result = Insert(entry.slotId, entry.source, options);
    if (!result.Ok())
        problems.push_back(entry.slotId + ": " + result.message);
    for (const std::string& line : result.report)
        problems.push_back(entry.slotId + ": " + line);
}

/// endregion </Operations>

void MediaManager::NoteChangeLanded(const std::string& slotId, const std::string& what)
{
    // A change the guest's history must know: a recording keeps it as an event of this boundary (M3, Q4). A replay
    // lands nothing of its own
    if (!_context || _context->ttdReplayActive || !_context->pTimeTravelHooks)
        return;
    _context->pTimeTravelHooks->NoteMediaChange(slotId.c_str(), what.c_str());
}

void MediaManager::ApplyPending()
{
    std::vector<std::unique_ptr<Medium>> retired;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        const ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
        _historyExists.store(ttd && ttd->HasHistory(), std::memory_order_relaxed);
        FollowReplay();   // a replay that reached the frame a medium came in: the medium is back for its device
        for (auto& [id, state] : _slots)
        {
            ApplySlot(id, state, retired);
            // Session writes reach their journal within [MEDIA] SessionFlushSeconds
            if (state.attached)
                if (SessionWriteMap* session = state.attached->Session())
                    session->Tick();
        }
    }
    for (auto& old : retired)
        Retire(std::move(old));
    _applied.notify_all();
}

/// What a medium is by content (F12): its source file's bytes hashed (FNV-1a), for files up to 64 MB; 0 for anything
/// else (Medium::Identity then falls back to the volume id). Sessions store it, so the hash stays FNV-1a. A file
/// hashes once per size and modification time: the configured media of a machine are opened by every new instance
/// (TS-Conf's SD card took ~50 ms of each instance's creation)
static uint64_t ContentIdentity(const MediaSource& source)
{
    constexpr uint64_t kLimit = uint64_t(64) << 20;
    if (source.type != MediaSourceType::File || source.path.empty())
        return 0;
    std::error_code ec;
    const std::filesystem::path path = FileHelper::ToFsPath(source.path);
    const uint64_t size = std::filesystem::file_size(path, ec);
    if (ec || size > kLimit)
        return 0;
    const std::filesystem::file_time_type modified = std::filesystem::last_write_time(path, ec);
    if (ec)
        return 0;

    struct Known
    {
        uint64_t size;
        std::filesystem::file_time_type modified;
        uint64_t hash;
    };
    static std::mutex knownMutex;
    static std::map<std::filesystem::path, Known> known;
    {
        std::lock_guard lock(knownMutex);
        const auto it = known.find(path);
        if (it != known.end() && it->second.size == size && it->second.modified == modified)
            return it->second.hash;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return 0;
    uint64_t hash = 0xcbf29ce484222325ULL ^ size;
    std::vector<char> chunk(1 << 20);
    while (in)
    {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize got = in.gcount();
        for (std::streamsize i = 0; i < got; ++i)
        {
            hash ^= static_cast<uint8_t>(chunk[static_cast<size_t>(i)]);
            hash *= 0x100000001b3ULL;
        }
    }
    hash = hash ? hash : 1;
    // Read while the file did not change (its size and time again): a file written meanwhile hashes next time
    const bool unchanged = std::filesystem::file_size(path, ec) == size && !ec &&
                           std::filesystem::last_write_time(path, ec) == modified && !ec;
    if (unchanged)
    {
        std::lock_guard lock(knownMutex);
        known[path] = {size, modified, hash};
    }
    return hash;
}

/// The medium of one slot, unit by unit, for its undo log: block sectors under the read tap (a seek's change is not
/// a read of the recording), floppy tracks, the write-protect switch
class MediaManager::SlotUnits : public MediaUndoLog::IUnits
{
public:
    explicit SlotUnits(SlotState& state) : _state(state) {}

    bool Read(MediaUndoLog::Unit unit, uint64_t index, MediaUndoLog::Image& out) override
    {
        switch (unit)
        {
            case MediaUndoLog::Unit::Sector:
            {
                IBlockDevice* device = Device();
                std::vector<uint8_t> bytes(IBlockDevice::kSectorSize);
                if (!device || !device->ReadSector(index, bytes.data()))
                    return false;
                out = std::move(bytes);
                return true;
            }
            case MediaUndoLog::Unit::Track:
            {
                const DiskImage::Track* track = Track(index);
                if (!track)
                    return false;
                out = static_cast<const DiskImage::RawTrack&>(*track);
                return true;
            }
            case MediaUndoLog::Unit::Protect:
                out = _state.writeProtect;
                return true;
        }
        return false;
    }

    bool Write(MediaUndoLog::Unit unit, uint64_t index, const MediaUndoLog::Image& image) override
    {
        switch (unit)
        {
            case MediaUndoLog::Unit::Sector:
            {
                IBlockDevice* device = Device();
                const auto* bytes = std::get_if<std::vector<uint8_t>>(&image);
                return device && bytes && bytes->size() == IBlockDevice::kSectorSize &&
                       device->WriteSector(index, bytes->data());
            }
            case MediaUndoLog::Unit::Track:
            {
                DiskImage::Track* track = Track(index);
                const auto* raw = std::get_if<DiskImage::RawTrack>(&image);
                if (!track || !raw)
                    return false;
                static_cast<DiskImage::RawTrack&>(*track) = *raw;
                track->reindex();  // the sector index points into the stream
                track->markDirty();
                return true;
            }
            case MediaUndoLog::Unit::Protect:
            {
                const auto* on = std::get_if<bool>(&image);
                if (!on)
                    return false;
                _state.writeProtect = *on;
                if (_state.attached && !_state.heldForReplay)
                    _state.slot->SetWriteProtectSwitch(*on);
                return true;
            }
        }
        return false;
    }

private:
    IBlockDevice* Device()
    {
        if (!_state.attached)
            return nullptr;
        if (MediaReadTap* tap = _state.attached->ReadTap())
            return &tap->Base();
        return _state.attached->Block();
    }
    DiskImage::Track* Track(uint64_t index)
    {
        DiskImage* disk = _state.attached ? _state.attached->Floppy() : nullptr;
        if (!disk || index > 0x1FF)
            return nullptr;
        return disk->getTrackForCylinderAndSide(static_cast<uint8_t>(index >> 1), static_cast<uint8_t>(index & 1));
    }

    SlotState& _state;
};

bool MediaManager::KeepsVersions() const
{
    if (!_context || _context->ttdReplayActive)
        return false;
    const ttd::ITimeTravelHooks* ttd = _context->pTimeTravelHooks;
    return ttd && (ttd->IsRecording() || _historyExists.load(std::memory_order_relaxed));
}

void MediaManager::MarkWrite(SlotState& state)
{
    if (state.writeMarkedThisFrame)
        return;
    state.writeMarkedThisFrame = true;
    if (_context && _context->ttdReplayActive)
    {
        // A replay runs the recording's writes again: from a recorded version (a seek set it) the medium follows its
        // history; from anything else it is no recorded version any more
        if (state.followsHistory)
        {
            ++state.writtenFrames;
            ++_writeStamp;
        }
        else
            state.versionLost = true;
        return;
    }
    const bool keeps = KeepsVersions();
    if (keeps && state.versionLost)
    {
        state.undo.Reset(state.writtenFrames);
        state.versionLost = false;
    }
    if (keeps)
        state.undo.DropAbove(state.writtenFrames);  // a change after a seek back: the history's future is gone
    ++state.writtenFrames;
    ++_writeStamp;
    if (keeps)
        state.undo.Reach(state.writtenFrames);
    else
        state.undo.Reset(state.writtenFrames);
}

void MediaManager::ForgetVersions(SlotState& state)
{
    // Unique within the run and, with the random start, across runs
    static std::atomic<uint32_t> next{std::random_device{}()};
    uint32_t lineage = next.fetch_add(1, std::memory_order_relaxed);
    if (lineage == 0)
        lineage = next.fetch_add(1, std::memory_order_relaxed);
    state.lineage = lineage;
    state.writtenFrames = 0;
    ++_writeStamp;
    state.undo.Reset(0);
    state.followsHistory = false;
    state.versionLost = false;
}

void MediaManager::NoteWrite(const std::string& slotId, const char* detail)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return;
    SlotState& state = it->second;
    _guestProgramWatch.Forget(slotId);  // the card's files may be new after a write
    MarkWrite(state);
    // A replay records nothing: the writes are the recording's
    if ((_context && _context->ttdReplayActive) || state.writeEventThisFrame)
        return;
    state.writeEventThisFrame = true;

    ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    if (ttd && ttd->IsRecording())
    {
        std::string reason = "Media write " + slotId;
        if (detail && *detail)
            reason += std::string(": ") + detail;
        ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite, reason.c_str());
    }
}

void MediaManager::BeforeSectorWrite(const std::string& slotId, uint64_t lba, IBlockDevice& below)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return;
    SlotState& state = it->second;
    _guestProgramWatch.Forget(slotId);
    MarkWrite(state);
    if (!KeepsVersions() || state.undo.Has(state.writtenFrames, MediaUndoLog::Unit::Sector, lba))
        return;
    std::vector<uint8_t> before(IBlockDevice::kSectorSize);
    if (!below.ReadSector(lba, before.data()))
    {
        state.undo.Reset(state.writtenFrames);  // the sector cannot go back: neither can the medium, before here
        return;
    }
    const uint64_t frame = _context ? _context->emulatorState.frame_counter : 0;
    state.undo.Keep(state.writtenFrames, frame, MediaUndoLog::Unit::Sector, lba, std::move(before));
}

void MediaManager::BeforeTrackWrite(const std::string& slotId, const DiskImage::Track& track)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return;
    SlotState& state = it->second;
    MarkWrite(state);
    const uint64_t index = uint64_t(track.cylinder()) * 2 + track.side();
    if (!KeepsVersions() || state.undo.Has(state.writtenFrames, MediaUndoLog::Unit::Track, index))
        return;
    const uint64_t frame = _context ? _context->emulatorState.frame_counter : 0;
    state.undo.Keep(state.writtenFrames, frame, MediaUndoLog::Unit::Track, index,
                    static_cast<const DiskImage::RawTrack&>(track));
}

void MediaManager::ToSessionAccess(const std::string& slotId, SlotState& state)
{
    Medium& medium = *state.attached;
    if (HostWriteHold* hold = medium.Hold())
    {
        MediaReadTap* tap = medium.ReadTap();
        if (!tap)
            return;
        // The session layer goes over the hold: what the hold kept (a replay's writes) moves into it, nothing
        // reaches the file
        auto held = hold->TakeHeld();
        SessionWriteMap* session = nullptr;
        tap->WrapBase([&session](std::unique_ptr<IBlockDevice> base) {
            auto map = std::make_unique<SessionWriteMap>(std::move(base));
            session = map.get();
            return std::unique_ptr<IBlockDevice>(std::move(map));
        });
        for (const auto& [lba, bytes] : held)
            session->WriteSector(lba, bytes.data());
        medium.SetSession(session);
    }
    else if (!medium.Floppy())
        return;   // nothing written through: nothing to keep from the file
    medium.SetAccess(AccessMode::Session);
    _revision++;
    LOGINFO("MediaManager: %s goes back in the recorded history: write-through turns to session access, the file "
            "keeps its content until it is saved (F13)", slotId.c_str());
}

void MediaManager::ApplyProtect(const std::string& slotId, SlotState& state, bool on)
{
    if (state.writeProtect == on)
        return;
    const bool keeps = KeepsVersions();
    if (keeps)
    {
        if (state.versionLost)
        {
            state.undo.Reset(state.writtenFrames);
            state.versionLost = false;
        }
        state.undo.DropAbove(state.writtenFrames);
    }
    // A version of its own: a replay meets it at its frame boundary (FollowReplay), and the frame's writes after it
    // are the next version
    ++state.writtenFrames;
    ++_writeStamp;
    const uint64_t frame = _context ? _context->emulatorState.frame_counter : 0;
    if (keeps)
        state.undo.Keep(state.writtenFrames, frame, MediaUndoLog::Unit::Protect, 0, state.writeProtect, on);
    else
        state.undo.Reset(state.writtenFrames);
    state.writeProtect = on;
    if (state.attached && !state.heldForReplay)
        state.slot->SetWriteProtectSwitch(on);
    _revision++;
    NoteChangeLanded(slotId, on ? "write protect on" : "write protect off");
}

void MediaManager::CurrentVersions(std::vector<MediaVersionInfo>& out) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    out.clear();
    for (const auto& [id, state] : _slots)
    {
        MediaVersionInfo info;
        info.slot = id;
        if (state.attached)
        {
            info.format = state.attached->Format();
            // What the medium is by content, the same whatever the guest wrote: versions are counted on top of it
            info.contentId = state.attached->Identity();
            info.hasVersions = state.attached->ReadTap() || state.attached->Floppy();
        }
        info.version = VersionOf(state);
        out.push_back(std::move(info));
    }
}

bool MediaManager::Reaches(const std::string& slotId, uint64_t version) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return false;
    const SlotState& state = it->second;
    const uint32_t lineage = static_cast<uint32_t>(version >> 32);
    version &= 0xFFFFFFFFull;
    if (lineage != state.lineage && version != 0)
        return false;
    return !state.versionLost && state.undo.Reaches(state.writtenFrames, version);
}

bool MediaManager::SetHead(const std::string& slotId, uint64_t version)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return false;
    SlotState& state = it->second;
    // A seek: the frame starts again, its first write moves the version
    state.writeMarkedThisFrame = false;
    state.writeEventThisFrame = false;
    // Another lineage (another insert, another run): only its untouched medium is the same - the caller matched
    // the content (Identity) - and only when this one can go back to its own start
    const uint32_t lineage = static_cast<uint32_t>(version >> 32);
    version &= 0xFFFFFFFFull;
    if (lineage != state.lineage && version != 0)
    {
        state.followsHistory = false;
        return false;
    }
    if (state.versionLost || !state.undo.Reaches(state.writtenFrames, version))
    {
        state.followsHistory = false;
        return false;
    }
    if (state.writtenFrames != version && state.attached && state.attached->Access() == AccessMode::WriteThrough)
        ToSessionAccess(slotId, state);
    SlotUnits units(state);
    if (!state.undo.Move(state.writtenFrames, version, units))
    {
        state.followsHistory = false;
        return false;
    }
    if (state.writtenFrames != version)
        ++_writeStamp;
    state.writtenFrames = version;
    state.followsHistory = true;
    return true;
}

void MediaManager::GuestEject(const std::string& slotId)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return;
    SlotState& state = it->second;
    if (!state.attached || state.ejectRequested)
        return;
    ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    if (ttd && ttd->IsReplayActive())
        return;
    if (state.attached->ChangedUnits() > 0)
    {
        LOGWARNING("MediaManager: the guest ejected '%s', which has unsaved writes: the medium stays in the slot", slotId.c_str());
        return;
    }
    // The normal eject, applied at the frame boundary (never inside the peripheral's own command)
    state.ejectRequested = true;
}

Medium* MediaManager::GetMedium(const std::string& slotId)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    return it == _slots.end() ? nullptr : it->second.attached.get();
}

/// region <Helpers>

void MediaManager::ApplySlot(const std::string& slotId, SlotState& state, std::vector<std::unique_ptr<Medium>>& retired)
{
    state.writeMarkedThisFrame = false;  // a new frame: the next write marks again
    state.writeEventThisFrame = false;

    if (state.ejectRequested)
    {
        if (state.slot->IsBusy())
            return;  // a transfer in flight: next frame
        if (!state.heldForReplay)
            state.slot->Detach();
        state.heldForReplay = false;
        Post(NC_MEDIA_EJECTED, slotId, state.attached.get());
        retired.push_back(std::move(state.attached));
        state.ejectRequested = false;
        ForgetVersions(state);
        if (!state.incoming)
            NoteChangeLanded(slotId, "ejected");
        state.changedUnits = 0;
        state.changes.clear();
    }

    if (state.incoming)
    {
        if (state.emptyFramesLeft > 0)
        {
            state.emptyFramesLeft--;
            return;
        }
        if (state.slot->IsBusy())
            return;
        state.attached = std::move(state.incoming);
        if (HostWriteHold* hold = state.attached->Hold())
            hold->SetHolding(_holdHostWrites);
        if (MediaReadTap* tap = state.attached->ReadTap())
            tap->Bind(&_readJournal, slotId, this, &_guestProgramWatch);
        _guestProgramWatch.Reset(slotId);
        if (CdImage* cd = state.attached->Cd())
            cd->BindReadJournal(&_readJournal, slotId);
        state.slot->Attach(*state.attached);
        state.slot->SetWriteProtectSwitch(state.writeProtect);
        state.changedUnits = state.attached->ChangedUnits();
        state.landedFrame = _context ? _context->emulatorState.frame_counter : 0;
        ForgetVersions(state);
        Post(NC_MEDIA_INSERTED, slotId, state.attached.get());
        const Medium& in = *state.attached;
        NoteChangeLanded(slotId, in.Source().path.empty() ? in.Describe() : in.Source().path);
    }

    if (state.discardRequested)
    {
        if (state.attached && state.attached->Session())
            state.attached->Session()->Discard();
        state.discardRequested = false;
        ForgetVersions(state);
        NoteChangeLanded(slotId, "discarded");
    }

    // A write-protect toggle made while recording: an event of the recording at this boundary
    if (state.pendingProtect)
    {
        const bool on = *state.pendingProtect;
        state.pendingProtect.reset();
        ApplyProtect(slotId, state, on);
    }

    // While a replay runs nothing reaches the host (FR-20): the disk stays
    // dirty and the next live frame writes it
    if (state.attached && state.attached->Floppy() && state.attached->Access() == AccessMode::WriteThrough &&
        !_holdHostWrites && (!_context || MediaWriteGate::HostWritesAllowed(*_context)))
        WriteThroughFloppy(slotId, state);

    // Per-frame snapshot for readers on other threads
    if (state.attached)
    {
        const uint64_t changed = state.attached->ChangedUnits();
        if (state.changedUnits == 0 && changed > 0)
            Post(NC_MEDIA_DIRTY, slotId, state.attached.get());
        else if (state.changedUnits > 0 && changed == 0)
            Post(NC_MEDIA_CLEAN, slotId, state.attached.get());
        if (changed != state.changedUnits || (changed > 0 && state.attached->Floppy()))
            state.changes = changed ? state.attached->DescribeChanges() : std::string();
        state.changedUnits = changed;
    }
}

SlotInfo MediaManager::Describe(const std::string& slotId, const SlotState& state) const
{
    SlotInfo info;
    info.descriptor = state.slot->Descriptor();
    const SlotDescriptor& d = info.descriptor;

    // Tags: the kind, what the manager knows, then the peripheral's own
    info.tags.push_back(MediaKindName(d.kind));
    if (d.removable)
        info.tags.push_back("removable");
    if (d.acceptsFolder)
        info.tags.push_back("folder");
    for (const std::string& tag : d.tags)
    {
        if (std::find(info.tags.begin(), info.tags.end(), tag) == info.tags.end())
            info.tags.push_back(tag);
    }

    // Index: the n-th slot of this kind, in id order (the map is id-ordered)
    int index = 0;
    for (const auto& [id, other] : _slots)
    {
        if (id == slotId)
            break;
        if (other.slot->Descriptor().kind == d.kind)
            index++;
    }
    info.index = index;

    info.present = state.attached != nullptr;
    info.pending = state.incoming != nullptr || state.ejectRequested || state.discardRequested;
    info.writeProtect = state.pendingProtect.value_or(state.writeProtect);  // the switch as the user set it
    if (state.attached)
    {
        info.source = state.attached->Source().path.empty() ? state.attached->Describe() : state.attached->Source().path;
        info.format = state.attached->Format();
        info.access = state.attached->Access();
        // A stopped machine is read live; a running one through the frame's snapshot
        const bool live = CanApplyNow();
        info.changedUnits = live ? state.attached->ChangedUnits() : state.changedUnits;
        info.changes = live ? state.attached->DescribeChanges() : state.changes;
        info.dirty = info.changedUnits > 0;
        if (info.dirty)
            info.onRelease = OnReleaseOf(*state.attached);
        info.volumeId = state.attached->VolumeId();
    }
    return info;
}

std::string MediaManager::OnReleaseOf(const Medium& medium)
{
    const SessionWriteMap* session = medium.Session();
    const CompositeInfo* composite = medium.Source().type == MediaSourceType::Composite ? medium.Composite() : nullptr;
    if (composite && session && _saveOnRelease)
    {
        const std::string& policy = composite->writesSave;
        if (policy == "flat")
            return "delta";  // flat needs a path: the release keeps them as a delta (D-8)
        return policy;
    }
    return session && session->JournalRecoverable() ? "journal" : "lost";
}

std::vector<SlotInfo> MediaManager::Unsaved() const
{
    std::vector<SlotInfo> unsaved;
    for (SlotInfo& info : List())
        if (info.dirty)
            unsaved.push_back(std::move(info));
    for (SlotInfo& info : Detached())
        if (info.dirty)
            unsaved.push_back(std::move(info));
    return unsaved;
}

Medium* MediaManager::FindMedium(const std::string& slotId, SlotState** state)
{
    *state = nullptr;
    auto it = _slots.find(slotId);
    if (it != _slots.end())
    {
        *state = &it->second;
        return it->second.attached.get();
    }
    auto parked = _parked.find(slotId);
    return parked != _parked.end() ? parked->second.get() : nullptr;
}

MediaResult MediaManager::ApplyDisposition(const std::string& slotId, Medium& medium, Disposition disposition,
                                           const std::string& exportPath, const SaveOptions& save)
{
    auto it = _slots.find(slotId);
    const uint64_t changed = (CanApplyNow() || it == _slots.end()) ? medium.ChangedUnits() : it->second.changedUnits;
    if (changed == 0)
        return MediaResult::Success();  // nothing to decide about

    switch (disposition)
    {
        case Disposition::None:
            return MediaResult::Fail(MediaError::Dirty,
                                     "slot '" + slotId + "' has " + std::to_string(changed) +
                                         " unsaved changes: say save, export <path> or discard");
        case Disposition::Discard:
            return MediaResult::Success();
        case Disposition::Save:
            return SaveMedium(slotId, medium, it != _slots.end() ? it->second.slot : nullptr, save, nullptr);
        case Disposition::Export:
            if (exportPath.empty())
                return MediaResult::Fail(MediaError::BadRequest, "export needs a path");
            return ExportMedium(slotId, medium, exportPath);
    }
    return MediaResult::Fail(MediaError::BadRequest, "unknown disposition");
}

MediaResult MediaManager::ExportMedium(const std::string& slotId, Medium& medium, const std::string& path,
                                       const BlockWriteOptions& options)
{
    // The guest writes into the medium without a lock; a consistent export of
    // a running machine needs the versioned change layer (media history H1)
    if (!CanApplyNow())
        return MediaResult::Fail(MediaError::NotSupported, "pause the emulator to export slot '" + slotId + "'");

    if (FileHelper::AbsolutePath(path, /*resolveSymlinks*/ true) == medium.SourceKey())
        return MediaResult::Fail(MediaError::InUse, "the export target is the medium's own source");
    if (!medium.Block() && (options.compact || options.fs || options.size))
        return MediaResult::Fail(MediaError::BadRequest, "compact, fs and size apply to block media (FAT disks and cards)");
    if (!medium.Block() && (!options.compression.empty() || !options.parent.empty()))
        return MediaResult::Fail(MediaError::BadRequest, "compression and parent apply to block media exported as .chd");
    if (!medium.Block() && !options.vhd.empty())
        return MediaResult::Fail(MediaError::BadRequest, "vhd applies to block media exported as .vhd");

    std::vector<std::string> exportReport;
    if (DiskImage* disk = medium.Floppy())
    {
        // The format writers mark the disk clean and rename it: an export is a
        // copy, so both are put back
        const DiskImage::DirtyState before = disk->captureDirtyState();
        const FloppySaveResult written = FloppyFormats::Save(_context, *disk, path, /*allowRetarget*/ false);
        disk->restoreDirtyState(before);
        if (!written.saved)
            return MediaResult::Fail(MediaError::IoError, written.reason);
    }
    else if (medium.Block())
    {
        // Sectors the guest did not change are the source's: a CHD export keeps
        // the source CHD's stored hunks for them
        std::function<bool(uint64_t, uint64_t)> unchanged;
        if (SessionWriteMap* session = medium.Session())
            unchanged = [session](uint64_t first, uint64_t count) { return !session->ChangedIn(first, count); };
        else if (medium.Access() == AccessMode::ReadOnly)
            unchanged = [](uint64_t, uint64_t) { return true; };
        const MediaResult written = BlockFormats::Write(*medium.Block(), path, options, unchanged);
        if (!written.Ok())
            return written;
        exportReport = written.report;
    }
    else if (const TapeImage* tape = medium.Tape())
    {
        // A .tap takes ROM-standard byte blocks only; anything else is a .tzx
        std::string error;
        const bool tap = StringHelper::ToLower(FileHelper::GetFileExtension(path)) == "tap";
        const bool written = tap ? TapArchiveWriter::Save(*tape, path, error) : TzxArchiveWriter::Save(*tape, path, error);
        if (!written)
            return MediaResult::Fail(tap && TapArchiveWriter::IsExportable(*tape) ? MediaError::IoError : MediaError::BadRequest,
                                     error);
    }
    else
    {
        return MediaResult::Fail(MediaError::NotSupported, "this medium cannot be exported yet");
    }

    Post(NC_MEDIA_EXPORTED, slotId, &medium, path);
    MediaResult exported = MediaResult::Success();
    exported.report = exportReport;
    return exported;
}

MediaResult MediaManager::SaveMedium(const std::string& slotId, Medium& medium, IMediaSlot* slot,
                                     const SaveOptions& options, SaveOutcome* outcome)
{
    DiskImage* disk = medium.Floppy();
    if (!disk && medium.Block())
        return SaveBlockMedium(slotId, medium, slot, options, outcome);
    if (!disk)
        return MediaResult::Fail(MediaError::NotSupported, "slot '" + slotId + "': this medium cannot be saved; export it");
    if (!CanApplyNow())
        return MediaResult::Fail(MediaError::NotSupported, "pause the emulator to save slot '" + slotId + "'");

    // Only a disk image file can take the disk back
    std::string target = options.path;
    if (target.empty())
    {
        const bool ownFile = medium.Source().type == MediaSourceType::File && medium.Format() != "hobeta";
        if (!ownFile || medium.Source().path.empty())
            return MediaResult::Fail(MediaError::NotSupported,
                                     "slot '" + slotId + "' holds a disk without an image file of its own: export it to a path");
        target = medium.Source().path;
    }

    const FloppySaveResult written = FloppyFormats::Save(_context, *disk, target, options.allowRetarget);
    if (!written.saved)
        return MediaResult::Fail(MediaError::IoError, written.reason);

    disk->markClean();
    if (written.savedPath != medium.Source().path || medium.Source().type != MediaSourceType::File)
    {
        MediaSource saved;
        saved.type = MediaSourceType::File;
        saved.path = written.savedPath;
        medium.Rebase(saved);
        if (slot)
            slot->SourceChanged(medium);
    }

    if (outcome)
    {
        outcome->savedPath = written.savedPath;
        outcome->retargeted = written.retargeted;
        outcome->note = written.reason;
    }
    MediaResult result = MediaResult::Success();
    if (written.retargeted)
        result.report.push_back(written.reason + ": saved as '" + written.savedPath + "'");
    Post(NC_MEDIA_SAVED, slotId, &medium, written.savedPath);
    return result;
}

MediaResult MediaManager::SaveBlockMedium(const std::string& slotId, Medium& medium, IMediaSlot* slot, const SaveOptions& options,
                                          SaveOutcome* outcome)
{
    if (!CanApplyNow())
        return MediaResult::Fail(MediaError::NotSupported, "pause the emulator to save slot '" + slotId + "'");

    // DT-9: a composite without a target path saves its session as the descriptor says (S2 by default)
    // (a composite saved flat to a path stands for that file afterwards: its source is no longer the descriptor)
    const CompositeInfo* composite = medium.Source().type == MediaSourceType::Composite ? medium.Composite() : nullptr;
    if (composite && medium.Session())
    {
        // D-8: one policy for a save, an eject / swap / rescan disposition and the emulator going away
        std::string strategy = options.strategy;
        if (strategy.empty())
            strategy = options.path.empty() ? composite->writesSave : "flat";
        if (strategy == "ask")
            strategy = "delta";  // the GUI asks before it saves; a save that comes here keeps the writes
        if (strategy == "discard")
        {
            if (!options.disposition)
                return MediaResult::Fail(MediaError::BadRequest,
                                         "discard (writes.save or strategy) keeps nothing on a save: name a strategy that saves, or discard the writes");
            MediaResult dropped = MediaResult::Success();
            dropped.report.push_back(std::to_string(medium.Session()->ChangedSectors()) +
                                     " changed sector(s) dropped (writes.save: discard)");
            return dropped;
        }
        if (strategy == "delta")
            return SaveDelta(slotId, medium, *composite, options, {}, outcome);
        if (strategy != "flat" && strategy != "commit" && strategy != "write-back")
            return MediaResult::Fail(MediaError::BadRequest, "strategy '" + strategy + "': expected flat, delta, commit or write-back");
        if (strategy != "flat" || options.path.empty())
        {
            MediaResult done = strategy == "commit"       ? CommitComposite(slotId, medium, slot, *composite, options, outcome)
                               : strategy == "write-back" ? WriteBackComposite(slotId, medium, *composite, options, outcome)
                                                          : MediaResult::Fail(MediaError::BadRequest,
                                                                              "a flat save writes a new image: name the path to save to");
            if (done.Ok() || !options.disposition || options.strict || options.plan)
                return done;
            // A medium that leaves never loses its writes: they wait in the session delta for the next insert
            MediaResult kept = SaveDelta(slotId, medium, *composite, options,
                                         strategy + " failed (" + done.message + "): the writes are kept as a session delta",
                                         outcome);
            if (!kept.Ok())
                return MediaResult::Fail(done.error, done.message + "; a session delta failed too: " + kept.message);
            return kept;
        }
    }
    else if (!options.strategy.empty() && options.strategy != "flat")
        return MediaResult::Fail(MediaError::BadRequest, "strategy '" + options.strategy + "' is for composite media with session writes");

    const std::string before = medium.Source().path;
    BlockWriteOptions write;
    write.compression = options.compression;
    write.compact = options.compact;
    write.fs = options.fs;
    write.size = options.size;
    write.vhd = options.vhd;
    if (!options.vhd.empty() && options.path.empty())
        return MediaResult::Fail(MediaError::BadRequest, "vhd writes a new image: name the .vhd path to save to");
    if (options.compact && options.path.empty())
        return MediaResult::Fail(MediaError::BadRequest, "compact writes a new image: name the path to save to");
    std::string savedPath;
    MediaResult result = BlockFormats::Save(medium, options.path, write, savedPath);
    if (!result.Ok())
        return result;
    if (slot && medium.Source().path != before)
        slot->SourceChanged(medium);
    if (outcome)
    {
        outcome->savedPath = savedPath;
        outcome->retargeted = false;
        outcome->note.clear();
    }
    Post(NC_MEDIA_SAVED, slotId, &medium, savedPath);
    return result;
}

MediaResult MediaManager::SaveDelta(const std::string& slotId, Medium& medium, const CompositeInfo& composite, const SaveOptions& options,
                                    const std::string& note, SaveOutcome* outcome)
{
    std::filesystem::path path = options.path.empty() ? composite.delta : FileHelper::ToFsPath(options.path);
    if (path.empty())
        return MediaResult::Fail(MediaError::BadRequest,
                                 "an inline descriptor has no delta file: name writes.delta in it, or save flat to a path");
    if (!medium.DeltaConflict().empty() && options.path.empty() && !options.force)
        return MediaResult::Fail(MediaError::Dirty, medium.DeltaConflict() + ": save with force to replace it, or flat to a path");

    MediaResult result = SessionDelta::Save(path, *medium.Session(), CompositeMediumFactory::DeltaIdentityOf(composite));
    if (!result.Ok())
        return result;
    medium.MarkPersisted();
    medium.SetDeltaConflict({});
    const std::string savedPath = FileHelper::FromFsPath(path);
    result.report.push_back("session delta: " + std::to_string(medium.Session()->ChangedSectors()) + " sector(s) in " + savedPath);
    if (!note.empty())
        result.report.push_back(note);
    if (outcome)
    {
        outcome->savedPath = savedPath;
        outcome->retargeted = false;
        outcome->note = result.report.back();
    }
    Post(NC_MEDIA_SAVED, slotId, &medium, savedPath);
    return result;
}

MediaResult MediaManager::WriteBackComposite(const std::string& slotId, Medium& medium, const CompositeInfo& composite,
                                             const SaveOptions& options, SaveOutcome* outcome)
{
    if (const std::optional<std::string> refused = CommitRefusal(composite))
        return MediaResult::Fail(MediaError::BadRequest, *refused);
    if (composite.descriptor == ComposeDescriptor::kInlineName)
        return MediaResult::Fail(MediaError::BadRequest, "an inline descriptor cannot take write-back: write it to a file");
    const ComposeDescriptor d = ComposeDescriptor::Load(FileHelper::ToFsPath(composite.descriptor));
    if (!d.Ok())
        return MediaResult::Fail(MediaError::BadRequest, d.error);
    WriteBackOptions wb;
    wb.force = options.force;
    wb.keepBoth = options.keepBoth;
    WriteBackPlan plan;
    MediaResult planned = WriteBack::Plan(medium, d, wb, plan);
    if (!planned.Ok())
        return planned;

    MediaResult result = MediaResult::Success();
    for (const WriteBackStep& s : plan.steps)
    {
        std::string line = std::string(WriteBackStep::KindName(s.kind)) + " " + (s.partition.empty() ? s.path : s.partition + ":" + s.path);
        if (!s.layer.empty())
            line += " [" + s.layer + "]";
        if (!s.host.empty())
            line += " -> " + FileHelper::FromFsPath(s.host);
        if (s.kind == WriteBackStep::Kind::Write)
            line += " (" + std::to_string(s.bytes) + " bytes)";
        if (!s.detail.empty())
            line += ": " + s.detail;
        result.report.push_back(line);
    }
    for (const std::string& e : plan.errors)
        result.report.push_back("error: " + e);
    if (options.plan)
    {
        result.report.push_back(plan.errors.empty() ? "plan only: nothing was written" : "plan only: write-back would be refused");
        return result;
    }
    MediaResult applied = WriteBack::Apply(medium, d, plan);
    if (!applied.Ok())
    {
        applied.report.insert(applied.report.begin(), result.report.begin(), result.report.end());
        return applied;
    }
    result.report.insert(result.report.end(), applied.report.begin(), applied.report.end());

    // The host now holds the guest's files: build again, with an empty change layer
    medium.Session()->Discard();
    std::error_code ec;
    if (!composite.delta.empty() && std::filesystem::remove(composite.delta, ec))
        result.report.push_back(FileHelper::FromFsPath(composite.delta.filename()) + " removed: the layers hold its changes now");
    if (outcome)
    {
        outcome->savedPath = composite.descriptor;
        outcome->retargeted = false;
        outcome->note = result.report.empty() ? std::string() : result.report.back();
    }
    Post(NC_MEDIA_SAVED, slotId, &medium, composite.descriptor);
    if (auto it = _slots.find(slotId); it != _slots.end() && it->second.attached.get() == &medium)
    {
        RescanOptions again;
        again.force = true;  // the host files changed in place: the id may not show it
        const MediaResult rebuilt = Rescan(slotId, again);
        result.report.push_back(rebuilt.Ok() ? "rebuilt from the layers" : "rebuild: " + rebuilt.message);
    }
    return result;
}

bool MediaManager::UsedElsewhere(const Medium& self, const std::string& path) const
{
    const std::string key = FileHelper::AbsolutePath(path, /*resolveSymlinks*/ true);
    auto uses = [&key](const Medium& m) {
        if (m.SourceKey() == key)
            return true;
        if (const CompositeInfo* info = m.Composite())
            for (const CompositeLayerInfo& layer : info->layers)
                if (!layer.path.empty() && FileHelper::AbsolutePath(layer.path, true) == key)
                    return true;
        return false;
    };
    for (const auto& [id, state] : _slots)
        if (state.attached && state.attached.get() != &self && uses(*state.attached))
            return true;
    for (const auto& [id, parked] : _parked)
        if (parked && parked.get() != &self && uses(*parked))
            return true;
    return false;
}

MediaResult MediaManager::CommitComposite(const std::string& slotId, Medium& medium, IMediaSlot* slot, const CompositeInfo& composite,
                                          const SaveOptions& options, SaveOutcome* outcome)
{
    if (const std::optional<std::string> refused = CommitRefusal(composite))
        return MediaResult::Fail(MediaError::BadRequest, *refused);
    const std::string policy = options.commit.empty() ? composite.writesCommit : options.commit;
    if (policy != "all" && policy != "partial")
        return MediaResult::Fail(MediaError::BadRequest, "commit '" + policy + "': expected all or partial");
    // DT-14: a graft over a writable image, used nowhere else, with a consistent guest file system
    SessionWriteMap* session = medium.Session();
    if (session && dynamic_cast<const PartitionedDisk*>(&session->Base()))
        return CommitPartitioned(slotId, medium, composite, options, policy == "partial", outcome);
    const auto* graft = session ? dynamic_cast<const GraftVolume*>(&session->Base()) : nullptr;
    if (!graft || composite.layers.empty())
        return MediaResult::Fail(MediaError::BadRequest, "commit needs a graft composite (a FAT image at the bottom, build: graft): "
                                                         "flatten it to an image (strategy flat) instead");
    const std::string base = composite.layers[0].path;
    std::string error;
    const std::string format = HddImageFormats::Probe(base, &error);
    if (format != "raw" && format != "hdf" && format != "hdi" && format != "vhd")
        return MediaResult::Fail(MediaError::NotSupported, "the base " + base + " is " + (format.empty() ? error : "a " + format + " image") +
                                                               ": it cannot be written in place; flatten to a .chd child or an image instead");
    if (UsedElsewhere(medium, base))
        return MediaResult::Fail(MediaError::InUse, "the base " + base + " is in use in another slot: a commit needs it alone");
    MediumChanges changes;
    if (ListMediumChanges(medium, changes).Ok() && !options.force)
    {
        for (const std::string& w : changes.warnings)
            if (w.find("lost clusters") != std::string::npos || w.find("cross-linked") != std::string::npos)
                return MediaResult::Fail(MediaError::Dirty, "the guest's file system is inconsistent (" + w +
                                                                "): commit with force, or flatten to an image");
    }

    // The plan: patches, grafted files, guest writes; each sector as the composite reads it now. Walked from the
    // three sorted sources each time (C8d): no list of every sector in memory
    std::vector<uint64_t> patches = graft->PatchLbas();
    std::sort(patches.begin(), patches.end());
    std::vector<std::pair<uint64_t, uint64_t>> runs = graft->GraftedSectorRuns();
    std::sort(runs.begin(), runs.end());
    uint64_t grafted = 0;
    for (const auto& run : runs)
        grafted += run.second;
    auto walk = [&]() {
        return [&, p = size_t(0), r = size_t(0), offset = uint64_t(0), guest = session->NextChanged(0),
                last = std::optional<uint64_t>()]() mutable -> std::optional<uint64_t> {
            for (;;)
            {
                std::optional<uint64_t> next;
                auto offer = [&next](uint64_t lba) { next = next ? std::min(*next, lba) : lba; };
                if (p < patches.size())
                    offer(patches[p]);
                if (r < runs.size())
                    offer(runs[r].first + offset);
                if (guest)
                    offer(*guest);
                if (!next)
                    return std::nullopt;
                if (p < patches.size() && patches[p] == *next)
                    p++;
                if (r < runs.size() && runs[r].first + offset == *next && ++offset == runs[r].second)
                {
                    r++;
                    offset = 0;
                }
                if (guest && *guest == *next)
                    guest = session->NextChanged(*guest + 1);
                if (last && *last == *next)
                    continue;  // in two sources
                last = next;
                return next;
            }
        };
    };
    uint64_t total = 0;
    uint64_t last = 0;
    {
        auto next = walk();
        for (std::optional<uint64_t> lba = next(); lba; lba = next())
        {
            total++;
            last = *lba;
        }
    }
    MediaResult result = MediaResult::Success();
    result.report.push_back("commit into " + base + ": " + std::to_string(total) + " sectors (" +
                            std::to_string(patches.size()) + " re-encoded, " + std::to_string(grafted) +
                            " of grafted files, " + std::to_string(session->ChangedSectors()) + " written by the guest)");
    if (options.plan)
    {
        result.report.push_back("plan only: nothing was written");
        return result;
    }

    const std::filesystem::path basePath = FileHelper::ToFsPath(base);
    auto device = HddImageFormats::OpenBlock(base, format, RawImage::Access::ReadWrite, &error);
    if (!device)
        return MediaResult::Fail(MediaError::IoError, "cannot open " + base + " for writing: " + error);
    const uint64_t originalSectors = device->SectorCount();
    if (!CommitJournal::Write(basePath, *device, walk(), &error))
        return MediaResult::Fail(MediaError::IoError, error);
    if (last >= originalSectors)
    {
        // A cut-down image: grafted files land past its end, the file grows to hold them
        if (format != "raw")
        {
            CommitJournal::Remove(basePath);
            return MediaResult::Fail(MediaError::NotSupported, "the base " + base + " ends before its volume does: only a raw image can grow");
        }
        device.reset();
        std::error_code ec;
        std::filesystem::resize_file(basePath, (last + 1) * IBlockDevice::kSectorSize, ec);
        device = ec ? nullptr : HddImageFormats::OpenBlock(base, format, RawImage::Access::ReadWrite, &error);
        if (!device)
            return MediaResult::Fail(MediaError::IoError, "cannot grow " + base + ": the journal " +
                                                              FileHelper::FromFsPath(CommitJournal::PathFor(basePath)) + " undoes it at the next open");
    }
    uint8_t sector[IBlockDevice::kSectorSize];
    auto next = walk();
    for (std::optional<uint64_t> at = next(); at; at = next())
    {
        const uint64_t lba = *at;
        if (!session->ReadSector(lba, sector) || !device->WriteSector(lba, sector))
            return MediaResult::Fail(MediaError::IoError, "writing sector " + std::to_string(lba) + " of " + base + " failed: the journal " +
                                                              FileHelper::FromFsPath(CommitJournal::PathFor(basePath)) +
                                                              " restores the image at the next open");
    }
    device.reset();
    if (!CommitJournal::Sync(basePath))
        return MediaResult::Fail(MediaError::IoError, "cannot sync " + base + ": the journal is kept");
    CommitJournal::Remove(basePath);

    // The slot now holds the base itself: it has every layer's files and the guest's writes
    std::unique_ptr<IBlockDevice> old = session->ReleaseBase();
    old.reset();
    auto reopened = HddImageFormats::OpenBlock(base, format, RawImage::Access::ReadOnly, &error);
    if (!reopened)
        return MediaResult::Fail(MediaError::IoError, "committed, but " + base + " cannot be opened again: " + error);
    session->SetBase(std::move(reopened));
    session->Discard();
    // The session delta was written over the composite; the base holds those writes now (and SetComposite below
    // drops `composite`)
    const std::filesystem::path delta = composite.delta;
    MediaSource source;
    source.type = MediaSourceType::File;
    source.path = base;
    medium.Rebase(source);
    medium.SetFormat(format);
    medium.SetComposite(nullptr);
    if (slot)
        slot->SourceChanged(medium);
    result.report.push_back("the slot now holds " + base + "; the descriptor still names its upper layers (inserting it again "
                            "grafts them again)");
    std::error_code removeError;
    if (!delta.empty() && std::filesystem::remove(delta, removeError))
        result.report.push_back(FileHelper::FromFsPath(delta.filename()) + " removed: the base image holds its changes now");
    if (outcome)
    {
        outcome->savedPath = base;
        outcome->retargeted = false;
        outcome->note = result.report.front();
    }
    Post(NC_MEDIA_SAVED, slotId, &medium, base);
    return result;
}

MediaResult MediaManager::CommitPartitioned(const std::string& slotId, Medium& medium, const CompositeInfo& composite,
                                            const SaveOptions& options, bool partial, SaveOutcome* outcome)
{
    SessionWriteMap* session = medium.Session();
    const auto* disk = dynamic_cast<const PartitionedDisk*>(&session->Base());
    if (composite.descriptor == ComposeDescriptor::kInlineName)
        return MediaResult::Fail(MediaError::BadRequest, "an inline descriptor cannot be committed: write it to a file");
    const ComposeDescriptor d = ComposeDescriptor::Load(FileHelper::ToFsPath(composite.descriptor));
    if (!d.Ok())
        return MediaResult::Fail(MediaError::BadRequest, d.error);
    const std::vector<PartitionedDisk::Part>& parts = disk->Parts();
    if (d.partitions.size() != parts.size())
        return MediaResult::Fail(MediaError::BadRequest, "the descriptor's partitions changed since the build: rescan first");
    MediumChanges changes;
    if (ListMediumChanges(medium, changes).Ok() && !options.force)
    {
        for (const std::string& w : changes.warnings)
            if (w.find("lost clusters") != std::string::npos || w.find("cross-linked") != std::string::npos)
                return MediaResult::Fail(MediaError::Dirty, "the guest's file system is inconsistent (" + w +
                                                                "): commit with force, or flatten to an image");
    }

    // Where each partition goes: a passthrough into its image (at its partition's place), a graft into its base
    struct Target
    {
        size_t part = 0;
        std::string file;
        std::string format;
        uint64_t offset = 0;      ///< the volume's first sector in the file
        uint64_t fileSectors = 0;
        CommitWalk walk;
    };
    std::vector<Target> targets;
    targets.reserve(parts.size());  // the walks are referred to by address
    std::vector<std::string> blocked;
    const auto guestWrites = [session](uint64_t start, uint64_t sectors) {
        const std::optional<uint64_t> lba = session->NextChanged(start);
        return lba && *lba < start + sectors;
    };
    for (size_t i = 0; i < parts.size(); i++)
    {
        const PartitionedDisk::Part& part = parts[i];
        const ComposePartition& p = d.partitions[i];
        const std::string what = "partition '" + part.name + "'";
        std::optional<ComposeSource> source = p.source;
        const auto* graft = dynamic_cast<const GraftVolume*>(part.device.get());
        if (!source && graft && p.compose && !p.compose->layers.empty())
            source = p.compose->layers.front().source;
        if (!source || (!p.source && !graft))
        {
            if (guestWrites(part.start, part.sectors))
                blocked.push_back(what + " is rebuilt from folders: its changes have no image to go to (write them back, "
                                         "or flatten to an image)");
            continue;
        }
        Target t;
        t.part = i;
        t.file = FileHelper::FromFsPath(source->path);
        std::string error;
        t.format = HddImageFormats::Probe(t.file, &error);
        if (t.format != "raw" && t.format != "hdf" && t.format != "hdi" && t.format != "vhd")
            return MediaResult::Fail(MediaError::NotSupported, what + ": " + t.file + " is " +
                                                                   (t.format.empty() ? error : "a " + t.format + " image") +
                                                                   ": it cannot be written in place");
        auto image = HddImageFormats::OpenBlock(t.file, t.format, RawImage::Access::ReadOnly, &error);
        if (!image)
            return MediaResult::Fail(MediaError::IoError, what + ": cannot open " + t.file + ": " + error);
        t.fileSectors = image->SectorCount();
        if (source->partition)
        {
            FatPartition found;
            if (!FatVolumeReader::FindPartition(*image, *source->partition, found, &error))
                return MediaResult::Fail(MediaError::BadRequest, what + ": " + t.file + ": " + error);
            t.offset = found.first;
        }
        if (UsedElsewhere(medium, t.file))
            return MediaResult::Fail(MediaError::InUse, what + ": " + t.file + " is in use in another slot: a commit needs it alone");
        t.walk.session = session;
        t.walk.start = part.start;
        t.walk.sectors = part.sectors;
        if (graft)
        {
            t.walk.patches = graft->PatchLbas();
            std::sort(t.walk.patches.begin(), t.walk.patches.end());
            t.walk.runs = graft->GraftedSectorRuns();
            std::sort(t.walk.runs.begin(), t.walk.runs.end());
        }
        targets.push_back(std::move(t));
    }
    // The partition tables (the MBR, the EBRs) and the gaps between partitions belong to no source
    {
        std::optional<uint64_t> lba = session->NextChanged(0);
        while (lba)
        {
            const bool inside = std::any_of(parts.begin(), parts.end(), [&lba](const PartitionedDisk::Part& part) {
                return *lba >= part.start && *lba < part.start + part.sectors;
            });
            if (!inside)
            {
                blocked.push_back("sector " + std::to_string(*lba) + " (the partition table or a gap) was written: it has no source");
                break;
            }
            lba = session->NextChanged(*lba + 1);
        }
    }

    MediaResult result = MediaResult::Success();
    std::vector<uint64_t> counts(targets.size(), 0);
    for (size_t k = 0; k < targets.size(); k++)
    {
        const Target& t = targets[k];
        uint64_t last = 0;
        auto next = t.walk.Make();
        for (std::optional<uint64_t> lba = next(); lba; lba = next())
        {
            counts[k]++;
            last = *lba;
        }
        if (counts[k] && t.offset + last >= t.fileSectors)
            return MediaResult::Fail(MediaError::NotSupported, "partition '" + parts[t.part].name + "': " + t.file +
                                                                   " ends before its volume does: a partitioned commit does not grow images");
        if (counts[k])
            result.report.push_back("partition '" + parts[t.part].name + "' into " + t.file + ": " + std::to_string(counts[k]) +
                                    " sectors" + (t.offset ? " at sector " + std::to_string(t.offset) : std::string()));
    }
    if (!blocked.empty() && !partial)
    {
        std::string why;
        for (const std::string& b : blocked)
            why += (why.empty() ? "" : "; ") + b;
        return MediaResult::Fail(MediaError::BadRequest, "commit (writes.commit: all) writes everything or nothing: " + why +
                                                             ". Nothing was written (commit partial leaves these in the session)");
    }
    for (const std::string& b : blocked)
        result.report.push_back("left in the session: " + b);
    const bool anything = std::any_of(counts.begin(), counts.end(), [](uint64_t c) { return c > 0; });
    if (!anything)
        return MediaResult::Fail(MediaError::BadRequest, "nothing to commit: no partition with a source holds changes");
    if (options.plan)
    {
        result.report.push_back("plan only: nothing was written");
        return result;
    }

    // Every journal first (one per file, its sectors in order), then the sectors, then the journals go: a commit cut
    // short is undone in every file at its next open
    std::vector<std::string> files;
    for (const Target& t : targets)
        if (std::find(files.begin(), files.end(), t.file) == files.end())
            files.push_back(t.file);
    std::vector<size_t> order(targets.size());
    for (size_t k = 0; k < order.size(); k++)
        order[k] = k;
    std::sort(order.begin(), order.end(), [&targets](size_t a, size_t b) { return targets[a].offset < targets[b].offset; });
    std::vector<std::filesystem::path> journaled;
    const auto undo = [](const std::string& why) {
        return MediaResult::Fail(MediaError::IoError, why + ": the journals restore the images at their next open");
    };
    for (const std::string& file : files)
    {
        std::string error;
        const std::string format = HddImageFormats::Probe(file, &error);
        auto device = HddImageFormats::OpenBlock(file, format, RawImage::Access::ReadWrite, &error);
        if (!device)
        {
            for (const std::filesystem::path& j : journaled)
                CommitJournal::Remove(j);
            return MediaResult::Fail(MediaError::IoError, "cannot open " + file + " for writing: " + error);
        }
        // This file's targets, in file order: their walks one after another give its sectors in order
        size_t k = 0;
        std::function<std::optional<uint64_t>()> walk;
        uint64_t offset = 0;
        std::function<std::optional<uint64_t>()> fileWalk = [&]() -> std::optional<uint64_t> {
            for (;;)
            {
                if (walk)
                    if (const std::optional<uint64_t> lba = walk())
                        return offset + *lba;
                while (k < order.size() && targets[order[k]].file != file)
                    k++;
                if (k == order.size())
                    return std::nullopt;
                walk = targets[order[k]].walk.Make();
                offset = targets[order[k]].offset;
                k++;
            }
        };
        const std::filesystem::path path = FileHelper::ToFsPath(file);
        if (!CommitJournal::Write(path, *device, fileWalk, &error))
        {
            for (const std::filesystem::path& j : journaled)
                CommitJournal::Remove(j);
            return MediaResult::Fail(MediaError::IoError, error);
        }
        journaled.push_back(path);
    }
    uint8_t sector[IBlockDevice::kSectorSize];
    for (const std::string& file : files)
    {
        std::string error;
        const std::string format = HddImageFormats::Probe(file, &error);
        auto device = HddImageFormats::OpenBlock(file, format, RawImage::Access::ReadWrite, &error);
        if (!device)
            return undo("cannot open " + file + " for writing");
        for (const Target& t : targets)
        {
            if (t.file != file)
                continue;
            auto next = t.walk.Make();
            for (std::optional<uint64_t> lba = next(); lba; lba = next())
                if (!session->ReadSector(t.walk.start + *lba, sector) || !device->WriteSector(t.offset + *lba, sector))
                    return undo("writing sector " + std::to_string(t.offset + *lba) + " of " + file + " failed");
        }
        device.reset();
        if (!CommitJournal::Sync(FileHelper::ToFsPath(file)))
            return undo("cannot sync " + file);
    }
    for (const std::filesystem::path& j : journaled)
        CommitJournal::Remove(j);

    // The slot is built again from the descriptor over the committed sources; what had no source (partial) goes
    // into the new session sector by sector, at the same places (the layout does not move)
    InsertOptions again;
    again.access = medium.Access();
    again.fs = medium.Options().fs;
    again.codePage = medium.Options().codePage;
    again.freeBytes = medium.Options().freeBytes;
    std::unique_ptr<Medium> fresh;
    MediaResult opened = OpenForSlot(slotId, medium.Source(), again, fresh);
    if (!opened.Ok())
        return MediaResult::Fail(opened.error, "committed, but the composite cannot be built again: " + opened.message);
    uint64_t kept = 0;
    if (!blocked.empty() && fresh->Session())
    {
        for (std::optional<uint64_t> lba = session->NextChanged(0); lba; lba = session->NextChanged(*lba + 1))
        {
            const bool committed = std::any_of(targets.begin(), targets.end(), [&lba, &parts](const Target& t) {
                return *lba >= parts[t.part].start && *lba < parts[t.part].start + parts[t.part].sectors;
            });
            if (committed)
                continue;
            if (!session->ReadSector(*lba, sector) || !fresh->Session()->WriteSector(*lba, sector))
                return MediaResult::Fail(MediaError::IoError, "committed, but sector " + std::to_string(*lba) +
                                                                  " left in the session could not be carried over");
            kept++;
        }
    }
    const std::filesystem::path delta = composite.delta;
    const std::string descriptor = composite.descriptor;
    again.disposition = Disposition::Discard;
    again.immediate = true;
    MediaResult inserted = Insert(slotId, std::move(fresh), again);  // `medium` and `composite` are gone after this
    if (!inserted.Ok())
        return MediaResult::Fail(inserted.error, "committed, but the slot could not take the new build: " + inserted.message);
    result.report.push_back(kept ? std::to_string(kept) + " sector(s) without a source stay in the session"
                                 : "the slot was built again over the committed sources");
    std::error_code removeError;
    if (!kept && !delta.empty() && std::filesystem::remove(delta, removeError))
        result.report.push_back(FileHelper::FromFsPath(delta.filename()) + " removed: the sources hold its changes now");
    if (outcome)
    {
        outcome->savedPath = descriptor;
        outcome->retargeted = false;
        outcome->note = result.report.front();
    }
    SlotState* state = nullptr;
    if (Medium* now = FindMedium(slotId, &state))
    {
        // A medium that leaves keeps what had no source as its session delta (D-8)
        if (kept && options.disposition && now->Composite())
        {
            SaveOptions deltaOnly;
            deltaOnly.disposition = true;
            MediaResult saved = SaveDelta(slotId, *now, *now->Composite(), deltaOnly, {}, nullptr);
            result.report.insert(result.report.end(), saved.report.begin(), saved.report.end());
            if (!saved.Ok())
                return MediaResult::Fail(saved.error, "committed; the changes left in the session could not be kept: " + saved.message);
        }
        Post(NC_MEDIA_SAVED, slotId, now, descriptor);
    }
    return result;
}

bool MediaManager::CanApplyNow() const
{
    if (_releasing)
        return true;
    if (_applyNowProbe)
        return _applyNowProbe();
    Emulator* emulator = _context ? _context->pEmulator : nullptr;
    // Not running, paused and parked, or the caller holds the CPU (Emulator::CpuHold: LoadDisk, LoadTape, ...)
    return emulator == nullptr || emulator->CanMutateNow();
}

MediaResult MediaManager::CheckInUse(const std::string& slotId, const Medium& medium) const
{
    auto conflicts = [&medium](const Medium* other) {
        return other && other->SourceKey() == medium.SourceKey() &&
               !(other->Access() == AccessMode::ReadOnly && medium.Access() == AccessMode::ReadOnly);
    };

    for (const auto& [id, state] : _slots)
    {
        if (id == slotId)
            continue;
        if (conflicts(state.attached.get()) || conflicts(state.incoming.get()))
            return MediaResult::Fail(MediaError::InUse, "'" + medium.Source().path + "' is already in slot '" + id +
                                                            "' (only read-only media can share a source)");
    }
    for (const auto& [id, parked] : _parked)
    {
        if (id != slotId && conflicts(parked.get()))
            return MediaResult::Fail(MediaError::InUse, "'" + medium.Source().path + "' is parked for slot '" + id + "'");
    }
    return MediaResult::Success();
}

uint32_t MediaManager::DelayFrames(uint32_t swapDelayMs) const
{
    if (swapDelayMs == 0)
        return 0;
    const uint64_t frameUs = (_context && _context->config.frame_duration_us) ? _context->config.frame_duration_us : 20000;
    return static_cast<uint32_t>((static_cast<uint64_t>(swapDelayMs) * 1000 + frameUs - 1) / frameUs);
}

void MediaManager::HoldHostWrites(bool hold)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _holdHostWrites = hold;
    for (auto& [slotId, state] : _slots)
        if (state.attached)
            if (HostWriteHold* h = state.attached->Hold())
                if (!h->SetHolding(hold))
                    LOGWARNING("MediaManager: %s: a sector written during a replay could not be written to its file",
                               slotId.c_str());
    FollowReplay();
}

void MediaManager::FollowReplay()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    const uint64_t frame = _context ? _context->emulatorState.frame_counter : 0;
    // On the recorded history: a replay, or the machine at (or running from) a point in the past
    const ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    const bool inHistory = _holdHostWrites || (ttd && ttd->GetState() == ttd::TTDSessionState::Detached);
    for (auto& [slotId, state] : _slots)
    {
        // A replay reaching the frame boundary where the recording's write-protect toggle landed sets it again
        if (_context && _context->ttdReplayActive && state.followsHistory)
            if (const std::optional<bool> on = state.undo.SwitchAt(state.writtenFrames, frame))
            {
                state.writeProtect = *on;
                if (state.attached && !state.heldForReplay)
                    state.slot->SetWriteProtectSwitch(*on);
                ++state.writtenFrames;
                ++_writeStamp;
            }
        const bool hold = inHistory && state.attached && frame < state.landedFrame;
        if (hold == state.heldForReplay)
            continue;
        if (hold)
            state.slot->Detach();
        else if (state.attached)
        {
            state.slot->Attach(*state.attached);
            state.slot->SetWriteProtectSwitch(state.writeProtect);
            // Back live before the medium came in (a recording resumed from the past): it is in from here
            if (!inHistory && frame < state.landedFrame)
                state.landedFrame = frame;
        }
        state.heldForReplay = hold;
    }
}

void MediaManager::WriteThroughFloppy(const std::string& slotId, SlotState& state)
{
    Medium& medium = *state.attached;
    DiskImage* disk = medium.Floppy();
    if (!disk->isDirty() || medium.Source().type != MediaSourceType::File || medium.Source().path.empty())
        return;
    const FloppySaveResult written = FloppyFormats::Save(_context, *disk, medium.Source().path, /*allowRetarget*/ false);
    if (!written.saved)
    {
        // The file's format cannot hold what the guest wrote (a TRD after a
        // non-TR-DOS format): keep the disk in memory; Info shows "session"
        // and an explicit save can retarget it
        medium.SetAccess(AccessMode::Session);
        return;
    }
    disk->markClean();
    Post(NC_MEDIA_SAVED, slotId, &medium, written.savedPath);
}

void MediaManager::Post(const char* topic, const std::string& slotId, const Medium* medium, const std::string& path) const
{
    if (_releasing)
        return;  // the emulator is going away: nobody is left to tell

    std::string kind;
    std::string source;
    std::string access;
    if (medium)
    {
        kind = MediaKindName(medium->Kind());
        source = medium->Source().path.empty() ? medium->Describe() : medium->Source().path;
        access = AccessModeName(medium->Access());
    }
    _revision++;  // every notification is a change a polling client must see
    auto* payload = new MediaSlotPayload(std::string(), slotId, kind, source, access);
    // Every payload names its emulator: the context's id is there from the start (the emulator object may not be)
    if (_context)
        payload->emulatorId = _context->emulatorId;
    payload->path = path;
    if (medium)
        payload->volumeId = medium->VolumeId();
    if (medium && medium->IsDirty())
        payload->onRelease = OnReleaseOf(*medium);
    MessageCenter::DefaultMessageCenter().Post(topic, payload, true);
}

void MediaManager::AttachJournal(Medium& medium, const MediaSource& source, JournalChoice choice)
{
    SessionWriteMap* session = medium.Session();
    if (!session)
        return;
    if (choice == JournalChoice::Default)
        choice = SessionWriteMap::Defaults().journal ? JournalChoice::Replay : JournalChoice::Off;
    // A source of its own on the host: an image, a folder, a descriptor (not an inline one, not an upload)
    std::string path = source.path;
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
        path.pop_back();
    if (path.empty() || !source.inlineBody.empty() || source.type == MediaSourceType::Upload ||
        source.type == MediaSourceType::Blank)
        return;
    path += ".usession";

    const SessionWriteMap::JournalMode mode = choice == JournalChoice::Discard ? SessionWriteMap::JournalMode::Discard
                                              : choice == JournalChoice::Off   ? SessionWriteMap::JournalMode::Off
                                                                               : SessionWriteMap::JournalMode::Replay;
    const SessionWriteMap::JournalOpen open = session->OpenJournal(path, mode);
    const std::string name = FileHelper::FromFsPath(FileHelper::ToFsPath(path).filename());
    using Outcome = SessionWriteMap::JournalOpen::Outcome;
    switch (open.outcome)
    {
        case Outcome::Created:
        case Outcome::Off:
            break;
        case Outcome::Replayed:
            medium.Report().push_back("session journal " + name + " replayed: " + std::to_string(open.sectors) +
                                      " sector(s) the guest wrote before the emulator stopped" +
                                      (open.badSlots ? " (" + std::to_string(open.badSlots) + " damaged slot(s) skipped)" : ""));
            break;
        case Outcome::Discarded:
            medium.Report().push_back("session journal " + name + " discarded unread (journal: discard)");
            break;
        case Outcome::Stale:
            medium.Report().push_back("session journal " + name + " not replayed, " + open.detail + ": kept as " + open.kept);
            break;
        case Outcome::InUse:
            medium.Report().push_back("session journal " + name + " is used by another slot: this one keeps its writes in a "
                                      "temp file (not recoverable after a crash)");
            break;
    }
}

void MediaManager::Retire(std::unique_ptr<Medium> medium, bool keepJournal)
{
    if (!medium)
        return;
    if (SessionWriteMap* session = medium->Session())
        session->CloseJournal(keepJournal);
    // A staged upload's file goes with its medium - after the medium has
    // closed it (an open file cannot be deleted on Windows)
    const bool upload = medium->Source().type == MediaSourceType::Upload && !medium->Source().path.empty();
    const std::string path = medium->Source().path;
    medium.reset();
    if (upload)
    {
        std::error_code ec;
        std::filesystem::remove(FileHelper::ToFsPath(path), ec);
    }
}

/// endregion </Helpers>
