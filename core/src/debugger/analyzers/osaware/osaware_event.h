#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Shared data model for OS-aware analyzers (NedoOS, Sprinter DSS).
/// Pure data + a query mixin: no OS-specific logic lives here, so the core
/// stays generic (devtools roadmap N2; the declarative descriptor engine of
/// Phase 6 will later replace the compiled-in profiles, not this event model).

enum class OsEventKind : uint8_t
{
    OS_DETECTED,        // A known OS image was found and is now tracked
    PROFILE_UNKNOWN,    // Vector activity without a recognized build profile
    SYSCALL,            // Decoded OS/BIOS call at its entry gate
    TASK_START,         // A task appeared in the OS task table
    TASK_EXIT,          // A task left the task table
    PAGE_ALLOC,         // Page/block ownership changed free -> task
    PAGE_FREE,          // Page/block ownership changed task -> free
    FILE_OPEN,          // File handle acquired by a task
    FILE_CLOSE,         // File handle released
    FINDING             // Rule violation: leak, corruption, unbalance
};

enum class OsSeverity : uint8_t
{
    INFO,
    WARNING,
    ERROR_
};

inline const char* OsEventKindName(OsEventKind kind)
{
    switch (kind)
    {
        case OsEventKind::OS_DETECTED: return "OS_DETECTED";
        case OsEventKind::PROFILE_UNKNOWN: return "PROFILE_UNKNOWN";
        case OsEventKind::SYSCALL: return "SYSCALL";
        case OsEventKind::TASK_START: return "TASK_START";
        case OsEventKind::TASK_EXIT: return "TASK_EXIT";
        case OsEventKind::PAGE_ALLOC: return "PAGE_ALLOC";
        case OsEventKind::PAGE_FREE: return "PAGE_FREE";
        case OsEventKind::FILE_OPEN: return "FILE_OPEN";
        case OsEventKind::FILE_CLOSE: return "FILE_CLOSE";
        case OsEventKind::FINDING: return "FINDING";
    }
    return "?";
}

inline const char* OsSeverityName(OsSeverity severity)
{
    switch (severity)
    {
        case OsSeverity::INFO: return "info";
        case OsSeverity::WARNING: return "warning";
        case OsSeverity::ERROR_: return "error";
    }
    return "?";
}

/// One semantic OS event (ring-buffered like TRDOSEvent).
struct OsEvent
{
    uint64_t timestamp = 0;      // CPU T-states (cpu->tt), as in TRDOSEvent
    uint32_t frameNumber = 0;
    OsEventKind kind = OsEventKind::SYSCALL;
    OsSeverity severity = OsSeverity::INFO;
    uint16_t pc = 0;             // PC at the trap
    uint16_t caller = 0;         // Return address from the stack, when known
    uint8_t taskId = 0;          // 0 = system/unknown
    uint8_t syscall = 0;         // Function/command number (C register)
    uint16_t arg16 = 0;          // One decoded argument (per-event meaning)
    std::string message;         // Short human line, e.g. "DSS #3D GETMEM"
    std::string detail;          // Extra decoded fields, e.g. "pages=2"

    std::string format() const;
};

/// A task of the guest OS, as shown by a snapshot.
struct OsTask
{
    uint8_t id = 0;
    uint8_t parent = 0;
    uint8_t flags = 0;
    uint8_t mainpg = 0;          // NedoOS mainpg; DSS: 0
    std::string name;            // first word of the task's command line, when known
    uint8_t stdinFd = 0;         // NedoOS stdin pipe handle (0x80+i); 0 = none
    uint8_t stdoutFd = 0;
    uint8_t stderrFd = 0;
    uint8_t lastTime = 0;        // NedoOS time quanta consumed (raw kernel counter)
};

/// One owned memory unit. NedoOS: a 16K RAM page; DSS: a block id in MEMTAB.
struct OsBlock
{
    uint16_t index = 0;
    uint8_t owner = 0;           // task id, 0xFF = system
};

/// One file handle owned by a task (DSS FM table entries, NedoOS FAT FIL entries).
struct OsHandle
{
    uint16_t index = 0;
    uint8_t owner = 0;
    std::string name;            // best-effort name, may be empty
    uint32_t position = 0;       // file pointer, when the OS stores one
    uint32_t size = 0;           // file size, when the OS stores one
};

/// One pipe between NedoOS tasks (handle numbers are 0x80+i like the kernel's).
struct OsPipe
{
    uint16_t handle = 0;
    uint8_t openSides = 0;       // sides still open (writer bits / reader bit)
    uint8_t reader = 0;          // task id that owns the read end (0 = none yet)
    uint8_t lines = 0;           // terminated lines queued
    uint16_t queued = 0;         // bytes queued
};

/// Point-in-time OS state for surfaces (tasks, ownership, handles).
struct OsSnapshot
{
    bool detected = false;
    std::string profile;         // e.g. "nedoos-44049473", "dss-1.71.66"
    std::string os;              // "nedoos" | "dss"
    std::vector<OsTask> tasks;
    std::vector<OsBlock> blocks;
    std::vector<OsHandle> handles;
    std::vector<OsPipe> pipes;
};

/// Query mixin implemented by OS-aware analyzers; surfaces branch on this
/// interface once for every OS analyzer instead of per-analyzer classes.
class IOSAnalyzer
{
public:
    virtual ~IOSAnalyzer() = default;

    virtual std::vector<OsEvent> getOsEvents() const = 0;
    virtual std::vector<OsEvent> getOsEventsSince(uint64_t timestamp) const = 0;
    virtual std::vector<OsEvent> getNewOsEvents() = 0;
    virtual void clearOsEvents() = 0;
    virtual OsSnapshot getOsSnapshot() const = 0;
    virtual std::string osProfileName() const = 0;
};
