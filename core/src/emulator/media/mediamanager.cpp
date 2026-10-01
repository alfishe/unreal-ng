#include "stdafx.h"

#include "mediamanager.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "common/logger.h"
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
}  // namespace

MediaManager::MediaManager(EmulatorContext* context) : _context(context) {}

MediaManager::~MediaManager()
{
    // Peripherals unregister before the manager goes; anything left is plain
    // data. Staged uploads die with their medium
    for (auto& [id, state] : _slots)
    {
        Retire(std::move(state.attached));
        Retire(std::move(state.incoming));
    }
    for (auto& [id, medium] : _parked)
        Retire(std::move(medium));
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
        result.push_back(std::move(info));
    }
    return result;
}

MediaResult MediaManager::Insert(const std::string& slotId, const MediaSource& source, const InsertOptions& options)
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
    const bool folder = source.type == MediaSourceType::Folder || FileHelper::IsFolder(source.path);
    if (!options.access && folder && request.access == AccessMode::WriteThrough)
        request.access = AccessMode::Session;
    request.fs = options.fs.value_or(descriptor.defaultFs);
    request.allowedFs = descriptor.fsCompatibility;
    request.codePage = options.codePage;
    request.freeBytes = options.freeBytes;

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

    std::unique_ptr<Medium> medium;
    MediaResult opened = MediaFormatRegistry::Open(request, medium);
    if (!opened.Ok())
        return opened;

    // The same matrix for an inserted image: a FAT volume of a flavour the
    // controller cannot read is refused. A folder cannot land here wrong - it
    // was built into an allowed flavour above; a non-FAT image is none of the
    // matrix's business (a blank disk the guest formats itself is legitimate)
    if (!descriptor.fsCompatibility.empty() && !folder && medium->Kind() == MediaKind::Block && medium->Block())
    {
        if (const std::optional<FatType> fs = ProbeFatType(*medium->Block());
            fs && std::find(descriptor.fsCompatibility.begin(), descriptor.fsCompatibility.end(), *fs)
                     == descriptor.fsCompatibility.end())
        {
            return MediaResult::Fail(MediaError::BadRequest,
                                     "slot '" + slotId + "' reads " + FatTypeNames(descriptor.fsCompatibility) +
                                         " volumes: '" + source.path + "' is a " + FatTypeName(*fs) + " volume");
        }
    }

    // Advisory only: does sector 0 look like the layout this slot's boot path
    // actually reads (MBR-partitioned vs. raw FAT)? Never refuses the insert
    if ((medium->Kind() == MediaKind::Block) && medium->Block())
    {
        std::string mismatch = DescribeBlockLayoutMismatch(*medium->Block(), descriptor.tags);
        if (!mismatch.empty())
            medium->Report().push_back(std::move(mismatch));
    }

    MediaResult inserted = Insert(slotId, std::move(medium), options);
    inserted.report.insert(inserted.report.begin(), opened.report.begin(), opened.report.end());
    return inserted;
}

MediaResult MediaManager::Insert(const std::string& slotId, std::unique_ptr<Medium> medium, const InsertOptions& options)
{
    if (!medium)
        return MediaResult::Fail(MediaError::UnreadableSource, "no medium");

    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");

    SlotState& state = it->second;
    const SlotDescriptor& descriptor = state.slot->Descriptor();
    if (medium->Kind() != descriptor.kind)
        return MediaResult::Fail(MediaError::KindMismatch, std::string("slot '") + slotId + "' takes " +
                                                               MediaKindName(descriptor.kind) + " media, not " +
                                                               MediaKindName(medium->Kind()));

    if (MediaResult inUse = CheckInUse(slotId, *medium); !inUse.Ok())
        return inUse;
    if (MediaResult recording = CheckRecording(options.endRecording); !recording.Ok())
        return recording;
    // The medium in the slot leaves: its unsaved writes need a decision
    if (state.attached && !state.ejectRequested)
    {
        if (MediaResult kept = ApplyDisposition(slotId, *state.attached, options.disposition, options.exportPath); !kept.Ok())
            return kept;
    }
    MediaResult result = MediaResult::Success();
    result.report = medium->Report();

    // Replace any insert still waiting; the medium in the slot goes out first
    if (state.incoming)
        Retire(std::move(state.incoming));
    state.incoming = std::move(medium);
    state.writeProtect = options.writeProtect;
    if (state.attached)
        state.ejectRequested = true;
    state.emptyFramesLeft = (state.attached && !options.immediate) ? DelayFrames(state.swapDelayMs.value_or(descriptor.swapDelayMs)) : 0;

    if (CanApplyNow())
    {
        std::vector<std::unique_ptr<Medium>> retired;
        state.emptyFramesLeft = 0;  // nobody runs to see the empty slot
        ApplySlot(slotId, state, retired);
        for (auto& old : retired)
            Retire(std::move(old));
    }
    return result;
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
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");

    SlotState& state = it->second;
    if (!state.attached && !state.incoming)
        return MediaResult::Success();

    if (MediaResult recording = CheckRecording(options.endRecording); !recording.Ok())
        return recording;
    if (state.attached && !state.ejectRequested)
    {
        if (MediaResult kept = ApplyDisposition(slotId, *state.attached, options.disposition, options.exportPath); !kept.Ok())
            return kept;
    }

    if (state.incoming)
        Retire(std::move(state.incoming));
    state.ejectRequested = state.attached != nullptr;

    if (CanApplyNow())
    {
        std::vector<std::unique_ptr<Medium>> retired;
        ApplySlot(slotId, state, retired);
        for (auto& old : retired)
            Retire(std::move(old));
    }
    return MediaResult::Success();
}

MediaResult MediaManager::Discard(const std::string& slotId)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto parked = _parked.find(slotId);
    if (parked != _parked.end() && !_slots.count(slotId))
    {
        // A detached medium is dropped with its writes
        std::unique_ptr<Medium> gone = std::move(parked->second);
        _parked.erase(parked);
        Post(NC_MEDIA_EJECTED, slotId, gone.get());
        Retire(std::move(gone));
        return MediaResult::Success();
    }

    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
    if (MediaResult recording = CheckRecording(false); !recording.Ok())
        return recording;

    SlotState& state = it->second;
    if (!state.attached)
        return MediaResult::Success();

    if (state.attached->Floppy())
    {
        // The writes live in the disk image itself: open the source again
        const Medium& disk = *state.attached;
        if (disk.Source().type != MediaSourceType::File && disk.Source().type != MediaSourceType::Folder)
            return MediaResult::Fail(MediaError::NotSupported,
                                     "slot '" + slotId + "' holds a disk without a source to go back to: eject it with discard");
        MediaSource source = disk.Source();
        InsertOptions again;
        again.access = disk.Access();
        again.fs = disk.Options().fs;
        again.codePage = disk.Options().codePage;
        again.freeBytes = disk.Options().freeBytes;
        again.writeProtect = state.writeProtect;
        again.immediate = true;
        again.disposition = Disposition::Discard;
        return Insert(slotId, source, again);
    }
    if (!state.attached->Session())
        return MediaResult::Success();

    state.discardRequested = true;
    if (CanApplyNow())
    {
        std::vector<std::unique_ptr<Medium>> retired;
        ApplySlot(slotId, state, retired);
    }
    return MediaResult::Success();
}

MediaResult MediaManager::Export(const std::string& slotId, const std::string& path)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    SlotState* state = nullptr;
    Medium* medium = FindMedium(slotId, &state);
    if (!medium)
        return MediaResult::Fail(_slots.count(slotId) ? MediaError::UnreadableSource : MediaError::UnknownSlot,
                                 "slot '" + slotId + "' is empty");
    return ExportMedium(slotId, *medium, path);
}

MediaResult MediaManager::Save(const std::string& slotId, const SaveOptions& options, SaveOutcome* outcome)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    SlotState* state = nullptr;
    Medium* medium = FindMedium(slotId, &state);
    if (!medium)
        return MediaResult::Fail(_slots.count(slotId) ? MediaError::UnreadableSource : MediaError::UnknownSlot,
                                 "slot '" + slotId + "' is empty");
    MediaResult result = SaveMedium(slotId, *medium, state ? state->slot : nullptr, options, outcome);
    if (result.Ok() && state)
    {
        state->changedUnits = 0;
        state->changes.clear();
    }
    return result;
}

MediaResult MediaManager::SetWriteProtect(const std::string& slotId, bool on)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
    it->second.writeProtect = on;
    if (it->second.attached)
        it->second.slot->SetWriteProtectSwitch(on);
    _revision++;
    return MediaResult::Success();
}

MediaResult MediaManager::Rescan(const std::string& slotId)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
    SlotState& state = it->second;
    if (!state.attached || state.attached->Source().type != MediaSourceType::Folder)
        return MediaResult::Fail(MediaError::NotSupported, "slot '" + slotId + "' does not hold a folder");
    const uint64_t changed = CanApplyNow() ? state.attached->ChangedUnits() : state.changedUnits;
    if (changed > 0)
        return MediaResult::Fail(MediaError::Dirty, "slot '" + slotId + "' has " + std::to_string(changed) +
                                                        " unsaved changes: export or discard them before a rescan");

    const Medium& current = *state.attached;
    MediaSource source = current.Source();
    InsertOptions again;
    again.access = current.Access();
    again.fs = current.Options().fs;
    again.codePage = current.Options().codePage;
    again.freeBytes = current.Options().freeBytes;
    again.writeProtect = state.writeProtect;
    return Insert(slotId, source, again);
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

void MediaManager::ApplyPending()
{
    std::vector<std::unique_ptr<Medium>> retired;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (auto& [id, state] : _slots)
            ApplySlot(id, state, retired);
    }
    for (auto& old : retired)
        Retire(std::move(old));
    _applied.notify_all();
}

void MediaManager::NoteWrite(const std::string& slotId, const char* detail)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end() || it->second.writeMarkedThisFrame)
        return;
    it->second.writeMarkedThisFrame = true;

    ttd::TimeTravelManager* ttd = _context ? _context->pTimeTravelManager : nullptr;
    if (ttd && ttd->IsRecording())
    {
        std::string reason = "Media write " + slotId;
        if (detail && *detail)
            reason += std::string(": ") + detail;
        ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite, reason.c_str());
    }
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

    if (state.ejectRequested)
    {
        if (state.slot->IsBusy())
            return;  // a transfer in flight: next frame
        state.slot->Detach();
        Post(NC_MEDIA_EJECTED, slotId, state.attached.get());
        retired.push_back(std::move(state.attached));
        state.ejectRequested = false;
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
        state.slot->Attach(*state.attached);
        state.slot->SetWriteProtectSwitch(state.writeProtect);
        state.changedUnits = state.attached->ChangedUnits();
        Post(NC_MEDIA_INSERTED, slotId, state.attached.get());
    }

    if (state.discardRequested)
    {
        if (state.attached && state.attached->Session())
            state.attached->Session()->Discard();
        state.discardRequested = false;
    }

    if (state.attached && state.attached->Floppy() && state.attached->Access() == AccessMode::WriteThrough)
        WriteThroughFloppy(slotId, state);

    // Per-frame snapshot for readers on other threads
    if (state.attached)
    {
        const uint64_t changed = state.attached->ChangedUnits();
        if (state.changedUnits == 0 && changed > 0)
            Post(NC_MEDIA_DIRTY, slotId, state.attached.get());
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
    info.writeProtect = state.writeProtect;
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
    }
    return info;
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
                                           const std::string& exportPath)
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
        {
            SaveOptions options;
            options.allowRetarget = true;
            return SaveMedium(slotId, medium, it != _slots.end() ? it->second.slot : nullptr, options, nullptr);
        }
        case Disposition::Export:
            if (exportPath.empty())
                return MediaResult::Fail(MediaError::BadRequest, "export needs a path");
            return ExportMedium(slotId, medium, exportPath);
    }
    return MediaResult::Fail(MediaError::BadRequest, "unknown disposition");
}

MediaResult MediaManager::ExportMedium(const std::string& slotId, Medium& medium, const std::string& path)
{
    // The guest writes into the medium without a lock; a consistent export of
    // a running machine needs the versioned change layer (media history H1)
    if (!CanApplyNow())
        return MediaResult::Fail(MediaError::NotSupported, "pause the emulator to export slot '" + slotId + "'");

    if (FileHelper::AbsolutePath(path, /*resolveSymlinks*/ true) == medium.SourceKey())
        return MediaResult::Fail(MediaError::InUse, "the export target is the medium's own source");

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
        std::string error;
        if (!ExportBlockDevice(*medium.Block(), path, &error))
            return MediaResult::Fail(MediaError::IoError, error);
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
    return MediaResult::Success();
}

MediaResult MediaManager::SaveMedium(const std::string& slotId, Medium& medium, IMediaSlot* slot,
                                     const SaveOptions& options, SaveOutcome* outcome)
{
    DiskImage* disk = medium.Floppy();
    if (!disk)
        return MediaResult::Fail(MediaError::NotSupported,
                                 "slot '" + slotId + "': a block medium's source is never written in a session; export it to a new image file");
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

bool MediaManager::CanApplyNow() const
{
    if (_applyNowProbe)
        return _applyNowProbe();
    Emulator* emulator = _context ? _context->pEmulator : nullptr;
    return emulator == nullptr || !emulator->IsRunning() || emulator->IsPaused();
}

MediaResult MediaManager::CheckRecording(bool endRecording)
{
    ttd::TimeTravelManager* ttd = _context ? _context->pTimeTravelManager : nullptr;
    if (!ttd)
        return MediaResult::Success();
    if (ttd->IsRecording())
    {
        if (!endRecording)
            return MediaResult::Fail(MediaError::Recording,
                                     "the media set is fixed while a TTD recording runs; end the recording first");
        ttd->StopRecording();
    }
    // A kept session was recorded with the old media: its checkpoints no
    // longer describe this machine (the rule LoadDisk follows)
    ttd->InvalidateSession("media-change");
    return MediaResult::Success();
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
    std::string emulatorId;
    if (_context && _context->pEmulator)
        emulatorId = _context->pEmulator->GetId();

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
    auto* payload = new MediaSlotPayload(emulatorId, slotId, kind, source, access);
    payload->path = path;
    MessageCenter::DefaultMessageCenter().Post(topic, payload, true);
}

void MediaManager::Retire(std::unique_ptr<Medium> medium)
{
    if (!medium)
        return;
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
