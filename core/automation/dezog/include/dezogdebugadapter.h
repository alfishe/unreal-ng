#pragma once

// DezogDebugAdapter - IDebugInterface implementation on top of the real Emulator.
//
// Socket-free by design so it can be compiled into core-tests and exercised
// end-to-end against a live emulator instance without any DZRP transport.
// AutomationDezog owns one adapter and forwards its pause notifications to
// dzrp::Server::notifyPause.
//
// Pause notification sources (exactly one NTF_PAUSE per stop):
//   - pause()                    → BreakReason::MANUAL (after the Z80 thread parked)
//   - NC_EXECUTION_BREAKPOINT    → BREAKPOINT / WATCHPOINT_READ / WATCHPOINT_WRITE
// GUI-initiated pauses are deliberately NOT forwarded: DeZog only expects a
// notification in response to its own CMD_PAUSE / CMD_CONTINUE.

#include "dzrpserver.h"
#include "3rdparty/message-center/eventqueue.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <vector>

class Emulator;
namespace ttd { class TimeTravelManager; }

class DezogDebugAdapter : public dzrp::IDebugInterface, public Observer
{
public:
    // reason, address, bank (wire format bank+1, 0 = unknown/any)
    using PauseNotifier = std::function<void(dzrp::BreakReason, uint16_t, uint8_t)>;

    // emulator == nullptr → resolve EmulatorManager::GetMostRecentEmulator() lazily
    explicit DezogDebugAdapter(std::shared_ptr<Emulator> emulator = nullptr);
    ~DezogDebugAdapter() override;

    DezogDebugAdapter(const DezogDebugAdapter&) = delete;
    DezogDebugAdapter& operator=(const DezogDebugAdapter&) = delete;

    void setEmulator(std::shared_ptr<Emulator> emulator);
    std::shared_ptr<Emulator> getEmulator() const;

    // Notification sink (server.notifyPause in production, lambda in tests)
    void setPauseNotifier(PauseNotifier notifier);

    // --- IDebugInterface ---
    void pause() override;
    void resume() override;
    bool isPaused() const override;

    Registers getRegisters() const override;
    void setRegister(dzrp::RegisterId regId, uint16_t value) override;

    std::vector<uint8_t> readMemory(uint16_t addr, uint16_t len) const override;
    void writeMemory(uint16_t addr, const std::vector<uint8_t>& data) override;

    std::vector<uint8_t> getSlots() const override;
    void setSlot(uint8_t slot, uint8_t bank) override;
    void writeBank(uint8_t bank, const std::vector<uint8_t>& data) override;

    uint16_t addBreakpoint(uint16_t addr, uint8_t bank = 0, const std::string& condition = "",
                           bool temporary = false) override;
    void removeBreakpoint(uint16_t id) override;
    void clearTemporaryBreakpoints() override;

    bool addWatchpoint(uint16_t addr, uint8_t bank, uint16_t size, dzrp::WatchAccess access) override;
    void removeWatchpoint(uint16_t addr, uint8_t bank, uint16_t size, dzrp::WatchAccess access) override;

    std::vector<uint8_t> captureState() const override;
    void restoreState(const std::vector<uint8_t>& state) override;

    dzrp::MachineType getMachineType() const override;
    void setBorder(uint8_t color) override;

    uint8_t readPort(uint16_t port) const override;
    void writePort(uint16_t port, uint8_t value) override;

    bool waitForTarget(uint32_t timeoutMs) const override;

    // CMD_INIT: starts TTD recording so instruction history is available at the
    // first stop (no-op when history is disabled via UNREAL_DEZOG_HISTORY=0).
    void onSessionOpened() override;

    // Drops every dezog-owned breakpoint/watchpoint (temporaries included) and
    // resumes the emulator if it is paused, so a detached DeZog never leaves the
    // target stuck or littered with stale breakpoints. Non-dezog breakpoints
    // (CLI / GUI / analyzers) are preserved.
    void onSessionClosed() override;

    // --- Instruction history over TTD ---
    // Entries are served from the TTD per-frame decode cache
    // (TimeTravelManager::GetFrameCache): a DeZog index is mapped to
    // (frame, entry-in-frame) via lazily-built frame segments, and the decoded
    // record (registers, slots, opcodes at PC, word at SP) is read straight
    // from the cache. The emulator itself stays at the present while browsing
    // (DeZog's model: the history UI renders from entry-carried data;
    // getRegisters()/readMemory()/getSlots() during a browse reflect the
    // present state). Any forward-moving command (resume/pause/register or
    // memory writes/state restore/session close) first returns to the present
    // by restoring the snapshot taken on history entry.
    bool isHistoryAvailable() const override;
    bool isHistoryRecording() const override;
    std::optional<HistoryEntry> getHistoryEntry(uint32_t index) override;

    // Cursor position: -1 = present, otherwise the history index currently materialized
    int64_t getHistoryCursor() const;
    bool isBrowsingHistory() const { return getHistoryCursor() >= 0; }
    void setHistoryEnabled(bool enabled);
    bool isHistoryEnabled() const;

    static constexpr const char* HISTORY_ENV_VAR = "UNREAL_DEZOG_HISTORY";

    // --- Introspection (tests / diagnostics) ---
    size_t getTemporaryBreakpointCount() const;
    size_t getWatchpointCount() const;
    bool isBreakpointTemporary(uint16_t id) const;

    // DZRP bank numbering (DeZog ZX128 memory model): RAM pages 0..7 map 1:1,
    // ROM0 = 8, ROM1 = 9. CMD_SET_SLOT additionally accepts the 0xFE/0xFF aliases.
    static constexpr uint8_t ROM_BANK_BASE = 8;
    static constexpr uint8_t ROM0_ALIAS = 0xFE;
    static constexpr uint8_t ROM1_ALIAS = 0xFF;

    static constexpr const char* BREAKPOINT_OWNER = "dezog";

private:
    struct WatchKey
    {
        uint16_t addr;
        uint8_t bank;
        uint16_t size;
        uint8_t access;
        bool operator<(const WatchKey& o) const
        {
            return std::tie(addr, bank, size, access) < std::tie(o.addr, o.bank, o.size, o.access);
        }
    };

    std::shared_ptr<Emulator> resolveEmulator() const;
    void ensureDebugEnabled(Emulator& emulator);

    // History helpers (all require a paused emulator; called on the control thread)
    void ensureHistoryRecording(Emulator& emulator);
    void leaveHistory(Emulator& emulator);
    void leaveHistoryIfBrowsing();
    // Out-of-band mutation (register/memory/slot/bank write from the debugger):
    // TTD reconstructs history by deterministic replay, which such edits break.
    // Record a DebuggerEdit marker and restart the recording from the edited state.
    void onDebuggerEdit(Emulator& emulator, const char* what);
    void subscribe();
    void unsubscribe();
    void notify(dzrp::BreakReason reason, uint16_t addr);

    // MessageCenter observer callback (Observer-method signature)
    void onBreakpointMessage(int id, Message* message);

    mutable std::mutex _mutex;
    std::shared_ptr<Emulator> _emulator;              // Explicit binding (nullptr = dynamic)
    PauseNotifier _notifier;

    std::set<uint16_t> _temporaryBreakpoints;         // BreakpointManager IDs
    std::map<WatchKey, std::vector<uint16_t>> _watchpoints;

    bool _subscribed = false;

    // History state.
    //
    // STRATEGY (see docs/inprogress/2026-08-27-dezog-integration/reverse-debugging.md
    // §5 "Best strategy for DeZog"): a DeZog history index is served from the TTD
    // per-frame decode cache (TimeTravelManager::GetFrameCache), not by a live
    // seek per read. Instructions are grouped by frame; we map a global DeZog
    // index → (frame, entry-in-frame) via `_historySegs` (built lazily from the
    // present backward) and read the decoded record straight from the cache.
    // This is O(1) per entry after a one-time ~ms frame fill, vs a ~ms replay
    // per read for the old TimePoint-seek approach.
    //
    // TO CHANGE THE STRATEGY: everything lives in getHistoryEntry() + the
    // `_historySegs` mapping below. To go back to seek-per-read, drop the cache
    // calls and SeekTo each entry's TimePoint (see git history / the doc §3).
    struct HistoryFrameSeg
    {
        uint64_t frame;       // timeline frame this segment covers
        uint32_t count;       // DeZog-visible entries in this frame
        uint64_t cumBefore;   // total visible entries in more-recent frames
        bool     isPresent;   // present (partial) frame: only entries before the stop count
    };

    bool _historyEnabled = true;
    int64_t _historyCursor = -1;                      // -1 = present (live), else last index served
    std::pair<uint64_t, uint32_t> _present{0, 0};     // TimePoint of the present (valid while browsing)
    std::vector<HistoryFrameSeg> _historySegs;        // index→frame map, built backward from present
    // Snapshot of the present captured on entering history. Restoring it is the
    // robust way back: the present is a mid-frame stop, always beyond the last
    // frame-boundary checkpoint, so TTD seek/ResumeRecordingFrom cannot target it.
    std::vector<uint8_t> _presentSnapshot;

    // Resolve a global DeZog history index to (frame, entryIndexInFrame),
    // extending _historySegs backward as needed. Returns false when the index is
    // beyond recorded history. Requires the timeline frozen (StopRecording).
    bool resolveHistoryIndex(Emulator& emulator, uint32_t index, uint64_t& frameOut,
                             uint32_t& entryIdxOut);

    // Snapshot helpers (no history side effects, unlike captureState/restoreState)
    std::vector<uint8_t> captureSnapshotBytes(Emulator& emulator) const;
    bool restoreSnapshotBytes(Emulator& emulator, const std::vector<uint8_t>& bytes) const;
};
