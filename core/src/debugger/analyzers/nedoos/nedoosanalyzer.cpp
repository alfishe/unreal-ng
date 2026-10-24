#include "nedoosanalyzer.h"

#include "debugger/analyzers/analyzermanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "common/uuid.h"

#include <cstdio>
#include <sstream>

// ==================== OsEvent formatting ====================

std::string OsEvent::format() const
{
    std::ostringstream ss;
    ss << "[Frame " << frameNumber << "] " << OsEventKindName(kind);
    if (severity != OsSeverity::INFO)
    {
        ss << " (" << OsSeverityName(severity) << ")";
    }
    if (taskId != 0)
    {
        ss << " task=" << static_cast<unsigned>(taskId);
    }
    if (!message.empty())
    {
        ss << " " << message;
    }
    if (!detail.empty())
    {
        ss << " [" << detail << "]";
    }
    if (pc != 0)
    {
        ss << " pc=#" << std::hex << pc << std::dec;
    }
    return ss.str();
}

// ==================== Profiles ====================

// Symbol values of revision 44049473 (tools/poc/020-nedoos-layer symbol set,
// generated from the NedoOS sources; see nedoos-integration kernel reference).
const std::vector<NedoProfile>& NedoOsAnalyzer::knownProfiles()
{
    static const std::vector<NedoProfile> profiles = {
        {"nedoos-44049473", 0x0B7E, 0x0B57, 0x0909, 0x2B29, 0x00C0, 0x0786, 0x0112, 0x0068, 16},
    };
    return profiles;
}

// BDOS command names (from the kernel symbol table; QUIT is documented in
// nedoos.txt as command 0).
static const char* bdosCommandName(uint8_t cmd)
{
    switch (cmd)
    {
        case 0x00: return "QUIT";
        case 0x05: return "PRCHAR";
        case 0x0E: return "SETDRV";
        case 0x0F: return "FOPEN";
        case 0x10: return "FCLOSE";
        case 0x11: return "FSEARCHFIRST";
        case 0x12: return "FSEARCHNEXT";
        case 0x13: return "FDEL";
        case 0x14: return "FREAD";
        case 0x15: return "FWRITE";
        case 0x16: return "FCREATE";
        case 0x1A: return "SETDTA";
        case 0x21: return "RNDRD";
        case 0x22: return "RNDWR";
        case 0x43: return "OPENHANDLE";
        case 0x44: return "CREATEHANDLE";
        case 0x45: return "CLOSEHANDLE";
        case 0x48: return "READHANDLE";
        case 0x49: return "WRITEHANDLE";
        case 0x4A: return "SEEKHANDLE";
        case 0x4D: return "DELETE";
        case 0x4E: return "RENAME";
        case 0x5A: return "CHDIR";
        case 0x5C: return "PARSEFNAME";
        case 0x5E: return "GETPATH";
        case 0xC6: return "PUTKEY";
        case 0xC7: return "GETGFX";
        case 0xC8: return "GETPAL";
        case 0xC9: return "SETTIME";
        case 0xCA: return "GETMEMPORTS";
        case 0xCB: return "GETPAGEOWNER";
        case 0xCC: return "GETCONFIG";
        case 0xCD: return "GETCHILDRESULT";
        case 0xCF: return "OPENDIR";
        case 0xD0: return "READDIR";
        case 0xD1: return "HIDEFROMPARENT";
        case 0xD2: return "SETSTDINOUT";
        case 0xD3: return "GETSTDINOUT";
        case 0xD4: return "PLAYCOVOX";
        case 0xD5: return "SETMUSIC";
        case 0xD6: return "READSECTORS";
        case 0xD7: return "WRITESECTORS";
        case 0xD8: return "SETBORDER";
        case 0xD9: return "SETWAITING";
        case 0xDA: return "GETFILESIZE";
        case 0xDB: return "WIZNETOPEN";
        case 0xDC: return "WIZNETCLOSE";
        case 0xDD: return "WIZNETREAD";
        case 0xDE: return "WIZNETWRITE";
        case 0xDF: return "DROPAPP";
        case 0xE0: return "GETAPPMAINPAGES";
        case 0xE1: return "GETXY";
        case 0xE2: return "GETTIME";
        case 0xE3: return "GETFILETIME";
        case 0xE4: return "SETFILETIME";
        case 0xE5: return "TELLHANDLE";
        case 0xE6: return "SCROLLUP";
        case 0xE7: return "SCROLLDOWN";
        case 0xE8: return "GETFILINFO";
        case 0xE9: return "SETMAINPAGE";
        case 0xEA: return "SETSYSDRV";
        case 0xEB: return "MKDIR";
        case 0xEC: return "CHECKPID";
        case 0xED: return "FREEZEAPP";
        case 0xEE: return "GETATTR";
        case 0xEF: return "MOUNT";
        case 0xF0: return "GETKEYMATRIX";
        case 0xF1: return "GETTIMER";
        case 0xF2: return "YIELD";
        case 0xF3: return "RUNAPP";
        case 0xF4: return "NEWAPP";
        case 0xF5: return "PRATTR";
        case 0xF6: return "CLS";
        case 0xF7: return "SETCOLOR";
        case 0xF8: return "SETXY";
        case 0xF9: return "SETGFX";
        case 0xFA: return "SETPAL";
        case 0xFB: return "GETMAINPAGES";
        case 0xFC: return "NEWPAGE";
        case 0xFD: return "DELPAGE";
        case 0xFE: return "SETSCREEN";
        case 0xFF: return "YIELDKEEP";
        default: return nullptr;
    }
}

// Hot-path commands that fire thousands of times per second under load:
// counted, not streamed (they would evict everything else from the ring).
static bool isQuietCommand(uint8_t cmd)
{
    return cmd == 0x05   // PRCHAR
           || cmd == 0xF5  // PRATTR
           || cmd == 0xF2  // YIELD
           || cmd == 0xFF; // YIELDKEEP
}

// ==================== Construction / lifecycle ====================

NedoOsAnalyzer::NedoOsAnalyzer(EmulatorContext* context)
    : _context(context), _events(EVENT_BUFFER_SIZE)
{
    _uuid = unreal::UUID::Generate().toString();
}

void NedoOsAnalyzer::onActivate(AnalyzerManager* manager)
{
    _manager = manager;
    _state = State::SCANNING;
    _profileIndex = -1;
    _pgsysPage = 0;
    _pollPending = false;
    _lastOwners.clear();
    _lastTaskIds.fill(0);
    _skippedQuiet = 0;
}

void NedoOsAnalyzer::onDeactivate()
{
    _state = State::ARMING;
    // Breakpoints and subscriptions are auto-released by AnalyzerManager.
}

void NedoOsAnalyzer::armTraps(AnalyzerManager* manager)
{
    // Page-pinned so unrelated code mapped at #0004/#0009 never fires.
    manager->requestExecutionBreakpointInPage(BP_BDOS_CALL, static_cast<uint8_t>(_pgsysPage),
                                               BANK_RAM, _registrationId);
    manager->requestExecutionBreakpointInPage(BP_QUIT_STUB, static_cast<uint8_t>(_pgsysPage),
                                              BANK_RAM, _registrationId);
}

// ==================== Detection ====================

int NedoOsAnalyzer::matchProfile(const uint8_t* page)
{
    if (!page)
    {
        return -1;
    }
    // pgsys carries the CP/M-style entry stubs: jp sys_quit @#0004,
    // jp callbdos @#0009, jp sys_sysint @#0038 (POC 020 detect()).
    if (page[0x0004] != 0xC3 || page[0x0009] != 0xC3 || page[0x0038] != 0xC3)
    {
        return -1;
    }
    const uint16_t quitTarget = static_cast<uint16_t>(page[0x0005] | (page[0x0006] << 8));
    const uint16_t bdosTarget = static_cast<uint16_t>(page[0x000A] | (page[0x000B] << 8));
    const uint16_t intTarget = static_cast<uint16_t>(page[0x0039] | (page[0x003A] << 8));

    const auto& profiles = knownProfiles();
    for (size_t i = 0; i < profiles.size(); i++)
    {
        if (profiles[i].sysQuit == quitTarget && profiles[i].callBdos == bdosTarget
            && profiles[i].sysSysint == intTarget)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void NedoOsAnalyzer::scanForKernel()
{
    if (!_context || !_context->pMemory)
    {
        return;
    }
    Memory& memory = *_context->pMemory;
    for (uint16_t page = 0; page < MAX_RAM_PAGES_SCAN; page++)
    {
        uint8_t* base = memory.RAMPageAddress(page);
        if (!base)
        {
            continue;
        }
        if (base[0x0004] != 0xC3)
        {
            continue; // Fast reject: almost every page fails here
        }
        int index = matchProfile(base);
        if (index >= 0)
        {
            _pgsysPage = page;
            _profileIndex = index;
            _state = State::DETECTED;

            const NedoProfile& profile = knownProfiles()[index];
            OsEvent event;
            event.kind = OsEventKind::OS_DETECTED;
            event.message = "NedoOS kernel detected";
            char detail[96];
            std::snprintf(detail, sizeof(detail), "profile=%s pgsys=RAM page %u", profile.name,
                          static_cast<unsigned>(page));
            event.detail = detail;
            emitEvent(std::move(event));

            if (_manager)
            {
                armTraps(_manager);
            }
            pollOwnership();
            pollTaskTable();
            return;
        }
    }
}

// ==================== Physical page helpers ====================

uint8_t NedoOsAnalyzer::readPhys(uint16_t offset) const
{
    if (!_context || !_context->pMemory)
    {
        return 0;
    }
    uint8_t* base = _context->pMemory->RAMPageAddress(_pgsysPage);
    return base ? base[offset & 0x3FFF] : 0;
}

uint16_t NedoOsAnalyzer::readPhysWord(uint16_t offset) const
{
    return static_cast<uint16_t>(readPhys(offset) | (readPhys(offset + 1) << 8));
}

uint8_t NedoOsAnalyzer::currentTaskId() const
{
    if (_profileIndex < 0)
    {
        return 0;
    }
    const NedoProfile& profile = knownProfiles()[_profileIndex];
    // `ld iy,(appaddr)`: the word at appaddr points at the current STRUCT app.
    uint16_t appPtr = readPhysWord(profile.appAddr);
    if (appPtr < 0x4000)
    {
        return readPhys(static_cast<uint16_t>(appPtr + APP_ID));
    }
    return 0;
}

uint16_t NedoOsAnalyzer::ownerPageCount() const
{
    if (_profileIndex < 0)
    {
        return 0;
    }
    // sys_npages is an assemble-time constant of the kernel build (an EQU in
    // the symbol file, e.g. 0x00C0 = 192 pages), not a byte in memory.
    return knownProfiles()[_profileIndex].sysNpages;
}

// ==================== Frame hook ====================

void NedoOsAnalyzer::onFrameEnd()
{
    if (_state == State::SCANNING)
    {
        scanForKernel();
    }
    else if (_state == State::DETECTED)
    {
        if (_pollPending)
        {
            _pollPending = false;
            pollOwnership();
        }
        pollTaskTable();
    }
}

// ==================== Breakpoint handling ====================

void NedoOsAnalyzer::onBreakpointHit(uint16_t address, Z80* cpu)
{
    if (_state != State::DETECTED || !cpu)
    {
        return;
    }
    if (address == BP_BDOS_CALL)
    {
        handleBdosCall(cpu);
    }
    else if (address == BP_QUIT_STUB)
    {
        handleQuit(cpu, BP_QUIT_STUB);
    }
}

void NedoOsAnalyzer::handleBdosCall(Z80* cpu)
{
    const uint8_t cmd = cpu->c;
    if (isQuietCommand(cmd))
    {
        _skippedQuiet++;
        return;
    }

    OsEvent event;
    event.timestamp = cpu->tt;
    event.pc = cpu->pc;
    // At the callee entry the return address sits at [SP].
    if (_context && _context->pMemory)
    {
        Memory& memory = *_context->pMemory;
        event.caller = static_cast<uint16_t>(memory.DirectReadFromZ80Memory(cpu->sp)
                                             | (memory.DirectReadFromZ80Memory(static_cast<uint16_t>(cpu->sp + 1)) << 8));
    }
    event.taskId = currentTaskId();
    event.syscall = cmd;
    event.kind = OsEventKind::SYSCALL;

    const char* name = bdosCommandName(cmd);
    char message[48];
    std::snprintf(message, sizeof(message), "BDOS #CB%02X %s", cmd, name ? name : "");
    event.message = message;

    char detail[64];
    std::snprintf(detail, sizeof(detail), "b=#%02X de=#%04X hl=#%04X", cpu->b, cpu->de, cpu->hl);
    event.detail = detail;
    emitEvent(std::move(event));

    if (cmd == 0xFC || cmd == 0xFD)
    {
        // NEWPAGE / DELPAGE: diff the owner array after the call completes.
        _pollPending = true;
    }
}

void NedoOsAnalyzer::handleQuit(Z80* cpu, uint16_t pc)
{
    const uint8_t taskId = currentTaskId();
    auditTaskExit(taskId, pc);
    _pollPending = true; // The kernel frees the task's pages after QUIT
    (void)cpu;
}

void NedoOsAnalyzer::auditTaskExit(uint8_t taskId, uint16_t pc)
{
    if (_profileIndex < 0 || taskId == 0)
    {
        return;
    }
    const NedoProfile& profile = knownProfiles()[_profileIndex];

    OsEvent exitEvent;
    exitEvent.kind = OsEventKind::TASK_EXIT;
    exitEvent.taskId = taskId;
    exitEvent.pc = pc;
    exitEvent.message = "task exiting";
    emitEvent(std::move(exitEvent));

    const uint16_t pageCount = ownerPageCount();
    if (pageCount == 0 || pageCount > 256 || profile.tsysPages + pageCount > 0x4000)
    {
        return;
    }
    uint8_t* base = _context && _context->pMemory
                        ? _context->pMemory->RAMPageAddress(_pgsysPage)
                        : nullptr;
    if (!base)
    {
        return;
    }
    const uint8_t owned = countOwnedPages(base + profile.tsysPages, pageCount, taskId);
    if (owned > 0)
    {
        // The kernel releases these pages itself right after QUIT, so this is
        // a warning ("forgot DELPAGE"), not a hard leak - but it is exactly
        // the signal the audience question asked for.
        OsEvent finding;
        finding.kind = OsEventKind::FINDING;
        finding.severity = OsSeverity::WARNING;
        finding.taskId = taskId;
        finding.pc = pc;
        char message[96];
        std::snprintf(message, sizeof(message), "NedoOS task %u exits owning %u page(s)",
                      static_cast<unsigned>(taskId), static_cast<unsigned>(owned));
        finding.message = message;
        finding.detail = "kernel will free them; the app forgot CMD_DELPAGE";
        emitEvent(std::move(finding));
    }
}

uint8_t NedoOsAnalyzer::countOwnedPages(const uint8_t* owners, uint16_t count, uint8_t taskId)
{
    if (!owners || taskId == 0)
    {
        return 0; // 0 marks a free page, never an owner
    }
    uint8_t owned = 0;
    for (uint16_t i = 0; i < count; i++)
    {
        if (owners[i] == taskId)
        {
            owned++;
        }
    }
    return owned;
}

// ==================== Ownership / task table polling ====================

void NedoOsAnalyzer::pollOwnership()
{
    if (_profileIndex < 0 || !_context || !_context->pMemory)
    {
        return;
    }
    const NedoProfile& profile = knownProfiles()[_profileIndex];
    uint8_t* base = _context->pMemory->RAMPageAddress(_pgsysPage);
    if (!base || profile.tsysPages >= 0x4000)
    {
        return;
    }
    const uint16_t pageCount = ownerPageCount();
    if (pageCount == 0 || profile.tsysPages + pageCount > 0x4000)
    {
        return;
    }
    const uint8_t* owners = base + profile.tsysPages;

    if (_lastOwners.empty())
    {
        _lastOwners.assign(owners, owners + pageCount);
        return;
    }
    const uint16_t common = static_cast<uint16_t>(
        pageCount < _lastOwners.size() ? pageCount : _lastOwners.size());
    for (uint16_t i = 0; i < common; i++)
    {
        if (owners[i] == _lastOwners[i])
        {
            continue;
        }
        OsEvent event;
        event.frameNumber = _context ? _context->emulatorState.frame_counter : 0;
        const bool becameOwned = owners[i] != 0;
        event.kind = becameOwned ? OsEventKind::PAGE_ALLOC : OsEventKind::PAGE_FREE;
        event.arg16 = i;
        if (becameOwned)
        {
            event.taskId = owners[i];
            char message[64];
            std::snprintf(message, sizeof(message), "page %u -> task %u", static_cast<unsigned>(i),
                          static_cast<unsigned>(owners[i]));
            event.message = message;
        }
        else
        {
            event.taskId = _lastOwners[i];
            char message[64];
            std::snprintf(message, sizeof(message), "page %u freed (was task %u)",
                          static_cast<unsigned>(i), static_cast<unsigned>(_lastOwners[i]));
            event.message = message;
        }
        emitEvent(std::move(event));
    }
    _lastOwners.assign(owners, owners + pageCount);
}

void NedoOsAnalyzer::pollTaskTable()
{
    if (_profileIndex < 0 || !_context || !_context->pMemory)
    {
        return;
    }
    const NedoProfile& profile = knownProfiles()[_profileIndex];
    uint8_t* base = _context->pMemory->RAMPageAddress(_pgsysPage);
    if (!base)
    {
        return;
    }
    const uint32_t frame = _context ? _context->emulatorState.frame_counter : 0;
    for (uint8_t i = 0; i < profile.maxApps && i < _lastTaskIds.size(); i++)
    {
        const uint16_t slot = static_cast<uint16_t>(profile.app1
                                                    + static_cast<uint32_t>(i) * profile.appSz);
        if (slot + APP_MAINPG >= 0x4000)
        {
            break;
        }
        const uint8_t id = base[slot + APP_ID];
        if (id == _lastTaskIds[i])
        {
            continue;
        }
        OsEvent event;
        event.frameNumber = frame;
        event.taskId = id != 0 ? id : _lastTaskIds[i];
        if (id != 0)
        {
            event.kind = OsEventKind::TASK_START;
            event.arg16 = slot;
            char message[64];
            std::snprintf(message, sizeof(message), "task %u started (parent %u)",
                          static_cast<unsigned>(id), static_cast<unsigned>(base[slot + APP_PARENT]));
            event.message = message;
        }
        else if (_lastTaskIds[i] != 0)
        {
            event.kind = OsEventKind::TASK_EXIT;
            char message[64];
            std::snprintf(message, sizeof(message), "task %u left the table",
                          static_cast<unsigned>(_lastTaskIds[i]));
            event.message = message;
        }
        else
        {
            continue;
        }
        emitEvent(std::move(event));
        _lastTaskIds[i] = id;
    }
}

// ==================== Events / snapshot ====================

void NedoOsAnalyzer::emitEvent(OsEvent&& event)
{
    // Keep the ring's timestamp filter meaningful: breakpoint events carry
    // cpu->tt, frame polls do not - synthesize a strictly monotonic value.
    if (event.timestamp <= _lastEmittedTs)
    {
        event.timestamp = _lastEmittedTs + 1;
    }
    _lastEmittedTs = event.timestamp;
    if (_context)
    {
        event.frameNumber = _context->emulatorState.frame_counter;
    }
    _events.push(std::move(event));
}

std::vector<OsEvent> NedoOsAnalyzer::getOsEvents() const
{
    return _events.getAll();
}

std::vector<OsEvent> NedoOsAnalyzer::getOsEventsSince(uint64_t timestamp) const
{
    return _events.getSince(timestamp);
}

std::vector<OsEvent> NedoOsAnalyzer::getNewOsEvents()
{
    auto events = _events.getSince(_lastQueryTime);
    if (!events.empty())
    {
        _lastQueryTime = events.back().timestamp;
    }
    return events;
}

void NedoOsAnalyzer::clearOsEvents()
{
    _events.clear();
}

OsSnapshot NedoOsAnalyzer::getOsSnapshot() const
{
    OsSnapshot snapshot;
    snapshot.os = "nedoos";
    snapshot.detected = (_state == State::DETECTED);
    snapshot.profile = osProfileName();
    if (!snapshot.detected)
    {
        return snapshot;
    }

    const NedoProfile& profile = knownProfiles()[_profileIndex];
    uint8_t* base = _context && _context->pMemory ? _context->pMemory->RAMPageAddress(_pgsysPage)
                                                  : nullptr;
    if (!base)
    {
        return snapshot;
    }

    for (uint8_t i = 0; i < profile.maxApps; i++)
    {
        const uint16_t slot = static_cast<uint16_t>(profile.app1
                                                    + static_cast<uint32_t>(i) * profile.appSz);
        if (slot + APP_MAINPG >= 0x4000)
        {
            break;
        }
        if (base[slot + APP_ID] == 0)
        {
            continue;
        }
        OsTask task;
        task.id = base[slot + APP_ID];
        task.parent = base[slot + APP_PARENT];
        task.flags = base[slot + APP_FLAGS];
        task.mainpg = base[slot + APP_MAINPG];
        snapshot.tasks.push_back(task);
    }

    const uint16_t pageCount = ownerPageCount();
    if (pageCount > 0 && profile.tsysPages + pageCount <= 0x4000)
    {
        const uint8_t* owners = base + profile.tsysPages;
        for (uint16_t p = 0; p < pageCount; p++)
        {
            if (owners[p] != 0)
            {
                snapshot.blocks.push_back(OsBlock{p, owners[p]});
            }
        }
    }
    return snapshot;
}

std::string NedoOsAnalyzer::osProfileName() const
{
    if (_profileIndex < 0)
    {
        return _state == State::ARMING ? "" : "unknown";
    }
    return knownProfiles()[_profileIndex].name;
}
