#pragma once

/// @file mediamanager.h
/// @brief One per emulator: owns every medium, knows every slot, applies
/// insert / eject on the emulation thread.
/// Design: docs/inprogress/2026-09-28-storage-manager/technical-design.md §1-§3.
///
/// Threading. Every public method may be called from any thread. Requests are
/// validated and their media opened on the calling thread; they reach the
/// slots in ApplyPending(), which the main loop calls at every frame boundary
/// (MainLoop::CompleteFrame). While the emulator is not running (created,
/// paused, stopped, or driven synchronously by tests) requests apply at once.
/// Slots therefore see Attach / Detach only when the machine is not executing.
///
/// Guest writes go straight from the peripheral into the medium's block stack,
/// with no lock on that path. Everything the manager reports about a running
/// machine (dirty, changed units) is a per-frame snapshot taken in
/// ApplyPending, so automation never reads a medium the guest is writing.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "emulator/media/mediaconfig.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/mediaslot.h"
#include "emulator/media/medium.h"
#include "emulator/media/mediatypes.h"

class EmulatorContext;

struct InsertOptions
{
    std::optional<AccessMode> access;  ///< the slot's default when not set
    std::optional<FatType> fs;         ///< folder volumes; the slot's default when not set
    std::optional<CodePage> codePage;  ///< folder volumes' short names; the folder's manifest, else CP866
    std::optional<uint64_t> freeBytes; ///< folder volumes: room for guest writes (default 256 MiB)
    bool writeProtect = false;         ///< the slot's write-protect switch
    bool endRecording = false;         ///< end a TTD recording instead of refusing
    bool immediate = false;            ///< no swap delay (media the firmware boots from)
    /// A dirty medium already in the slot: what happens to its writes
    Disposition disposition = Disposition::None;
    std::string exportPath;            ///< Disposition::Export
};

struct EjectOptions
{
    /// A dirty medium: what happens to its writes (None refuses with "dirty")
    Disposition disposition = Disposition::None;
    std::string exportPath;     ///< Disposition::Export
    bool endRecording = false;  ///< end a TTD recording instead of refusing
};

struct SaveOptions
{
    std::string path;           ///< empty: the medium's own image file
    bool allowRetarget = true;  ///< floppies: save as <stem>.udi when the format cannot hold the disk
};

/// What a save did
struct SaveOutcome
{
    std::string savedPath;
    bool retargeted = false;  ///< the requested format refused the disk; it went to savedPath (.udi)
    std::string note;         ///< why it was retargeted, or the writer's first warning
};

/// The media of one machine on their way to another (a model switch,
/// technical-design.md §8): live media, session writes included
struct MediaTransfer
{
    struct Entry
    {
        std::string slotId;
        std::unique_ptr<Medium> medium;
        bool writeProtect = false;  ///< the slot's switch
    };
    std::vector<Entry> entries;
};

/// Where the media of a transfer went
struct MediaTransferReport
{
    std::vector<std::string> attached;  ///< slot ids: in the same slot on the new machine
    std::vector<std::string> detached;  ///< no such slot, unsaved writes: kept as detached media
    std::vector<std::string> closed;    ///< no such slot, nothing unsaved: closed
    std::vector<std::string> lines;     ///< the same for people
};

struct SlotInfo
{
    SlotDescriptor descriptor;
    std::vector<std::string> tags;  ///< the descriptor's, plus the kind name, "removable", "folder"
    int index = -1;               ///< n-th slot of its kind on this machine, in id order
    bool detached = false;        ///< a medium whose slot went away (descriptor.id is that slot)
    bool present = false;         ///< a medium is attached
    bool pending = false;         ///< an insert or eject is waiting for the emulation thread
    std::string source;           ///< source path or description
    std::string format;
    AccessMode access = AccessMode::Session;
    bool dirty = false;
    uint64_t changedUnits = 0;
    std::string changes;          ///< the unsaved changes for people ("1 track: 3 sectors")
    bool writeProtect = false;
};

class MediaManager
{
public:
    explicit MediaManager(EmulatorContext* context);
    ~MediaManager();

    MediaManager(const MediaManager&) = delete;
    MediaManager& operator=(const MediaManager&) = delete;

    /// region <Slots>
    /// A peripheral registers its slots when it is created (or when its card is
    /// fitted). A parked medium for the same id is attached again at once; a
    /// slot that arrives after the configured media went in (a card fitted at
    /// run time) gets its configured medium instead
    void RegisterSlot(IMediaSlot& slot);
    /// Before the peripheral goes away: its medium is detached and parked,
    /// session writes included, until a slot with that id returns
    void UnregisterSlot(const std::string& slotId);
    bool HasSlot(const std::string& slotId) const;
    /// endregion </Slots>

    /// region <Operations>
    std::vector<SlotInfo> List() const;
    std::optional<SlotInfo> Info(const std::string& slotId) const;

    /// Open `source` for the slot through the format registry and insert it
    MediaResult Insert(const std::string& slotId, const MediaSource& source, const InsertOptions& options = {});
    /// Insert a medium built by the caller (tests, folder builders)
    MediaResult Insert(const std::string& slotId, std::unique_ptr<Medium> medium, const InsertOptions& options = {});
    MediaResult Eject(const std::string& slotId, const EjectOptions& options = {});
    /// Drop the medium's session changes
    MediaResult Discard(const std::string& slotId);
    /// Write the medium's current contents to a new image file. The medium
    /// keeps its source and its unsaved changes. Also for a detached medium
    MediaResult Export(const std::string& slotId, const std::string& path);
    /// Floppies: write the disk to its image file (or to `options.path`, which
    /// it then stands for) and mark it clean. A disk from a folder, a Hobeta
    /// file or a blank one needs a path. The emulator must not be running
    MediaResult Save(const std::string& slotId, const SaveOptions& options = {}, SaveOutcome* outcome = nullptr);
    /// The slot's write-protect switch
    MediaResult SetWriteProtect(const std::string& slotId, bool on);
    /// Build a folder medium again from its folder (host files changed).
    /// Refused while it has unsaved writes
    MediaResult Rescan(const std::string& slotId);
    /// Media whose slot went away, keyed by that slot's id (add-on removed)
    std::vector<SlotInfo> Detached() const;
    /// Increases with every change of slots, media or dirty state: a polling
    /// client reloads the list when it moves
    uint64_t Revision() const { return _revision.load(); }
    /// Block until the slot has no pending insert / eject, at most `timeoutMs`.
    /// While the emulator is not running the pending change is applied here.
    /// True when nothing is pending any more
    bool WaitApplied(const std::string& slotId, uint32_t timeoutMs);
    /// Insert the media a config states, at creation, before the first reset:
    /// no swap delay (firmware may boot from them). Entries for slots this
    /// machine does not have are reported, or ignored when they come from a
    /// legacy section. Returns the problems met. The set is kept for slots
    /// registered later (see RegisterSlot)
    std::vector<std::string> ApplyConfiguredMedia(const std::vector<MediaSetEntry>& mediaSet);

    /// A model switch, old machine: every medium leaves its slot (pending
    /// changes applied first) and the detached ones leave too, writes and all.
    /// The emulator must not be running
    MediaTransfer TakeMediaSet();
    /// A model switch, new machine: each medium goes into the slot with the
    /// same id and kind, replacing what the config put there. One with no such
    /// slot is kept detached when it has unsaved writes, else closed
    MediaTransferReport AdoptMediaSet(MediaTransfer transfer);
    /// endregion </Operations>

    /// Emulation thread, once per frame (and from the operations themselves
    /// while the emulator is not running)
    void ApplyPending();

    /// A slot's peripheral reports a guest write to its medium (emulation
    /// thread). While TTD records, the first write of each frame is a replay
    /// barrier: the medium changed, a seek must not cross it silently.
    /// `detail` (the controller's command) goes into the marker's text
    void NoteWrite(const std::string& slotId, const char* detail = nullptr);

    /// The attached medium (tests, peripherals' diagnostics); nullptr if empty.
    /// Only valid on the emulation thread or while the emulator is not running
    Medium* GetMedium(const std::string& slotId);

    /// Replace the "is the machine executing?" check (tests drive the queue
    /// without a running main loop). nullptr restores the emulator's state
    void SetApplyNowProbe(std::function<bool()> probe) { _applyNowProbe = std::move(probe); }

private:
    struct SlotState
    {
        IMediaSlot* slot = nullptr;
        std::unique_ptr<Medium> attached;
        std::unique_ptr<Medium> incoming;  ///< waiting for the emulation thread
        bool ejectRequested = false;
        bool discardRequested = false;
        uint32_t emptyFramesLeft = 0;      ///< swap delay still to run
        bool writeProtect = false;
        uint64_t changedUnits = 0;         ///< per-frame snapshot of attached->ChangedUnits()
        std::string changes;               ///< per-frame snapshot of attached->DescribeChanges()
        std::optional<uint32_t> swapDelayMs;  ///< config override of the slot's default
        bool writeMarkedThisFrame = false;    ///< a TTD barrier already recorded this frame
    };

    bool CanApplyNow() const;
    SlotInfo Describe(const std::string& slotId, const SlotState& state) const;
    /// A dirty medium leaving its slot (or detached): apply the disposition first
    MediaResult ApplyDisposition(const std::string& slotId, Medium& medium, Disposition disposition,
                                 const std::string& exportPath);
    MediaResult SaveMedium(const std::string& slotId, Medium& medium, IMediaSlot* slot, const SaveOptions& options,
                           SaveOutcome* outcome);
    MediaResult ExportMedium(const std::string& slotId, Medium& medium, const std::string& path);
    /// The medium in a slot, or detached under that id
    Medium* FindMedium(const std::string& slotId, SlotState** state);
    MediaResult CheckRecording(bool endRecording);
    MediaResult CheckInUse(const std::string& slotId, const Medium& medium) const;
    uint32_t DelayFrames(uint32_t swapDelayMs) const;
    /// One configured entry for a registered slot: its options, then its medium
    void ApplyConfiguredEntry(const MediaSetEntry& entry, std::vector<std::string>& problems);
    void ApplySlot(const std::string& slotId, SlotState& state, std::vector<std::unique_ptr<Medium>>& retired);
    void Post(const char* topic, const std::string& slotId, const Medium* medium, const std::string& path = {}) const;
    /// A write-through floppy with new guest writes goes back to its file (emulation thread)
    void WriteThroughFloppy(const std::string& slotId, SlotState& state);
    /// Destroy a medium that left the machine (a staged upload's file goes too)
    static void Retire(std::unique_ptr<Medium> medium);

    EmulatorContext* _context = nullptr;
    std::function<bool()> _applyNowProbe;
    mutable std::recursive_mutex _mutex;
    std::map<std::string, SlotState> _slots;
    std::map<std::string, std::unique_ptr<Medium>> _parked;
    std::optional<std::vector<MediaSetEntry>> _configured;  ///< set once ApplyConfiguredMedia ran
    mutable std::atomic<uint64_t> _revision{0};  // also moved by Post (const)
    std::condition_variable_any _applied;  ///< signalled after every ApplyPending
};
