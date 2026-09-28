#include "stdafx.h"

#include "mediamanager.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

#include "common/modulelogger.h"
#include "common/filehelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/floppyformats.h"
#include "emulator/notifications.h"

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

    // A card that comes back finds its medium where it left it
    auto parked = _parked.find(id);
    if (parked != _parked.end())
    {
        state.attached = std::move(parked->second);
        _parked.erase(parked);
        slot.Attach(*state.attached);
        slot.SetWriteProtectSwitch(state.writeProtect);
        Post(NC_MEDIA_INSERTED, id, state.attached.get());
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
    {
        if (auto info = Info(id))
            result.push_back(*info);
    }
    return result;
}

std::optional<SlotInfo> MediaManager::Info(const std::string& slotId) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return std::nullopt;

    const SlotState& state = it->second;
    SlotInfo info;
    info.descriptor = state.slot->Descriptor();
    info.present = state.attached != nullptr;
    info.pending = state.incoming != nullptr || state.ejectRequested || state.discardRequested;
    info.writeProtect = state.writeProtect;
    if (state.attached)
    {
        info.source = state.attached->Source().path.empty() ? state.attached->Describe() : state.attached->Source().path;
        info.format = state.attached->Format();
        info.access = state.attached->Access();
        // A stopped machine is read live; a running one through the frame's snapshot
        info.changedUnits = CanApplyNow() ? state.attached->ChangedUnits() : state.changedUnits;
        info.dirty = info.changedUnits > 0;
    }
    return info;
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
    request.fs = options.fs.value_or(descriptor.defaultFs);
    request.codePage = options.codePage;
    request.freeBytes = options.freeBytes;

    std::unique_ptr<Medium> medium;
    MediaResult opened = MediaFormatRegistry::Open(request, medium);
    if (!opened.Ok())
        return opened;

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

MediaResult MediaManager::Eject(const std::string& slotId, const EjectOptions& options)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");

    SlotState& state = it->second;
    if (!state.attached && !state.incoming)
        return MediaResult::Success();

    const uint64_t changed = CanApplyNow() && state.attached ? state.attached->ChangedUnits() : state.changedUnits;
    if (changed > 0 && !options.force)
        return MediaResult::Fail(MediaError::Dirty, "slot '" + slotId + "' has " + std::to_string(changed) +
                                                        " unsaved changes: export or discard them, or eject with force");
    if (MediaResult recording = CheckRecording(options.endRecording); !recording.Ok())
        return recording;

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
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");
    if (MediaResult recording = CheckRecording(false); !recording.Ok())
        return recording;

    SlotState& state = it->second;
    if (state.attached && state.attached->Floppy())
        return MediaResult::Fail(MediaError::NotSupported,
                                 "a floppy keeps its changes in the disk image: eject it with force and insert the source again");
    if (!state.attached || !state.attached->Session())
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
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");

    SlotState& state = it->second;
    if (!state.attached || (!state.attached->Block() && !state.attached->Floppy()))
        return MediaResult::Fail(MediaError::UnreadableSource, "slot '" + slotId + "' is empty");

    // The guest writes into the medium without a lock; a consistent export of
    // a running machine needs the versioned change layer (media history H1)
    if (!CanApplyNow())
        return MediaResult::Fail(MediaError::NotSupported, "pause the emulator to export slot '" + slotId + "'");

    if (FileHelper::AbsolutePath(path, /*resolveSymlinks*/ true) == state.attached->SourceKey())
        return MediaResult::Fail(MediaError::InUse, "the export target is the medium's own source");

    if (DiskImage* disk = state.attached->Floppy())
    {
        // The format writers mark the disk clean and rename it: an export is a
        // copy, so both are put back
        const DiskImage::DirtyState before = disk->captureDirtyState();
        const FloppySaveResult written = FloppyFormats::Save(_context, *disk, path, /*allowRetarget*/ false);
        disk->restoreDirtyState(before);
        if (!written.saved)
            return MediaResult::Fail(MediaError::IoError, written.reason);
    }
    else
    {
        std::string error;
        if (!ExportBlockDevice(*state.attached->Block(), path, &error))
            return MediaResult::Fail(MediaError::IoError, error);
    }

    Post(NC_MEDIA_EXPORTED, slotId, state.attached.get(), path);
    return MediaResult::Success();
}

MediaResult MediaManager::Save(const std::string& slotId, const SaveOptions& options, SaveOutcome* outcome)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    if (it == _slots.end())
        return MediaResult::Fail(MediaError::UnknownSlot, "no slot '" + slotId + "' on this machine");

    SlotState& state = it->second;
    Medium* medium = state.attached.get();
    if (!medium)
        return MediaResult::Fail(MediaError::UnreadableSource, "slot '" + slotId + "' is empty");
    DiskImage* disk = medium->Floppy();
    if (!disk)
        return MediaResult::Fail(MediaError::NotSupported,
                                 "only floppies are saved; export block media to a new image file");
    if (!CanApplyNow())
        return MediaResult::Fail(MediaError::NotSupported, "pause the emulator to save slot '" + slotId + "'");

    // Only a disk image file can take the disk back
    std::string target = options.path;
    if (target.empty())
    {
        const bool ownFile = medium->Source().type == MediaSourceType::File && medium->Format() != "hobeta";
        if (!ownFile || medium->Source().path.empty())
            return MediaResult::Fail(MediaError::NotSupported,
                                     "slot '" + slotId + "' holds a disk without an image file of its own: save it to a path");
        target = medium->Source().path;
    }

    const FloppySaveResult written = FloppyFormats::Save(_context, *disk, target, options.allowRetarget);
    if (!written.saved)
        return MediaResult::Fail(MediaError::IoError, written.reason);

    disk->markClean();
    state.changedUnits = 0;
    if (written.savedPath != medium->Source().path || medium->Source().type != MediaSourceType::File)
    {
        MediaSource saved;
        saved.type = MediaSourceType::File;
        saved.path = written.savedPath;
        medium->Rebase(saved);
        state.slot->SourceChanged(*medium);
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
    Post(NC_MEDIA_SAVED, slotId, medium, written.savedPath);
    return result;
}

std::vector<std::string> MediaManager::ApplyConfiguredMedia(const std::vector<MediaSetEntry>& mediaSet)
{
    std::vector<std::string> problems;
    for (const MediaSetEntry& entry : mediaSet)
    {
        if (!HasSlot(entry.slotId))
        {
            if (!entry.legacy)
                problems.push_back("[MEDIA] " + entry.slotId + ": this machine has no such slot");
            continue;
        }
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
            continue;

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
    return problems;
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
        state.changedUnits = changed;
    }
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
    if (!ttd || !ttd->IsRecording())
        return MediaResult::Success();
    if (!endRecording)
        return MediaResult::Fail(MediaError::Recording,
                                 "the media set is fixed while a TTD recording runs; end the recording first");
    ttd->StopRecording();
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
