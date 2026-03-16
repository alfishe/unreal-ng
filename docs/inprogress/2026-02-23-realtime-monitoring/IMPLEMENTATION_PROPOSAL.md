# Real-Time Monitoring Subsystem — Implementation Proposal

**Created:** 2026-02-23
**Status:** Implementation Proposal — Awaiting Review

## Table of Contents
- [Executive Summary](#executive-summary)
- [Existing Codebase Analysis](#existing-codebase-analysis)
- [Implementation Architecture](#implementation-architecture)
- [Section Specifications](#section-specifications)
- [Performance Optimization Strategies](#performance-optimization-strategies)
- [Integration Points](#integration-points)
- [Control Channel Design](#control-channel-design)
- [Frame Sync Notification](#frame-sync-notification)
- [Addressing Open Questions](#addressing-open-questions)
- [Potential Issues & Mitigations](#potential-issues--mitigations)
- [Phased Implementation Roadmap](#phased-implementation-roadmap)

---

## Executive Summary

This document provides a detailed implementation plan for extending the existing ZX-Spectrum emulator with a flexible, high-performance real-time monitoring subsystem. The design builds upon **existing, proven patterns** in the codebase and extends the current 5MB shared memory infrastructure with a **manifest protocol** while maintaining **zero-cost when disabled**.

### Design Goals

| Priority | Goal | Rationale |
|----------|------|-----------|
| **P0** | Zero-cost when disabled | Critical for production emulation performance |
| **P0** | Lock-free hot paths | Logging and state updates must not block emulation |
| **P1** | Runtime reconfiguration | Enable/disable sections without restart |
| **P1** | Cross-platform | POSIX shm_open / Windows CreateFileMapping |
| **P2** | Coherent reads | Observers see consistent snapshots, not torn states |

---

## Existing Codebase Analysis

### Proven Patterns to Leverage

| Component | Status | Integration Path | Location |
|------------|--------|------------------|----------|
| **Shared Memory** (Memory class) | ✅ Production | Extend with manifest header + section descriptors | `core/src/emulator/memory/memory.cpp` |
| **RingBuffer** | ✅ Production | Use for LOG_STREAM telemetry section | `core/src/common/ringbuffer.h` |
| **OpcodeProfiler** | ✅ Production | Direct mapping to OPCODE_TRACE section | `core/src/emulator/cpu/opcode_profiler.h` |
| **CallTraceBuffer** | ✅ Production | Direct mapping to CALL_TRACE section | `core/src/emulator/memory/calltrace.h` |
| **MemoryAccessTracker** | ✅ Production | Direct mapping to DEBUG_COUNTERS section | `core/src/emulator/memory/memoryaccesstracker.h` |
| **Z80Registers** (packed, 128 bytes) | ✅ Available | Expose as CHIP_STATE_Z80 section | `core/src/emulator/cpu/z80.h` |
| **AY-3-8910** (16 registers + generators) | ✅ Available | Expose as CHIP_STATE_AY section | `core/src/emulator/sound/chips/soundchip_ay8910.h` |

### Existing Shared Memory Pattern

**Implementation** (`Memory::AllocateAndExportMemoryToMmap()`):
- **Windows**: `CreateFileMappingA()` with name `Local\zxspectrum_memory-{instanceId}`
- **POSIX**: `shm_open()` + `mmap()` with name `/zxspectrum_memory-{instanceId}`
- **Size**: 5MB (PAGE_SIZE × MAX_PAGES)
- **Fallback**: Heap allocation on failure
- **Cleanup**: Graceful via `UnmapMemory()`

This pattern will be extended with manifest metadata.

### Existing Data Structures

#### Z80Registers (128 bytes, packed)
```cpp
struct Z80Registers {
    union { uint32_t tt; struct { uint8_t t_l; unsigned t : 24; }; };
    union { uint16_t pc; struct { uint8_t pcl, pch; }; };
    union { uint16_t sp; struct { uint8_t spl, sph; }; };
    union { uint16_t ir_; struct { uint8_t r_low, i; }; };
    union { uint32_t int_flags; struct {
        uint8_t r_hi, iff1, iff2, halted;
    }; };
    union { uint16_t bc; struct { uint8_t c, b; }; };
    union { uint16_t de; struct { uint8_t e, d; }; };
    union { uint16_t hl; struct { uint8_t l, h; }; };
    union { uint16_t af; struct { uint8_t f, a; }; };
    union { uint16_t ix; struct { uint8_t xl, xh; }; };
    union { uint16_t iy; struct { uint8_t yl, yh; }; };
    struct { uint16_t bc, de, hl, af; } alt;

    // Undocumented registers
    union { uint16_t memptr; struct { uint8_t meml, memh; }; };
    uint8_t q;  // Q register for CCF/SCF behavior
};
```

#### AY-3-8910 State
- 16 registers (R0-R15)
- 3 tone generators (period, volume, envelope/noise/tone/noise flags)
- 1 noise generator (period, LFSR state)
- 1 envelope generator (period, shape, phase)

---

## Implementation Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Shared Memory Region                           │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Manifest Header (64 bytes)                              │  │
│  │  ├─ magic: 0x554E524C ("UNRL")                   │  │
│  │  ├─ version: uint16_t                                    │  │
│  │  ├─ section_count: uint16_t                              │  │
│  │  ├─ total_size: uint64_t                                 │  │
│  │  ├─ frame_epoch: atomic<uint64_t>  ← frame version      │  │
│  │  └─ reserved: 40 bytes                                   │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Section Descriptors (32 bytes × N)                      │  │
│  │  ├─ type: enum SectionType                              │  │
│  │  ├─ offset: uint32_t                                    │  │
│  │  ├─ length: uint32_t                                    │  │
│  │  ├─ epoch: atomic<uint64_t>  ← per-section version       │  │
│  │  └─ flags: uint16_t (ENABLED, BIDIRECTIONAL)            │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Section Data Blocks                                       │  │
│  │  ├─ [0] Memory Pages (~5MB, existing)              │  │
│  │  ├─ [1] Chip States (Z80, AY, FDC)               │  │
│  │  ├─ [2] Debug Counters                                 │  │
│  │  ├─ [3] Log Ring Buffer (4MB)                       │  │
│  │  ├─ [4] Opcode Trace                                   │  │
│  │  ├─ [5] Call Trace                                     │  │
│  │  └─ [6] Input State (keyboard/joystick)             │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

### Core Components

#### MonitoringManager Class

```cpp
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

class EmulatorContext;
class Memory;

/// Section type enumeration
enum class SectionType : uint8_t {
    MEMORY_PAGES = 0,
    CHIP_STATE_Z80 = 1,
    CHIP_STATE_AY = 2,
    CHIP_STATE_FDC = 3,
    DEBUG_COUNTERS = 4,
    LOG_STREAM = 5,
    OPCODE_TRACE = 6,
    CALL_TRACE = 7,
    INPUT_STATE = 8
};

/// Section flags
enum class SectionFlags : uint16_t {
    ENABLED = 0x0001,
    BIDIRECTIONAL = 0x0002,
    SEPARATE_REGION = 0x0004  // For resizable sections
};

/// Manifest header
struct alignas(64) ManifestHeader {
    char magic[4];                // "UNRL"
    uint16_t version;              // 1
    uint16_t section_count;
    uint64_t total_size;
    std::atomic<uint64_t> global_epoch{0};
    std::atomic<uint64_t> frame_epoch{0};
    char reserved[48];
};

/// Section descriptor
struct alignas(32) SectionDescriptor {
    SectionType type;
    uint32_t offset;
    uint32_t length;
    std::atomic<uint64_t> epoch{0};
    SectionFlags flags;
    char name[16];              // Human-readable name
    char reserved[6];
};

class MonitoringManager {
public:
    explicit MonitoringManager(EmulatorContext* context);
    ~MonitoringManager();

    /// Initialize monitoring subsystem
    bool Initialize();

    /// Enable/disable a section
    bool EnableSection(SectionType type, bool enable);

    /// Check if section is enabled
    bool IsSectionEnabled(SectionType type) const;

    /// Get section pointer (for read-only access)
    void* GetSection(SectionType type);

    /// Signal frame complete (called from SoundManager)
    void SignalFrameComplete();

    /// Get manifest header (observer uses this)
    const ManifestHeader* GetManifestHeader() const;

private:
    EmulatorContext* _context;
    Memory* _memory;

    // Shared memory region (manifest + all sections)
    void* _shmBase;
    size_t _shmSize;
    std::string _shmName;

#ifdef _WIN32
    HANDLE _shmHandle;
#else
    int _shmFd;
#endif

    // Section tracking
    std::unordered_map<SectionType, SectionDescriptor*> _sections;

    // Frame sync primitives
#ifdef _WIN32
    HANDLE _frameEvent;
#elif __APPLE__
    sem_t* _frameSemaphore;
#else
    int _frameEventFd;
#endif

    /// Create shared memory region with manifest
    bool CreateManifest();

    /// Create individual sections
    bool CreateSection(SectionType type, uint32_t size, const char* name, SectionFlags flags);

    /// Initialize frame sync primitives
    bool InitializeFrameSync();

    /// Cleanup resources
    void Cleanup();
};
```

---

## Section Specifications

### Section 0: MEMORY_PAGES (Existing)

**Purpose**: Expose all emulated memory pages (RAM, ROM, CACHE, MISC) for direct access

**Source**: `Memory::_memory` pointer (existing)

**Size**: ~5MB (PAGE_SIZE × MAX_PAGES)

**Update**: Per-frame (already synced)

**Status**: ✅ Ready - no changes needed

**Descriptor**:
```cpp
{
    .type = SectionType::MEMORY_PAGES,
    .offset = 0,  // Start of shared memory region
    .length = sizeof(uint8_t) * PAGE_SIZE * MAX_PAGES,
    .flags = SectionFlags::ENABLED,
    .name = "Memory Pages"
}
```

### Section 1: CHIP_STATE_Z80 (New)

**Purpose**: Expose Z80 CPU registers and flags for real-time debugging

**Size**: 128 bytes

**Update**: Per-instruction (3.5 MHz) - **THROTTLING REQUIRED**

**Throttling Strategy**: Update every Nth instruction or frame-sync

**Structure**:
```cpp
#pragma pack(push, 1)
struct alignas(64) Z80StateSection {
    std::atomic<uint64_t> epoch{0};

    // From Z80Registers - packed 128 bytes
    uint32_t tt;
    uint16_t pc, sp, ir_;
    uint32_t int_flags;  // iff1, iff2, halted
    uint16_t bc, de, hl, af;
    uint16_t ix, iy;

    // Shadow registers
    struct {
        uint16_t bc, de, hl, af;
    } alt;

    // Undocumented registers
    uint16_t memptr;
    uint8_t q;

    // Additional state
    uint8_t im;           // Interrupt mode
    uint32_t reserved0;

    // Update flags (for hot-path optimization)
    std::atomic<bool> dirty{false};
};
#pragma pack(pop)
static_assert(sizeof(Z80StateSection) == 128, "Z80StateSection should be 128 bytes");
```

**Throttling Implementation**:
```cpp
class Z80 : public Z80State {
private:
    static constexpr uint32_t Z80_UPDATE_INTERVAL = 10000;  // Update every 10K instructions (~3ms at 3.5MHz)
    uint32_t _z80UpdateCounter = Z80_UPDATE_INTERVAL;

public:
    void Z80Step() {
        // ... existing emulation code ...

        #if ENABLE_MONITORING
        if (_monitoringEnabled && _z80StateSection) {
            if (--_z80UpdateCounter == 0) {
                UpdateZ80StateSection();
                _z80UpdateCounter = Z80_UPDATE_INTERVAL;
            }
        }
        #endif
    }

private:
    void UpdateZ80StateSection() {
        memcpy(&_z80StateSection->tt, &tt, sizeof(Z80Registers));

        if (_z80StateSection->dirty.load()) {
            _z80StateSection->dirty.store(false, std::memory_order_relaxed);
            _z80StateSection->epoch.store(
                ++globalEpoch,
                std::memory_order_release
            );
        }
    }
};
```

### Section 2: CHIP_STATE_AY (New)

**Purpose**: Expose AY-3-8910 state for audio debugging

**Size**: ~64 bytes

**Update**: Per-frame (50 Hz) - frame-sync

**Throttling**: Not needed - low frequency

**Structure**:
```cpp
#pragma pack(push, 1)
struct alignas(64) AYStateSection {
    std::atomic<uint64_t> epoch{0};

    // AY registers
    uint8_t registers[16];  // R0-R15
    uint8_t current_register;

    // Tone generator states (3 channels)
    struct {
        uint16_t period;
        uint8_t volume;
        uint8_t envelope_enabled;
        uint8_t tone_enabled;
        uint8_t noise_enabled;
        uint8_t reserved[2];
    } tone[3];

    // Noise generator state
    uint8_t noise_period;
    uint32_t noise_lfsr;

    // Envelope generator state
    uint16_t envelope_period;
    uint8_t envelope_shape;
    uint8_t envelope_phase;
    uint8_t reserved1[5];

    std::atomic<bool> dirty{false};
};
#pragma pack(pop)
```

### Section 3: DEBUG_COUNTERS (Existing)

**Purpose**: Expose memory access counters (R/W/X per address) for heatmap visualization

**Source**: `MemoryAccessTracker` counters

**Size**: 128KB
- Z80 address space: 64KB × 3 counters × 2 bytes = 384KB
- Physical page counters: variable
- **Optimization**: Export only active pages (sparse representation)

**Update**: Per-instruction (via existing tracking hooks)

**Status**: ✅ Ready - expose via shared memory

**Export Structure**:
```cpp
#pragma pack(push, 1)
struct alignas(64) DebugCountersSection {
    std::atomic<uint64_t> epoch{0};

    // Z80 address space counters (64KB addresses)
    // Format: [read_count][write_count][exec_count] for each address
    uint16_t z80_counters[65536];  // Compact 16-bit counters
    uint32_t total_reads;
    uint32_t total_writes;
    uint32_t total_executes;
    uint32_t reserved[5];
};
#pragma pack(pop)
```

### Section 4: LOG_STREAM (New)

**Purpose**: Modular, hierarchical logging with real-time streaming to external observers

**Size**: 4MB

**Update**: On-log (append-only)

**Reuse**: Existing RingBuffer pattern (SPSC variant for IPC)

**Structure**:
```cpp
#pragma pack(push, 1)
struct alignas(64) LogStreamSection {
    std::atomic<uint64_t> write_epoch{0};

    // Log entry header
    struct LogEntry {
        uint32_t category_id;  // Hash of category path (e.g., "fdc.read")
        uint32_t level;       // Level enum (Trace=0, Debug=1, Info=2, Warn=3, Error=4)
        uint64_t timestamp;    // High-resolution timestamp
        uint16_t length;       // Followed by: char message[length]
        uint16_t reserved;
    };

    static constexpr size_t CAPACITY = 4 * 1024 * 1024 / sizeof(LogEntry);  // 4MB
    static constexpr size_t MASK = CAPACITY - 1;

    // SPSC ring buffer
    std::atomic<uint64_t> write_pos{0};
    std::atomic<uint64_t> read_pos{0};
    LogEntry entries[CAPACITY];

    // Overflow tracking
    std::atomic<bool> overflow{false};
    std::atomic<uint64_t> total_dropped{0};
    std::atomic<uint64_t> total_produced{0};
};
#pragma pack(pop)
```

**Integration with Existing Logger**:
```cpp
// Extend existing logging macros to use shared memory
#define EMU_LOG_MONITOR(category, level, fmt, ...) \
    do { \
        if (category.is_enabled(level) && monitoringManager) { \
            LogStreamSection* logSection = monitoringManager->GetSection<LogStreamSection>(); \
            if (logSection) { \
                LogEntry entry; \
                entry.category_id = category.id(); \
                entry.level = static_cast<uint32_t>(level); \
                entry.timestamp = GetHighResTimestamp(); \
                char buffer[1024]; \
                entry.length = snprintf(buffer, sizeof(buffer), fmt, ##__VA_ARGS__); \
                logSection->PushEntry(entry, buffer); \
            } \
        } \
    } while(0)
```

### Section 5: INPUT_STATE (New)

**Purpose**: Bi-directional input state (keyboard, joystick) for external control

**Size**: ~16 bytes

**Update**: On-change (bidirectional - emulator reads, observer writes)

**Flags**: BIDIRECTIONAL

**Structure**:
```cpp
#pragma pack(push, 1)
struct alignas(64) InputStateSection {
    std::atomic<uint64_t> epoch{0};

    // Keyboard state (8 rows × 5 cols = 40 bits = 5 bytes)
    struct {
        uint8_t row[8];  // Each byte = 5 key cols (use lower 5 bits)
        uint8_t reserved[3];
    } keyboard;

    // Joystick state (up to 2 joysticks)
    struct {
        uint8_t up : 1;
        uint8_t down : 1;
        uint8_t left : 1;
        uint8_t right : 1;
        uint8_t fire : 1;
        uint8_t reserved : 3;
    } joystick[2];

    uint8_t reserved2[4];

    // Timestamp of last input
    uint64_t last_update;
};
#pragma pack(pop)
```

**Usage Pattern**:
```cpp
// Emulator reads input (consumer)
void Z80::in(uint16_t port) {
    // ... existing port decoding ...

    #if ENABLE_MONITORING
    if (_monitoringEnabled && _inputStateSection) {
        uint64_t epoch = _inputStateSection->epoch.load();
        // Read keyboard/joystick state from shared memory
        // Apply to internal state
    }
    #endif
}

// Observer writes input (producer)
// External tool writes to shared memory:
inputStateSection->keyboard.row[3] |= (1 << 4);  // Key down
inputStateSection->epoch.fetch_add(1);
```

---

## Performance Optimization Strategies

### 1. Zero-Cost When Disabled

**Compile-time feature gates** eliminate all monitoring overhead when disabled:

```cpp
// In build configuration (CMakeLists.txt)
option(ENABLE_MONITORING "Enable real-time monitoring subsystem" OFF)

// Runtime check (fast path)
void Z80::Z80Step() {
    // ... existing emulation code ...

    #if ENABLE_MONITORING
    if (monitoringEnabled && chipStateSection) {
        if (--updateCounter == 0) {
            updateChipStateSection();
            updateCounter = UPDATE_INTERVAL;
        }
    }
    #endif
}
```

### 2. Dirty Flag Optimization

Only bump epoch when section actually changed:

```cpp
void updateChipStateSection() {
    memcpy(&chipStateSection->registers, _registers, sizeof(_registers));

    if (chipStateSection->dirty.load()) {
        chipStateSection->dirty.store(false, std::memory_order_relaxed);
        chipStateSection->epoch.store(++globalEpoch, std::memory_order_release);
    }
}
```

**Benefits**:
- Observer avoids unnecessary cache invalidations
- Reduced atomic operations on hot paths
- Better cache locality

### 3. Coherency Protocol (Epoch-Based)

**Producer (Emulator)**:
```cpp
constexpr uint64_t UPDATING = 0xFFFFFFFFFFFFFFFFULL;

void updateSection(Section* section, const void* data, size_t size) {
    section->epoch.store(UPDATING, std::memory_order_release);
    memcpy(section->data, data, size);
    section->epoch.store(globalEpoch++, std::memory_order_release);
}
```

**Consumer (Observer) - retry if torn:
```cpp
bool readSection(Section* section, void* out, size_t size) {
    uint64_t before = section->epoch.load(std::memory_order_acquire);
    if (before == UPDATING) return false;

    memcpy(out, section->data, size);
    std::atomic_thread_fence(std::memory_order_acquire);

    return (before == section->epoch.load(std::memory_order_acquire));
}
```

**Retry Pattern**:
```cpp
void readWithRetry(Section* section, void* out, size_t size, int maxRetries = 100) {
    for (int i = 0; i < maxRetries; ++i) {
        if (readSection(section, out, size)) {
            return;  // Success
        }
        std::this_thread::yield();
    }
    // Handle timeout or give up
}
```

### 4. Cache-Line Alignment

All structures aligned to cache lines (64 bytes) to prevent false sharing:

```cpp
struct alignas(64) Z80StateSection { ... };
struct alignas(64) AYStateSection { ... };
struct alignas(64) LogStreamSection { ... };
```

**Benefits**:
- Prevents false sharing between producer/consumer
- Reduces cache coherence traffic
- Better multi-core scalability

---

## Integration Points

### 1. Memory Class Extension

```cpp
class Memory {
private:
    // New: MonitoringManager reference
    MonitoringManager* _monitoringManager = nullptr;

public:
    void AllocateAndExportMemoryToMmap() {
        // ... existing shared memory creation code ...

        // NEW: Create monitoring manifest
        if (_feature_sharedmemory_enabled && _monitoringManager) {
            _monitoringManager->CreateManifest(
                _memory, _memorySize, instanceId
            );
        }
    }

    // Set monitoring manager reference
    void SetMonitoringManager(MonitoringManager* manager) {
        _monitoringManager = manager;
    }

    // Update chip state sections (frame-sync)
    void UpdateMonitoringSections() {
        if (_monitoringManager) {
            _monitoringManager->UpdateChipStates();
        }
    }
};
```

### 2. Z80 Hook Points

```cpp
class Z80 : public Z80State {
private:
    // Monitoring integration
    static constexpr uint32_t Z80_UPDATE_INTERVAL = 10000;
    uint32_t _z80UpdateCounter = Z80_UPDATE_INTERVAL;
    bool _monitoringEnabled = false;
    Z80StateSection* _z80StateSection = nullptr;

public:
    void SetMonitoringEnabled(bool enabled) {
        _monitoringEnabled = enabled;
    }

    void SetZ80StateSection(Z80StateSection* section) {
        _z80StateSection = section;
    }

    void Z80Step() {
        // ... existing emulation code ...

        // NEW: Update Z80 state section (throttled)
        #if ENABLE_MONITORING
        if (_monitoringEnabled && _z80StateSection) {
            if (--_z80UpdateCounter == 0) {
                UpdateZ80StateSection();
                _z80UpdateCounter = Z80_UPDATE_INTERVAL;
            }
        }
        #endif
    }

private:
    void UpdateZ80StateSection() {
        memcpy(&_z80StateSection->tt, &tt, sizeof(Z80Registers));
        _z80StateSection->dirty.store(false);
        _z80StateSection->epoch.store(
            GetGlobalEpoch(),
            std::memory_order_release
        );
    }
};
```

### 3. SoundManager Hook Points

```cpp
class SoundManager {
private:
    MonitoringManager* _monitoringManager = nullptr;
    bool _monitoringEnabled = false;
    AYStateSection* _ayStateSection = nullptr;

public:
    void SetMonitoringManager(MonitoringManager* manager) {
        _monitoringManager = manager;
    }

    void SetAYStateSection(AYStateSection* section) {
        _ayStateSection = section;
    }

    void handleFrameEnd() {
        // ... existing audio processing code ...

        // NEW: Update AY state section (frame-sync)
        #if ENABLE_MONITORING
        if (_monitoringEnabled && _ayStateSection) {
            memcpy(&_ayStateSection->registers,
                   _ay8910->getRegisters(), 16);
            _ayStateSection->envelope_period = _ay8910->getEnvelopeGenerator()->period();
            _ayStateSection->envelope_shape = _ay8910->getEnvelopeGenerator()->shape();
            _ayStateSection->dirty.store(false);
            _ayStateSection->epoch.store(
                GetGlobalEpoch(),
                std::memory_order_release
            );
        }

        // NEW: Signal frame complete
        if (_monitoringEnabled) {
            _monitoringManager->SignalFrameComplete();
        }
        #endif
    }
};
```

### 4. EmulatorContext Extension

```cpp
class EmulatorContext {
    // ... existing fields ...

public:
    // NEW: Monitoring manager reference
    MonitoringManager* pMonitoringManager = nullptr;
};
```

### 5. Emulator Class Integration

```cpp
class Emulator {
public:
    // NEW: Initialize monitoring subsystem
    bool InitializeMonitoring() {
        _context.pMonitoringManager = new MonitoringManager(&_context);

        if (!_context.pMonitoringManager->Initialize()) {
            delete _context.pMonitoringManager;
            _context.pMonitoringManager = nullptr;
            return false;
        }

        // Set up section references
        _context.pCore->GetZ80()->SetMonitoringEnabled(true);
        _context.pCore->GetZ80()->SetZ80StateSection(
            _context.pMonitoringManager->GetSection<Z80StateSection>()
        );

        _context.pSoundManager->SetMonitoringManager(_context.pMonitoringManager);
        _context.pSoundManager->SetAYStateSection(
            _context.pMonitoringManager->GetSection<AYStateSection>()
        );

        _context.pMemory->SetMonitoringManager(_context.pMonitoringManager);

        return true;
    }

    ~Emulator() {
        delete _context.pMonitoringManager;
        _context.pMonitoringManager = nullptr;
    }
};
```

---

## Control Channel Design

### Hybrid Architecture

**Rationale**: Shared memory for data (zero-copy, high throughput) + platform-specific control channel (blocking reads, simpler observer implementation).

```
┌─────────────────────────────────────────────────────────────┐
│  DATA PLANE: Shared Memory                                 │
│  - Manifest + all sections                                  │
│  - Zero-copy reads                                         │
│  - Epoch-based coherency                                  │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  CONTROL PLANE: Platform-Specific                        │
│  Linux: Unix Domain Socket (/tmp/unreal_ctl_{id})        │
│  macOS: Unix Domain Socket (/tmp/unreal_ctl_{id})        │
│  Windows: Named Pipe (\\.\pipe\unreal_ctl_{id})     │
│                                                          │
│  Messages:                                                │
│  - EnableSection(SectionType, bool)                     │
│  - DisableSection(SectionType)                              │
│  - SetLogLevel(uint32_t category_id, Level)                │
│  - EnumerateSections()                                    │
│  - AckResize(SectionType, uint64_t new_size)             │
└─────────────────────────────────────────────────────────────┘
```

### Message Format

```cpp
#pragma pack(push, 1)
struct ControlMessage {
    enum class Type : uint8_t {
        EnableSection = 0,
        DisableSection = 1,
        SetLogLevel = 2,
        EnumerateSections = 3,
        AckResize = 4,
        Ping = 5
    };

    Type type;
    SectionType section_type;
    uint8_t level;           // For SetLogLevel
    uint32_t reserved[2];
};
#pragma pack(pop)
```

### Platform-Specific Implementation

#### Linux (Unix Domain Socket)
```cpp
class ControlChannelLinux {
private:
    int _socketFd = -1;
    std::string _socketPath;

public:
    bool Create(const std::string& instanceId) {
        _socketPath = "/tmp/unreal_ctl_" + instanceId;
        unlink(_socketPath.c_str());

        _socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (_socketFd < 0) return false;

        struct sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, _socketPath.c_str(), sizeof(addr.sun_path));

        return bind(_socketFd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    }

    bool SendMessage(const ControlMessage& msg) {
        return send(_socketFd, &msg, sizeof(msg), 0) == sizeof(msg);
    }

    bool ReceiveMessage(ControlMessage& msg) {
        return recv(_socketFd, &msg, sizeof(msg), 0) > 0;
    }
};
```

#### Windows (Named Pipe)
```cpp
class ControlChannelWindows {
private:
    HANDLE _pipeHandle = INVALID_HANDLE_VALUE;
    std::string _pipeName;

public:
    bool Create(const std::string& instanceId) {
        _pipeName = "\\\\.\\pipe\\unreal_ctl_" + instanceId;

        _pipeHandle = CreateNamedPipeA(
            _pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,  // Max instances
            4096,  // Out buffer
            4096,  // In buffer
            0,    // Default timeout
            NULL
        );

        return _pipeHandle != INVALID_HANDLE_VALUE;
    }

    bool SendMessage(const ControlMessage& msg) {
        DWORD written = 0;
        return WriteFile(_pipeHandle, &msg, sizeof(msg),
                        &written, NULL) && written == sizeof(msg);
    }

    bool ReceiveMessage(ControlMessage& msg) {
        DWORD read = 0;
        return ReadFile(_pipeHandle, &msg, sizeof(msg),
                       &read, NULL) && read == sizeof(msg);
    }
};
```

---

## Frame Sync Notification

### Cross-Platform Implementation

**Purpose**: Signal observers when a new frame is complete (50 Hz), enabling near-zero-jitter updates.

#### Linux (eventfd)
```cpp
class FrameSyncLinux {
private:
    int _eventFd = -1;

public:
    bool Initialize() {
        _eventFd = eventfd(0, EFD_NONBLOCK);
        return _eventFd >= 0;
    }

    void Signal() {
        uint64_t val = 1;
        write(_eventFd, &val, sizeof(val));
    }

    void Wait() {
        struct pollfd pfd = {.fd = _eventFd, .events = POLLIN};
        poll(&pfd, 1, -1);

        uint64_t val;
        read(_eventFd, &val, sizeof(val));
    }
};
```

#### macOS (POSIX Semaphore)
```cpp
class FrameSyncMacOS {
private:
    sem_t* _semaphore = nullptr;
    std::string _semName;

public:
    bool Initialize(const std::string& instanceId) {
        _semName = "/unreal_frame_" + instanceId;
        _semaphore = sem_open(_semName.c_str(), O_CREAT, 0644, 0);
        return _semaphore != SEM_FAILED;
    }

    void Signal() {
        sem_post(_semaphore);
    }

    void Wait() {
        sem_wait(_semaphore);
    }
};
```

#### Windows (Named Event)
```cpp
class FrameSyncWindows {
private:
    HANDLE _eventHandle = INVALID_HANDLE_VALUE;
    std::string _eventName;

public:
    bool Initialize(const std::string& instanceId) {
        _eventName = "unreal_frame_" + instanceId;

        _eventHandle = CreateEventA(
            NULL, FALSE, FALSE,
            _eventName.c_str()
        );

        return _eventHandle != NULL;
    }

    void Signal() {
        SetEvent(_eventHandle);
    }

    void Wait() {
        WaitForSingleObject(_eventHandle, INFINITE);
    }
};
```

### Memory Ordering Guarantee

All three mechanisms provide **full memory barrier semantics**:
- Writes before `SetEvent()` / `sem_post()` / `write(eventfd)` are visible after `WaitForSingleObject()` / `sem_wait()` / `poll()` returns
- Ensures observer sees consistent frame data

### Usage Pattern

**Emulator (producer)**:
```cpp
void SoundManager::handleFrameEnd() {
    // ... complete frame rendering ...

    // Signal waiting observers
    #if ENABLE_MONITORING
    if (_monitoringEnabled) {
        _monitoringManager->SignalFrameComplete();
    }
    #endif
}
```

**Observer (consumer)** - two options:

**Option A: Blocking (dedicated observer thread)**:
```cpp
void ObserverThread() {
    while (running) {
        frameSync.Wait();  // ~5-20µs wakeup latency

        // Read frame data
        readWithRetry(&manifest->frame_epoch, frameData, frameSize);

        // Process frame...
    }
}
```

**Option B: Polling (simpler, no IPC setup)**:
```cpp
void ObserverPoll() {
    uint64_t lastEpoch = 0;

    while (running) {
        uint64_t currentEpoch = manifest->frame_epoch.load(std::memory_order_acquire);

        if (currentEpoch != lastEpoch) {
            lastEpoch = currentEpoch;

            // Read frame data
            readWithRetry(&manifest->frame_epoch, frameData, frameSize);

            // Process frame...
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}
```

---

## Addressing Open Questions

### 1. Performance Impact of IPC

**Answer**: Zero-cost when disabled via compile-time flags. When enabled:

| Operation | Overhead | Mitigation |
|-----------|----------|-------------|
| Z80 state update (throttled) | ~100ns every 3ms | Configurable interval, dirty flag |
| AY state update (frame-sync) | ~50ns every 20ms | Low frequency, no throttling needed |
| Log write (append) | ~200ns per log | SPSC lock-free, drop on overflow |
| Memory access counter (existing) | ~10ns per access | Already zero-cost when tracking disabled |

**Benchmark Strategy**:
1. Measure per-instruction time with all sections disabled
2. Measure with CHIP_STATE_Z80 enabled (various throttling intervals)
3. Measure with LOG_STREAM enabled (various log rates)
4. Compare to determine optimal default settings

### 2. Coherency During Section Reconfiguration

**Answer**: Epoch-based protocol with `UPDATING` sentinel prevents torn reads.

**Scenario**: Observer reads while emulator reconfigures a section
```cpp
// Emulator: Reconfigure section
void ReconfigureSection(Section* section, uint32_t newSize) {
    // 1. Mark as updating (observer will retry)
    section->epoch.store(UPDATING, std::memory_order_release);

    // 2. Reconfigure (may take time)
    ReallocateSection(section, newSize);

    // 3. Bump epoch (observer sees consistent state)
    section->epoch.store(++globalEpoch, std::memory_order_release);
}

// Observer: Retry on torn read
void ReadWithRetry(Section* section) {
    while (true) {
        uint64_t before = section->epoch.load(std::memory_order_acquire);

        if (before == UPDATING) {
            std::this_thread::yield();
            continue;  // Retry
        }

        memcpy(buffer, section->data, section->length);
        std::atomic_thread_fence(std::memory_order_acquire);

        uint64_t after = section->epoch.load(std::memory_order_acquire);
        if (before == after) break;  // Consistent read
    }
}
```

### 3. Handling Large Data Sections (Video, Audio)

**Answer**: **Separate shared memory regions** with versioning protocol.

**Strategy**:
1. Allocate new region with updated size
2. Update section descriptor with new offset/length
3. Bump epoch in descriptor
4. Old region remains valid until observer acknowledges

**Implementation**:
```cpp
void ResizeVideoSection(uint32_t newWidth, uint32_t newHeight) {
    size_t newSize = newWidth * newHeight * 4;  // 32bpp

    // 1. Allocate new region
    void* newRegion = CreateSeparateRegion(newSize, "framebuffer_v2");

    // 2. Copy existing data (if applicable)
    if (framebuffer) {
        CopyData(newRegion, framebuffer, min(newSize, currentSize));
    }

    // 3. Update descriptor with version bump
    videoDescriptor->offset = GetOffset(newRegion);
    videoDescriptor->length = newSize;
    videoDescriptor->epoch.store(++globalEpoch, std::memory_order_release);

    // 4. Old region valid until observer switches
    oldRegion = framebuffer;
    framebuffer = newRegion;

    // 5. Cleanup old region after grace period (via control channel)
    ScheduleCleanup(oldRegion);
}
```

**Observer Pattern**:
```cpp
void ReadFrameBuffer() {
    uint64_t epoch = videoDescriptor->epoch.load();

    // Read descriptor to get current region info
    void* buffer = GetRegionBuffer(videoDescriptor->offset, videoDescriptor->length);

    // Process frame...
    ProcessFrame(buffer, videoDescriptor->length);
}
```

### 4. Handling Binary Data (Audio Buffer)

**Answer**: Append sections follow frame buffer with offset pointer, or separate ring buffer region.

**Option A: Inline with offset** (simpler, fixed size):
```cpp
struct AudioSection {
    std::atomic<uint64_t> epoch{0};

    uint32_t write_index;     // Current write position (0-1023)
    uint32_t read_index;      // Observer's read position
    uint32_t sample_rate;     // e.g., 44100 Hz
    uint16_t channels;        // e.g., 2 (stereo)

    // Fixed-size buffer (e.g., 100ms @ 44.1kHz stereo = 8820 samples)
    int16_t samples[8820 * 2];  // 16-bit PCM
};
```

**Option B: Separate ring buffer** (more flexible):
```cpp
struct AudioRingBuffer {
    std::atomic<uint64_t> write_pos{0};
    std::atomic<uint64_t> read_pos{0};

    static constexpr size_t CAPACITY = 65536;  // ~1.5 seconds @ 44.1kHz
    int16_t samples[CAPACITY];

    std::atomic<bool> overflow{false};
};
```

### 5. Handling Control Data (Keyboard, Joystick - Bidirectional)

**Answer**: **Bidirectional INPUT_STATE section** with BIDIRECTIONAL flag. Emulator consumes input, observer produces control commands.

**Architecture**:
```
Emulator Thread          Observer Thread
     │                       │
     ▼                       ▼
┌─────────────────────────────────────────────┐
│  INPUT_STATE (shared memory)        │
│  - Keyboard state (40 bits)          │
│  - Joystick state (10 bits)          │
│  - Atomic epoch counter               │
└─────────────────────────────────────────────┘
     ▲                       ▲
     │ (reads input)          │ (writes input)
```

**Synchronization**: Atomic epoch counter provides happens-before ordering.

**Emulator Pattern**:
```cpp
void Z80::in(uint16_t port) {
    // ... existing port decoding ...

    #if ENABLE_MONITORING
    if (_monitoringEnabled && _inputStateSection) {
        uint64_t epoch = _inputStateSection->epoch.load(std::memory_order_acquire);

        // Read keyboard/joystick state from shared memory
        ApplyInputState(&inputStateSection->keyboard, &inputStateSection->joystick);

        // No need to bump epoch - observer is producer
    }
    #endif
}
```

**Observer Pattern**:
```cpp
void SendKey(uint8_t row, uint8_t col, bool pressed) {
    // Update shared memory
    if (pressed) {
        inputStateSection->keyboard.row[row] |= (1 << col);
    } else {
        inputStateSection->keyboard.row[row] &= ~(1 << col);
    }

    // Bump epoch to signal change
    inputStateSection->epoch.fetch_add(1, std::memory_order_release);
}
```

---

## Potential Issues & Mitigations

### 1. Observer Crashes Leaving Stale Data

**Risk**: Observer process crashes with held references to shared memory regions, leaving emulator unable to reclaim resources.

**Mitigation**: Heartbeat detection + timeout-based cleanup.

```cpp
class MonitoringManager {
private:
    std::atomic<uint64_t> _lastObserverHeartbeat{0};
    static constexpr uint64_t OBSERVER_TIMEOUT_MS = 5000;  // 5 seconds

public:
    void OnObserverHeartbeat() {
        _lastObserverHeartbeat.store(
            GetCurrentTimestamp(),
            std::memory_order_release
        );
    }

    void CheckObserverHealth() {
        uint64_t last = _lastObserverHeartbeat.load(std::memory_order_acquire);

        if (GetCurrentTimestamp() - last > OBSERVER_TIMEOUT_MS) {
            // Observer likely crashed - auto-disable high-overhead sections
            DisableHighOverheadSections();
        }
    }

private:
    void DisableHighOverheadSections() {
        // Keep CHIP_STATE_Z80 disabled (high-frequency)
        // Keep INPUT_STATE enabled (low-frequency, safe)
        EnableSection(SectionType::CHIP_STATE_Z80, false);
    }
};
```

**Observer Heartbeat**:
```cpp
void ObserverThread() {
    while (running) {
        // ... process monitoring data ...

        // Send heartbeat every second
        if (ShouldSendHeartbeat()) {
            SendHeartbeat();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

### 2. Memory Ordering Issues

**Risk**: Incorrect memory ordering causes observer to see inconsistent state.

**Mitigation**: Use `std::atomic_thread_fence` + platform-specific barriers. Verify with stress tests.

**Stress Test Pattern**:
```cpp
// Producer: Rapid updates
void StressTestProducer() {
    for (int i = 0; i < 1000000; ++i) {
        chipStateSection->epoch.store(UPDATING, std::memory_order_release);
        memcpy(chipStateSection->data, newData, 128);
        chipStateSection->epoch.store(++counter, std::memory_order_release);
    }
}

// Consumer: Concurrent reads
void StressTestConsumer() {
    for (int i = 0; i < 1000000; ++i) {
        uint64_t before = chipStateSection->epoch.load(std::memory_order_acquire);

        if (before == UPDATING) {
            std::this_thread::yield();
            --i;  // Retry
            continue;
        }

        memcpy(buffer, chipStateSection->data, 128);
        std::atomic_thread_fence(std::memory_order_acquire);

        uint64_t after = chipStateSection->epoch.load(std::memory_order_acquire);

        if (before != after) {
            std::this_thread::yield();
            --i;  // Retry
            continue;
        }

        // Validate data integrity
        if (!ValidateData(buffer)) {
            std::cerr << "Coherency violation detected at iteration " << i << "\n";
        }
    }
}
```

### 3. Windows Named Pipe vs Unix Socket

**Risk**: Different semantics (blocking vs non-blocking, message boundaries).

**Mitigation**: Thin platform abstraction layer. Same message format, different transport.

**Abstraction Interface**:
```cpp
class IControlChannel {
public:
    virtual ~IControlChannel() = default;
    virtual bool Create(const std::string& instanceId) = 0;
    virtual bool SendMessage(const ControlMessage& msg) = 0;
    virtual bool ReceiveMessage(ControlMessage& msg) = 0;
    virtual void Close() = 0;
};

// Platform-specific implementations
class ControlChannelLinux : public IControlChannel { ... };
class ControlChannelWindows : public IControlChannel { ... };

// Factory
std::unique_ptr<IControlChannel> CreateControlChannel() {
#ifdef _WIN32
    return std::make_unique<ControlChannelWindows>();
#elif __APPLE__
    return std::make_unique<ControlChannelMacOS>();
#else
    return std::make_unique<ControlChannelLinux>();
#endif
}
```

### 4. macOS sem_open vs sem_unlink Cleanup

**Risk**: Orphaned semaphores if emulator crashes without cleanup.

**Mitigation**: Robust cleanup in destructor. Fallback to polling if semaphore creation fails.

```cpp
FrameSyncMacOS::~FrameSyncMacOS() {
    if (_semaphore && _semaphore != SEM_FAILED) {
        sem_close(_semaphore);

        // Best-effort unlink (may fail if already unlinked)
        sem_unlink(_semName.c_str());
        _semaphore = nullptr;
    }
}

bool FrameSyncMacOS::Initialize(const std::string& instanceId) {
    _semName = "/unreal_frame_" + instanceId;

    _semaphore = sem_open(_semName.c_str(), O_CREAT, 0644, 0);
    if (_semaphore == SEM_FAILED) {
        // Fallback to polling if semaphore creation fails
        LOGWARNING("Failed to create semaphore, falling back to polling mode");
        return false;
    }

    return true;
}
```

### 5. High-Frequency Z80 Updates Blocking Emulation

**Risk**: Updating Z80 state every instruction (3.5 MHz) would overwhelm emulator.

**Mitigation**: Throttling strategy. Configurable threshold (per-instruction or frame-sync).

**Configurable Strategy**:
```cpp
enum class Z80UpdateMode : uint8_t {
    Disabled = 0,          // Never update (zero cost)
    PerInstruction = 1,      // Every instruction (max overhead)
    Sampled = 2,           // Every N instructions
    FrameSync = 3            // Once per frame (min overhead)
};

class Z80 {
private:
    Z80UpdateMode _z80UpdateMode = Z80UpdateMode::Sampled;
    uint32_t _z80UpdateSampleInterval = 10000;  // Default: every 10K instructions

public:
    void SetZ80UpdateMode(Z80UpdateMode mode, uint32_t interval = 10000) {
        _z80UpdateMode = mode;
        _z80UpdateSampleInterval = interval;
    }

    void Z80Step() {
        // ... existing emulation code ...

        #if ENABLE_MONITORING
        if (_monitoringEnabled && _z80StateSection) {
            switch (_z80UpdateMode) {
                case Z80UpdateMode::Disabled:
                    break;

                case Z80UpdateMode::PerInstruction:
                    UpdateZ80StateSection();
                    break;

                case Z80UpdateMode::Sampled:
                    if (--_z80UpdateCounter == 0) {
                        UpdateZ80StateSection();
                        _z80UpdateCounter = _z80UpdateSampleInterval;
                    }
                    break;

                case Z80UpdateMode::FrameSync:
                    // Updated via OnFrameComplete() hook
                    break;
            }
        }
        #endif
    }
};
```

### 6. Section Reallocation Tearing

**Risk**: Observer reads from old region while emulator writes to new region, causing torn data.

**Mitigation**: Versioning protocol with graceful handoff.

**Protocol**:
```cpp
struct SectionDescriptor {
    // ...
    uint32_t version;  // Incremented on reallocation
};

// Emulator: Reallocation with version bump
void ReallocateSection(SectionDescriptor* desc, size_t newSize) {
    // 1. Allocate new region
    void* newRegion = Allocate(newSize);

    // 2. Copy existing data
    CopyData(newRegion, desc->data, desc->length);

    // 3. Update descriptor (new offset, new version)
    uint32_t newVersion = desc->version + 1;
    desc->offset = GetOffset(newRegion);
    desc->length = newSize;
    desc->version = newVersion;

    // 4. Bump epoch to signal change
    desc->epoch.store(++globalEpoch, std::memory_order_release);

    // 5. Old region valid until observer switches
    ScheduleCleanup(desc->data, desc->version);

    desc->data = newRegion;
}

// Observer: Detect version change
void ReadFrameBuffer() {
    SectionDescriptor* desc = GetDescriptor(VIDEO_SECTION);
    uint32_t version = desc->version;

    // Read from current region
    void* buffer = GetRegionBuffer(desc->offset, desc->length);
    ProcessFrame(buffer, desc->length, version);

    // Acknowledge version (allows cleanup of old)
    SendAckVersion(VIDEO_SECTION, version);
}

// Emulator: Cleanup after acknowledgment
void OnAckVersion(SectionType type, uint32_t version) {
    SectionDescriptor* desc = GetDescriptor(type);

    if (desc->version > version) {
        // Observer acknowledged old version - safe to free
        FreeRegion(desc->data);
    }
}
```

---

## Phased Implementation Roadmap

### Phase 1: Foundation (Week 1-2)

**Goal**: Establish manifest protocol and section infrastructure.

**Tasks**:
- [ ] Create `MonitoringManager` class skeleton
- [ ] Implement `ManifestHeader` and `SectionDescriptor` structures
- [ ] Implement shared memory creation with manifest
- [ ] Implement section registration system
- [ ] Extend `Memory::AllocateAndExportMemoryToMmap()` with manifest creation
- [ ] Add CLI command: `monitor status`
- [ ] Write unit tests for manifest parsing

**Validation**: External tool can discover and enumerate sections.

**Success Criteria**:
- [ ] `monitor status` outputs all sections with offsets/sizes
- [ ] Observer tool can parse manifest header
- [ ] Zero performance impact when monitoring disabled

### Phase 2: Append-Only Telemetry (Week 3)

**Goal**: Implement modular logging with real-time streaming.

**Tasks**:
- [ ] Implement `LogStreamSection` with SPSC ring buffer
- [ ] Integrate with existing logging system (hierarchical categories)
- [ ] Implement control channel (Unix domain socket / named pipe)
- [ ] Implement observer tool: `unreal-logger`
- [ ] Add CLI commands: `monitor log --enable fdc.read`, `monitor log --disable *`
- [ ] Write stress tests for ring buffer overflow

**Validation**: Real-time log viewing with category filtering.

**Success Criteria**:
- [ ] `unreal-logger` shows live log stream
- [ ] Category filtering works (`--filter fdc.read`)
- [ ] Runtime enable/disable works
- [ ] Ring buffer overflow detection and recovery

### Phase 3: Chip States (Week 4)

**Goal**: Expose Z80 and AY chip states for real-time debugging.

**Tasks**:
- [ ] Implement `Z80StateSection` structure
- [ ] Implement `AYStateSection` structure
- [ ] Hook into `Z80::Z80Step()` with throttling
- [ ] Hook into `SoundManager::handleFrameEnd()`
- [ ] Implement frame sync notification (eventfd/semaphore/named event)
- [ ] Implement observer tool: `unreal-state-viewer`
- [ ] Benchmark Z80 update throttling intervals
- [ ] Add CLI commands: `monitor chip --enable z80 --sampled 10000`

**Validation**: External debugger shows live register values.

**Success Criteria**:
- [ ] `unreal-state-viewer` displays Z80 registers
- [ ] Register values update in real-time (throttled or frame-sync)
- [ ] Frame sync notification works (<20µs latency)
- [ ] Configurable throttling works

### Phase 4: Debug Counters (Week 5)

**Goal**: Expose existing profiling data (counters, traces) via shared memory.

**Tasks**:
- [ ] Implement `DebugCountersSection` structure
- [ ] Expose `OpcodeProfiler` data via `OPCODE_TRACE` section
- [ ] Expose `CallTraceBuffer` data via `CALL_TRACE` section
- [ ] Integrate `MemoryAccessTracker` counters
- [ ] Implement observer tools: `unreal-heatmap`, `unreal-trace-viewer`
- [ ] Performance benchmarking (end-to-end observer latency)
- [ ] Add CLI commands: `monitor counters --export heatmap.yaml`

**Validation**: Heatmap and trace visualization in external tools.

**Success Criteria**:
- [ ] `unreal-heatmap` shows memory access heatmaps
- [ ] `unreal-trace-viewer` shows call traces
- [ ] Observer tools handle large datasets efficiently
- [ ] Export to YAML works

---

## Summary

The proposed architecture:

1. **Extends existing patterns** - reuses RingBuffer, shared memory infrastructure, existing profiler classes
2. **Zero-cost when disabled** - compile-time feature gates (`#if ENABLE_MONITORING`)
3. **Flexible and extensible** - modular sections, runtime enable/disable, hierarchical logging
4. **Maximum performance** - lock-free hot paths, dirty flag optimization, configurable throttling
5. **Cross-platform** - proven POSIX/Windows patterns from Memory class, platform abstraction for control channel
6. **Production-ready foundation** - RingBuffer, OpcodeProfiler, CallTraceBuffer, MemoryAccessTracker are battle-tested

The architecture documentation provides an excellent foundation. The implementation should follow the phased rollout plan to validate each pattern incrementally and ensure production quality.

---

## Appendix: Naming Convention

| Object | Pattern | Example |
|--------|----------|----------|
| Shared Memory (manifest) | `/unreal_monitor_{instanceId}` (POSIX) or `Local\unreal_monitor_{instanceId}` (Windows) |
| Frame Sync | `/unreal_frame_{instanceId}` (POSIX semaphore) or `unreal_frame_{instanceId}` (Windows event) |
| Control Channel | `/tmp/unreal_ctl_{instanceId}` (POSIX socket) or `\\.\pipe\unreal_ctl_{instanceId}` (Windows pipe) |

**Instance ID**: Truncated UUID (last 12 characters) for readability while maintaining uniqueness.
