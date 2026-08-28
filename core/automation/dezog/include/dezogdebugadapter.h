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

    // CMD_INIT: starts TTD recording so instruction history is available at the
    // first stop (no-op when history is disabled via UNREAL_DEZOG_HISTORY=0).
    void onSessionOpened() override;

    // Drops every dezog-owned breakpoint/watchpoint (temporaries included) and
    // resumes the emulator if it is paused, so a detached DeZog never leaves the
    // target stuck or littered with stale breakpoints. Non-dezog breakpoints
    // (CLI / GUI / analyzers) are preserved.
    void onSessionClosed() override;

    // --- Instruction history over TTD ---
    // The emulator is physically moved through recorded history: entry `index`
    // is served by positioning the emulator at the M1 of the (index+1)-th most
    // recently executed instruction (StopRecording → ReverseStepInstructions /
    // StepForwardInstruction relative to the current cursor). While browsing,
    // getRegisters()/readMemory()/getSlots() reflect that historic state. Any
    // forward-moving command (resume/pause/register or memory writes/state
    // restore/session close) first returns to the present (ResumeRecordingFrom).
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

    // History state
    // Reverse debugging walks the TTD timeline by TimePoint identity, not by
    // step count: the number of physical StepBackInstruction calls needed to
    // advance one DeZog index is not constant (a pre-execution breakpoint stop
    // absorbs one, a mid-instruction manual pause absorbs two), so each DeZog
    // index is pinned to the distinct (frame,tInFrame) it first resolved to and
    // re-materialized with SeekTo. _historyPositions[i] = TimePoint of index i.
    bool _historyEnabled = true;
    int64_t _historyCursor = -1;                      // -1 = present (live)
    std::pair<uint64_t, uint32_t> _present{0, 0};     // TimePoint of the present (valid while browsing)
    std::vector<std::pair<uint64_t, uint32_t>> _historyPositions;
    // Snapshot of the present captured on entering history. Restoring it is the
    // robust way back: the present is a mid-frame stop, always beyond the last
    // frame-boundary checkpoint, so TTD seek/ResumeRecordingFrom cannot target it.
    std::vector<uint8_t> _presentSnapshot;

    // Snapshot helpers (no history side effects, unlike captureState/restoreState)
    std::vector<uint8_t> captureSnapshotBytes(Emulator& emulator) const;
    bool restoreSnapshotBytes(Emulator& emulator, const std::vector<uint8_t>& bytes) const;
};
