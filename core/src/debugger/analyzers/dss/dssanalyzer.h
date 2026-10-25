#pragma once

#include "debugger/analyzers/ianalyzer.h"
#include "debugger/analyzers/osaware/osaware_event.h"
#include "common/ringbuffer.h"

#include <cstdint>
#include <string>
#include <vector>

// Forward declarations
class AnalyzerManager;
class Z80;
class EmulatorContext;

/// Build profile: anchors of one DSS kernel build (kernel page = Z80 window 0,
/// the kernel is assembled with ORG 0, so all values are Z80 addresses).
/// From system.exp / system.sym of the 1.71.66 build
/// (testdata/machines/sprinter/dss/1.71.66/) and the official 1.70b sources.
struct DssProfile
{
    const char* name;
    uint16_t retfar;      // word@0x0001: JP target of the 0x0000 vector (RETFAR)
    uint16_t rst10;       // word@0x0011: JP target of the RST #10 dispatcher
    uint16_t leave;       // LEAVE: normal task exit
    uint16_t task;        // byte: current task id
    uint16_t memtab;      // 256 bytes: block id -> owner task (0 = free)
    uint16_t fmBuf;       // file handle table (0 = not known for this build)
    uint16_t fmCount;
    uint16_t fmStride;
    uint16_t fmOwnerOff;
};

/// Sprinter DSS-aware analyzer: detects a running Estex DSS kernel in Z80
/// window 0, streams decoded DSS calls (RST #10, function in C) and BIOS
/// memory commands (RST #08), tracks the MEMTAB block-owner table and the FM
/// file-handle table, and reports a task that exits owning blocks or leaving
/// file handles open (plan #99; dss-system-state.md / leak-corruption-
/// playbook.md classes A, B, C).
class DssAnalyzer : public IAnalyzer, public IOSAnalyzer
{
public:
    static constexpr size_t EVENT_BUFFER_SIZE = 2048;

    // Entry gates (Z80 addresses; the kernel sits in window 0 while resident)
    static constexpr uint16_t BP_LEAVE_VECTOR = 0x0000;  // JP RETFAR: error exit
    static constexpr uint16_t BP_RST08 = 0x0008;         // BIOS call, command in C
    static constexpr uint16_t BP_RST10 = 0x0010;         // DSS call, function in C

    explicit DssAnalyzer(EmulatorContext* context);

    // IAnalyzer
    std::string getName() const override { return "DssAnalyzer"; }
    std::string getUUID() const override { return _uuid; }
    void onActivate(AnalyzerManager* manager) override;
    void onDeactivate() override;
    void onFrameEnd() override;
    void onBreakpointHit(uint16_t address, Z80* cpu) override;

    // IOSAnalyzer
    std::vector<OsEvent> getOsEvents() const override;
    std::vector<OsEvent> getOsEventsSince(uint64_t timestamp) const override;
    std::vector<OsEvent> getNewOsEvents() override;
    void clearOsEvents() override;
    OsSnapshot getOsSnapshot() const override;
    std::string osProfileName() const override;

    static const std::vector<DssProfile>& knownProfiles();

    // Exposed for tests: detection and audit on raw window-0 images
    static bool hasDssShape(const uint8_t* window0);
    static int matchProfile(const uint8_t* window0);   // index or -1
    static std::vector<uint16_t> ownedBlockIds(const uint8_t* memtab, uint8_t taskId);
    static std::vector<OsHandle> fmHandlesOfTask(const uint8_t* fm, const DssProfile& profile,
                                                 uint8_t taskId);

private:
    enum class State : uint8_t { ARMING, SCANNING, DETECTED, UNKNOWN };

    void detectKernel();
    void armTraps(AnalyzerManager* manager);
    void handleDssCall(Z80* cpu);
    void handleBiosCall(Z80* cpu);
    void handleTaskExit(uint16_t pc);
    void auditTaskExit(uint8_t taskId, uint16_t pc);
    void pollMemtab();
    void emitEvent(OsEvent&& event);
    bool kernelStillResident() const;

    uint8_t readZ80(uint16_t addr) const;
    uint16_t readZ80Word(uint16_t sp) const;
    uint8_t currentTaskId() const;

    std::string _uuid;
    EmulatorContext* _context;
    AnalyzerManager* _manager = nullptr;

    State _state = State::ARMING;
    int _profileIndex = -1;
    bool _pollPending = false;
    std::vector<uint8_t> _lastMemtab;    // 256-byte snapshot
    uint64_t _skippedQuiet = 0;
    uint64_t _lastQueryTime = 0;
    uint64_t _lastEmittedTs = 0;         // Monotonic guard for the ring's filter

    RingBuffer<OsEvent> _events;
};
