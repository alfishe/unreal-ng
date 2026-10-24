#pragma once

#include "debugger/analyzers/ianalyzer.h"
#include "debugger/analyzers/osaware/osaware_event.h"
#include "common/ringbuffer.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Forward declarations
class AnalyzerManager;
class Z80;
class EmulatorContext;

/// Build profile: symbol values of one NedoOS kernel revision.
/// All values are offsets inside the kernel's system page (pgsys).
struct NedoProfile
{
    const char* name;
    uint16_t sysQuit;      // jp sys_quit stub at #0004
    uint16_t callBdos;     // jp callbdos stub at #0009
    uint16_t sysSysint;    // jp sys_sysint at #0038
    uint16_t tsysPages;    // page owner array: byte per physical page
    uint16_t sysNpages;    // byte holding the owner array length
    uint16_t appAddr;      // word holding the current app struct pointer
    uint16_t app1;         // first STRUCT app slot
    uint16_t appSz;        // slot stride
    uint8_t maxApps;
};

/// NedoOS-aware analyzer: detects a running NedoOS kernel, streams decoded
/// BDOS calls (CALL #0005, command in C), tracks the app table and the page
/// owner array, and reports a task that exits while still owning pages
/// (the NedoOS layer requirement set NK-7/NK-15, POC 020 findings).
class NedoOsAnalyzer : public IAnalyzer, public IOSAnalyzer
{
public:
    static constexpr size_t EVENT_BUFFER_SIZE = 2048;
    static constexpr uint16_t MAX_RAM_PAGES_SCAN = 256;

    // Entry gates inside pgsys. Apps reach them through the user-kernel stubs
    // in their own page (CALL #0005 / RST 0), whose OUT (#FD) switches window 0
    // to pgsys first - so these pgsys-pinned traps see every call post-switch,
    // with the command still in C and the caller's return address on the stack.
    static constexpr uint16_t BP_BDOS_CALL = 0x0009;  // jp callbdos, command in C
    static constexpr uint16_t BP_QUIT_STUB = 0x0004;  // jp sys_quit (QUIT is RST 0)

    // struct app field offsets within a slot (syskrnl.asm STRUCT app)
    static constexpr uint16_t APP_FLAGS = 0;
    static constexpr uint16_t APP_ID = 1;
    static constexpr uint16_t APP_PARENT = 2;
    static constexpr uint16_t APP_MAINPG = 3;

    explicit NedoOsAnalyzer(EmulatorContext* context);

    // IAnalyzer
    std::string getName() const override { return "NedoOsAnalyzer"; }
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

    // Known-build profiles (values from the POC 020 symbol set, rev 44049473)
    static const std::vector<NedoProfile>& knownProfiles();

    // Exposed for tests: detection and audit on a raw 16K page image
    static int matchProfile(const uint8_t* page);  // index into knownProfiles(), -1 if none
    void setPageForTest(uint16_t page) { _pgsysPage = page; _state = State::DETECTED; }
    static uint8_t countOwnedPages(const uint8_t* owners, uint16_t count, uint8_t taskId);

private:
    enum class State : uint8_t { ARMING, SCANNING, DETECTED, UNKNOWN };

    void scanForKernel();
    void armTraps(AnalyzerManager* manager);
    void handleBdosCall(Z80* cpu);
    void handleQuit(Z80* cpu, uint16_t pc);
    void auditTaskExit(uint8_t taskId, uint16_t pc);
    void pollOwnership();
    void pollTaskTable();
    void emitEvent(OsEvent&& event);

    uint8_t readPhys(uint16_t offset) const;         // pgsys[offset]
    uint16_t readPhysWord(uint16_t offset) const;
    uint8_t currentTaskId() const;
    uint16_t ownerPageCount() const;                 // sys_npages, 0 means 256

    std::string _uuid;
    EmulatorContext* _context;
    AnalyzerManager* _manager = nullptr;

    State _state = State::ARMING;
    int _profileIndex = -1;
    uint16_t _pgsysPage = 0;

    bool _pollPending = false;
    std::vector<uint8_t> _lastOwners;   // tsys_pages snapshot
    std::array<uint8_t, 16> _lastTaskIds{};

    uint64_t _skippedQuiet = 0;         // YIELD/PRCHAR-class calls, counted not streamed
    uint64_t _lastQueryTime = 0;
    uint64_t _lastEmittedTs = 0;        // Monotonic guard for the ring's timestamp filter

    RingBuffer<OsEvent> _events;
};
