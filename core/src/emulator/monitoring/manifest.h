#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

/// @file manifest.h
/// Binary protocol for self-describing shared memory monitoring.
/// Designed for cross-process IPC: all structures are POD with explicit alignment,
/// no pointers, no virtual functions.

namespace monitoring {

/// Magic value: "UNRL" in little-endian
static constexpr uint32_t MANIFEST_MAGIC = 0x4C524E55;

/// Current protocol version
static constexpr uint16_t MANIFEST_VERSION = 1;

/// Sentinel epoch value indicating a section is being updated (writer active)
static constexpr uint64_t EPOCH_UPDATING = UINT64_MAX;

/// Initial capacity for section descriptor table
static constexpr uint16_t INITIAL_MAX_SECTIONS = 32;

// =============================================================================
// ManifestHeader — 128 bytes, 2 cache lines
// =============================================================================

struct alignas(64) ManifestHeader
{
    // ── Cache line 0: identity + global state ──
    uint32_t magic;                          // MANIFEST_MAGIC
    uint16_t version;                        // Protocol version
    uint16_t header_size;                    // sizeof(ManifestHeader) — for forward compat
    uint64_t total_size;                     // Total shared memory region size in bytes
    std::atomic<uint64_t> global_epoch;      // Bumped on every frame end
    std::atomic<uint64_t> frame_counter;     // Current emulator frame number
    uint32_t emulator_pid;                   // PID of emulator process
    uint32_t flags;                          // ManifestFlags bitmask
    uint16_t section_count;                  // Current number of section descriptors
    uint16_t max_sections;                   // Capacity of descriptor array
    uint32_t descriptor_size;                // sizeof(SectionDescriptor) — for forward compat

    // ── Cache line 1: layout tracking + reserved ──
    std::atomic<uint64_t> layout_epoch;      // Bumped when sections added/removed/resized
    uint32_t data_region_offset;             // Byte offset where section data blocks begin
    uint8_t reserved[52];                    // Future expansion
};

static_assert(sizeof(ManifestHeader) == 128, "ManifestHeader must be 128 bytes");

/// Flags for ManifestHeader::flags
enum ManifestFlags : uint32_t
{
    MANIFEST_RUNNING   = 0x0001,
    MANIFEST_PAUSED    = 0x0002,
    MANIFEST_STOPPED   = 0x0004,
    MANIFEST_RELOCATED = 0x0008,  // SHM was reallocated; read new name from reserved area
};

// =============================================================================
// SectionDescriptor — 64 bytes per section
// =============================================================================

/// Section type identifiers
enum class SectionType : uint16_t
{
    NONE            = 0x0000,

    // Existing external memory (separate SHM)
    MEMORY_PAGES    = 0x0001,

    // Chip state snapshots (epoch-protected, frame-synced)
    CHIP_STATE_Z80  = 0x0010,
    CHIP_STATE_AY   = 0x0011,
    CHIP_STATE_FDC  = 0x0012,

    // Input (bidirectional)
    INPUT_STATE     = 0x0020,

    // Telemetry
    LOG_STREAM      = 0x0100,
    HEATMAP_Z80     = 0x0101,
    HEATMAP_PHYS    = 0x0102,
    HEATMAP_PAGES   = 0x0103,
    OPCODE_TRACE    = 0x0110,
    CALL_TRACE      = 0x0111,

    // Media
    FRAMEBUFFER     = 0x0200,
    AUDIO_BUFFER    = 0x0201,

    // Control
    CONTROL_RING    = 0xFF00,

    // Configuration
    LOGGER_STATE    = 0x0300,

    // Analysis
    SEGMENTATION_MAP = 0x0120,
    PORT_ACTIVITY    = 0x0121,

    // User/plugin range: 0x1000–0x1FFF
    USER_BASE       = 0x1000,
};

/// Per-section flags
enum SectionFlags : uint16_t
{
    SECTION_ENABLED       = 0x0001,  // Section is actively updated
    SECTION_BIDIRECTIONAL = 0x0002,  // Observer can write (INPUT_STATE, CONTROL_RING)
    SECTION_SEPARATE_SHM  = 0x0004,  // Lives in a separate shared memory region
    SECTION_RING_BUFFER   = 0x0008,  // Section is a ring buffer (has head/tail pointers)
    SECTION_REMOVED       = 0x0010,  // Tombstone: section was removed, skip
};

struct alignas(64) SectionDescriptor
{
    SectionType type;                        // Section type enum
    uint16_t flags;                          // SectionFlags bitmask
    uint32_t offset;                         // Byte offset from SHM start (0 if SEPARATE_SHM)
    uint32_t length;                         // Section data size in bytes
    uint32_t reserved1;
    std::atomic<uint64_t> epoch;             // Per-section version; EPOCH_UPDATING = writer active
    char name[16];                           // Human-readable name ("z80_regs", "log", etc.)
    char shm_name[16];                       // SHM name if SEPARATE_SHM flag set, else empty
};

static_assert(sizeof(SectionDescriptor) == 64, "SectionDescriptor must be 64 bytes");

// =============================================================================
// Control Channel — embedded in CONTROL_RING section
// =============================================================================

/// Control command types (observer → emulator)
enum class ControlCommandType : uint8_t
{
    NONE = 0,
    ENABLE_SECTION,
    DISABLE_SECTION,
    SET_LOG_LEVEL,
    ENABLE_LOG_CATEGORY,
    DISABLE_LOG_CATEGORY,
    LIST_CATEGORIES,
    PING,

    // ModuleLogger commands (module/submodule granularity)
    // Field mapping: section_index=module_id, log_level=level,
    //   reserved=enable_flag, pattern[0..1]=submodule_mask (LE)
    SET_MODULE_ENABLED,      // Enable/disable a module + optional submodule
    SET_MODULE_LOG_LEVEL,    // Set log level: section_index=0 for global, 1-12 for per-module
    LIST_MODULES,            // Request module/submodule state dump
};

struct ControlCommand
{
    std::atomic<uint8_t> type;               // ControlCommandType
    uint8_t section_index;                   // Target section index
    uint8_t log_level;                       // Log level value
    uint8_t reserved;
    char pattern[60];                        // Category pattern: "fdc.*"
    std::atomic<bool> processed;             // Set by emulator when done
};

static constexpr size_t CONTROL_CMD_SLOTS = 4;

struct ControlBlock
{
    ControlCommand commands[CONTROL_CMD_SLOTS];
    std::atomic<uint32_t> write_idx;         // Observer increments (mod CONTROL_CMD_SLOTS)
    std::atomic<uint32_t> read_idx;          // Emulator increments (mod CONTROL_CMD_SLOTS)

    // Response area (emulator writes, observer reads)
    std::atomic<uint64_t> response_epoch;
    char response_data[2048];
};

// =============================================================================
// Z80 State Section — flat POD snapshot of Z80 registers
// =============================================================================

/// Flat snapshot of Z80 registers for monitoring.
/// 128 bytes, written once per frame by MonitoringManager.
struct alignas(64) Z80StateSnapshot
{
    // Main registers
    uint16_t af;
    uint16_t bc;
    uint16_t de;
    uint16_t hl;

    // Alternate registers
    uint16_t af_alt;
    uint16_t bc_alt;
    uint16_t de_alt;
    uint16_t hl_alt;

    // Index registers
    uint16_t ix;
    uint16_t iy;
    uint16_t sp;
    uint16_t pc;

    // Special registers
    uint16_t ir;             // I register (high) + R register (low)
    uint16_t memptr;         // Internal WZ register

    // Interrupt state
    uint8_t  iff1;
    uint8_t  iff2;
    uint8_t  im;             // Interrupt mode (0, 1, 2)
    uint8_t  halted;         // CPU is in HALT state

    // Timing
    uint32_t t;              // T-state within current frame (24-bit used)
    uint64_t total_tstates;  // Total T-states since reset

    // Debugging info
    uint16_t prev_pc;        // PC on previous cycle
    uint16_t m1_pc;          // PC when M1 cycle started

    // Flags decoded for convenience
    uint8_t  flag_s;         // Sign
    uint8_t  flag_z;         // Zero
    uint8_t  flag_h;         // Half carry
    uint8_t  flag_pv;        // Parity/Overflow
    uint8_t  flag_n;         // Subtract
    uint8_t  flag_c;         // Carry

    uint8_t  reserved[42];   // Pad to 128 bytes
};

static_assert(sizeof(Z80StateSnapshot) == 128, "Z80StateSnapshot must be 128 bytes");

// =============================================================================
// AY-8910 State Section — flat POD snapshot of AY registers
// =============================================================================

/// Flat snapshot of AY-8910 sound chip for monitoring.
/// 64 bytes, written once per frame by MonitoringManager.
struct alignas(64) AYStateSnapshot
{
    uint8_t  registers[16];      // AY registers R0-R15
    uint8_t  current_register;   // Currently selected register (#BFFD)
    uint8_t  chip_index;         // Which chip (0 = primary, 1 = TurboSound secondary)
    uint8_t  chip1_selected;     // 1 if software has selected chip 1 (TurboSound active)
    uint8_t  reserved1;

    // Tone generator state (decoded for observer convenience)
    uint16_t tone_period_a;      // Channel A tone period
    uint16_t tone_period_b;      // Channel B tone period
    uint16_t tone_period_c;      // Channel C tone period
    uint16_t noise_period;       // Noise generator period
    uint16_t envelope_period;    // Envelope period

    // Volumes (decoded from registers)
    uint8_t  volume_a;           // Channel A volume (0-15)
    uint8_t  volume_b;           // Channel B volume (0-15)
    uint8_t  volume_c;           // Channel C volume (0-15)
    uint8_t  envelope_shape;     // Envelope shape (0-15)

    // Mixer state (decoded from R7)
    uint8_t  tone_enable;        // Tone enable bits (bit 0=A, 1=B, 2=C)
    uint8_t  noise_enable;       // Noise enable bits (bit 0=A, 1=B, 2=C)

    // Per-register activity counters (written by emulator, read by companion for indicators)
    uint8_t  reg_write_counts[16]; // Write count per register this frame (0-255 saturating)
    uint16_t reg_read_mask;        // Bitmask: bit i set if register i was read this frame
};

static_assert(sizeof(AYStateSnapshot) == 64, "AYStateSnapshot must be 64 bytes");

// =============================================================================
// WD1793 FDC State Section — flat POD snapshot
// =============================================================================

/// Flat snapshot of WD1793 FDC state for monitoring.
/// 64 bytes, written once per frame by MonitoringManager.
struct alignas(64) FDCStateSnapshot
{
    // WD1793 registers
    uint8_t  command_register;   // Last command executed
    uint8_t  status_register;    // Current status register
    uint8_t  track_register;     // Current track position
    uint8_t  sector_register;    // Current sector
    uint8_t  data_register;      // Data register

    // Drive state
    uint8_t  selected_drive;     // Currently selected drive (0-3)
    uint8_t  side;               // Current side (0 or 1)
    uint8_t  fsm_state;          // Current FSM state (WDSTATE enum value)

    // Beta128 controller
    uint8_t  beta128_status;     // DRQ/INTRQ output flags
    uint8_t  last_command;       // Last decoded command (WD_COMMANDS enum)

    // Signal state
    uint8_t  intrq;              // Interrupt request active
    uint8_t  drq;                // Data request active

    // Error flags
    uint8_t  error_lost_data;
    uint8_t  error_crc;
    uint8_t  error_not_found;
    uint8_t  error_write_fault;

    // Disk presence per drive (bit 0 = drive A, etc.)
    uint8_t  disk_inserted;      // Bitmask of inserted disks

    uint8_t  reserved[47];       // Pad to 64 bytes
};

static_assert(sizeof(FDCStateSnapshot) == 64, "FDCStateSnapshot must be 64 bytes");

// =============================================================================
// Input State Section — bidirectional keyboard/joystick
// =============================================================================

/// Bidirectional input state. Observer can write to inject events.
/// 64 bytes, read/written every frame.
struct alignas(64) InputStateSnapshot
{
    // ZX Spectrum keyboard matrix (8 half-rows, active low)
    uint8_t  keyboard_matrix[8];

    // Kempston joystick state (active high, bits: 0=right, 1=left, 2=down, 3=up, 4=fire)
    uint8_t  kempston_joystick;

    uint8_t  reserved1[3];

    // Observer → Emulator: key injection commands
    // When inject_flags != 0, the emulator processes the command
    uint8_t  inject_flags;       // Bit 0: key press pending, Bit 1: key release pending
    uint8_t  inject_key;         // ZX key code to inject
    uint8_t  reserved2[2];

    uint8_t  reserved3[48];      // Pad to 64 bytes
};

static_assert(sizeof(InputStateSnapshot) == 64, "InputStateSnapshot must be 64 bytes");

// =============================================================================
// Framebuffer Section Header — precedes pixel data
// =============================================================================

/// Header for the FRAMEBUFFER section. Pixel data follows immediately after.
struct FramebufferSectionHeader
{
    uint16_t width;              // Framebuffer width in pixels
    uint16_t height;             // Framebuffer height in pixels
    uint8_t  video_mode;         // VideoModeEnum value
    uint8_t  pixel_format;       // 0 = RGBA8888 (32-bit)
    uint8_t  active_buffer;      // Double-buffer index (0 or 1)
    uint8_t  reserved1;
    uint32_t buffer_size;        // Size of one buffer in bytes
    uint32_t buffer_offset;      // Offset from section start to pixel data
};

static_assert(sizeof(FramebufferSectionHeader) == 16, "FramebufferSectionHeader must be 16 bytes");

// =============================================================================
// Audio Buffer Section Header — precedes PCM data
// =============================================================================

/// Header for the AUDIO_BUFFER section. PCM data follows immediately after.
struct AudioBufferSectionHeader
{
    uint32_t sampling_rate;      // 44100
    uint16_t channels;           // 2 (stereo)
    uint16_t bits_per_sample;    // 16 (int16_t)
    uint32_t samples_per_frame;  // 882 (44100/50)
    uint32_t buffer_size;        // Total PCM bytes (3528)
};

static_assert(sizeof(AudioBufferSectionHeader) == 16, "AudioBufferSectionHeader must be 16 bytes");

// =============================================================================
// Logger State Section — module enable/level configuration snapshot
// =============================================================================

/// Flat snapshot of ModuleLogger configuration for zero-cost SHM reads.
/// Written once per frame by MonitoringManager.
/// Mirror of MODULE_MAX_COUNT from platform.h — verified by static_assert in monitoringmanager.cpp.
static constexpr int LOGGER_MODULE_COUNT = 13;

struct alignas(64) LoggerStateSnapshot
{
    uint32_t module_enable_mask;                        // Bitmask: bit i = module i enabled
    uint16_t submodule_masks[LOGGER_MODULE_COUNT];      // Per-module submodule enable bitmasks
    uint8_t  module_levels[LOGGER_MODULE_COUNT];        // Per-module log level (0 = inherit global)
    uint8_t  global_level;                              // Global LoggerLevel enum value
    uint8_t  muted;                                     // 1 if logger is globally muted
    // Total: 4 + 26 + 13 + 1 + 1 = 45 bytes → pad to 64
    uint8_t  reserved[19];
};

static_assert(sizeof(LoggerStateSnapshot) == 64, "LoggerStateSnapshot must be 64 bytes");

// =============================================================================
// Memory Pages Section — full RAM/ROM snapshot (~5MB)
// =============================================================================

/// Constants for memory page layout (must match platform.h values)
static constexpr uint16_t MEMORY_MAX_PAGES = 323;   // 256 RAM + 2 CACHE + 1 MISC + 64 ROM
static constexpr uint32_t MEMORY_PAGE_SIZE = 16384;  // 16KB per page

/// Header for the MEMORY_PAGES section. Page data follows immediately after.
/// Written once per frame by MonitoringManager.
struct MemoryPagesSectionHeader
{
    uint16_t page_count;         // Number of pages (MEMORY_MAX_PAGES = 323)
    uint16_t page_size;          // Bytes per page (MEMORY_PAGE_SIZE = 16384)
    uint16_t ram_pages;          // Number of RAM pages (256)
    uint16_t rom_pages;          // Number of ROM pages (64)

    // Current bank mapping: which physical page is mapped to each 16KB Z80 bank
    uint16_t bank_pages[4];      // bank_pages[i] = physical page index for Z80 bank i

    uint8_t  reserved[8];        // Pad to 24 bytes
    // Immediately followed by:
    //   uint8_t page_data[page_count * page_size]  (~5,292,032 bytes)
};

static_assert(sizeof(MemoryPagesSectionHeader) == 24, "MemoryPagesSectionHeader must be 24 bytes");

/// Total size of the MEMORY_PAGES section (header + all page data)
static constexpr uint32_t MEMORY_PAGES_DATA_SIZE =
    sizeof(MemoryPagesSectionHeader) + MEMORY_MAX_PAGES * MEMORY_PAGE_SIZE;  // ~5,292,056 bytes

// =============================================================================
// Z80 Memory Heatmap Section — per-address R/W/X counters (768KB)
// =============================================================================

/// Z80 address space heatmap: 3 counter arrays (read, write, execute) × 65536 addresses.
/// Total: 786,432 bytes (768KB). Written once per frame by MonitoringManager
/// only when the MemoryAccessTracker session is active.
struct HeatmapZ80Header
{
    uint32_t address_count;      // Always 65536 (Z80 address space)
    uint8_t  session_active;     // 1 if memory tracking session is capturing
    uint8_t  reserved[3];
    // Immediately followed by:
    //   uint32_t read_counters[65536]     (256KB)
    //   uint32_t write_counters[65536]    (256KB)
    //   uint32_t execute_counters[65536]  (256KB)
};

static_assert(sizeof(HeatmapZ80Header) == 8, "HeatmapZ80Header must be 8 bytes");

/// Size of the full Z80 heatmap section including header and 3 counter arrays
static constexpr uint32_t HEATMAP_Z80_DATA_SIZE =
    sizeof(HeatmapZ80Header) + 3 * 65536 * sizeof(uint32_t);  // 786,440 bytes

// =============================================================================
// Page-Level Heatmap Section — per-page aggregated R/W/X counters (~4KB)
// =============================================================================

/// Per-physical-page aggregated heatmap: 3 counter arrays × MAX_PAGES (323) pages.
/// Compact summary for quick overview of active memory pages.
struct HeatmapPagesHeader
{
    uint16_t page_count;         // Number of pages (323)
    uint8_t  session_active;     // 1 if memory tracking session is capturing
    uint8_t  reserved[5];
    // Immediately followed by:
    //   uint32_t read_counters[page_count]
    //   uint32_t write_counters[page_count]
    //   uint32_t execute_counters[page_count]
};

static_assert(sizeof(HeatmapPagesHeader) == 8, "HeatmapPagesHeader must be 8 bytes");

// MAX_PAGES = 323 (from platform.h: 256 RAM + 2 CACHE + 1 MISC + 64 ROM)
static constexpr uint16_t HEATMAP_PAGE_COUNT = 323;
static constexpr uint32_t HEATMAP_PAGES_DATA_SIZE =
    sizeof(HeatmapPagesHeader) + 3 * HEATMAP_PAGE_COUNT * sizeof(uint32_t);  // ~3,884 bytes

// =============================================================================
// Physical Page Heatmap Section — per-address R/W/X counters for active pages
// =============================================================================

/// Per-page descriptor within the HEATMAP_PHYS section
struct HeatmapPhysPageDescriptor
{
    uint16_t abs_page_index;     // Absolute page index [0, MAX_PAGES)
    uint8_t  page_type;          // 0=RAM, 1=ROM, 2=CACHE, 3=MISC
    uint8_t  logical_page_num;   // Logical page number within type
    uint8_t  is_mapped;          // 1 if currently mapped to a Z80 bank
    int8_t   mapped_bank;        // Z80 bank (0-3) or -1 if not mapped
    uint8_t  reserved[2];
    // Immediately followed by:
    //   uint32_t read_counters[16384]    (64KB)
    //   uint32_t write_counters[16384]   (64KB)
    //   uint32_t execute_counters[16384] (64KB)
};

static_assert(sizeof(HeatmapPhysPageDescriptor) == 8, "HeatmapPhysPageDescriptor must be 8 bytes");

/// Header for the HEATMAP_PHYS section.
/// Contains per-address details for the top-N active physical pages.
struct HeatmapPhysHeader
{
    uint16_t active_page_count;  // Number of active pages in this snapshot
    uint16_t max_pages;          // Maximum pages this section can hold
    uint8_t  session_active;     // 1 if memory tracking session is capturing
    uint8_t  reserved[3];
    // Immediately followed by:
    //   active_page_count × { HeatmapPhysPageDescriptor + 3 × 16384 × uint32_t }
};

static_assert(sizeof(HeatmapPhysHeader) == 8, "HeatmapPhysHeader must be 8 bytes");

/// Max active pages per snapshot (limits section size to ~6MB)
static constexpr uint16_t HEATMAP_PHYS_MAX_PAGES = 32;
/// Size of one page entry: descriptor + 3 counter arrays (192KB per page)
static constexpr uint32_t HEATMAP_PHYS_PAGE_ENTRY_SIZE =
    sizeof(HeatmapPhysPageDescriptor) + 3 * 16384 * sizeof(uint32_t);
/// Total section size
static constexpr uint32_t HEATMAP_PHYS_DATA_SIZE =
    sizeof(HeatmapPhysHeader) + HEATMAP_PHYS_MAX_PAGES * HEATMAP_PHYS_PAGE_ENTRY_SIZE;  // ~6MB

// =============================================================================
// Opcode Trace Section — recent Z80 opcode execution trace
// =============================================================================

/// Flat POD trace entry matching OpcodeTraceEntry layout (16 bytes).
/// Used in the shared memory opcode trace ring buffer.
struct OpcodeTraceEntryPOD
{
    uint16_t pc;       // Program counter where opcode executed
    uint16_t prefix;   // Prefix code (0, 0xCB, 0xDD, 0xED, 0xFD, 0xDDCB, 0xFDCB)
    uint8_t  opcode;   // Opcode byte (0-255)
    uint8_t  flags;    // F register value
    uint8_t  a;        // A register value
    uint8_t  reserved; // Alignment padding
    uint32_t frame;    // Frame number
    uint32_t tState;   // T-state within frame
};

static_assert(sizeof(OpcodeTraceEntryPOD) == 16, "OpcodeTraceEntryPOD must be 16 bytes");

/// Header for the OPCODE_TRACE section.
/// Followed by an array of OpcodeTraceEntryPOD entries (ring buffer, newest first).
struct OpcodeTraceHeader
{
    uint32_t entry_count;        // Number of valid entries in the buffer
    uint32_t max_entries;        // Maximum capacity of the ring buffer
    uint64_t total_executions;   // Total opcode executions since session start
    uint8_t  session_active;     // 1 if opcode profiler is capturing
    uint8_t  reserved[7];
    // Immediately followed by:
    //   OpcodeTraceEntryPOD entries[entry_count]  (newest first)
};

static_assert(sizeof(OpcodeTraceHeader) == 24, "OpcodeTraceHeader must be 24 bytes");

/// Default number of trace entries to snapshot (last N, newest first)
static constexpr uint32_t OPCODE_TRACE_SNAPSHOT_COUNT = 1024;
static constexpr uint32_t OPCODE_TRACE_DATA_SIZE =
    sizeof(OpcodeTraceHeader) + OPCODE_TRACE_SNAPSHOT_COUNT * sizeof(OpcodeTraceEntryPOD);

// =============================================================================
// Call Trace Section — recent Z80 control flow events
// =============================================================================

/// Flat POD call trace entry (32 bytes). Simplified from Z80ControlFlowEvent
/// for cross-process IPC (no std::array, no bool — only fixed-size integers).
struct CallTraceEntryPOD
{
    uint16_t m1_pc;              // Instruction address (PC at M1 cycle)
    uint16_t target_addr;        // Jump/call/return target address
    uint8_t  opcode_bytes[4];    // Full instruction bytes (1-4)
    uint8_t  opcode_len;         // Number of valid bytes in opcode_bytes (1-4)
    uint8_t  flags;              // Z80 F register at execution
    uint8_t  type;               // Z80CFType enum value (JP=0, JR=1, CALL=2, etc.)
    uint8_t  reserved1;
    uint16_t sp;                 // Stack pointer after execution
    uint16_t stack_top[3];       // Top 3 stack values (for RET/RETI)
    uint32_t loop_count;         // Consecutive occurrences (loop compression)
};

static_assert(sizeof(CallTraceEntryPOD) == 24, "CallTraceEntryPOD must be 24 bytes");

/// Header for the CALL_TRACE section.
/// Followed by CallTraceEntryPOD entries (newest first).
struct CallTraceHeader
{
    uint32_t entry_count;        // Number of valid entries in the buffer
    uint32_t max_entries;        // Maximum capacity
    uint64_t cold_size;          // Total events in cold buffer
    uint64_t hot_size;           // Total events in hot buffer
    uint8_t  session_active;     // 1 if call trace is capturing
    uint8_t  reserved[7];
    // Immediately followed by:
    //   CallTraceEntryPOD entries[entry_count]  (newest first)
};

static_assert(sizeof(CallTraceHeader) == 32, "CallTraceHeader must be 32 bytes");

/// Default number of call trace entries to snapshot
static constexpr uint32_t CALL_TRACE_SNAPSHOT_COUNT = 512;
static constexpr uint32_t CALL_TRACE_DATA_SIZE =
    sizeof(CallTraceHeader) + CALL_TRACE_SNAPSHOT_COUNT * sizeof(CallTraceEntryPOD);

// =============================================================================
// Helper functions
// =============================================================================

/// Calculate byte offset to the start of section descriptor array
inline uint32_t descriptorsOffset(const ManifestHeader* header)
{
    return header->header_size;
}

/// Get pointer to the i-th section descriptor
inline SectionDescriptor* getDescriptor(void* shmBase, const ManifestHeader* header, uint16_t index)
{
    if (index >= header->section_count)
        return nullptr;
    auto* base = static_cast<char*>(shmBase);
    return reinterpret_cast<SectionDescriptor*>(base + header->header_size + index * header->descriptor_size);
}

/// Get pointer to the i-th section descriptor (const)
inline const SectionDescriptor* getDescriptor(const void* shmBase, const ManifestHeader* header, uint16_t index)
{
    if (index >= header->section_count)
        return nullptr;
    auto* base = static_cast<const char*>(shmBase);
    return reinterpret_cast<const SectionDescriptor*>(base + header->header_size + index * header->descriptor_size);
}

/// Get pointer to a section's data block
inline void* getSectionData(void* shmBase, const SectionDescriptor* desc)
{
    if (!desc || desc->offset == 0)
        return nullptr;
    return static_cast<char*>(shmBase) + desc->offset;
}

/// Get pointer to a section's data block (const)
inline const void* getSectionData(const void* shmBase, const SectionDescriptor* desc)
{
    if (!desc || desc->offset == 0)
        return nullptr;
    return static_cast<const char*>(shmBase) + desc->offset;
}

/// Epoch-safe read of a state section. Returns true if data was read without torn read.
/// @param desc Section descriptor to read from
/// @param shmBase Base of shared memory region
/// @param out Output buffer (must be at least desc->length bytes)
/// @param size Number of bytes to read
/// @return true if read was coherent (no concurrent write)
inline bool epochSafeRead(const SectionDescriptor* desc, const void* shmBase, void* out, size_t size)
{
    uint64_t before = desc->epoch.load(std::memory_order_acquire);
    if (before == EPOCH_UPDATING)
        return false;  // Writer active, try again later

    std::memcpy(out, getSectionData(shmBase, desc), size);

    std::atomic_thread_fence(std::memory_order_acquire);
    uint64_t after = desc->epoch.load(std::memory_order_relaxed);

    return (before == after);
}

}  // namespace monitoring
