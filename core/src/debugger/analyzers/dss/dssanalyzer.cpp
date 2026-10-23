#include "dssanalyzer.h"

#include "debugger/analyzers/analyzermanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "common/uuid.h"

#include <cstdio>
#include <sstream>

// ==================== Profiles ====================

// Anchors verified against the 1.71.66 build's own export
// (testdata/machines/sprinter/dss/1.71.66/system.exp + system.sym):
//   A0000=0 (vector 0x0000 is JP RETFAR), RETFAR=0x1CAC, RST_10=0x0068,
//   LEAVE=0x1E7E, TASK=0x1E72, CORE_BUFFERS.MemoryTable=0x38EB,
//   CORE_BUFFERS.FM_BUF=0x318E (10 handles, stride 54, FTASK +42).
const std::vector<DssProfile>& DssAnalyzer::knownProfiles()
{
    static const std::vector<DssProfile> profiles = {
        {"dss-1.71.66", 0x1CAC, 0x0068, 0x1E7E, 0x1E72, 0x38EB, 0x318E, 10, 54, 42},
    };
    return profiles;
}

// DSS function names (DSS.INC of the official sources)
static const char* dssFunctionName(uint8_t fn)
{
    switch (fn)
    {
        case 0x00: return "VERSION";
        case 0x01: return "CHDISK";
        case 0x02: return "CURDISK";
        case 0x03: return "DSKINFO";
        case 0x09: return "BOOTDSK";
        case 0x0A: return "CREATE";
        case 0x0B: return "CREAT_N";
        case 0x0E: return "DELETE";
        case 0x10: return "RENAME";
        case 0x11: return "OPEN";
        case 0x12: return "CLOSE";
        case 0x13: return "READ";
        case 0x14: return "WRITE";
        case 0x15: return "MOVE_FP";
        case 0x16: return "ATTRIB";
        case 0x17: return "GET_D_T";
        case 0x18: return "PUT_D_T";
        case 0x19: return "F_FIRST";
        case 0x1A: return "F_NEXT";
        case 0x1B: return "MKDIR";
        case 0x1C: return "RMDIR";
        case 0x1D: return "CHDIR";
        case 0x1E: return "CURDIR";
        case 0x21: return "SYSTIME";
        case 0x22: return "SETTIME";
        case 0x30: return "WAITKEY";
        case 0x31: return "SCANKEY";
        case 0x32: return "ECHOKEY";
        case 0x33: return "CTRLKEY";
        case 0x35: return "K_CLEAR";
        case 0x36: return "K_SETUP";
        case 0x37: return "TESTKEY";
        case 0x38: return "SETWIN";
        case 0x39: return "SETWIN1";
        case 0x3A: return "SETWIN2";
        case 0x3B: return "SETWIN3";
        case 0x3C: return "INFOMEM";
        case 0x3D: return "GETMEM";
        case 0x3E: return "FREEMEM";
        case 0x3F: return "SETMEM";
        case 0x40: return "EXEC";
        case 0x41: return "EXIT";
        case 0x42: return "WAIT";
        case 0x43: return "GSWITCH";
        case 0x44: return "DOSNAME";
        case 0x45: return "EX_PATH";
        case 0x46: return "ENVIRON";
        case 0x47: return "APPINFO";
        case 0x50: return "SETVMOD";
        case 0x51: return "GETVMOD";
        case 0x52: return "LOCATE";
        case 0x53: return "CURSOR";
        case 0x54: return "SELPAGE";
        case 0x55: return "SCROLL";
        case 0x56: return "CLEAR";
        case 0x57: return "RDCHAR";
        case 0x58: return "WRCHAR";
        case 0x59: return "WINCOPY";
        case 0x5A: return "WINREST";
        case 0x5B: return "PUTCHAR";
        case 0x5C: return "PCHARS";
        case 0x5F: return "PRINT";
        default: return nullptr;
    }
}

// BIOS commands seen at RST #08 (semantics from their use in the DSS sources;
// the full list lives in Shared_Includes/Docs/BIOS functions.asm)
static const char* biosCommandName(uint8_t cmd)
{
    switch (cmd)
    {
        case 0x9D: return "FREE_TAIL";
        case 0x9E: return "JOIN_BLOCKS";
        case 0xC0: return "MEMINFO";
        case 0xC2: return "ALLOC_PAGES";
        case 0xC3: return "FREE_BLOCK";
        case 0xC4: return "GET_PAGE";
        default: return nullptr;
    }
}

// Console/polling calls that fire constantly while a shell or editor runs,
// and the window mapper #C4 that every disk buffer access goes through:
// counted, not streamed.
static bool isQuietDssFunction(uint8_t fn)
{
    return fn == 0x30   // WAITKEY
           || fn == 0x31  // SCANKEY
           || fn == 0x33  // CTRLKEY
           || fn == 0x52  // LOCATE
           || fn == 0x53  // CURSOR
           || fn == 0x5B  // PUTCHAR
           || fn == 0x5C; // PCHARS
}

// ==================== Construction / lifecycle ====================

DssAnalyzer::DssAnalyzer(EmulatorContext* context)
    : _context(context), _events(EVENT_BUFFER_SIZE)
{
    _uuid = unreal::UUID::Generate().toString();
}

void DssAnalyzer::onActivate(AnalyzerManager* manager)
{
    _manager = manager;
    _state = State::SCANNING;
    _profileIndex = -1;
    _pollPending = false;
    _lastMemtab.clear();
    _skippedQuiet = 0;
}

void DssAnalyzer::onDeactivate()
{
    _state = State::ARMING;
}

void DssAnalyzer::armTraps(AnalyzerManager* manager)
{
    // Not page-pinned: on the Sprinter the kernel's page is whatever window 0
    // maps while DSS is resident; every hit re-checks the kernel shape.
    manager->requestExecutionBreakpoint(BP_LEAVE_VECTOR, _registrationId);
    manager->requestExecutionBreakpoint(BP_RST08, _registrationId);
    manager->requestExecutionBreakpoint(BP_RST10, _registrationId);
    if (_profileIndex >= 0)
    {
        manager->requestExecutionBreakpoint(knownProfiles()[_profileIndex].leave, _registrationId);
    }
}

// ==================== Detection ====================

bool DssAnalyzer::hasDssShape(const uint8_t* w)
{
    if (!w)
    {
        return false;
    }
    // 0x0000: JP nnnn (RETFAR), 0x0008: PUSH AF; LD A,0; OUT (#7C),A; POP AF; RET,
    // 0x0010: JP nnnn (RST_10) - stable across the 1.6x-1.7x kernels
    return w[0x00] == 0xC3 && w[0x08] == 0xF5 && w[0x09] == 0x3E && w[0x0A] == 0x00
           && w[0x0B] == 0xD3 && w[0x0C] == 0x7C && w[0x0D] == 0xF1 && w[0x0E] == 0xC9
           && w[0x10] == 0xC3;
}

int DssAnalyzer::matchProfile(const uint8_t* w)
{
    if (!hasDssShape(w))
    {
        return -1;
    }
    const uint16_t retfar = static_cast<uint16_t>(w[0x0001] | (w[0x0002] << 8));
    const uint16_t rst10 = static_cast<uint16_t>(w[0x0011] | (w[0x0012] << 8));
    const auto& profiles = knownProfiles();
    for (size_t i = 0; i < profiles.size(); i++)
    {
        if (profiles[i].retfar == retfar && profiles[i].rst10 == rst10)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DssAnalyzer::detectKernel()
{
    if (!_context || !_context->pMemory)
    {
        return;
    }
    Memory& memory = *_context->pMemory;
    uint8_t window0[0x0013];
    for (uint16_t i = 0; i < sizeof(window0); i++)
    {
        window0[i] = memory.DirectReadFromZ80Memory(i);
    }

    if (!hasDssShape(window0))
    {
        return;
    }

    const int index = matchProfile(window0);
    _profileIndex = index;
    _state = index >= 0 ? State::DETECTED : State::UNKNOWN;

    OsEvent event;
    event.kind = index >= 0 ? OsEventKind::OS_DETECTED : OsEventKind::PROFILE_UNKNOWN;
    char message[96];
    std::snprintf(message, sizeof(message), "%s DSS kernel in window 0",
                  index >= 0 ? "Estex" : "unknown-build");
    event.message = message;
    char detail[96];
    std::snprintf(detail, sizeof(detail), "profile=%s",
                  index >= 0 ? knownProfiles()[index].name : "unknown (vector-only mode)");
    event.detail = detail;
    emitEvent(std::move(event));

    if (_manager)
    {
        armTraps(_manager);
    }
    _pollPending = true;
}

bool DssAnalyzer::kernelStillResident() const
{
    if (!_context || !_context->pMemory)
    {
        return false;
    }
    Memory& memory = *_context->pMemory;
    return memory.DirectReadFromZ80Memory(0x0008) == 0xF5
           && memory.DirectReadFromZ80Memory(0x000B) == 0xD3
           && memory.DirectReadFromZ80Memory(0x000C) == 0x7C;
}

// ==================== Frame hook ====================

void DssAnalyzer::onFrameEnd()
{
    if (_state == State::SCANNING)
    {
        detectKernel();
    }
    else if ((_state == State::DETECTED || _state == State::UNKNOWN) && _pollPending)
    {
        _pollPending = false;
        pollMemtab();
    }
}

// ==================== Breakpoint handling ====================

void DssAnalyzer::onBreakpointHit(uint16_t address, Z80* cpu)
{
    if ((_state != State::DETECTED && _state != State::UNKNOWN) || !cpu)
    {
        return;
    }
    if (!kernelStillResident())
    {
        return; // A reset put other code at these addresses
    }
    if (address == BP_RST10)
    {
        handleDssCall(cpu);
    }
    else if (address == BP_RST08)
    {
        handleBiosCall(cpu);
    }
    else // BP_LEAVE_VECTOR (RETFAR) or LEAVE: a task is exiting
    {
        handleTaskExit(cpu->pc);
    }
}

void DssAnalyzer::handleDssCall(Z80* cpu)
{
    const uint8_t fn = cpu->c;
    if (isQuietDssFunction(fn))
    {
        _skippedQuiet++;
        return;
    }

    OsEvent event;
    event.timestamp = cpu->tt;
    event.pc = cpu->pc;
    event.caller = readZ80Word(cpu->sp); // RST pushed the return address
    event.taskId = currentTaskId();
    event.syscall = fn;
    event.kind = OsEventKind::SYSCALL;

    const char* name = dssFunctionName(fn);
    char message[48];
    std::snprintf(message, sizeof(message), "DSS #%02X %s", fn, name ? name : "");
    event.message = message;

    char detail[64];
    switch (fn)
    {
        case 0x3C: // INFOMEM
        case 0x3D: // GETMEM: B = pages wanted
        case 0x3F: // SETMEM: B = new size
            std::snprintf(detail, sizeof(detail), "b=%u de=#%04X hl=#%04X", cpu->b, cpu->de, cpu->hl);
            break;
        case 0x11: // OPEN
            std::snprintf(detail, sizeof(detail), "de=#%04X (FCB)", cpu->de);
            break;
        case 0x12: // CLOSE
            std::snprintf(detail, sizeof(detail), "a=%u (handle)", cpu->a);
            break;
        default:
            std::snprintf(detail, sizeof(detail), "b=%u de=#%04X hl=#%04X", cpu->b, cpu->de, cpu->hl);
            break;
    }
    event.detail = detail;

    const bool isFileOpen = (fn == 0x11);
    const bool isFileClose = (fn == 0x12);
    if (isFileOpen)
    {
        event.kind = OsEventKind::FILE_OPEN;
    }
    else if (isFileClose)
    {
        event.kind = OsEventKind::FILE_CLOSE;
    }
    emitEvent(std::move(event));

    if (fn == 0x3D || fn == 0x3E || fn == 0x3F)
    {
        _pollPending = true; // GETMEM / FREEMEM / SETMEM change MEMTAB
    }
    else if (fn == 0x41)
    {
        handleTaskExit(cpu->pc); // EXIT
    }
}

void DssAnalyzer::handleBiosCall(Z80* cpu)
{
    const uint8_t cmd = cpu->c;
    if (cmd == 0xC4)
    {
        // GET_PAGE: the window mapper behind every SETWIN, disk-buffer access
        // and driver page switch - thousands per disk operation
        _skippedQuiet++;
        return;
    }

    OsEvent event;
    event.timestamp = cpu->tt;
    event.pc = cpu->pc;
    event.caller = readZ80Word(cpu->sp);
    event.taskId = currentTaskId();
    event.syscall = cmd;
    event.kind = OsEventKind::SYSCALL;

    const char* name = biosCommandName(cmd);
    char message[48];
    std::snprintf(message, sizeof(message), "BIOS #%02X %s", cmd, name ? name : "");
    event.message = message;
    char detail[64];
    std::snprintf(detail, sizeof(detail), "b=%u (pages/size)", cpu->b);
    event.detail = detail;
    emitEvent(std::move(event));

    if (cmd == 0xC2 || cmd == 0xC3 || cmd == 0x9D || cmd == 0x9E)
    {
        _pollPending = true; // BIOS-level alloc/free: the real leak surface
    }
}

void DssAnalyzer::handleTaskExit(uint16_t pc)
{
    const uint8_t taskId = currentTaskId();
    auditTaskExit(taskId, pc);
    _pollPending = true; // LEAVMEM frees the task's blocks right after
}

// ==================== Audit ====================

std::vector<uint16_t> DssAnalyzer::ownedBlockIds(const uint8_t* memtab, uint8_t taskId)
{
    std::vector<uint16_t> ids;
    if (!memtab || taskId == 0)
    {
        return ids;
    }
    for (uint16_t block = 0; block < 256; block++)
    {
        if (memtab[block] == taskId)
        {
            ids.push_back(block);
        }
    }
    return ids;
}

std::vector<OsHandle> DssAnalyzer::fmHandlesOfTask(const uint8_t* fm, const DssProfile& profile,
                                                   uint8_t taskId)
{
    std::vector<OsHandle> handles;
    if (!fm || profile.fmBuf == 0 || profile.fmCount == 0 || taskId == 0)
    {
        return handles;
    }
    for (uint16_t i = 0; i < profile.fmCount; i++)
    {
        const uint8_t* entry = fm + static_cast<uint32_t>(i) * profile.fmStride;
        if (entry[profile.fmOwnerOff] != taskId)
        {
            continue;
        }
        OsHandle handle;
        handle.index = i;
        handle.owner = taskId;
        std::string name;
        for (int c = 0; c < 11; c++)
        {
            const uint8_t ch = entry[c];
            if (ch >= 0x20 && ch < 0x7F)
            {
                name.push_back(static_cast<char>(ch));
            }
        }
        handle.name = name;
        handles.push_back(handle);
    }
    return handles;
}

void DssAnalyzer::auditTaskExit(uint8_t taskId, uint16_t pc)
{
    if (taskId == 0)
    {
        return;
    }

    OsEvent exitEvent;
    exitEvent.kind = OsEventKind::TASK_EXIT;
    exitEvent.taskId = taskId;
    exitEvent.pc = pc;
    exitEvent.message = "task exiting";
    emitEvent(std::move(exitEvent));

    if (_profileIndex < 0 || !_context || !_context->pMemory)
    {
        return;
    }
    const DssProfile& profile = knownProfiles()[_profileIndex];
    Memory& memory = *_context->pMemory;

    uint8_t memtab[256];
    for (uint16_t i = 0; i < 256; i++)
    {
        memtab[i] = memory.DirectReadFromZ80Memory(static_cast<uint16_t>(profile.memtab + i));
    }
    const auto blocks = ownedBlockIds(memtab, taskId);
    if (!blocks.empty())
    {
        // LEAVMEM returns these blocks right after LEAVE, so this is the
        // "forgot FREEMEM" warning; a real forever-leak shows up as BIOS-level
        // alloc/free imbalance (class B), polled the same way.
        OsEvent finding;
        finding.kind = OsEventKind::FINDING;
        finding.severity = OsSeverity::WARNING;
        finding.taskId = taskId;
        finding.pc = pc;
        char message[128];
        std::snprintf(message, sizeof(message), "DSS task %u exits owning %zu block(s)",
                      static_cast<unsigned>(taskId), blocks.size());
        finding.message = message;
        std::string ids;
        for (size_t i = 0; i < blocks.size() && i < 8; i++)
        {
            char id[8];
            std::snprintf(id, sizeof(id), "%s%u", i ? "," : "", blocks[i]);
            ids += id;
        }
        finding.detail = "blocks " + ids + "; the kernel frees them (the app forgot FREEMEM)";
        emitEvent(std::move(finding));
    }

    if (profile.fmBuf != 0)
    {
        std::vector<uint8_t> fm(static_cast<uint32_t>(profile.fmCount) * profile.fmStride);
        for (uint32_t i = 0; i < fm.size(); i++)
        {
            fm[i] = memory.DirectReadFromZ80Memory(static_cast<uint16_t>(profile.fmBuf + i));
        }
        const auto handles = fmHandlesOfTask(fm.data(), profile, taskId);
        if (!handles.empty())
        {
            // LEAVE does NOT close the task's files: these handles stay in the
            // FM table forever (class C) - the "Unexpected application
            // termination" shape of plan #99's first case.
            OsEvent finding;
            finding.kind = OsEventKind::FINDING;
            finding.severity = OsSeverity::WARNING;
            finding.taskId = taskId;
            finding.pc = pc;
            char message[128];
            std::snprintf(message, sizeof(message), "DSS task %u exits leaving %zu file handle(s) open",
                          static_cast<unsigned>(taskId), handles.size());
            finding.message = message;
            std::string names;
            for (size_t i = 0; i < handles.size() && i < 4; i++)
            {
                if (i)
                {
                    names += ", ";
                }
                names += handles[i].name.empty() ? "?" : handles[i].name;
            }
            finding.detail = "LEAVE does not close files: " + names;
            emitEvent(std::move(finding));
        }
    }
}

// ==================== MEMTAB polling ====================

void DssAnalyzer::pollMemtab()
{
    if (_profileIndex < 0 || !_context || !_context->pMemory)
    {
        return;
    }
    const DssProfile& profile = knownProfiles()[_profileIndex];
    Memory& memory = *_context->pMemory;

    uint8_t memtab[256];
    for (uint16_t i = 0; i < 256; i++)
    {
        memtab[i] = memory.DirectReadFromZ80Memory(static_cast<uint16_t>(profile.memtab + i));
    }
    if (_lastMemtab.empty())
    {
        _lastMemtab.assign(memtab, memtab + 256);
        return;
    }
    for (uint16_t block = 0; block < 256; block++)
    {
        if (memtab[block] == _lastMemtab[block])
        {
            continue;
        }
        OsEvent event;
        event.arg16 = block;
        if (memtab[block] != 0)
        {
            event.kind = OsEventKind::PAGE_ALLOC;
            event.taskId = memtab[block];
            char message[64];
            std::snprintf(message, sizeof(message), "block %u -> task %u",
                          static_cast<unsigned>(block), static_cast<unsigned>(memtab[block]));
            event.message = message;
        }
        else
        {
            event.kind = OsEventKind::PAGE_FREE;
            event.taskId = _lastMemtab[block];
            char message[64];
            std::snprintf(message, sizeof(message), "block %u freed (was task %u)",
                          static_cast<unsigned>(block), static_cast<unsigned>(_lastMemtab[block]));
            event.message = message;
        }
        emitEvent(std::move(event));
    }
    _lastMemtab.assign(memtab, memtab + 256);
}

// ==================== Events / snapshot ====================

void DssAnalyzer::emitEvent(OsEvent&& event)
{
    // Keep the ring's timestamp filter meaningful across mixed sources.
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

uint8_t DssAnalyzer::readZ80(uint16_t addr) const
{
    return _context && _context->pMemory ? _context->pMemory->DirectReadFromZ80Memory(addr) : 0;
}

uint16_t DssAnalyzer::readZ80Word(uint16_t addr) const
{
    return static_cast<uint16_t>(readZ80(addr) | (readZ80(static_cast<uint16_t>(addr + 1)) << 8));
}

uint8_t DssAnalyzer::currentTaskId() const
{
    if (_profileIndex < 0)
    {
        return 0;
    }
    return readZ80(knownProfiles()[_profileIndex].task);
}

std::vector<OsEvent> DssAnalyzer::getOsEvents() const
{
    return _events.getAll();
}

std::vector<OsEvent> DssAnalyzer::getOsEventsSince(uint64_t timestamp) const
{
    return _events.getSince(timestamp);
}

std::vector<OsEvent> DssAnalyzer::getNewOsEvents()
{
    auto events = _events.getSince(_lastQueryTime);
    if (!events.empty())
    {
        _lastQueryTime = events.back().timestamp;
    }
    return events;
}

void DssAnalyzer::clearOsEvents()
{
    _events.clear();
}

OsSnapshot DssAnalyzer::getOsSnapshot() const
{
    OsSnapshot snapshot;
    snapshot.os = "dss";
    snapshot.detected = (_state == State::DETECTED);
    snapshot.profile = osProfileName();
    if ((_state != State::DETECTED && _state != State::UNKNOWN) || !_context || !_context->pMemory)
    {
        return snapshot;
    }
    const DssProfile& profile = knownProfiles()[_profileIndex];
    Memory& memory = *_context->pMemory;

    // DSS keeps one current-task byte, not a task table
    const uint8_t task = readZ80(profile.task);
    if (task != 0)
    {
        OsTask current;
        current.id = task;
        snapshot.tasks.push_back(current);
    }

    if (profile.memtab != 0)
    {
        for (uint16_t block = 0; block < 256; block++)
        {
            const uint8_t owner = memory.DirectReadFromZ80Memory(static_cast<uint16_t>(profile.memtab + block));
            if (owner != 0)
            {
                snapshot.blocks.push_back(OsBlock{block, owner});
            }
        }
    }

    if (profile.fmBuf != 0)
    {
        std::vector<uint8_t> fm(static_cast<uint32_t>(profile.fmCount) * profile.fmStride);
        for (uint32_t i = 0; i < fm.size(); i++)
        {
            fm[i] = memory.DirectReadFromZ80Memory(static_cast<uint16_t>(profile.fmBuf + i));
        }
        for (uint16_t i = 0; i < profile.fmCount; i++)
        {
            const uint8_t* entry = fm.data() + static_cast<uint32_t>(i) * profile.fmStride;
            if (entry[profile.fmOwnerOff] == 0)
            {
                continue;
            }
            OsHandle handle;
            handle.index = i;
            handle.owner = entry[profile.fmOwnerOff];
            for (int c = 0; c < 11; c++)
            {
                const uint8_t ch = entry[c];
                if (ch >= 0x20 && ch < 0x7F)
                {
                    handle.name.push_back(static_cast<char>(ch));
                }
            }
            snapshot.handles.push_back(handle);
        }
    }
    return snapshot;
}

std::string DssAnalyzer::osProfileName() const
{
    if (_profileIndex < 0)
    {
        return _state == State::ARMING ? "" : "unknown";
    }
    return knownProfiles()[_profileIndex].name;
}
