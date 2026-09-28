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

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

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
};

struct EjectOptions
{
    bool force = false;         ///< eject even with unsaved changes
    bool endRecording = false;  ///< end a TTD recording instead of refusing
};

struct SlotInfo
{
    SlotDescriptor descriptor;
    bool present = false;         ///< a medium is attached
    bool pending = false;         ///< an insert or eject is waiting for the emulation thread
    std::string source;           ///< source path or description
    std::string format;
    AccessMode access = AccessMode::Session;
    bool dirty = false;
    uint64_t changedUnits = 0;
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
    /// fitted). A parked medium for the same id is attached again at once
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
    /// Write the medium's current contents to a new image file
    MediaResult Export(const std::string& slotId, const std::string& path);
    /// endregion </Operations>

    /// Emulation thread, once per frame (and from the operations themselves
    /// while the emulator is not running)
    void ApplyPending();

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
    };

    bool CanApplyNow() const;
    MediaResult CheckRecording(bool endRecording);
    MediaResult CheckInUse(const std::string& slotId, const Medium& medium) const;
    uint32_t DelayFrames(const SlotDescriptor& descriptor) const;
    void ApplySlot(const std::string& slotId, SlotState& state, std::vector<std::unique_ptr<Medium>>& retired);
    void Post(const char* topic, const std::string& slotId, const Medium* medium) const;
    /// Destroy a medium that left the machine (a staged upload's file goes too)
    static void Retire(std::unique_ptr<Medium> medium);

    EmulatorContext* _context = nullptr;
    std::function<bool()> _applyNowProbe;
    mutable std::recursive_mutex _mutex;
    std::map<std::string, SlotState> _slots;
    std::map<std::string, std::unique_ptr<Medium>> _parked;
};
