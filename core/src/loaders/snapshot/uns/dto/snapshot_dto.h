#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// --- Metadata ---
struct MetadataDTO
{
    std::string manifest_version;   // Manifest format version
    std::string emulator_id;        // Emulator identifier
    std::string emulator_version;   // Emulator version
    std::string host_platform;      // Host OS/platform
    std::string emulated_platform;  // Emulated machine/platform
    std::string save_time;          // ISO8601 timestamp
    std::string description;        // User description
};

// --- Machine State ---
struct TimingDTO
{
    std::string preset;       // Timing preset name
    uint32_t frame;           // Frame count
    uint32_t line;            // Line count
    uint32_t int_period;      // Interrupt period
    uint32_t intstart;        // Interrupt start
    uint32_t intlen;          // Interrupt length
    uint64_t total_t_states;  // Total T-states since start
};

struct MachineConfigDTO
{
    int even_m1;    // Even M1 flag
    int border_4t;  // Border 4T flag
    int floatbus;   // Floatbus flag
    int floatdos;   // FloatDOS flag
    int portff;     // Port FF flag
};

struct UlaDTO
{
    int border_color;         // Border color
    uint32_t frame_counter;   // Frame counter
    bool flash_state;         // Flash state
    std::string screen_mode;  // Screen mode
    uint32_t frame_t_states;  // T-states in current frame
};

struct MachineDTO
{
    std::string model;        // Machine model
    uint32_t ram_size_kb;     // RAM size in KB
    TimingDTO timing;         // Timing info
    MachineConfigDTO config;  // Machine config
    UlaDTO ula;               // ULA state
};

// --- Memory ---
struct MemoryPageDTO
{
    std::string file;  // Page file path
    struct
    {
        std::string algorithm;  // Checksum algorithm
        std::string value;      // Checksum value
    } checksum;                 // Checksum info
};

struct MemoryMapEntryDTO
{
    std::string type;  // "ROM" or "RAM"
    int bank;          // Bank number
};

struct MemoryDTO
{
    std::map<std::string, MemoryPageDTO> pages;           // Map of page name to page info
    std::map<std::string, MemoryMapEntryDTO> memory_map;  // Address to memory map entry
    std::map<std::string, uint8_t> ports;                 // Port values
};

// --- Z80 State ---
struct Z80RegistersDTO
{
    uint16_t af, bc, de, hl;         // Main register pairs
    uint16_t af_, bc_, de_, hl_;     // Shadow register pairs
    uint16_t ix, iy, pc, sp;         // Index, PC, SP
    uint8_t i, r;                    // Interrupt, refresh
};

struct Z80InterruptsDTO
{
    int im;       // Interrupt mode
    bool iff1;    // IFF1 flag
    bool iff2;    // IFF2 flag
    bool halted;  // CPU halted flag
};

struct Z80DTO
{
    Z80RegistersDTO registers;    // Z80 registers
    Z80InterruptsDTO interrupts;  // Z80 interrupt state
};

// --- PSG (AY/YM) State ---
struct PSGEnvelopeDTO
{
    int phase;              // Envelope phase
    int step_counter;       // Envelope step counter
    int current_volume;     // Envelope current volume
    std::string direction;  // Envelope direction
};

struct PSGToneDTO
{
    int counter_a;  // Tone counter A
    int counter_b;  // Tone counter B
    int counter_c;  // Tone counter C
    int output_a;   // Output state A
    int output_b;   // Output state B
    int output_c;   // Output state C
};

struct PSGNoiseDTO
{
    uint32_t shift_register;  // Noise shift register
    int counter;              // Noise counter
};

struct PSGDTO
{
    std::string chip_type;                     // PSG chip type
    std::map<std::string, uint8_t> registers;  // PSG registers
    std::string selected_register;             // Selected register
    PSGEnvelopeDTO envelope;                   // Envelope state
    PSGToneDTO tone;                           // Tone state
    PSGNoiseDTO noise;                         // Noise state
};

// --- WD1793 FDC State ---
struct WD1793StateMachineDTO
{
    int phase;         // State machine phase
    int timer;         // State machine timer
    int busy_counter;  // Busy counter
};

struct WD1793DTO
{
    uint8_t command;                      // Last command
    uint8_t track;                        // Track register
    uint8_t sector;                       // Sector register
    uint8_t data;                         // Data register
    uint8_t status;                       // Status register
    bool motor_on;                        // Motor on flag
    bool head_loaded;                     // Head loaded flag
    bool write_protect;                   // Write protect flag
    uint32_t motor_timeout;               // Motor timeout
    uint32_t index_counter;               // Index counter
    WD1793StateMachineDTO state_machine;  // Internal state
};

// --- Peripherals ---
struct PeripheralsDTO
{
    PSGDTO psg0;       // First PSG chip
    PSGDTO psg1;       // Second PSG chip (TurboSound)
    WD1793DTO wd1793;  // Floppy Disk Controller
    // ...keyboard, joystick, mouse, etc.
};

// --- Media ---
struct FloppyDriveDTO
{
    std::string drive_id;  // Drive identifier (A/B)
    std::string file;      // Disk image file
    std::string type;      // Disk type (TRD, etc.)
    struct
    {
        std::string algorithm;  // Checksum algorithm
        std::string value;      // Checksum value
    } checksum;                 // Checksum info
    bool write_protected;       // Write-protect flag
    bool motor_on;              // Motor on flag
    int track_head_position;    // Track/head position
};

struct TapePlayerDTO
{
    std::string file;  // Tape image file
    struct
    {
        std::string algorithm;  // Checksum algorithm
        std::string value;      // Checksum value
    } checksum;                 // Checksum info
    int position;               // Tape position
    bool playing;               // Playing flag
    bool auto_start;            // Auto-start flag
    bool traps_enabled;         // Traps enabled flag
};

struct HardDiskDTO
{
    int drive_id;      // Hard disk ID
    std::string file;  // Hard disk image file
    std::string type;  // Disk type (IDE, etc.)
    struct
    {
        std::string algorithm;  // Checksum algorithm
        std::string value;      // Checksum value
    } checksum;                 // Checksum info
    int size_mb;                // Disk size in MB
    bool read_only;             // Read-only flag
};

struct MediaDTO
{
    std::vector<FloppyDriveDTO> floppy_drives;  // List of floppy drives
    std::optional<TapePlayerDTO> tape_player;   // Optional tape player
    std::vector<HardDiskDTO> hard_disks;        // List of hard disks
};

// --- Debug Info ---
struct DebugLabelDTO
{
    std::string type;      // "address" or "physical"
    uint32_t value;        // Address (if type==address)
    std::string name;      // Label name
    std::string mem_type;  // "RAM" or "ROM" (if physical)
    int page;              // Page number (if physical)
    int offset;            // Offset in page (if physical)
};

struct BreakpointDTO
{
    std::string type;       // "address" or "physical"
    uint32_t value;         // Address (if type==address)
    std::string mem_type;   // "RAM" or "ROM" (if physical)
    int page;               // Page number (if physical)
    int offset;             // Offset in page (if physical)
    bool enabled;           // Breakpoint enabled
    std::string condition;  // Optional condition
};

struct WatchpointDTO
{
    std::string type;      // "address" or "physical"
    uint32_t value;        // Address (if type==address)
    std::string mem_type;  // "RAM" or "ROM" (if physical)
    int page;              // Page number (if physical)
    int offset;            // Offset in page (if physical)
    int size;              // Size in bytes
    bool read;             // Read watch
    bool write;            // Write watch
};

struct DebugDTO
{
    std::vector<std::string> label_files;        // List of label files
    std::vector<DebugLabelDTO> embedded_labels;  // Embedded labels
    std::vector<std::string> breakpoint_files;   // List of breakpoint files
    std::vector<BreakpointDTO> breakpoints;      // Embedded breakpoints
    std::vector<WatchpointDTO> watchpoints;      // Embedded watchpoints
    std::vector<uint32_t> call_stack;            // Call stack addresses
};

// --- Emulator Config ---
struct EmulatorFeaturesDTO
{
    bool turbo_mode;            // Turbo mode enabled
    bool magic_button_enabled;  // Magic button enabled
    // ...other flags
};

// --- Emulator Config Subsections (Stubs) ---
struct DebugFeaturesDTO
{
    bool debug_mode;        // Debug mode enabled
    bool memory_counters;   // Memory counters enabled
    bool call_trace;        // Call trace enabled
    // ...add more as needed
};

struct VideoConfigDTO
{
    std::string filter;     // Video filter name
    int scanlines;          // Scanlines setting
    int border_size;        // Border size in pixels
    // ...add more as needed
};

struct SoundConfigDTO
{
    bool enabled;           // Sound enabled
    int sample_rate;        // Audio sample rate
    int buffer_size;        // Audio buffer size
    // ...add more as needed
};

struct InputConfigDTO
{
    std::string mouse_type;         // Mouse type (e.g., KEMPSTON)
    std::string joystick_type;      // Joystick type (e.g., KEMPSTON)
    std::string keyboard_layout;    // Keyboard layout
    // ...add more as needed
};

struct EmulatorConfigDTO
{
    EmulatorFeaturesDTO features;      // Emulator features
    DebugFeaturesDTO debug_features;   // Debug features
    VideoConfigDTO video;              // Video config
    SoundConfigDTO sound;              // Sound config
    InputConfigDTO input;              // Input config
};

// --- The Root Snapshot DTO ---
struct SnapshotDTO
{
    MetadataDTO metadata;               // Metadata section
    MachineDTO machine;                 // Machine section
    MemoryDTO memory;                   // Memory section
    Z80DTO z80;                         // Z80 CPU state
    PeripheralsDTO peripherals;         // Peripherals state
    MediaDTO media;                     // Media devices
    DebugDTO debug;                     // Debug info
    EmulatorConfigDTO emulator_config;  // Emulator config
    // ...screenshots, etc.
};