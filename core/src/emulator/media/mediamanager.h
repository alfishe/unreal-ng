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

#include "emulator/media/mediahistory.h"
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

#include "emulator/media/blockformats.h"
#include "emulator/media/mediaconfig.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/mediaslot.h"
#include "emulator/media/medium.h"
#include "emulator/media/mediatypes.h"

class EmulatorContext;
struct CompositeInfo;

/// What an insert does with the session journal next to the medium (multi-source phases/c10e-session-journal.md §4)
enum class JournalChoice : uint8_t
{
    Default,  ///< [MEDIA] SessionJournal: replay when on, off when off
    Replay,   ///< a journal left by a crash is replayed
    Discard,  ///< a journal left by a crash is deleted unread; a new one starts
    Off,      ///< no journal next to the medium this time (a temp file); an old one is left as it is
};

struct InsertOptions
{
    std::optional<AccessMode> access;  ///< the slot's default when not set
    std::optional<FatType> fs;         ///< folder volumes; the slot's default when not set
    std::optional<CodePage> codePage;  ///< folder volumes' short names; the folder's manifest, else CP866
    std::optional<uint64_t> freeBytes; ///< folder volumes: room for guest writes (default 256 MiB)
    bool writeProtect = false;         ///< the slot's write-protect switch
    bool endRecording = false;         ///< end a TTD recording instead of refusing
    const char* ttdReason = "media-change";  ///< the TTD session the old media described ends with this reason
    bool immediate = false;            ///< no swap delay (media the firmware boots from)
    JournalChoice journal = JournalChoice::Default;  ///< media written in `session` access
    /// A dirty medium already in the slot: what happens to its writes
    Disposition disposition = Disposition::None;
    std::string exportPath;            ///< Disposition::Export
    /// Disposition::Save of a composite (D-8): its strategy (empty: the descriptor's writes.save), onConflict
    /// keep-both, and strict (a failed commit / write-back refuses instead of keeping the writes as a delta)
    std::string strategy;
    bool keepBoth = false;
    bool strict = false;

    /// Folder volumes only (BUGS.md #3): forwarded to OpenRequest so a caller
    /// scanning off the UI thread (the GUI's async insert worker) can abort a
    /// large or slow/network folder and report progress. Empty: no
    /// cancellation, no progress (every non-GUI caller - WebAPI, CLI, MCP -
    /// keeps today's plain synchronous behavior unchanged)
    std::function<bool()> cancelRequested;
    std::function<void(uint64_t entriesScanned, uint64_t bytesScanned)> onProgress;
};

struct EjectOptions
{
    /// A dirty medium: what happens to its writes (None refuses with "dirty")
    Disposition disposition = Disposition::None;
    std::string exportPath;     ///< Disposition::Export
    bool endRecording = false;  ///< end a TTD recording instead of refusing
    std::string strategy;       ///< Disposition::Save of a composite (D-8), as InsertOptions
    bool keepBoth = false;
    bool strict = false;
};

/// A rescan of a folder or composite (DT-16): what happens to unsaved writes when the sources changed
struct RescanOptions
{
    Disposition disposition = Disposition::None;  ///< None refuses with "dirty" (only when the sources changed)
    std::string exportPath;                       ///< Disposition::Export
    std::string strategy;                         ///< Disposition::Save of a composite (D-8), as InsertOptions
    bool keepBoth = false;
    bool strict = false;
};

struct SaveOptions
{
    std::string path;           ///< empty: the medium's own image file
    bool allowRetarget = true;  ///< floppies: save as <stem>.udi when the format cannot hold the disk
    std::string compression;    ///< block media saved as a CHD: the codecs (BlockWriteOptions)
    bool compact = false;       ///< block media: a re-synthesized FAT volume (S1 compact)
    std::optional<FatType> fs;  ///< compact: the FAT type
    std::optional<uint64_t> size;  ///< compact: total bytes
    std::string vhd;               ///< a new .vhd file: fixed (default) or dynamic (BlockWriteOptions)
    /// Composites (DT-9): flat (needs a path) | delta | commit | write-back; empty: a path means flat,
    /// no path the descriptor's writes.save (delta when it names none; ask saves a delta too, discard drops the
    /// writes of a medium that leaves)
    std::string strategy;
    bool force = false;          ///< delta: write over a delta that was made over other sources
    /// The medium leaves its slot (an eject / swap / rescan disposition, the emulator going away): the policy
    /// runs as for a plain save, and a strategy that fails keeps the writes as a session delta instead (D-8)
    bool disposition = false;
    bool strict = false;         ///< disposition: a failed strategy refuses (the medium stays) instead of the delta
    bool plan = false;           ///< commit / write-back: report what would be written, write nothing
    bool keepBoth = false;       ///< write-back: a host file changed since the build gets "name (guest).ext" next to it
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
    /// Unsaved changes only (else empty): what closing the emulator does with them (D-8). A composite's policy:
    /// "delta", "commit", "write-back" (they are saved), "discard" (dropped), "ask" (a GUI asks the user; without
    /// one a delta). Other media: "journal" (their session journal keeps them for the next insert) or "lost"
    std::string onRelease;
    uint64_t volumeId = 0;        ///< Medium::VolumeId: matches the notifications' MediaSlotPayload::volumeId
};

class IMediaReadJournal;
class MediaManager : public IMediaHistory
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
    /// keeps its source and its unsaved changes. Also for a detached medium.
    /// Block media: a `.chd` target is a CHD (`options`), anything else raw
    MediaResult Export(const std::string& slotId, const std::string& path, const BlockWriteOptions& options = {});
    /// Write the medium to its image file (or to `options.path`, which it then
    /// stands for) and mark it clean. Floppies: in the file's format. Block
    /// media: the changed sectors back into a raw / HDF / HDI / VHD file, a CHD
    /// written again. A disk from a folder, a Hobeta file or a blank one needs
    /// a path. The emulator must not be running
    MediaResult Save(const std::string& slotId, const SaveOptions& options = {}, SaveOutcome* outcome = nullptr);
    /// The slot's write-protect switch
    MediaResult SetWriteProtect(const std::string& slotId, bool on);
    /// Build a folder or composite medium again from its sources (host files changed), DT-16. Sources that give
    /// the same content id leave the medium as it is ("unchanged", writes kept). Otherwise unsaved writes need a
    /// disposition (they cannot follow a rebuild: clusters move); a save follows the policy, then the medium is
    /// built again
    MediaResult Rescan(const std::string& slotId, const RescanOptions& options = {});
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

    /// Time travel replays history (FR-20): hold every write-through medium's
    /// host writes in memory (true), then release them to their files (false,
    /// the state of that moment). Floppy write-through waits while held.
    /// Emulation thread
    void HoldHostWrites(bool hold);

    /// Time travel's media read journal (null = none): every block medium's
    /// sector reads are recorded into it, or played back from it. Emulation thread
    void SetReadJournal(IMediaReadJournal* journal) { _readJournal = journal; }
    IMediaReadJournal* GetReadJournal() const { return _readJournal; }
    bool HoldingHostWrites() const { return _holdHostWrites; }

    /// A slot's peripheral reports a guest write to its medium (emulation
    /// thread). While TTD records, the first write of each frame is a replay
    /// barrier: the medium changed, a seek must not cross it silently.
    /// `detail` (the controller's command) goes into the marker's text
    void NoteWrite(const std::string& slotId, const char* detail = nullptr);

    /// IMediaHistory (TTD Phase 3, Step 4). No change layer yet: a version is
    /// the count of frames that wrote the medium, and no slot goes back
    uint64_t VersionStamp() const override { return _revision.load() + _writeStamp.load(); }
    void CurrentVersions(std::vector<MediaVersionInfo>& out) const override;
    bool SetHead(const std::string& slotId, uint64_t version) override;
    /// A slot's peripheral reports that the GUEST took its medium out (a CD drive's START STOP UNIT
    /// eject; emulation thread, inside the command). The normal eject runs at the next frame
    /// boundary (ApplyPending: Detach, NC_MEDIA_EJECTED, revision): the slot is empty for every
    /// surface as after a user's eject. It is a consequence of guest I/O, not an outside input: no
    /// recording guard, no session invalidation. During TTD replay (sealed: the live run already
    /// emptied the slot) nothing on the host side changes. A medium with unsaved writes stays (no
    /// disposition without a user to ask; a CD has none)
    void GuestEject(const std::string& slotId);

    /// The attached medium (tests, peripherals' diagnostics); nullptr if empty.
    /// Only valid on the emulation thread or while the emulator is not running
    Medium* GetMedium(const std::string& slotId);

    /// Replace the "is the machine executing?" check (tests drive the queue
    /// without a running main loop). nullptr restores the emulator's state
    void SetApplyNowProbe(std::function<bool()> probe) { _applyNowProbe = std::move(probe); }

    /// The manager going away (the emulator closes, the application exits, a model switch that recreates the
    /// emulator) saves every composite's unsaved writes by its writes.save policy (D-8): delta by default and for
    /// ask, nothing for discard, commit / write-back with the delta fallback. On by default; the test runner turns
    /// it off so no test leaves a .delta next to its fixtures
    static void SetSaveOnRelease(bool on) { _saveOnRelease = on; }
    static bool SaveOnRelease() { return _saveOnRelease; }
    /// What the save on release did, one line per composite (logged as well)
    std::vector<std::string> SaveByPolicyOnRelease();
    /// The media with unsaved changes, attached and detached, each with its onRelease: what a GUI asks about
    /// before it closes the emulator ("ask" and "lost"). A query, not a notification: the answer is needed before
    /// the close goes on, and Message Center delivers on its own thread
    std::vector<SlotInfo> Unsaved() const;

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
        uint64_t writtenFrames = 0;           ///< frames that wrote the medium (its version, IMediaHistory)
    };

    bool CanApplyNow() const;
    SlotInfo Describe(const std::string& slotId, const SlotState& state) const;
    /// Open `source` the way the slot takes it (its FS matrix, its session journal): Insert and Rescan
    MediaResult OpenForSlot(const std::string& slotId, const MediaSource& source, const InsertOptions& options,
                            std::unique_ptr<Medium>& medium);
    /// A dirty medium leaving its slot (or detached): apply the disposition first
    MediaResult ApplyDisposition(const std::string& slotId, Medium& medium, Disposition disposition,
                                 const std::string& exportPath, const SaveOptions& save);
    MediaResult SaveMedium(const std::string& slotId, Medium& medium, IMediaSlot* slot, const SaveOptions& options,
                           SaveOutcome* outcome);
    MediaResult ExportMedium(const std::string& slotId, Medium& medium, const std::string& path,
                             const BlockWriteOptions& options = {});
    MediaResult SaveBlockMedium(const std::string& slotId, Medium& medium, IMediaSlot* slot, const SaveOptions& options,
                                SaveOutcome* outcome);
    /// S3: a graft composite's patches, grafted files and guest writes into its base image (DT-14)
    MediaResult CommitComposite(const std::string& slotId, Medium& medium, IMediaSlot* slot, const CompositeInfo& composite,
                                const SaveOptions& options, SaveOutcome* outcome);
    /// S4: the guest's file changes into the composite's writable folder layers (DT-10 to DT-12), then a rebuild
    MediaResult WriteBackComposite(const std::string& slotId, Medium& medium, const CompositeInfo& composite,
                                   const SaveOptions& options, SaveOutcome* outcome);
    /// Another slot (or detached medium) uses `path`: as its own file or as a layer source
    bool UsedElsewhere(const Medium& self, const std::string& path) const;
    /// S2: the change layer of a composite into its session delta file
    MediaResult SaveDelta(const std::string& slotId, Medium& medium, const CompositeInfo& composite, const SaveOptions& options,
                          const std::string& note, SaveOutcome* outcome);
    /// The medium in a slot, or detached under that id
    Medium* FindMedium(const std::string& slotId, SlotState** state);
    /// Refused while an explicit TTD recording runs, unless the caller ends it; changes nothing
    MediaResult CheckRecording(bool endRecording) const;
    /// The change goes ahead (every check passed): the recording the caller ends stops, and the TTD session the old
    /// media described ends. Never before a check that can still fail: a refused change keeps the session
    void EndSessionForMediaChange(bool endRecording, const char* reason);
    MediaResult CheckInUse(const std::string& slotId, const Medium& medium) const;
    uint32_t DelayFrames(uint32_t swapDelayMs) const;
    /// One configured entry for a registered slot: its options, then its medium
    void ApplyConfiguredEntry(const MediaSetEntry& entry, std::vector<std::string>& problems);
    void ApplySlot(const std::string& slotId, SlotState& state, std::vector<std::unique_ptr<Medium>>& retired);
    void Post(const char* topic, const std::string& slotId, const Medium* medium, const std::string& path = {}) const;
    /// A write-through floppy with new guest writes goes back to its file (emulation thread)
    void WriteThroughFloppy(const std::string& slotId, SlotState& state);
    /// Destroy a medium that left the machine (a staged upload's file goes too)
    /// A medium leaves for good. `keepJournal`: the emulator goes with its unsaved writes (they stay in the journal
    /// for the next insert); an eject or a swap decided about them already (a disposition), so its journal goes
    static void Retire(std::unique_ptr<Medium> medium, bool keepJournal = false);
    /// SlotInfo::onRelease of a medium with unsaved changes
    static std::string OnReleaseOf(const Medium& medium);
    /// A medium written in `session` access gets its journal `<source>.usession` (replayed, discarded or off as
    /// `choice` says); the outcome goes into its report
    static void AttachJournal(Medium& medium, const MediaSource& source, JournalChoice choice);

    EmulatorContext* _context = nullptr;
    std::function<bool()> _applyNowProbe;
    bool _releasing = false;  ///< the destructor saves by policy: nothing executes any more
    static inline std::atomic<bool> _saveOnRelease{true};
    mutable std::recursive_mutex _mutex;
    std::map<std::string, SlotState> _slots;
    bool _holdHostWrites = false;
    IMediaReadJournal* _readJournal = nullptr;   ///< the taps read it through its address
    std::map<std::string, std::unique_ptr<Medium>> _parked;
    std::optional<std::vector<MediaSetEntry>> _configured;  ///< set once ApplyConfiguredMedia ran
    std::atomic<uint64_t> _writeStamp{0};        ///< moves with every writtenFrames
    mutable std::atomic<uint64_t> _revision{0};  // also moved by Post (const)
    std::condition_variable_any _applied;  ///< signalled after every ApplyPending
};
