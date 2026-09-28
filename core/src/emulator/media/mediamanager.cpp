#include "stdafx.h"

#include "mediamanager.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/sessionwritemap.h"
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
        info.changedUnits = state.changedUnits;
        info.dirty = state.changedUnits > 0;
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

    std::error_code ec;
    if ((source.type == MediaSourceType::Folder || std::filesystem::is_directory(source.path, ec)) && !descriptor.acceptsFolder)
        return MediaResult::Fail(MediaError::KindMismatch, "slot '" + slotId + "' does not take a folder");

    // File I/O and folder scans happen here, on the caller's thread
    OpenRequest request;
    request.source = source;
    request.kind = descriptor.kind;
    request.access = options.access.value_or(descriptor.defaultAccess);
    request.fs = options.fs.value_or(descriptor.defaultFs);

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
    state.emptyFramesLeft = (state.attached && !options.immediate) ? DelayFrames(descriptor) : 0;

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
    if (!state.attached || !state.attached->Block())
        return MediaResult::Fail(MediaError::UnreadableSource, "slot '" + slotId + "' is empty");

    // The guest writes into the medium without a lock; a consistent export of
    // a running machine needs the versioned change layer (media history H1)
    if (!CanApplyNow())
        return MediaResult::Fail(MediaError::NotSupported, "pause the emulator to export slot '" + slotId + "'");

    std::error_code ec;
    if (std::filesystem::equivalent(path, state.attached->Source().path, ec))
        return MediaResult::Fail(MediaError::InUse, "the export target is the medium's own source");

    std::string error;
    if (!ExportBlockDevice(*state.attached->Block(), path, &error))
        return MediaResult::Fail(MediaError::IoError, error);

    Post(NC_MEDIA_EXPORTED, slotId, state.attached.get());
    return MediaResult::Success();
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

Medium* MediaManager::GetMedium(const std::string& slotId)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto it = _slots.find(slotId);
    return it == _slots.end() ? nullptr : it->second.attached.get();
}

/// region <Helpers>

void MediaManager::ApplySlot(const std::string& slotId, SlotState& state, std::vector<std::unique_ptr<Medium>>& retired)
{
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

uint32_t MediaManager::DelayFrames(const SlotDescriptor& descriptor) const
{
    if (descriptor.swapDelayMs == 0)
        return 0;
    const uint64_t frameUs = (_context && _context->config.frame_duration_us) ? _context->config.frame_duration_us : 20000;
    return static_cast<uint32_t>((static_cast<uint64_t>(descriptor.swapDelayMs) * 1000 + frameUs - 1) / frameUs);
}

void MediaManager::Post(const char* topic, const std::string& slotId, const Medium* medium) const
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
    MessageCenter::DefaultMessageCenter().Post(topic, new MediaSlotPayload(emulatorId, slotId, kind, source, access), true);
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
        std::filesystem::remove(path, ec);
    }
}

/// endregion </Helpers>
