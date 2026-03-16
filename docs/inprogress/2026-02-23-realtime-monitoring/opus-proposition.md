# Real-Time Monitoring Subsystem — Implementation Proposition

**Created:** 2026-02-23
**Status:** Proposition — Awaiting Review
**Author:** Claude Opus 4.6

---

## 1. Context & Motivation

The emulator already exports memory pages via shared memory (`shm_open`/`mmap` in `Memory::AllocateAndExportMemoryToMmap()`), but this fixed-layout approach cannot scale to expose chip states, structured logging, telemetry, and media buffers. We need a **self-describing shared memory protocol** that lets external observer tools discover and consume heterogeneous data sections in real-time.

### Existing Foundations (Reuse, Don't Reinvent)

| Component | Location | What We Reuse |
|-----------|----------|---------------|
| Shared memory IPC | `core/src/emulator/memory/memory.h:179` | Cross-platform `shm_open`/`CreateFileMapping` pattern |
| AnalyzerManager | `core/src/debugger/analyzers/analyzermanager.h` | Manager-class pattern, hot/warm/cold dispatch tiers, ownership tracking |
| RingBuffer\<T\> | `core/src/common/ringbuffer.h` | API design (push, getSince, statistics). **Not reusable for IPC** — uses `shared_mutex` + heap `vector` |
| FeatureManager | `core/src/base/featuremanager.h` | Feature-gate pattern (`registerFeature`, `isEnabled`, `features.ini` persistence) |
| ModuleLogger | `core/src/common/modulelogger.h` | Coexistence — new system is additive, ModuleLogger stays for cold-path diagnostics |
| MainLoop frame hooks | `core/src/emulator/mainloop.h:55-57` | `OnFrameStart()` / `OnFrameEnd()` — primary integration points |
| OpcodeProfiler | `core/src/emulator/cpu/opcode_profiler.h` | Atomic counter pattern, trace ring buffer — exposed as telemetry section |
| CallTraceBuffer | `core/src/emulator/memory/calltrace.h` | Hot/cold buffer segmentation — exposed as telemetry section |
| MemoryAccessTracker | `core/src/emulator/memory/memoryaccesstracker.h` | R/W/X counters — exposed as heatmap section |

### Design Goals

| Priority | Goal | Rationale |
|----------|------|-----------|
| **P0** | Zero-cost when disabled | A single `bool` check per frame when monitoring is OFF |
| **P0** | Lock-free hot paths | Logging and state updates must never block the Z80 emulation loop |
| **P1** | Runtime reconfiguration | Enable/disable log categories and sections without restart |
| **P1** | Cross-platform | POSIX `shm_open` / Windows `CreateFileMapping` |
| **P2** | Coherent reads | Observers see consistent snapshots via epoch-based protocol |
| **P2** | Self-describing | External tools discover sections via manifest — no hardcoded offsets |

---

## 2. Architecture Overview

```
Emulator Process                              Observer Process
┌──────────────────────────────────┐          ┌──────────────────────────┐
│  MonitoringManager               │          │  Observer Tool           │
│  ┌────────────────────────────┐  │          │  ┌────────────────────┐  │
│  │  ManifestHeader (128B)     │  │◄────────►│  │  attach(shm_name)  │  │
│  │  SectionDescriptor[N×64B]  │  │  shared  │  │  read manifest     │  │
│  ├────────────────────────────┤  │  memory  │  │  epoch-safe read   │  │
│  │  CHIP_STATE_Z80    (128B)  │  │          │  │  drain log ring    │  │
│  │  CHIP_STATE_AY      (32B)  │  │          │  │  send commands     │  │
│  │  CHIP_STATE_FDC     (64B)  │  │          │  └────────────────────┘  │
│  │  INPUT_STATE        (16B)  │  │          └──────────────────────────┘
│  │  LOG_STREAM     (64–256MB) │  │                     ▲
│  │  HEATMAP_Z80      (768KB)  │  │                     │
│  │  HEATMAP_PHYS      (60MB)  │  │          ┌──────────┴─────────────┐
│  │  CONTROL_RING       (4KB)  │  │          │  FrameNotifier         │
│  └────────────────────────────┘  │          │  sem_wait() / poll()   │
│  FrameNotifier (sem/eventfd)  ───┼──signal──│                        │
└──────────────────────────────────┘          └────────────────────────┘
```

### Data Flow Patterns

```
┌─────────────────────────────────────────────────────────────────────┐
│  CORE MANIFEST (always present when monitoring enabled)             │
│  ├─ ManifestHeader + SectionDescriptors                             │
│  ├─ Chip States (Z80, AY, FDC) — epoch-protected, frame-synced      │
│  └─ Input State (keyboard/joystick) — bidirectional                 │
├─────────────────────────────────────────────────────────────────────┤
│  TELEMETRY SECTIONS (modular, per-section enable/disable)           │
│  ├─ Log Stream — SPSC ring buffer, hierarchical categories          │
│  ├─ Memory Heatmaps — Z80 (768KB) + per-page (3.8KB) + phys (60MB)  │
│  ├─ Opcode Traces — snapshot OpcodeProfiler trace tail              │
│  └─ Call Traces — snapshot CallTraceBuffer latest                   │
├─────────────────────────────────────────────────────────────────────┤
│  MEDIA SECTIONS (opt-in, may use separate SHM regions)              │
│  ├─ Framebuffer — double-buffered RGBA, 196KB–573KB at 50Hz         │
│  └─ Audio Buffer — stereo PCM, ~3.5KB per frame                     │
├─────────────────────────────────────────────────────────────────────┤
│  CONTROL (observer → emulator)                                      │
│  └─ Control Ring — 4-slot command buffer, polled at 50Hz            │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. Component Design

### 3.1 MonitoringManager — Central Coordinator

**New files:** `core/src/emulator/monitoring/monitoringmanager.h` and `.cpp`

Follows the `AnalyzerManager` pattern: owned by `EmulatorContext`, constructed with `EmulatorContext*`, wired into the lifecycle via `MainLoop::OnFrameEnd()`.

```cpp
class MonitoringManager {
public:
    explicit MonitoringManager(EmulatorContext* context);
    ~MonitoringManager();

    // Lifecycle
    bool initialize();   // Allocate SHM, write manifest, register sections
    void shutdown();      // Unmap, cleanup

    // Frame hooks (called from MainLoop)
    void onFrameStart();  // Read bidirectional input, etc.
    void onFrameEnd();    // Snapshot states, flush, signal, poll control

    // Section management
    SectionHandle registerSection(const SectionDescriptor& desc);
    void enableSection(SectionHandle h);
    void disableSection(SectionHandle h);

    // Logging integration
    emu::log::LoggerRegistry& loggerRegistry();

    // Control channel polling
    void processControlCommands();

    // Feature gate
    bool isEnabled() const;

private:
    EmulatorContext* _context;
    SharedMemoryRegion _shmRegion;
    ManifestHeader* _manifestHeader = nullptr;
    std::vector<SectionRuntime> _sections;
    emu::log::LoggerRegistry _loggerRegistry;
    FrameNotifier _frameNotifier;
    bool _enabled = false;  // Cached from FeatureManager
};
```

**Feature gate:** New feature `"monitoring"` (alias `"mon"`, category `"debug"`) registered in FeatureManager. Defaults to OFF. When disabled, the only cost is a single `bool` check in `MainLoop::OnFrameEnd()`.

### 3.2 Data Manifest Protocol — Self-Describing Shared Memory

**New file:** `core/src/emulator/monitoring/manifest.h`

#### Binary Layout in Shared Memory

The manifest uses a **dynamic descriptor table** — no fixed cap on sections. The header stores `section_count` (current) and `max_sections` (capacity of the descriptor array). New sections can be registered at runtime; the SHM region is reallocated if the descriptor table needs to grow.

```
Offset 0:              ManifestHeader (128 bytes, 2 cache lines)
Offset 128:            SectionDescriptor[0..max_sections-1] (64 bytes each)
Offset 128 + 64*max:   Section data blocks (64-byte aligned)
```

#### ManifestHeader (128 bytes)

```cpp
struct alignas(64) ManifestHeader {
    // ── Cache line 0: identity + global state ──
    uint32_t magic;                      // 0x554E524C ("UNRL")
    uint16_t version;                    // Protocol version (start at 1)
    uint16_t header_size;                // sizeof(ManifestHeader) for forward compat
    uint64_t total_size;                 // Total shared memory region size
    std::atomic<uint64_t> global_epoch;  // Bumped on every frame end
    std::atomic<uint64_t> frame_counter; // Current emulator frame number
    uint32_t emulator_pid;               // PID of emulator process
    uint32_t flags;                      // RUNNING=1, PAUSED=2, STOPPED=4
    uint16_t section_count;              // Current number of active section descriptors
    uint16_t max_sections;               // Capacity of descriptor array (grows on realloc)
    uint32_t descriptor_size;            // sizeof(SectionDescriptor) for forward compat
    // ── Cache line 1: layout change tracking ──
    std::atomic<uint64_t> layout_epoch;  // Bumped when sections are added/removed/resized
                                         // Observer re-scans descriptors when this changes
    uint32_t data_region_offset;         // Byte offset where section data blocks begin
    uint8_t reserved[52];               // Future expansion
};
static_assert(sizeof(ManifestHeader) == 128);
```

**Forward compatibility:** `header_size` and `descriptor_size` allow newer versions to extend these structs without breaking older observers. An observer skips `header_size` bytes to reach descriptors, and strides by `descriptor_size` bytes between them.

**Dynamic growth:** When a new section is registered and `section_count == max_sections`, the emulator:
1. Allocates a new, larger SHM region (doubles `max_sections`)
2. Copies manifest + descriptors + inline section data
3. Bumps `layout_epoch`
4. Swaps the SHM name (or uses a generation suffix: `/unreal_monitor_{id}_g{N}`)
5. Marks old region as stale (sets `flags |= RELOCATED`, writes new SHM name)

Observers detect `RELOCATED` flag or `layout_epoch` change and re-attach.

#### SectionDescriptor (64 bytes)

Doubled from 32B to 64B to accommodate extensibility: longer names, SHM name for SEPARATE_SHM sections, and room for future fields.

```cpp
enum class SectionType : uint16_t {
    MEMORY_PAGES    = 0x0001,  // Existing ~5MB pages (SEPARATE_SHM flag)
    CHIP_STATE_Z80  = 0x0010,  // Z80 registers snapshot
    CHIP_STATE_AY   = 0x0011,  // AY-8910 registers
    CHIP_STATE_FDC  = 0x0012,  // WD1793 state
    INPUT_STATE     = 0x0020,  // Keyboard/joystick (bidirectional)
    LOG_STREAM      = 0x0100,  // SPSC ring buffer for log messages
    HEATMAP_Z80     = 0x0101,  // R/W/X counters for 64K Z80 address space (768KB)
    HEATMAP_PHYS    = 0x0102,  // R/W/X counters for all physical pages (~60MB, inline)
    HEATMAP_PAGES   = 0x0103,  // Per-page aggregated R/W/X (323 pages × 3 × uint32 = 3.8KB)
    OPCODE_TRACE    = 0x0110,  // OpcodeProfiler snapshot
    CALL_TRACE      = 0x0111,  // CallTraceBuffer snapshot
    FRAMEBUFFER     = 0x0200,  // RGBA framebuffer
    AUDIO_BUFFER    = 0x0201,  // Stereo PCM
    CONTROL_RING    = 0xFF00,  // Command ring (observer → emulator)

    // Type range 0x1000–0x1FFF reserved for user/plugin-defined sections
    USER_BASE       = 0x1000,
};

enum SectionFlags : uint16_t {
    ENABLED       = 0x0001,  // Section is actively updated
    BIDIRECTIONAL = 0x0002,  // Observer can write (INPUT_STATE, CONTROL_RING)
    SEPARATE_SHM  = 0x0004,  // Lives in a separate shared memory region
    RING_BUFFER   = 0x0008,  // Section is a ring buffer (has head/tail pointers)
    REMOVED       = 0x0010,  // Tombstone: section was removed, skip when iterating
};

struct alignas(64) SectionDescriptor {
    SectionType type;                    // Section type enum
    uint16_t flags;                      // SectionFlags bitmask
    uint32_t offset;                     // Byte offset from SHM start (0 if SEPARATE_SHM)
    uint32_t length;                     // Section data size in bytes
    uint32_t reserved1;
    std::atomic<uint64_t> epoch;         // Per-section version; UINT64_MAX = UPDATING
    char name[16];                       // Human-readable section name ("z80_regs", "fdc_log", etc.)
    char shm_name[16];                   // SHM name if SEPARATE_SHM flag set, else empty
};
static_assert(sizeof(SectionDescriptor) == 64);
```

#### Size Budget

| Section | Size | Notes |
|---------|------|-------|
| ManifestHeader | 128B | 2 cache lines |
| SectionDescriptors (N × 64B) | N × 64B | Dynamic; initial capacity 32 = 2KB |
| CHIP_STATE_Z80 | 128B | Registers + flags + padding |
| CHIP_STATE_AY | 32B | 16 registers + state |
| CHIP_STATE_FDC | 64B | WD1793 full state |
| INPUT_STATE | 16B | 5-byte keyboard matrix + joystick |
| LOG_STREAM | 64MB default, configurable up to 256MB | SPSC ring buffer (see capacity analysis below) |
| CONTROL_RING | 4KB | Command + response buffer |
| **Subtotal (core, no heatmaps)** | **~64MB** | State + logging + control |
| | | |
| HEATMAP_Z80 (opt-in) | 768KB | 64K addresses × 3 counters (R/W/X) × uint32 |
| HEATMAP_PAGES (opt-in) | 3.8KB | 323 pages × 3 × uint32 (aggregated per-page) |
| **Subtotal (core + Z80 heatmap)** | **~65MB** | |
| | | |
| HEATMAP_PHYS (opt-in) | ~60MB | 323 pages × 16K × 3 × uint32 (inline in main SHM) |
| **Subtotal (core + all heatmaps)** | **~125MB** | Main SHM region when everything enabled |
| | | |
| FRAMEBUFFER (opt-in) | varies | 196KB–573KB per buffer depending on video mode (SEPARATE_SHM) |
| AUDIO_BUFFER (opt-in) | ~3.5KB | Per-frame stereo PCM |

**Memory Heatmap sizing note:** The `MemoryAccessTracker` (`memoryaccesstracker.h`) tracks two independent counter sets:
- **Z80 address space** (64KB × 3 × uint32 = **768KB**) — always relevant, maps to the current bank configuration
- **Physical memory** (323 pages × 16KB × 3 × uint32 = **~60MB**) — tracks every physical page (256 RAM + 64 ROM + 2 CACHE + 1 MISC)

**Strategy:** All sections live inline in the single SHM region. Total SHM with everything enabled: ~125MB (well within the 512MB budget for any modern machine). The SHM is allocated at the needed size on `initialize()`, or reallocated when large sections are first enabled. Three levels of heatmap detail:
- **HEATMAP_PAGES** (3.8KB) — lightweight, always-on summary for "which pages are hot?"
- **HEATMAP_Z80** (768KB) — current Z80 address space view (reflects active bank mapping)
- **HEATMAP_PHYS** (~60MB) — full physical counters for all 323 pages, opt-in via control command

Memory pages remain in their existing separate SHM region (`/zxspectrum_memory-{id}`). The manifest descriptor for MEMORY_PAGES uses the `SEPARATE_SHM` flag.

**SHM naming convention:**

| Object | Name Pattern |
|--------|-------------|
| Monitoring SHM | `/unreal_monitor_{short_uuid}` |
| Frame Semaphore | `/unreal_frame_{short_uuid}` |
| Memory Pages (existing) | `/zxspectrum_memory-{id}` |

### 3.3 SPSC Ring Buffer for IPC

**New file:** `core/src/common/spsc_ringbuffer.h`

#### Why Not Reuse Existing `RingBuffer<T>`

The existing `RingBuffer<T>` (`core/src/common/ringbuffer.h`) cannot be used for cross-process IPC because:

1. **`std::shared_mutex`** — cannot be placed in shared memory (relies on kernel objects specific to one process)
2. **`std::vector<T>`** — stores heap pointers that are meaningless in another process's address space
3. **Blocking producer** — `push()` acquires a unique lock, violating "never block emulation"

#### New Design

```cpp
namespace emu {

// Designed for placement-new into shared memory.
// Power-of-2 capacity. Variable-length messages.
struct alignas(64) SPSCRingBuffer {

    // Header lives at the start of the shared memory region
    struct Header {
        std::atomic<uint64_t> write_pos;     // Only producer writes
        char _pad1[56];                       // ← separate cache line
        std::atomic<uint64_t> read_pos;      // Only consumer writes
        char _pad2[56];                       // ← separate cache line
        uint64_t capacity;                    // Power of 2
        uint64_t mask;                        // capacity - 1
        std::atomic<uint64_t> dropped_count;  // Messages dropped due to full buffer
    };

    // Per-message header (24 bytes, 8-byte aligned)
    struct alignas(8) MessageHeader {
        uint32_t category_id;    // FNV-1a hash of category name
        uint32_t length;         // Payload bytes (excl. header, excl. padding)
        uint64_t timestamp;      // steady_clock nanoseconds
        uint8_t  level;          // emu::log::Level enum value
        uint8_t  reserved[7];
    };
    static_assert(sizeof(MessageHeader) == 24);

    // Factory: initialize in pre-allocated memory
    static SPSCRingBuffer* create(void* memory, size_t capacity);

    // Factory: attach to existing shared memory (observer side)
    static SPSCRingBuffer* attach(void* memory);

    // Producer side (emulator) — never blocks
    bool try_write(uint32_t category_id, uint8_t level,
                   const char* payload, size_t len);

    // Consumer side (observer)
    bool try_read(MessageHeader& hdr_out, char* payload_out, size_t capacity);

    // Consumer: drain all available, up to max_msgs
    size_t drain(MessageHeader* hdrs, char** payloads, size_t max_msgs);

private:
    Header header_;
    // Data follows immediately: char data_[capacity]

    char* data() { return reinterpret_cast<char*>(this) + sizeof(Header); }
    void write_wrapped(uint64_t pos, const void* src, size_t len);
    void read_wrapped(uint64_t pos, void* dst, size_t len);
};

} // namespace emu
```

**Key design decisions:**
- **Cache-line separation** between `write_pos` and `read_pos` eliminates false sharing
- **Wrap-around via bitmask:** `index = pos & mask` (capacity must be power of 2)
- **Drop-on-full:** `try_write()` returns `false`, increments `dropped_count`. Never blocks.
- **No heap allocation:** everything is flat in the shared memory block
- **Portable timestamp:** `std::chrono::steady_clock::now().time_since_epoch().count()` (~25ns, no TSC dependency)

#### Log Stream Capacity Analysis

The ring buffer must be large enough to capture complete profiling sessions without dropping messages. Consider a real-world scenario: **profiling a non-standard disk loader** with FDC events + ROM procedure call tracking + breakpoints + analyzer information enabled simultaneously.

**Event rate estimation:**

| Source | Events/sec | Avg payload | Bytes/sec |
|--------|-----------|-------------|-----------|
| FDC commands (seek, read sector, etc.) | ~200 | ~80B | ~20KB/s |
| FDC data transfers (byte-level) | ~50,000 | ~40B | ~3.2MB/s |
| ROM procedure calls (via breakpoints) | ~10,000 | ~60B | ~770KB/s |
| Analyzer breakpoint hits | ~5,000 | ~50B | ~370KB/s |
| Z80 I/O port access logging | ~20,000 | ~40B | ~1.2MB/s |
| **Total (heavy profiling)** | **~85,000** | | **~5.5MB/s** |

With 24-byte `MessageHeader` overhead + 8-byte alignment padding per message, effective rate is approximately **8-10MB/s** during intensive profiling.

**Sizing for real use cases:**

| Scenario | Duration | Data Volume | Recommended Size |
|----------|----------|-------------|-----------------|
| Quick FDC command trace | 2-5s | ~100KB | 4MB (minimum) |
| Full disk load profiling | 5-15s | 50-150MB | 64MB (default) |
| Extended loader analysis | 30-60s | 250-600MB | 256MB (maximum) |
| Selective category trace | minutes | varies | 64MB with drain |

**Design decision:** Default to **64MB** ring buffer. Configurable up to **256MB** via control command or startup parameter. The philosophy is: **collect everything, browse later**. For sessions that exceed ring capacity, the observer tool should drain the ring in real-time and buffer to disk. The `dropped_count` atomic in the ring header lets observers detect if any messages were lost.

For the extended analysis case (>256MB), the observer tool operates in **streaming mode**: it drains the ring continuously, writing to a local file or database, and presents a scrollable/searchable view after the session ends.

### 3.4 Modular Logging — Hierarchical Zero-Cost Categories

**New files:**
- `core/src/common/emulog.h` — macros
- `core/src/common/emulog_category.h` — CategoryState struct
- `core/src/common/emulog_registry.h/.cpp` — LoggerRegistry

#### Coexistence with ModuleLogger

The existing `ModuleLogger` and `MLOGDEBUG`/`MLOGINFO` macros remain **unchanged**. The new `EMU_LOG_*` system is purely additive. Over time, hot-path logging (FDC operations, Z80 interrupts) migrates to `EMU_LOG_*`. Cold-path diagnostics (startup, configuration) stay with `ModuleLogger`.

#### CategoryState

```cpp
namespace emu::log {

// Compile-time category ID via FNV-1a
constexpr uint32_t hash_fnv1a(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (char c : name) {
        hash ^= static_cast<uint32_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

enum class Level : uint8_t {
    Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Off = 255
};

// Per-category runtime state — designed for cache efficiency
struct CategoryState {
    std::atomic<bool> enabled{false};            // Primary gate
    std::atomic<uint8_t> level{uint8_t(Level::Off)};  // Minimum level
    const char* name;                            // "fdc.read"
    uint32_t id;                                 // hash_fnv1a(name)
    CategoryState* parent{nullptr};              // "fdc" for "fdc.read"

    bool is_enabled(Level msg_level) const noexcept {
        return enabled.load(std::memory_order_relaxed) &&
               static_cast<uint8_t>(msg_level) >=
               level.load(std::memory_order_relaxed);
    }
};

} // namespace emu::log
```

#### Zero-Cost Macros

```cpp
// Category definition macro — creates static storage + auto-registration
#define EMU_LOG_DEFINE_CATEGORY(var, path) \
    static ::emu::log::CategoryState var##_state{ \
        {false}, {static_cast<uint8_t>(::emu::log::Level::Off)}, \
        path, ::emu::log::hash_fnv1a(path), nullptr}; \
    static ::emu::log::CategoryRegistrar var##_reg{var##_state}

// Logging macros — the hot-path check is 1 relaxed atomic load
#define EMU_LOG(cat, lvl, fmt, ...) \
    do { \
        if (__builtin_expect( \
                cat##_state.is_enabled(::emu::log::Level::lvl), 0)) \
            ::emu::log::emit(cat##_state, ::emu::log::Level::lvl, \
                             fmt, ##__VA_ARGS__); \
    } while(0)

#define EMU_LOG_TRACE(cat, fmt, ...) EMU_LOG(cat, Trace, fmt, ##__VA_ARGS__)
#define EMU_LOG_DEBUG(cat, fmt, ...) EMU_LOG(cat, Debug, fmt, ##__VA_ARGS__)
#define EMU_LOG_INFO(cat, fmt, ...)  EMU_LOG(cat, Info, fmt, ##__VA_ARGS__)
#define EMU_LOG_WARN(cat, fmt, ...)  EMU_LOG(cat, Warn, fmt, ##__VA_ARGS__)
#define EMU_LOG_ERROR(cat, fmt, ...) EMU_LOG(cat, Error, fmt, ##__VA_ARGS__)
```

**When disabled (the common case):**
1. Load `enabled` (relaxed atomic) → `false`
2. `__builtin_expect(..., 0)` → branch-not-taken
3. **Total cost: ~1-2ns** with near-perfect branch prediction

**`emit()` function** (cold path, only called when logging is actually enabled):
```cpp
template<typename... Args>
void emit(CategoryState& cat, Level level, const char* fmt, Args... args) {
    // Format into thread-local buffer
    thread_local char buf[4096];
    int len = std::snprintf(buf, sizeof(buf), fmt, args...);
    if (len <= 0) return;

    // Write to SPSC ring buffer (non-blocking)
    auto* ring = LoggerRegistry::instance().ringBuffer();
    if (ring) {
        ring->try_write(cat.id, static_cast<uint8_t>(level),
                        buf, static_cast<size_t>(len));
    }
}
```

#### Category Hierarchy

Categories use dot-separated names. The registry builds parent-child links on registration:

```
fdc                    → parent: (root)
├─ fdc.command         → parent: fdc
├─ fdc.read            → parent: fdc
├─ fdc.write           → parent: fdc
└─ fdc.seek            → parent: fdc

z80                    → parent: (root)
├─ z80.interrupt       → parent: z80
├─ z80.halt            → parent: z80
└─ z80.io              → parent: z80

memory                 → parent: (root)
├─ memory.rom          → parent: memory
├─ memory.ram          → parent: memory
└─ memory.bank         → parent: memory

sound                  → parent: (root)
├─ sound.ay            → parent: sound
└─ sound.beeper        → parent: sound
```

**Wildcard enable:** `setEnabled("fdc.*", true, Level::Trace)` iterates all registered categories with prefix `fdc.` and sets their flags. This is a **cold-path** operation (runs in control command handler, < 1 call/second).

#### LoggerRegistry

```cpp
class LoggerRegistry {
public:
    static LoggerRegistry& instance();

    // Registration (called from static initializers)
    void registerCategory(CategoryState& state);

    // Runtime control (called from control channel)
    void setEnabled(const std::string& pattern, bool enabled,
                    Level level = Level::Trace);
    void disableAll();

    // Query (for control channel responses)
    std::vector<std::pair<std::string, bool>> listCategories() const;

    // Ring buffer access
    SPSCRingBuffer* ringBuffer() { return _ringBuffer; }
    void setRingBuffer(SPSCRingBuffer* rb) { _ringBuffer = rb; }

private:
    std::mutex _mutex;  // Only for cold-path registration/reconfiguration
    std::unordered_map<uint32_t, CategoryState*> _byId;
    std::unordered_map<std::string, CategoryState*> _byName;
    SPSCRingBuffer* _ringBuffer = nullptr;
};
```

#### Usage Example (FDC)

```cpp
// In wd1793.cpp
#include "common/emulog.h"

EMU_LOG_DEFINE_CATEGORY(log_fdc,     "fdc");
EMU_LOG_DEFINE_CATEGORY(log_fdc_cmd, "fdc.command");
EMU_LOG_DEFINE_CATEGORY(log_fdc_rd,  "fdc.read");
EMU_LOG_DEFINE_CATEGORY(log_fdc_wr,  "fdc.write");
EMU_LOG_DEFINE_CATEGORY(log_fdc_sk,  "fdc.seek");

void WD1793::executeReadSector() {
    EMU_LOG_INFO(log_fdc_cmd, "READ_SECTOR T%d/H%d/S%d",
                 _track, _side, _sector);

    // ... perform read ...

    EMU_LOG_DEBUG(log_fdc_rd, "Read %zu bytes, CRC=%04X",
                  bytesRead, crc);
}
```

### 3.5 Coherency Protocol — Epoch-Based Seqlock

State sections (Z80 regs, AY, FDC — small, fixed-size) use a seqlock pattern:

```cpp
static constexpr uint64_t UPDATING = UINT64_MAX;

// ── Producer (emulator, in MonitoringManager::onFrameEnd) ──

void MonitoringManager::updateStateSection(size_t idx) {
    auto& desc = _sections[idx].descriptor;

    // 1. Signal "updating" — observers will retry
    desc.epoch.store(UPDATING, std::memory_order_release);

    // 2. Copy state (Z80: 128B memcpy → ~50ns)
    copyStateData(idx);

    // 3. Commit with new epoch value
    desc.epoch.store(
        _manifestHeader->global_epoch.load(std::memory_order_relaxed),
        std::memory_order_release);
}

// ── Consumer (observer process) ──

bool readSection(const SectionDescriptor& desc, void* out, size_t size) {
    uint64_t before = desc.epoch.load(std::memory_order_acquire);
    if (before == UPDATING) return false;  // Writer active

    std::memcpy(out, sectionDataPtr(desc), size);

    std::atomic_thread_fence(std::memory_order_acquire);
    uint64_t after = desc.epoch.load(std::memory_order_relaxed);

    return (before == after);  // True if no torn read
}
```

**Why this works:** For small sections (≤128B), `memcpy` completes in <100ns. The writer only updates once per frame (every 20ms). The probability of a torn read is astronomically low, and the observer simply retries.

**Ring buffer sections** (LOG_STREAM) do **not** need the epoch protocol — the SPSC design inherently guarantees coherent reads via `write_pos`/`read_pos` release-acquire ordering.

### 3.6 Control Channel — Embedded Shared Memory Polling

For Phase 1, control commands are embedded in the manifest's `CONTROL_RING` section. The emulator polls on every `OnFrameEnd()` (50Hz).

```cpp
struct ControlCommand {
    enum class Type : uint8_t {
        NONE = 0,
        ENABLE_SECTION,
        DISABLE_SECTION,
        SET_LOG_LEVEL,
        ENABLE_LOG_CATEGORY,
        DISABLE_LOG_CATEGORY,
        LIST_CATEGORIES,
        PING,
    };

    std::atomic<uint8_t> type{uint8_t(Type::NONE)};
    uint8_t section_index;
    uint8_t log_level;
    uint8_t reserved;
    char pattern[60];                // Category pattern: "fdc.*"
    std::atomic<bool> processed{true};
};

struct ControlBlock {
    static constexpr size_t CMD_SLOTS = 4;
    ControlCommand commands[CMD_SLOTS];
    std::atomic<uint32_t> write_idx{0};  // Observer increments
    std::atomic<uint32_t> read_idx{0};   // Emulator increments

    // Response area (emulator writes, observer reads)
    std::atomic<uint64_t> response_epoch{0};
    char response_data[2048];            // Category list, status, etc.
};
```

**Latency:** worst-case 20ms (one frame). This is perfectly acceptable for toggling log categories or enabling sections.

**Future (Phase 5+):** If sub-frame latency is needed (e.g., breakpoint injection from observer), add platform-specific signaling:
- Linux/macOS: Unix Domain Socket at `/tmp/unreal_ctl_{short_id}`
- Windows: Named Pipe at `\\.\pipe\unreal_ctl_{short_id}`

### 3.7 Frame Sync — Cross-Process Notification

**New file:** `core/src/common/framenotifier.h`

```cpp
class FrameNotifier {
public:
    bool create(const std::string& name);  // Emulator calls at init
    void signal();                          // Called from onFrameEnd(), non-blocking
    void destroy();                         // Cleanup
private:
#ifdef _WIN32
    HANDLE _event = INVALID_HANDLE_VALUE;   // CreateEventA (named, auto-reset)
#elif __APPLE__
    sem_t* _semaphore = SEM_FAILED;         // sem_open (named POSIX semaphore)
#else  // Linux
    int _eventfd = -1;                      // eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE)
#endif
};

class FrameWaiter {
public:
    bool attach(const std::string& name);   // Observer calls to connect
    bool wait(uint32_t timeout_ms = 100);   // Blocking wait for next frame
    void detach();
};
```

**Called from `MonitoringManager::onFrameEnd()`** after bumping `global_epoch`. The `signal()` call costs ~200ns (kernel semaphore post). Observers that don't need blocking wait can simply poll `global_epoch` in shared memory.

### 3.8 SharedMemoryRegion — Reusable Platform Abstraction

**New files:** `core/src/common/shared_memory_region.h` and `.cpp`

Extracts the cross-platform shared memory pattern from `Memory::AllocateAndExportMemoryToMmap()`:

```cpp
class SharedMemoryRegion {
public:
    ~SharedMemoryRegion();

    // Producer: create new shared memory region
    bool create(const std::string& name, size_t size);

    // Consumer: attach to existing region
    bool attach(const std::string& name);

    // Cleanup
    void close();

    void* data() const;
    size_t size() const;
    const std::string& name() const;

private:
    std::string _name;
    size_t _size = 0;
    void* _ptr = nullptr;
    bool _isOwner = false;

#ifdef _WIN32
    HANDLE _handle = INVALID_HANDLE_VALUE;
#else
    int _fd = -1;
#endif
};
```

The existing `Memory` class can optionally be refactored to use this in a later cleanup pass — no changes required for Phase 1.

---

## 4. Section Catalog — How Each Section Works

### State Sections (epoch-protected, written once per frame)

| Section | Source Data | Update Trigger | Size |
|---------|-----------|----------------|------|
| `CHIP_STATE_Z80` | `pCore->GetZ80()` registers union | `onFrameEnd()` | 128B |
| `CHIP_STATE_AY` | `pSoundManager->getAYChip(0)->getRegisters()` | `onFrameEnd()` | 32B |
| `CHIP_STATE_FDC` | `pBetaDisk` command/track/sector/status | `onFrameEnd()` | 64B |
| `INPUT_STATE` | Keyboard matrix + joystick state | `onFrameEnd()` | 16B |

### Telemetry Sections (append-only / snapshot)

| Section | Mechanism | Update Trigger | Size |
|---------|-----------|----------------|------|
| `LOG_STREAM` | SPSC ring buffer | On each `EMU_LOG_*` call | 64MB default (configurable up to 256MB) |
| `HEATMAP_Z80` | Snapshot Z80 address space counters | `onFrameEnd()` | 768KB |
| `HEATMAP_PAGES` | Snapshot per-page aggregated counters | `onFrameEnd()` | 3.8KB |
| `HEATMAP_PHYS` | Full physical page counters (opt-in) | `onFrameEnd()` | ~60MB (inline) |
| `OPCODE_TRACE` | Copy OpcodeProfiler trace tail | `onFrameEnd()` | ~2KB |
| `CALL_TRACE` | Copy CallTraceBuffer latest N | `onFrameEnd()` | ~16KB |

**Note on physical heatmap:** 323 pages = MAX_RAM_PAGES (256) + MAX_CACHE_PAGES (2) + MAX_MISC_PAGES (1) + MAX_ROM_PAGES (64), each 16KB (`PAGE_SIZE = 0x4000`). With 3 counter types × uint32 per address, this is `323 × 16384 × 3 × 4 = ~60MB`. This section lives inline in the main SHM region. When enabled via control command, the SHM is sized to accommodate it. For lightweight use cases, `HEATMAP_Z80` + `HEATMAP_PAGES` alone are sufficient.

### Media Sections (large, opt-in, frame-synced)

| Section | Source | Mechanism | Size |
|---------|--------|-----------|------|
| `FRAMEBUFFER` | `pScreen->GetFramebufferDescriptor()` | Double-buffer + epoch + version | Variable (see below) |
| `AUDIO_BUFFER` | `pSoundManager->getAudioBufferDescriptor()` | Copy on frame end | ~3.5KB |

**Framebuffer sizing note:** The framebuffer dimensions change at runtime when the video mode changes (`Screen::SetVideoMode()`). The `FramebufferDescriptor` struct carries `width`, `height`, `videoMode`, and `memoryBufferSize`. Supported raster modes (`screen.h`):

| Raster Mode | Resolution | RGBA Size | Video Modes |
|-------------|-----------|-----------|-------------|
| R_256_192 | 256×192 | ~196KB | M_ZX48, M_ZX128, M_PENTAGON128K |
| R_320_200 | 320×200 | ~256KB | M_ATM16, M_ATMHR, M_TS16, M_TS256 |
| R_320_240 | 320×240 | ~307KB | M_TSTX |
| R_360_288 | 360×288 | ~415KB | M_TS (extended) |
| R_384_304 | 384×304 | ~467KB | M_P384 (AlCo) |
| R_512_240 | 512×240 | ~492KB | M_PROFI |

The full video buffer is actually larger — `VID_WIDTH(448) × VID_HEIGHT(320) × RGBA = ~573KB` — because it includes border area. With double-buffering the section needs 2× this.

**Strategy for mode changes:** The framebuffer section uses `SEPARATE_SHM` with a **version counter** in the section descriptor. When the video mode changes:
1. MonitoringManager allocates a new SHM region sized for the new mode (with double-buffer space)
2. Updates the `SectionDescriptor` with new `length`, bumps section `epoch`
3. Writes new `width`, `height`, `videoMode` into a small **FramebufferHeader** at the start of the section data
4. Observer detects the epoch change, re-reads descriptor, re-attaches if `length` changed

```cpp
struct FramebufferSectionHeader {
    uint16_t width;          // Current framebuffer width in pixels
    uint16_t height;         // Current framebuffer height in pixels
    uint8_t  videoMode;      // VideoModeEnum value
    uint8_t  activeBuffer;   // Which of the 2 double-buffer slots is current (0 or 1)
    uint16_t stride;         // Bytes per row (width × 4 for RGBA)
    uint32_t bufferOffset;   // Offset from section start to pixel data
    uint32_t bufferSize;     // Single buffer size in bytes
};
// Followed by: pixel_data[2][bufferSize]  (double-buffered)
```

---

## 5. Integration Points

### EmulatorContext (`core/src/emulator/emulatorcontext.h`)

Add alongside existing subsystem pointers:
```cpp
MonitoringManager* pMonitoringManager = nullptr;
```

### MainLoop (`core/src/emulator/mainloop.cpp`)

In `OnFrameEnd()`, after the existing `SyncToDisk()` call:
```cpp
// Real-time monitoring: snapshot state, flush logs, signal observers
if (_context->pMonitoringManager && _context->pMonitoringManager->isEnabled())
    _context->pMonitoringManager->onFrameEnd();
```

### FeatureManager (`core/src/base/featuremanager.h`)

Add constants:
```cpp
constexpr const char* const kMonitoring = "monitoring";
constexpr const char* const kMonitoringAlias = "mon";
constexpr const char* const kMonitoringDesc =
    "Real-time monitoring via shared memory for external observer tools.";
```

### Emulator Init (`core/src/emulator/emulator.cpp`)

In `Emulator::Init()`, create MonitoringManager after FeatureManager:
```cpp
_context->pMonitoringManager = new MonitoringManager(_context);
if (_context->pFeatureManager->isEnabled(Features::kMonitoring)) {
    _context->pMonitoringManager->initialize();
}
```

### CMakeLists.txt

Add monitoring source files to the core library target.

---

## 6. Performance Analysis

### Per-Frame Overhead (monitoring enabled, no observer)

| Operation | Cost | Notes |
|-----------|------|-------|
| Feature check (`bool _enabled`) | ~0ns | Branch predicted, always same |
| Z80 state memcpy (128B) | ~50ns | L1 cache hit |
| AY + FDC state copy (96B) | ~30ns | |
| Epoch stores (2 atomics × 3 sections) | ~30ns | release stores |
| Frame signal (`sem_post`) | ~200ns | Kernel call |
| Control poll (4 atomic loads) | ~20ns | relaxed loads |
| Z80 heatmap snapshot (768KB) | ~100μs | memcpy of 3×64K uint32 arrays, only if enabled |
| Page summary snapshot (3.8KB) | ~50ns | 323 × 3 × uint32, trivial |
| Phys heatmap snapshot (~60MB) | ~8ms | Inline, opt-in. 40% of frame budget — acceptable for diagnostics |
| **Total (without heatmaps)** | **~330ns** | **0.0016% of 20ms frame** |
| **Total (with Z80 heatmap)** | **~100μs** | **0.5% of 20ms frame** |

### Per-Call-Site Overhead (EMU_LOG_* macro, category disabled)

| Operation | Cost |
|-----------|------|
| `enabled.load(relaxed)` | ~1ns |
| Branch-not-taken | ~0ns (predicted) |
| **Total** | **~1-2ns** |

### Monitoring Fully Disabled (feature OFF)

| Operation | Cost |
|-----------|------|
| `if (pMonitoringManager && pMonitoringManager->isEnabled())` | ~1ns |
| **Total per frame** | **~1ns** |

---

## 7. Phased Rollout

### Phase 1: Foundation — Manifest + Z80 State (Week 1-2)

**Goal:** External tool can discover and read Z80 registers in real-time.

**Deliverables:**
1. `MonitoringManager` class skeleton
2. `SharedMemoryRegion` cross-platform abstraction
3. `ManifestHeader` + `SectionDescriptor` binary protocol
4. `CHIP_STATE_Z80` section (epoch-protected, frame-synced)
5. `CONTROL_RING` section with ENABLE/DISABLE commands
6. `FrameNotifier` platform abstraction
7. Feature gate: `monitoring` in FeatureManager
8. CLI command: `monitor status`
9. Minimal `unreal-monitor-probe` observer tool

**Files to create:**
```
core/src/emulator/monitoring/monitoringmanager.h
core/src/emulator/monitoring/monitoringmanager.cpp
core/src/emulator/monitoring/manifest.h
core/src/common/shared_memory_region.h
core/src/common/shared_memory_region.cpp
core/src/common/framenotifier.h
tools/monitor-probe/main.cpp
```

**Files to modify:**
```
core/src/emulator/emulatorcontext.h
core/src/emulator/mainloop.cpp
core/src/base/featuremanager.h
CMakeLists.txt
```

**Validation:** Run `unreal-monitor-probe` while emulator runs → prints Z80 PC, SP, AF, BC, DE, HL updating at 50Hz.

---

### Phase 2: Modular Logging — SPSC + Categories + FDC Proof (Week 3)

**Goal:** FDC disk operations visible in real-time from external process.

**Deliverables:**
1. `SPSCRingBuffer` lock-free implementation
2. `LOG_STREAM` section in manifest
3. `emu::log::CategoryState` + `LoggerRegistry` + `EMU_LOG_*` macros
4. FDC categories: `fdc`, `fdc.command`, `fdc.read`, `fdc.write`, `fdc.seek`
5. Runtime enable/disable categories via control ring
6. `unreal-log-viewer` CLI observer tool

**Files to create:**
```
core/src/common/spsc_ringbuffer.h
core/src/common/emulog.h
core/src/common/emulog_category.h
core/src/common/emulog_registry.h
core/src/common/emulog_registry.cpp
tools/log-viewer/main.cpp
```

**Files to modify:**
```
core/src/emulator/io/fdc/wd1793.cpp (add EMU_LOG categories)
core/src/emulator/monitoring/monitoringmanager.cpp (add LOG_STREAM section)
```

**Validation:** Load a disk image → `unreal-log-viewer --filter fdc.read` shows sector reads in real-time → disable `fdc.read`, enable `fdc.seek` → only seek events visible.

---

### Phase 3: Media + More State (Week 4)

**Goal:** Full chip state coverage + framebuffer/audio for external viewers.

**Deliverables:**
1. `CHIP_STATE_AY`, `CHIP_STATE_FDC` sections
2. `INPUT_STATE` bidirectional section
3. `FRAMEBUFFER` section with double-buffering
4. `AUDIO_BUFFER` section
5. Frame-sync blocking wait in observer tools

**Validation:** External screen viewer shows live ZX-Spectrum display via manifest-based discovery.

---

### Phase 4: Telemetry Deep Dive (Week 5)

**Goal:** Memory heatmaps, opcode traces, call traces exposed for visualization tools.

**Deliverables:**
1. `HEATMAP_Z80` + `HEATMAP_PAGES` + `HEATMAP_PHYS` sections (snapshot `MemoryAccessTracker` counters)
2. `OPCODE_TRACE` section (snapshot `OpcodeProfiler` trace tail)
3. `CALL_TRACE` section (snapshot `CallTraceBuffer` latest)
4. More logging categories: `z80.*`, `memory.*`, `sound.*`

**Validation:** External visualization tool shows live memory access heatmap.

---

### Phase 5: Hardening & Documentation (Week 6)

**Deliverables:**
1. Multi-instance support (EmulatorManager with N instances, separate SHM per instance)
2. Graceful observer disconnect detection (PID check, heartbeat)
3. Unit tests: SPSCRingBuffer correctness + stress, manifest serialization, epoch torn-read
4. Performance benchmarks: frame time with/without monitoring, verify < 1μs overhead
5. Cross-platform CI: macOS, Linux (Docker), Windows
6. Protocol documentation for third-party tool developers

---

## 8. Observer Application — `unreal-monitor` (Separate Qt6 App)

A standalone Qt6 application that attaches to running emulator instances via the monitoring shared memory protocol. This is the primary consumer of everything described in sections 2–7. It replaces the ad-hoc CLI probe tools (`monitor-probe`, `log-viewer`) with a unified, visual, dockable workspace.

### 8.1 Architecture Overview

```
┌────────────────────────────────────────────────────────────────────────┐
│  unreal-monitor (Qt6 Application)                                      │
│                                                                        │
│  ┌─────────────┐  ┌──────────────────────────────────────────────────┐ │
│  │ Instance    │  │  Docking Workspace                               │ │
│  │ Selector    │  │  ┌──────────────┐ ┌──────────────────────────┐   │ │
│  │             │  │  │ Log Viewer   │ │ Memory Heatmap           │   │ │
│  │ ● emu-1     │  │  │              │ │ (calltrace-viz based)    │   │ │
│  │   emu-2     │  │  │ [TRACE] fdc… │ │ ┌──┐┌──┐┌──┐┌──┐         │   │ │
│  │   emu-3     │  │  │ [DEBUG] z80… │ │ │B0││B1││B2││B3│         │   │ │
│  │             │  │  │ [INFO]  mem… │ │ └──┘└──┘└──┘└──┘         │   │ │
│  │ [Connect]   │  │  └──────────────┘ └──────────────────────────┘   │ │
│  │ [WebAPI]    │  │  ┌──────────────┐ ┌──────────┐ ┌────────────┐    │ │
│  │ [SHM Probe] │  │  │ Chip State   │ │ Screen   │ │ Sound      │    │ │
│  │             │  │  │ Z80│AY│FDC   │ │ 256×192  │ │ ≋≋≋≋≋≋≋≋   │    │ │
│  │ CLI Console │  │  │ AF: 0x00FF   │ │ live fb  │ │ spectrum   │    │ │
│  │ > status    │  │  │ BC: 0x1234   │ │          │ │ waveform   │    │ │
│  │ > feature…  │  │  └──────────────┘ └──────────┘ └────────────┘    │ │
│  └─────────────┘  │  ┌────────────────────────────────────────────┐  │ │
│                   │  │ Call Trace / Opcode Trace (timeline)       │  │ │
│                   │  └────────────────────────────────────────────┘  │ │
│                   └──────────────────────────────────────────────────┘ │
│  [Status Bar: emu-1 | 50fps | SHM: /unreal_monitor_a1b2 | 125MB]       │
└────────────────────────────────────────────────────────────────────────┘
```

### 8.2 Instance Discovery & Connection

Two discovery mechanisms, matching the existing `unreal-screen-viewer` pattern:

#### WebAPI Discovery (primary, network-capable)

Reuses the `WebAPIClient` pattern from `unreal-screen-viewer`. Talks to the Drogon-based WebAPI server (port 8090) to:
- `GET /api/v1/emulator` — list all instances with IDs, models, states
- `POST /api/v1/emulator/{id}/feature` — enable `monitoring` feature, get back SHM name
- `GET /api/v1/emulator/{id}/monitoring` — query monitoring status, section list

This works across machines (remote emulator on another host).

#### SHM Probe (local, zero-config)

For local-only use without WebAPI:
- Scan `/dev/shm/` (Linux) or iterate POSIX shm namespace for `/unreal_monitor_*` names
- Validate each by reading `ManifestHeader.magic == 0x554E524C` and checking `emulator_pid` is alive
- Auto-populates instance list for local emulators even if WebAPI is disabled

#### Connection Flow

```
User selects instance → WebAPI: enable monitoring feature
                       → WebAPI returns SHM name + semaphore name
                       → SharedMemoryRegion::attach(shm_name)
                       → FrameWaiter::attach(sem_name)
                       → Read ManifestHeader → enumerate SectionDescriptors
                       → Start per-panel refresh (frame-synced or polled)
```

### 8.3 Docking Workspace

Uses `QMainWindow` docking (same pattern as `DebuggerWindow` in `unreal-qt`). Each visualization is a `QDockWidget` that can be:
- Docked, floated, tabbed, closed, reopened
- Layout saved/restored via `QSettings` (per-instance or global)
- Enabled/disabled independently — only active panels consume data

Dock panels:

| Panel | Data Source (SHM Section) | Refresh | Notes |
|-------|--------------------------|---------|-------|
| Log Viewer | `LOG_STREAM` | SPSC drain on frame signal | Main workhorse panel |
| Memory Heatmap | `HEATMAP_Z80` / `HEATMAP_PHYS` / `HEATMAP_PAGES` | Frame-synced epoch read | Evolved from `calltrace-viz` POC |
| Z80 Registers | `CHIP_STATE_Z80` | Frame-synced epoch read | |
| AY Registers | `CHIP_STATE_AY` | Frame-synced epoch read | |
| FDC State | `CHIP_STATE_FDC` | Frame-synced epoch read | |
| Screen Viewer | `FRAMEBUFFER` | Frame-synced double-buffer | |
| Sound Renderer | `AUDIO_BUFFER` | Frame-synced PCM playback | |
| Call Trace | `CALL_TRACE` | Frame-synced snapshot | Timeline view |
| Opcode Trace | `OPCODE_TRACE` | Frame-synced snapshot | Profiler counters + recent trace |
| Input Injector | `INPUT_STATE` (bidirectional) | On user action | Keyboard/joystick virtual panel |
| CLI Console | TCP socket to CLI (port 8765) | Interactive | Embedded telnet client |

### 8.4 Log Viewer Panel

The central panel for the profiling use case described in section 3.3.

#### Features

- **Real-time streaming:** Drains SPSC ring on each frame signal (~50Hz). Appends to an internal `QAbstractItemModel` backed by a flat `std::vector<LogEntry>` (millions of entries, virtual scrolling)
- **Category tree filter:** Sidebar tree widget mirrors the `emu::log` hierarchy (`fdc` → `fdc.command`, `fdc.read`, …). Checkbox per category. Checking/unchecking sends `ENABLE_LOG_CATEGORY` / `DISABLE_LOG_CATEGORY` commands via the control ring
- **Level filter:** Toggle buttons for Trace/Debug/Info/Warn/Error — client-side filtering of already-received messages
- **Text search:** Incremental search across message payloads. Highlights matches, jump-to-next/prev
- **Timestamp display:** Relative to session start or absolute. Nanosecond precision from `MessageHeader.timestamp`
- **Bookmarks:** Click to bookmark a log entry. Bookmark list in sidebar for quick navigation
- **Export:** Save visible (filtered) or all log entries to file (CSV, JSON, or plain text)
- **Auto-scroll lock:** Tail mode (follows new entries) vs. freeze mode (holds position while new data arrives in background)

#### Session Recording

For the "collect everything, browse later" workflow:
- Observer can optionally write all drained messages to a **session file** on disk (binary format matching SPSC `MessageHeader` + payload)
- Session files can be reopened in the Log Viewer panel without a running emulator
- Enables post-mortem analysis of disk loader profiling sessions

#### Data Model

```cpp
struct LogEntry {
    uint64_t timestamp;      // From MessageHeader
    uint32_t categoryId;     // FNV-1a hash → resolved to name via registry query
    uint8_t  level;
    QString  message;        // Payload decoded as UTF-8
    uint32_t sequenceNumber; // Local monotonic counter for stable sorting
};
```

### 8.5 Memory Heatmap Panel

Evolved from the `calltrace-viz` POC (`tools/poc/calltrace-viz/`). Reuses the rendering pipeline: QPainter-based 128×128 grids, gamma-corrected color LUT, glow overlays, dark/light themes.

#### Views

- **Z80 Address Space** (768KB, `HEATMAP_Z80`): 4-bank layout (0000–3FFF, 4000–7FFF, 8000–BFFF, C000–FFFF). Shows current bank mapping with R/W/X overlays. Matches existing `Memory16KBWidget` layout
- **Page Summary** (3.8KB, `HEATMAP_PAGES`): Grid of 323 page thumbnails (ROM + RAM + CACHE + MISC). Color intensity = aggregate access count. Click a page to drill into its full heatmap
- **Physical Page Detail** (~60MB, `HEATMAP_PHYS`): Full 16K × 3-counter heatmap for a selected page. Identical rendering to Z80 bank view but for a specific physical page

#### Overlay Layers (from calltrace-viz POC)

- Read overlay (blue), Write overlay (red), Execute overlay (green)
- Control flow heatmap (gold/orange)
- Source/target highlighting with arc rendering
- Entropy map (Viridis colorspace)
- Region classification (code/data/sprites/music/stack)
- Freshness tracking (time since last write)

#### Live Mode

Data refreshes at frame rate (50Hz). The epoch-read pattern ensures no torn reads. Overlay layers are toggled via sidebar checkboxes, same as the POC.

### 8.6 Chip State Panels

Three panels for Z80, AY-8910, and WD1793 state. Each reads its `CHIP_STATE_*` section using the epoch seqlock protocol.

#### Z80 Registers Panel

- Register pairs: AF, BC, DE, HL, AF', BC', DE', HL', IX, IY, SP, PC
- Flags decoded: S, Z, H, P/V, N, C (with individual bit indicators)
- IM mode, IFF1/IFF2, HALT state
- T-state counter within current frame
- **Change highlighting:** registers that changed since last frame shown in accent color

#### AY-8910 Registers Panel

- 16 registers in hex + decoded view
- Channel A/B/C: tone period, volume, envelope
- Noise period, mixer control
- Envelope period + shape
- **Visual:** per-channel volume bars (real-time VU meter style)

#### FDC (WD1793) State Panel

- Command register, status register (with bit decode)
- Track, sector, data registers
- Head position, motor state, drive select
- Current operation (IDLE, READ, WRITE, SEEK, etc.)
- **Visual:** disk diagram showing head position on track

### 8.7 Screen Viewer Panel

Reads the `FRAMEBUFFER` section (double-buffered RGBA). Handles runtime video mode changes via `FramebufferSectionHeader` version tracking.

- **Rendering:** `QImage` from raw RGBA data → `QWidget::paintEvent()` with aspect-ratio-preserving scale
- **Mode adaptation:** On `FramebufferSectionHeader.videoMode` change, re-reads dimensions, adjusts `QImage` format, re-attaches if SHM was reallocated
- **Zoom:** 1×, 2×, 3×, fit-to-panel
- **Border toggle:** Show/hide border area (full VID_WIDTH×VID_HEIGHT vs. raster-mode area only)
- **Screenshot:** Save current frame as PNG

### 8.8 Sound Renderer Panel

Reads the `AUDIO_BUFFER` section (stereo PCM, ~3.5KB per frame) and plays it through the local audio device.

#### Playback

- Uses `miniaudio` (already in the codebase) for audio device output
- Frame-synced: on each frame signal, copies PCM samples from SHM into a local `AudioRingBuffer`
- `miniaudio` callback drains the ring buffer at the device sample rate
- DC offset filter (reuse `FilterDC<int16_t>` from `unreal-videowall/src/emulator/soundmanager.h`)
- Mute/volume control in panel toolbar

#### Visualization

- **Waveform:** Scrolling stereo waveform display (left/right channels)
- **Spectrum analyzer:** FFT via `simple-fft` (already in `core/src/3rdparty/simple-fft/`) → bar-graph frequency display
- **AY channel isolation:** When combined with `CHIP_STATE_AY`, show per-channel contribution (requires AY register decode, not raw PCM separation — visual only)

### 8.9 Input Injector Panel

Writes to the `INPUT_STATE` section (bidirectional, 16 bytes) to inject keyboard, joystick, and mouse events into the emulator.

#### Virtual Keyboard

- Visual ZX-Spectrum 40-key keyboard layout
- Click or use PC keyboard → writes to `INPUT_STATE` keyboard matrix (5 × 8 bits)
- Key combos: Caps Shift + Symbol Shift combinations
- Macro support: record and replay key sequences (useful for automated testing)

#### Joystick

- Virtual D-pad + fire button
- Kempston / Sinclair / Cursor protocol selection
- PC gamepad binding (via `QGamepad` or raw `miniaudio`-style polling)

#### Mouse (if Kempston mouse emulation active)

- Relative X/Y movement injection
- Button state

### 8.10 CLI Console Panel

Embedded telnet client connecting to the emulator's `AutomationCLI` server (TCP port 8765). Provides full access to 80+ CLI commands without leaving the observer app.

#### Implementation

- `QTcpSocket` connection to `localhost:8765` (or remote host)
- Telnet protocol handling (IAC negotiation, echo mode) — reuse logic from existing `AutomationCLI` server, client-side mirror
- `QPlainTextEdit` for output (monospace, ANSI color support optional)
- `QLineEdit` for command input with history (up/down arrow)
- Command completion: parse `help` output to build completion list
- Auto-reconnect on connection drop

#### Use Cases

- Toggle features: `feature monitoring on`, `feature debugmode on`
- Profiler control: `profiler opcode start`, `profiler memory start`
- Breakpoint management: `breakpoint add exec 0x1234`
- Disk operations: `disk insert A: myfile.trd`
- Snapshot management: `snapshot save state1.sna`
- Script execution: `lua dofile("test.lua")`

This panel complements the visual panels — anything not yet exposed as a monitoring section can still be accessed via CLI.

### 8.11 Call Trace / Opcode Trace Panel

Reads `CALL_TRACE` and `OPCODE_TRACE` sections for execution flow analysis.

#### Call Trace View

- Timeline visualization: horizontal scrolling timeline of CALL/RET events
- Call stack depth shown as vertical nesting
- Click to inspect: shows source address, target address, stack depth, timestamp
- Filter by address range or symbol name (if symbol map loaded)
- Highlights recursive calls and deep nesting

#### Opcode Trace View

- Recent execution trace: last N instructions from `OpcodeProfiler` ring
- Opcode frequency table: 256 × 7 prefix groups, sortable by count
- Heatmap visualization of opcode usage (which opcodes are hot)

### 8.12 Build & Project Structure

```
unreal-monitor/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── MonitorWindow.h / .cpp          — QMainWindow, docking orchestration
│   ├── connection/
│   │   ├── InstanceSelector.h / .cpp   — Instance list + connect UI
│   │   ├── WebAPIDiscovery.h / .cpp    — WebAPI-based discovery (reuse from screen-viewer)
│   │   ├── SHMProbe.h / .cpp           — Local SHM probing
│   │   └── MonitorConnection.h / .cpp  — SHM attach + FrameWaiter + manifest reader
│   ├── panels/
│   │   ├── LogViewerPanel.h / .cpp     — SPSC drain, filtering, search, export
│   │   ├── HeatmapPanel.h / .cpp       — Memory heatmap (from calltrace-viz)
│   │   ├── ChipStatePanel.h / .cpp     — Z80 / AY / FDC register display
│   │   ├── ScreenPanel.h / .cpp        — Framebuffer rendering
│   │   ├── SoundPanel.h / .cpp         — Audio playback + waveform + FFT
│   │   ├── InputPanel.h / .cpp         — Virtual keyboard, joystick, mouse
│   │   ├── TracePanel.h / .cpp         — Call trace + opcode trace
│   │   └── CLIConsolePanel.h / .cpp    — Embedded telnet client
│   ├── model/
│   │   ├── ManifestReader.h / .cpp     — Parse ManifestHeader + SectionDescriptors
│   │   ├── SectionReader.h / .cpp      — Epoch-safe section reads
│   │   ├── LogDrainer.h / .cpp         — SPSC ring consumer, session recording
│   │   └── ControlWriter.h / .cpp      — Write commands to CONTROL_RING
│   ├── rendering/
│   │   ├── HeatmapRenderer.h / .cpp    — QPainter grid rendering (from calltrace-viz)
│   │   ├── WaveformRenderer.h / .cpp   — Audio waveform drawing
│   │   └── SpectrumRenderer.h / .cpp   — FFT bar graph rendering
│   └── util/
│       ├── TelnetClient.h / .cpp       — Telnet protocol handler
│       └── SessionFile.h / .cpp        — Log session save/load
├── resources/
│   ├── icons/                          — Panel icons, toolbar icons
│   └── keyboard.svg                    — ZX Spectrum keyboard layout
```

#### Dependencies

| Library | Source | Purpose |
|---------|--------|---------|
| Qt6::Widgets | System | UI framework, docking |
| Qt6::Network | System | WebAPI client, telnet socket |
| Qt6::Multimedia | System | Audio device (optional, or miniaudio) |
| miniaudio | `core/src/3rdparty/miniaudio/` | Audio playback (preferred, matches emulator) |
| simple-fft | `core/src/3rdparty/simple-fft/` | Spectrum analyzer |
| shared_memory_region | `core/src/common/` | SHM attach (link or copy) |
| spsc_ringbuffer | `core/src/common/` | Ring buffer consumer side |
| manifest.h | `core/src/emulator/monitoring/` | Protocol structs (header-only) |

The app links against the monitoring protocol headers but does **not** link against `unrealng::core`. It's a pure observer — all data comes through shared memory and TCP.

### 8.13 Phased Delivery (integrated with main rollout)

| Main Phase | Observer Deliverable |
|------------|---------------------|
| Phase 1 (Foundation) | `MonitorWindow` skeleton + `InstanceSelector` + `MonitorConnection` + `ChipStatePanel` (Z80 only) + `CLIConsolePanel` |
| Phase 2 (Logging) | `LogViewerPanel` with full filter/search/export + session recording |
| Phase 3 (Media) | `ScreenPanel` + `SoundPanel` + `InputPanel` + `ChipStatePanel` (AY, FDC) |
| Phase 4 (Telemetry) | `HeatmapPanel` + `TracePanel` |
| Phase 5 (Hardening) | Layout persistence, multi-instance switching, polish, CI |

---

## 9. Open Questions & Future Considerations

| # | Question | Current Decision | Revisit When |
|---|----------|-----------------|--------------|
| 1 | Control channel latency | 20ms (frame polling) is acceptable | If observer needs sub-frame breakpoint injection |
| 2 | Heatmap snapshot cost | Z80 heatmap: 768KB (~100μs). Physical heatmap: ~60MB for all 323 pages (~8ms, 40% of frame). All inline in single SHM — up to ~65MB total is fine for modern machines. | Could optimize with delta-encoding or dirty-page tracking if needed, but straightforward memcpy is simplest to start |
| 3 | Framebuffer resize on mode change | SEPARATE_SHM with version counter + FramebufferSectionHeader. Observer re-attaches when size changes. Raster modes range from 256×192 (~196KB) to 512×240 (~492KB); full VID buffer with border is 448×320 (~573KB). Mode switches via `Screen::SetVideoMode()` at runtime. | Already handled — 6 raster modes exist (R_256_192 through R_512_240). Allocate for worst-case (573KB × 2 double-buffer) or reallocate on mode change. |
| 4 | Multiple observers | Only 1 consumer for SPSC ring buffer | If needed, switch to broadcast (SPMC) or per-observer rings |
| 5 | ModuleLogger migration | Coexistence; no forced migration | Gradually move hot-path logging to EMU_LOG_* |
| 6 | Log message format | printf-style (`%d`, `%s`) | Consider `{fmt}` library for type safety |

---

## 10. File Manifest

### Files to Create

| File | Purpose |
|------|---------|
| `core/src/emulator/monitoring/monitoringmanager.h` | Central coordinator class |
| `core/src/emulator/monitoring/monitoringmanager.cpp` | Implementation |
| `core/src/emulator/monitoring/manifest.h` | ManifestHeader, SectionDescriptor, SectionType |
| `core/src/common/shared_memory_region.h` | Cross-platform SHM abstraction |
| `core/src/common/shared_memory_region.cpp` | Implementation |
| `core/src/common/spsc_ringbuffer.h` | Lock-free SPSC ring buffer for IPC |
| `core/src/common/framenotifier.h` | Cross-platform frame signal (sem/eventfd/event) |
| `core/src/common/emulog.h` | EMU_LOG_* macros |
| `core/src/common/emulog_category.h` | CategoryState struct |
| `core/src/common/emulog_registry.h` | LoggerRegistry class declaration |
| `core/src/common/emulog_registry.cpp` | LoggerRegistry implementation |
| `tools/monitor-probe/main.cpp` | Minimal CLI observer (Phase 1) |
| `tools/log-viewer/main.cpp` | Log stream viewer CLI (Phase 2) |

#### Observer Application (`unreal-monitor/`)

| File | Purpose |
|------|---------|
| `unreal-monitor/CMakeLists.txt` | Qt6 build config |
| `unreal-monitor/src/main.cpp` | Entry point |
| `unreal-monitor/src/MonitorWindow.h/.cpp` | Main window, docking orchestration |
| `unreal-monitor/src/connection/InstanceSelector.h/.cpp` | Instance list + connect UI |
| `unreal-monitor/src/connection/WebAPIDiscovery.h/.cpp` | WebAPI-based instance discovery |
| `unreal-monitor/src/connection/SHMProbe.h/.cpp` | Local SHM probing (zero-config) |
| `unreal-monitor/src/connection/MonitorConnection.h/.cpp` | SHM attach + FrameWaiter + manifest reader |
| `unreal-monitor/src/panels/LogViewerPanel.h/.cpp` | Log stream drain, filters, search, export |
| `unreal-monitor/src/panels/HeatmapPanel.h/.cpp` | Memory heatmap visualization |
| `unreal-monitor/src/panels/ChipStatePanel.h/.cpp` | Z80 / AY / FDC register display |
| `unreal-monitor/src/panels/ScreenPanel.h/.cpp` | Live framebuffer rendering |
| `unreal-monitor/src/panels/SoundPanel.h/.cpp` | Audio playback + waveform + FFT |
| `unreal-monitor/src/panels/InputPanel.h/.cpp` | Virtual keyboard, joystick, mouse |
| `unreal-monitor/src/panels/TracePanel.h/.cpp` | Call trace + opcode trace |
| `unreal-monitor/src/panels/CLIConsolePanel.h/.cpp` | Embedded telnet client |
| `unreal-monitor/src/model/ManifestReader.h/.cpp` | Parse manifest + section descriptors |
| `unreal-monitor/src/model/SectionReader.h/.cpp` | Epoch-safe section reads |
| `unreal-monitor/src/model/LogDrainer.h/.cpp` | SPSC ring consumer, session recording |
| `unreal-monitor/src/model/ControlWriter.h/.cpp` | Write commands to CONTROL_RING |
| `unreal-monitor/src/rendering/HeatmapRenderer.h/.cpp` | QPainter grid rendering (from calltrace-viz) |
| `unreal-monitor/src/rendering/WaveformRenderer.h/.cpp` | Audio waveform drawing |
| `unreal-monitor/src/rendering/SpectrumRenderer.h/.cpp` | FFT bar graph rendering |
| `unreal-monitor/src/util/TelnetClient.h/.cpp` | Telnet protocol handler |
| `unreal-monitor/src/util/SessionFile.h/.cpp` | Log session save/load |

### Files to Modify

| File | Change |
|------|--------|
| `core/src/emulator/emulatorcontext.h` | Add `MonitoringManager* pMonitoringManager` |
| `core/src/emulator/emulator.cpp` | Create/initialize MonitoringManager in `Init()` |
| `core/src/emulator/mainloop.cpp` | Hook `onFrameEnd()` in `OnFrameEnd()` |
| `core/src/base/featuremanager.h` | Add `kMonitoring`, `kMonitoringAlias`, `kMonitoringDesc` |
| `core/src/emulator/io/fdc/wd1793.cpp` | Add EMU_LOG categories (Phase 2) |
| `CMakeLists.txt` | Add monitoring + emulog source files + `unreal-monitor` subdirectory |
