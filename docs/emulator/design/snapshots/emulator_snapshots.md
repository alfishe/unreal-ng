# Emulator Snapshot Format - Technical Design Document

## Overview

The UnrealSpeccy-NG emulator snapshot format provides a complete, self-contained representation of the emulator state that can be restored on any compatible machine. The format uses a YAML manifest file (`snapshot.yaml`) that describes the snapshot contents and references all associated binary files.

## Design Goals

1. **Self-contained**: All files needed for restoration are included in a single folder
2. **Human-readable**: YAML manifest provides clear documentation of snapshot contents
3. **Extensible**: Format can accommodate new emulator features and platforms
4. **Backward compatible**: Versioned format allows for future evolution
5. **Cross-platform**: Works on any operating system with proper file paths
6. **Efficient**: Only stores memory pages that contain actual data
7. **Compressed**: Optional compression to reduce snapshot size while maintaining self-containment

## File Organization

```
snapshot_folder/
├── snapshot.yaml          # Main manifest file
├── memory/
│   ├── rom_0.bin         # ROM bank 0 (16KB)
│   ├── rom_1.bin         # ROM bank 1 (16KB)
│   ├── ram_0.bin         # RAM bank 0 (16KB)
│   ├── ram_2.bin         # RAM bank 2 (16KB)
│   ├── ram_5.bin         # RAM bank 5 (16KB)
│   ├── misc_0.bin        # MISC memory page (if used)
│   └── cache_0.bin       # CACHE memory page (if cache enabled)
├── media/
│   ├── disks/
│   │   ├── drive_a.trd   # Disk image for drive A
│   │   └── drive_b.trd   # Disk image for drive B
│   └── tapes/
│       └── tape.tap      # Tape image (if present)
├── screenshots/
│   ├── thumbnail.png     # Small preview image
│   └── fullscreen.png    # Full resolution screenshot
└── debug/
    ├── labels.sym        # Symbol file (optional)
    └── breakpoints.txt   # Debug breakpoints (optional)
```

**Distribution Note:**

The preferred method for distributing emulator snapshots is to use compression (7zip, .gz or similar). This packages the entire snapshot folder structure—including the manifest and all referenced files—into a single archive file, making sharing and storage more convenient. However, both compressed (single archive) and uncompressed (folder structure) snapshot formats are fully supported by the emulator.

## YAML Manifest Structure

[➡️ Example snapshot manifest (snapshot.yaml)](./snapshot.yaml)

### Version 1.8 Format (Example)

```yaml
# ZX Spectrum Emulator Snapshot Manifest
# Version: 1.8
---
metadata:
  manifest_version: "1.8"
  emulator_id: "UnrealSpeccy-NG"
  emulator_version: "0.40.0"
  host_platform: "macOS (Darwin) 15.5 (Sequoia)"
  emulated_platform: "ZX Spectrum 128K (Pentagon)"
  save_time: !!timestamp 2023-10-28T18:45:10Z
  description: |
    Snapshot for 'Dizzy Prince of the Yolkfolk'.
    Player is in the castle, just after solving the moat puzzle.

machine:
  model: PENTAGON
  ram_size_kb: 512
  timing: 
    preset: PENTAGON
    frame: 71680
    line: 224
    int: 50
    intstart: 13
    intlen: 32
    total_t_states: 4555555555  # Total t-states counter since emulator start
  config:
    even_m1: 0
    border_4t: 0
    floatbus: 0
    floatdos: 0
    portff: 0
  ula:
    border_color: 7
    frame_counter: 54321
    flash_state: false
    screen_mode: SCREEN_NORMAL  # SCREEN_NORMAL or SCREEN_SHADOW
    frame_t_states: 1234567     # Current T-state count within frame

memory:
  pages:
    rom_0: { file: "memory/rom_0.bin", checksum: { algorithm: "crc32", value: "0x12345678" } }
    rom_1: { file: "memory/rom_1.bin", checksum: { algorithm: "crc32", value: "0x23456789" } }
    rom_2: { file: "memory/rom_2.bin", checksum: { algorithm: "crc32", value: "0x3456789A" } }
    rom_3: { file: "memory/rom_3.bin", checksum: { algorithm: "crc32", value: "0x456789AB" } }
    ram_0: { file: "memory/ram_0.bin", checksum: { algorithm: "crc32", value: "0x56789ABC" } }
    ram_2: { file: "memory/ram_2.bin", checksum: { algorithm: "crc32", value: "0x6789ABCD" } }
    ram_5: { file: "memory/ram_5.bin", checksum: { algorithm: "crc32", value: "0x789ABCDE" } }
    misc_0: { file: "memory/misc_0.bin", checksum: { algorithm: "crc32", value: "0xABCDEF01" } }
    cache_0: { file: "memory/cache_0.bin", checksum: { algorithm: "crc32", value: "0xBCDEF012" } }
  memory_map:
    '0x0000': { type: ROM, bank: 0 }
    '0x4000': { type: RAM, bank: 5 }
    '0x8000': { type: RAM, bank: 2 }
    '0xC000': { type: RAM, bank: 0 }
  ports:
    0x7FFD: 0x00
    0x1FFD: 0x00
    0xDFFD: 0x00
    0x7EFD: 0x00
    0xFE: 0x7F
    0x1F: 0xFF
    0x37: 0xFF
    0x57: 0xFF
    0xFB: 0xFF
    0xFD: 0x0E
    0x3F: 0x3F
    0xBF: 0xFF
    0xFF: 0xFF

z80:
  registers:
    a: 0x1A
    f: 0b01000111
    b: 0x24
    c: 0x7A
    d: 0x33
    e: 0x33
    h: 0x5B
    l: 0x67
    af_: 0xFFFF
    bc_: 0xFFFF
    de_: 0xFFFF
    hl_: 0xFFFF
    ix: 0x8000
    iy: 0x5C3A
    pc: 0x5B68
    sp: 0xFC00
    i: 0x3F
    r: 0x7F
  interrupts:
    im: 1
    iff1: true
    iff2: true
    halted: false

peripherals:
  psg0:
    chip_type: YM2149F
    registers:
      R0:  0xFD
      R1:  0x0C
      R2:  0x00
      R3:  0x00
      R4:  0x00
      R5:  0x00
      R6:  0x10
      R7:  0xB8
      R8:  0x0F
      R9:  0x0F
      R10: 0x0F
      R11: 0x61
      R12: 0x00
      R13: 0x04
      R14: 0x00
      R15: 0x00
    selected_register: R14
  psg1:
    chip_type: YM2149F
    registers:
      R0:  0xFD
      R1:  0x0C
      R2:  0x00
      R3:  0x00
      R4:  0x00
      R5:  0x00
      R6:  0x10
      R7:  0xB8
      R8:  0x0F
      R9:  0x0F
      R10: 0x0F
      R11: 0x61
      R12: 0x00
      R13: 0x04
      R14: 0x00
      R15: 0x00
    selected_register: R2
  wd1793:
    command: 0xD8
    track:   0x1A
    sector:  0x03
    data:    0xE5
    status:  0b10010000
    motor_on: true
    head_loaded: true
    write_protect: false
    motor_timeout: 10240000
    index_counter: 1024
  keyboard:
    matrix: [0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]
  joystick:
    kempston: { up: false, down: false, left: false, right: false, fire: false }
    sinclair1: { up: false, down: false, left: false, right: false, fire: false }
    sinclair2: { up: false, down: false, left: false, right: false, fire: false }
  mouse:
    kempston: { x: 128, y: 128, buttons: 0 }
    ay: { x: 128, y: 128, buttons: 0 }

media:
  floppy_drives:
    - { drive_id: A, file: "media/disks/drive_a.trd", type: TRD, checksum: { algorithm: "crc32", value: "0xA1B2C3D4" }, write_protected: false, motor_on: true, track_head_position: 12 }
    - { drive_id: B, file: "media/disks/drive_b.trd", type: TRD, checksum: { algorithm: "crc32", value: "0xB2C3D4E5" }, write_protected: true, motor_on: false, track_head_position: 0 }
  tape_player:
    file: "media/tapes/tape.tap"
    checksum: { algorithm: "crc32", value: "0xC3D4E5F6" }
    position: 48912
    playing: false
    auto_start: true
    traps_enabled: true
  hard_disk:
    - { drive_id: 0, file: "media/hdd/harddisk.img", type: IDE, checksum: { algorithm: "crc32", value: "0xD4E5F607" }, size_mb: 40, read_only: false }

screenshots:
  thumbnail:
    format: PNG
    encoding: base64
    data: !!binary |
      iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAA...
  screen_only:
    format: PNG
    file: "screenshots/screen.png"
  fullscreen:
    format: PNG
    width: 352
    height: 288
    file: "screenshots/fullscreen.png"

debug:
  label_files:
    - "debug/labels.sym"
  embedded_labels:
    0x4000: "GameLoopStart"
    0x402A: "PlayerUpdate"
    0x5B68: "CurrentPCPosition"
  breakpoints:
    - { type: address, value: 0x5B6A, enabled: true, condition: "A == 0x1A" }
    - { type: physical, mem_type: ROM, page: 1, offset: 0x200, enabled: true }
  watchpoints:
    - { type: address, value: 0xC000, size: 2, read: true, write: true }
  call_stack:
    - 0x5B68
    - 0x402A
    - 0x4000

emulator_config:
  features:
    # (feature flags go here)
  debug_features:
    debug_mode: false
    memory_counters: true
    call_trace: false
  video:
    filter: "2xSAI"
    scanlines: 0
    border_size: 32
  sound:
    enabled: true
    sample_rate: 44100
    buffer_size: 1024
  input:
    mouse_type: KEMPSTON
    joystick_type: KEMPSTON
    keyboard_layout: default

## Supported Checksum Algorithms for Media Files

The `algorithm` field in the `checksum` structure for any media file must be one of the following options:

- **crc32**: 32-bit Cyclic Redundancy Check (fast, good for most files)
- **sha1**: SHA-1 hash (160-bit, cryptographically secure)
- **sha256**: SHA-256 hash (256-bit, highest security)
- **md5**: MD5 hash (128-bit, legacy support)
- **adler32**: Adler-32 checksum (fast, good for network transmission)

This matches the supported algorithms for memory pages, ensuring consistency and flexibility for file integrity verification.

## Supported Machine Models

Based on the existing emulator configuration, the following models are supported:

- **PENTAGON**: Pentagon 128K/256K/512K/1024K
- **SPECTRUM48**: ZX Spectrum 48K
- **SPECTRUM128**: ZX Spectrum 128K/+2A
- **PLUS3**: ZX Spectrum +2B/+3
- **TSL**: TS-Config
- **ATM3**: ATM Turbo 3.0
- **ATM710**: ATM Turbo 7.1.0
- **ATM450**: ATM Turbo 4.5.0
- **PROFI**: Profi 1024K
- **SCORP**: Scorpion ZS256
- **PROFSCORP**: Scorpion ZS256 + ProfROM
- **GMX**: GMX
- **KAY**: Kay 1024
- **QUORUM**: Quorum
- **LSY256**: LSY256
- **PHOENIX**: Phoenix

## RAM Size Support

The emulator supports various RAM configurations:
- 48KB (SPECTRUM48)
- 128KB (SPECTRUM128, PLUS3, PENTAGON)
- 256KB (SCORP, ATM450)
- 512KB (PENTAGON, ATM450)
- 1024KB (PENTAGON, ATM450, PROFI, ATM710)
- 2048KB (GMX)
- 4096KB (ATM3, TSL)

## File Format Compatibility

The snapshot format is designed to work alongside existing formats:
- **SNA**: Standard ZX Spectrum snapshot format
- **Z80**: Extended ZX Spectrum snapshot format
- **TRD**: TR-DOS disk format
- **TAP**: ZX Spectrum tape format

## Implementation Notes

### YAML Parsing

The snapshot loader uses the rapidyaml library for parsing the YAML manifest:

```cpp
#include "3rdparty/rapidyaml/ryml_all.hpp"

// Parse snapshot manifest
ryml::Tree tree = ryml::parse_in_place(yaml_content);
auto metadata = tree["metadata"];
std::string version = metadata["manifest_version"].val();
```

### Memory Page Loading

Pages are loaded individually based on the manifest with compression support:

```cpp
auto pages = tree["memory"]["pages"];
for (auto page : pages) {
    std::string pageName = page.key().str();
    std::string fileName = page["file"].val();
    std::string compression = page["compression"].val();
    
    // Parse checksum
    auto checksum = page["checksum"];
    std::string algorithm = checksum["algorithm"].val();
    std::string value = checksum["value"].val();
    
    // Load page file with compression handling
    LoadMemoryPage(fileName, pageName, algorithm, value, compression);
}
```

### Compression Handling

Different compression algorithms are supported:

```cpp
std::vector<uint8_t> LoadCompressedPage(const std::string& fileName, const std::string& compression) {
    if (compression == "none") {
        return LoadRawFile(fileName);
    } else if (compression == "gzip") {
        return LoadGzipFile(fileName);
    }
    throw std::runtime_error("Unsupported compression: " + compression);
}

std::vector<uint8_t> LoadGzipFile(const std::string& fileName) {
    // Use zlib or system gzip utilities
    // Implementation depends on available libraries
    return DecompressGzip(fileName);
}
```

### Port Loading

Ports are loaded from the ports section with their current values:

```cpp
auto ports = tree["memory"]["ports"];
for (auto port : ports) {
    uint16_t portNumber = std::stoul(port.key().str(), nullptr, 16);
    uint8_t portValue = port.val<uint8_t>();
    
    // Restore port state
    RestorePort(portNumber, portValue);
}
```

### Bank Determination

Bank numbers are determined from the memory_map section:

```cpp
auto memoryMap = tree["memory"]["memory_map"];
std::map<uint16_t, uint8_t> addressToBank;
for (auto region : memoryMap) {
    uint16_t start = std::stoul(region.key().str(), nullptr, 16);
    uint8_t bank = region["bank"].val<uint8_t>();
    addressToBank[start] = bank;
}
```

### Page Type Detection

Page types are determined from the section name:

```cpp
std::string GetPageType(const std::string& pageName) {
    if (pageName.substr(0, 3) == "rom") return "ROM";
    if (pageName.substr(0, 3) == "ram") return "RAM";
    if (pageName.substr(0, 4) == "misc") return "MISC";
    if (pageName.substr(0, 5) == "cache") return "CACHE";
    return "UNKNOWN";
}

uint8_t GetPageBank(const std::string& pageName) {
    // Extract bank number from page name (e.g., "ram_5" -> 5)
    size_t underscore = pageName.find('_');
    if (underscore != std::string::npos) {
        return std::stoul(pageName.substr(underscore + 1));
    }
    return 0;
}
```

### Checksum Verification

Different checksum algorithms are supported:

```cpp
bool VerifyChecksum(const std::string& algorithm, const std::string& expected, 
                   const uint8_t* data, size_t size) {
    if (algorithm == "crc32") {
        return CalculateCRC32(data, size) == std::stoul(expected, nullptr, 16);
    } else if (algorithm == "sha1") {
        return CalculateSHA1(data, size) == expected;
    } else if (algorithm == "sha256") {
        return CalculateSHA256(data, size) == expected;
    }
    // ... other algorithms
    return false;
}
```

### File Path Resolution

All file paths in the manifest are relative to the snapshot folder. The loader resolves paths as:

```cpp
std::string snapshotPath = GetSnapshotFolderPath();
std::string pageFile = snapshotPath + "/" + tree["memory"]["pages"]["ram_0"]["file"].val();
```

### Memory Layout

The memory map section describes the current paging configuration:

```cpp
auto memoryMap = tree["memory"]["memory_map"];
for (auto region : memoryMap) {
    uint16_t start = std::stoul(region.key().str(), nullptr, 16);
    std::string type = region["type"].val();
    uint8_t bank = region["bank"].val<uint8_t>();
    // Apply memory paging...
}
```

### Register Restoration

Z80 registers are restored in the correct order to maintain emulator state:

```cpp
auto z80 = tree["z80"]["registers"];
z80->a = z80["a"].val<uint8_t>();
z80->f = z80["f"].val<uint8_t>();
// ... restore all registers
```

## Memory Page Optimization

### Page Detection

The snapshot system can detect which pages contain actual data:

```cpp
bool IsPageUsed(uint8_t* pageData, size_t pageSize) {
    // Check if page contains non-zero data
    for (size_t i = 0; i < pageSize; i++) {
        if (pageData[i] != 0) return true;
    }
    return false;
}
```

### Compression Decision

The system can decide whether to compress a page based on content:

```cpp
std::string ChooseCompression(uint8_t* pageData, size_t pageSize) {
    // Check if page would benefit from compression
    if (IsPageCompressible(pageData, pageSize)) {
        return "gzip";
    }
    return "none";
}

bool IsPageCompressible(uint8_t* pageData, size_t pageSize) {
    // Simple heuristic: check for repeated patterns or low entropy
    // More sophisticated algorithms can be used
    return CalculateEntropy(pageData, pageSize) < 0.8;
}
```

### Checksum Calculation

Each page includes a checksum for integrity verification:

```cpp
std::string CalculatePageChecksum(const std::string& algorithm, 
                                 uint8_t* pageData, size_t pageSize) {
    if (algorithm == "crc32") {
        uint32_t crc = CalculateCRC32(pageData, pageSize);
        return "0x" + std::format("{:08X}", crc);
    } else if (algorithm == "sha1") {
        return CalculateSHA1(pageData, pageSize);
    }
    // ... other algorithms
    return "";
}
```

### Incremental Saves

The system can save only changed pages:

```cpp
void SaveChangedPages() {
    for (int bank = 0; bank < MAX_RAM_PAGES; bank++) {
        if (IsPageDirty(bank)) {
            std::string pageName = "ram_" + std::to_string(bank);
            std::string compression = ChooseCompression(GetPageData(bank), PAGE_SIZE);
            SaveMemoryPage(pageName, bank, compression);
            ClearDirtyFlag(bank);
        }
    }
}
```

## Compression Implementation

### GZIP Compression

GZIP compression is implemented using standard libraries:

```cpp
std::vector<uint8_t> CompressGzip(const uint8_t* data, size_t size) {
    // Use zlib or system gzip utilities
    // Implementation depends on available libraries
    return CompressWithGzip(data, size);
}

std::vector<uint8_t> DecompressGzip(const std::string& fileName) {
    // Read gzip file and decompress
    // Implementation depends on available libraries
    return DecompressGzipFile(fileName);
}
```

### Compression Benefits

- **RAM pages**: Typically 60-80% size reduction
- **ROM pages**: Typically 40-60% size reduction (less compressible due to code)
- **Overall snapshot**: 50-70% size reduction for typical snapshots

### Future Compression Algorithms

The format is designed to support additional compression algorithms:

- **7z**: LZMA compression (higher compression ratio, slower)
- **bzip2**: Burrows-Wheeler compression (good balance)
- **lz4**: Fast compression/decompression
- **zstd**: Modern compression with good ratio/speed balance

## Error Handling

The snapshot loader implements comprehensive error handling:

1. **Validation**: Check manifest version and required fields
2. **File existence**: Verify all referenced files exist
3. **Size validation**: Ensure binary files match expected sizes
4. **Checksum verification**: Verify page integrity
5. **Compression validation**: Verify compressed files can be decompressed
6. **Rollback**: If loading fails, restore previous emulator state

## Future Extensions

The format is designed to be extensible for future features:

- **Multiple screenshots**: Support for multiple screenshots with timestamps
- **Audio recording**: Include audio state or recordings
- **Network state**: For emulated network interfaces
- **Custom peripherals**: Support for user-defined hardware extensions
- **Advanced compression**: Support for 7z, bzip2, lz4, zstd
- **Differential snapshots**: Store only changes from a base snapshot

## Version Compatibility

- **Version 1.8**: Current format with per-page memory storage, flexible port structure, and compression support
- **Version 1.0-1.7**: Legacy formats (backward compatibility maintained)
- **Future versions**: Will maintain backward compatibility where possible

## Security Considerations

- **File validation**: All binary files are validated before loading
- **Path sanitization**: File paths are sanitized to prevent directory traversal
- **Size limits**: Maximum file sizes are enforced to prevent memory exhaustion
- **Checksums**: Page-level checksums for file integrity verification
- **Compression validation**: Compressed files are validated before decompression

## Performance Considerations

- **Lazy loading**: Large files (screenshots, debug data) are loaded on demand
- **Memory mapping**: Large binary files can be memory-mapped for efficiency
- **Page compression**: Individual pages can be compressed to reduce size
- **Incremental saves**: Only changed memory pages are saved
- **Parallel loading**: Multiple pages can be loaded in parallel
- **Compression caching**: Decompressed pages can be cached for faster access

### Debug Section Note

**Breakpoints, Labels, and Watchpoints Addressing:**

All debug entries (labels, breakpoints, watchpoints) use a uniform object structure for clarity and robust parsing. Each entry must specify its addressing mode:
- `{ type: address, value: 0xXXXX, ... }` for Z80 address space
- `{ type: physical, mem_type: RAM/ROM, page: N, offset: 0xYYY, ... }` for physical RAM/ROM page

**Examples:**
- Embedded label in Z80 address space: `{ type: address, value: 0x4000, name: "GameLoopStart" }`
- Embedded label in RAM page: `{ type: physical, mem_type: RAM, page: 2, offset: 0x100, name: "Ram2Routine" }`
- Breakpoint in Z80 address space: `{ type: address, value: 0x5B6A, enabled: true }`
- Breakpoint in ROM page: `{ type: physical, mem_type: ROM, page: 1, offset: 0x200, enabled: true }`
- Watchpoint in Z80 address space: `{ type: address, value: 0xC000, size: 2, read: true, write: true }`
- Watchpoint in RAM page: `{ type: physical, mem_type: RAM, page: 2, offset: 0x200, size: 4, read: true, write: false }`

**File-based and Embedded Debug Data:**
- Both embedded and file-based entries (label_files, breakpoint_files) are supported.
- If both are present, the emulator MUST merge them, with embedded entries taking precedence in case of conflict (e.g., same address/page).

---

### Memory Section Note

- Any RAM page not listed in `memory.pages` MUST be initialized to all zero bytes (0x00).
- Unlisted ROM pages are a critical error and MUST cause the loader to fail or prompt for the missing ROM.

---

### Peripheral State Completeness

For perfect restoration, the snapshot must capture not only register values but also internal state for each peripheral. For example:
- **AY/YM Sound Chip:** Envelope generator state (phase, step counter, current volume, direction), noise generator state (shift register, counter).
- **WD1793 FDC:** State machine phase, timers, busy counter, and any other internal counters required for precise command continuation.

---

### emulator_config.features Example

The `features` section under `emulator_config` is intended for boolean or enumerated feature flags. Example:
```yaml
features:
  turbo_mode: true
  magic_button_enabled: false
```

---

### Versioning and Compatibility Policy

- Unknown top-level keys MUST be ignored for forward compatibility.
- Breaking changes MUST increment the `manifest_version`. A changelog MUST be maintained.
- Older emulators encountering a newer version MAY warn or refuse to load, but MUST NOT crash.
- If a loader encounters an unknown field, it MUST ignore it unless it is required for correct operation in the current version.

## Memory Page Optimization

### Page Detection

The snapshot system can detect which pages contain actual data:

```cpp
bool IsPageUsed(uint8_t* pageData, size_t pageSize) {
    // Check if page contains non-zero data
    for (size_t i = 0; i < pageSize; i++) {
        if (pageData[i] != 0) return true;
    }
    return false;
}
```

### Compression Decision

The system can decide whether to compress a page based on content:

```cpp
std::string ChooseCompression(uint8_t* pageData, size_t pageSize) {
    // Check if page would benefit from compression
    if (IsPageCompressible(pageData, pageSize)) {
        return "gzip";
    }
    return "none";
}

bool IsPageCompressible(uint8_t* pageData, size_t pageSize) {
    // Simple heuristic: check for repeated patterns or low entropy
    // More sophisticated algorithms can be used
    return CalculateEntropy(pageData, pageSize) < 0.8;
}
```

### Checksum Calculation

Each page includes a checksum for integrity verification:

```cpp
std::string CalculatePageChecksum(const std::string& algorithm, 
                                 uint8_t* pageData, size_t pageSize) {
    if (algorithm == "crc32") {
        uint32_t crc = CalculateCRC32(pageData, pageSize);
        return "0x" + std::format("{:08X}", crc);
    } else if (algorithm == "sha1") {
        return CalculateSHA1(pageData, pageSize);
    }
    // ... other algorithms
    return "";
}
```

### Incremental Saves

The system can save only changed pages:

```cpp
void SaveChangedPages() {
    for (int bank = 0; bank < MAX_RAM_PAGES; bank++) {
        if (IsPageDirty(bank)) {
            std::string pageName = "ram_" + std::to_string(bank);
            std::string compression = ChooseCompression(GetPageData(bank), PAGE_SIZE);
            SaveMemoryPage(pageName, bank, compression);
            ClearDirtyFlag(bank);
        }
    }
}
```

## Compression Implementation

### GZIP Compression

GZIP compression is implemented using standard libraries:

```cpp
std::vector<uint8_t> CompressGzip(const uint8_t* data, size_t size) {
    // Use zlib or system gzip utilities
    // Implementation depends on available libraries
    return CompressWithGzip(data, size);
}

std::vector<uint8_t> DecompressGzip(const std::string& fileName) {
    // Read gzip file and decompress
    // Implementation depends on available libraries
    return DecompressGzipFile(fileName);
}
```

### Compression Benefits

- **RAM pages**: Typically 60-80% size reduction
- **ROM pages**: Typically 40-60% size reduction (less compressible due to code)
- **Overall snapshot**: 50-70% size reduction for typical snapshots

### Future Compression Algorithms

The format is designed to support additional compression algorithms:

- **7z**: LZMA compression (higher compression ratio, slower)
- **bzip2**: Burrows-Wheeler compression (good balance)
- **lz4**: Fast compression/decompression
- **zstd**: Modern compression with good ratio/speed balance

## Error Handling

The snapshot loader implements comprehensive error handling:

1. **Validation**: Check manifest version and required fields
2. **File existence**: Verify all referenced files exist
3. **Size validation**: Ensure binary files match expected sizes
4. **Checksum verification**: Verify page integrity
5. **Compression validation**: Verify compressed files can be decompressed
6. **Rollback**: If loading fails, restore previous emulator state

## Future Extensions

The format is designed to be extensible for future features:

- **Multiple screenshots**: Support for multiple screenshots with timestamps
- **Audio recording**: Include audio state or recordings
- **Network state**: For emulated network interfaces
- **Custom peripherals**: Support for user-defined hardware extensions
- **Advanced compression**: Support for 7z, bzip2, lz4, zstd
- **Differential snapshots**: Store only changes from a base snapshot

## Version Compatibility

- **Version 1.8**: Current format with per-page memory storage, flexible port structure, and compression support
- **Version 1.0-1.7**: Legacy formats (backward compatibility maintained)
- **Future versions**: Will maintain backward compatibility where possible

## Security Considerations

- **File validation**: All binary files are validated before loading
- **Path sanitization**: File paths are sanitized to prevent directory traversal
- **Size limits**: Maximum file sizes are enforced to prevent memory exhaustion
- **Checksums**: Page-level checksums for file integrity verification
- **Compression validation**: Compressed files are validated before decompression

## Performance Considerations

- **Lazy loading**: Large files (screenshots, debug data) are loaded on demand
- **Memory mapping**: Large binary files can be memory-mapped for efficiency
- **Page compression**: Individual pages can be compressed to reduce size
- **Incremental saves**: Only changed memory pages are saved
- **Parallel loading**: Multiple pages can be loaded in parallel
- **Compression caching**: Decompressed pages can be cached for faster access

### Debug Section Note

**Breakpoints, Labels, and Watchpoints Addressing:**

All debug entries (labels, breakpoints, watchpoints) use a uniform object structure for clarity and robust parsing. Each entry must specify its addressing mode:
- `{ type: address, value: 0xXXXX, ... }` for Z80 address space
- `{ type: physical, mem_type: RAM/ROM, page: N, offset: 0xYYY, ... }` for physical RAM/ROM page

**Examples:**
- Embedded label in Z80 address space: `{ type: address, value: 0x4000, name: "GameLoopStart" }`
- Embedded label in RAM page: `{ type: physical, mem_type: RAM, page: 2, offset: 0x100, name: "Ram2Routine" }`
- Breakpoint in Z80 address space: `{ type: address, value: 0x5B6A, enabled: true }`
- Breakpoint in ROM page: `{ type: physical, mem_type: ROM, page: 1, offset: 0x200, enabled: true }`
- Watchpoint in Z80 address space: `{ type: address, value: 0xC000, size: 2, read: true, write: true }`
- Watchpoint in RAM page: `{ type: physical, mem_type: RAM, page: 2, offset: 0x200, size: 4, read: true, write: false }`

**File-based and Embedded Debug Data:**
- Both embedded and file-based entries (label_files, breakpoint_files) are supported.
- If both are present, the emulator MUST merge them, with embedded entries taking precedence in case of conflict (e.g., same address/page).

---

### Memory Section Note

- Any RAM page not listed in `memory.pages` MUST be initialized to all zero bytes (0x00).
- Unlisted ROM pages are a critical error and MUST cause the loader to fail or prompt for the missing ROM.

---

### Peripheral State Completeness

For perfect restoration, the snapshot must capture not only register values but also internal state for each peripheral. For example:
- **AY/YM Sound Chip:** Envelope generator state (phase, step counter, current volume, direction), noise generator state (shift register, counter).
- **WD1793 FDC:** State machine phase, timers, busy counter, and any other internal counters required for precise command continuation.

---

### emulator_config.features Example

The `features` section under `emulator_config` is intended for boolean or enumerated feature flags. Example:
```yaml
features:
  turbo_mode: true
  magic_button_enabled: false
```

---

### Versioning and Compatibility Policy

- Unknown top-level keys MUST be ignored for forward compatibility.
- Breaking changes MUST increment the `manifest_version`. A changelog MUST be maintained.
- Older emulators encountering a newer version MAY warn or refuse to load, but MUST NOT crash.
- If a loader encounters an unknown field, it MUST ignore it unless it is required for correct operation in the current version.

## Memory Page Optimization

### Page Detection

The snapshot system can detect which pages contain actual data:

```cpp
bool IsPageUsed(uint8_t* pageData, size_t pageSize) {
    // Check if page contains non-zero data
    for (size_t i = 0; i < pageSize; i++) {
        if (pageData[i] != 0) return true;
    }
    return false;
}
```

### Compression Decision

The system can decide whether to compress a page based on content:

```cpp
std::string ChooseCompression(uint8_t* pageData, size_t pageSize) {
    // Check if page would benefit from compression
    if (IsPageCompressible(pageData, pageSize)) {
        return "gzip";
    }
    return "none";
}

bool IsPageCompressible(uint8_t* pageData, size_t pageSize) {
    // Simple heuristic: check for repeated patterns or low entropy
    // More sophisticated algorithms can be used
    return CalculateEntropy(pageData, pageSize) < 0.8;
}
```

### Checksum Calculation

Each page includes a checksum for integrity verification:

```cpp
std::string CalculatePageChecksum(const std::string& algorithm, 
                                 uint8_t* pageData, size_t pageSize) {
    if (algorithm == "crc32") {
        uint32_t crc = CalculateCRC32(pageData, pageSize);
        return "0x" + std::format("{:08X}", crc);
    } else if (algorithm == "sha1") {
        return CalculateSHA1(pageData, pageSize);
    }
    // ... other algorithms
    return "";
}
```

### Incremental Saves

The system can save only changed pages:

```cpp
void SaveChangedPages() {
    for (int bank = 0; bank < MAX_RAM_PAGES; bank++) {
        if (IsPageDirty(bank)) {
            std::string pageName = "ram_" + std::to_string(bank);
            std::string compression = ChooseCompression(GetPageData(bank), PAGE_SIZE);
            SaveMemoryPage(pageName, bank, compression);
            ClearDirtyFlag(bank);
        }
    }
}
```

## Compression Implementation

### GZIP Compression

GZIP compression is implemented using standard libraries:

```cpp
std::vector<uint8_t> CompressGzip(const uint8_t* data, size_t size) {
    // Use zlib or system gzip utilities
    // Implementation depends on available libraries
    return CompressWithGzip(data, size);
}

std::vector<uint8_t> DecompressGzip(const std::string& fileName) {
    // Read gzip file and decompress
    // Implementation depends on available libraries
    return DecompressGzipFile(fileName);
}
```

### Compression Benefits

- **RAM pages**: Typically 60-80% size reduction
- **ROM pages**: Typically 40-60% size reduction (less compressible due to code)
- **Overall snapshot**: 50-70% size reduction for typical snapshots

### Future Compression Algorithms

The format is designed to support additional compression algorithms:

- **7z**: LZMA compression (higher compression ratio, slower)
- **bzip2**: Burrows-Wheeler compression (good balance)
- **lz4**: Fast compression/decompression
- **zstd**: Modern compression with good ratio/speed balance

## Error Handling

The snapshot loader implements comprehensive error handling:

1. **Validation**: Check manifest version and required fields
2. **File existence**: Verify all referenced files exist
3. **Size validation**: Ensure binary files match expected sizes
4. **Checksum verification**: Verify page integrity
5. **Compression validation**: Verify compressed files can be decompressed
6. **Rollback**: If loading fails, restore previous emulator state

## Future Extensions

The format is designed to be extensible for future features:

- **Multiple screenshots**: Support for multiple screenshots with timestamps
- **Audio recording**: Include audio state or recordings
- **Network state**: For emulated network interfaces
- **Custom peripherals**: Support for user-defined hardware extensions
- **Advanced compression**: Support for 7z, bzip2, lz4, zstd
- **Differential snapshots**: Store only changes from a base snapshot

## Version Compatibility

- **Version 1.8**: Current format with per-page memory storage, flexible port structure, and compression support
- **Version 1.0-1.7**: Legacy formats (backward compatibility maintained)
- **Future versions**: Will maintain backward compatibility where possible

## Security Considerations

- **File validation**: All binary files are validated before loading
- **Path sanitization**: File paths are sanitized to prevent directory traversal
- **Size limits**: Maximum file sizes are enforced to prevent memory exhaustion
- **Checksums**: Page-level checksums for file integrity verification
- **Compression validation**: Compressed files are validated before decompression

## Performance Considerations

- **Lazy loading**: Large files (screenshots, debug data) are loaded on demand
- **Memory mapping**: Large binary files can be memory-mapped for efficiency
- **Page compression**: Individual pages can be compressed to reduce size
- **Incremental saves**: Only changed memory pages are saved
- **Parallel loading**: Multiple pages can be loaded in parallel
- **Compression caching**: Decompressed pages can be cached for faster access

### Debug Section Note

**Breakpoints, Labels, and Watchpoints Addressing:**

All debug entries (labels, breakpoints, watchpoints) use a uniform object structure for clarity and robust parsing. Each entry must specify its addressing mode:
- `{ type: address, value: 0xXXXX, ... }` for Z80 address space
- `{ type: physical, mem_type: RAM/ROM, page: N, offset: 0xYYY, ... }` for physical RAM/ROM page

**Examples:**
- Embedded label in Z80 address space: `{ type: address, value: 0x4000, name: "GameLoopStart" }`
- Embedded label in RAM page: `{ type: physical, mem_type: RAM, page: 2, offset: 0x100, name: "Ram2Routine" }`
- Breakpoint in Z80 address space: `{ type: address, value: 0x5B6A, enabled: true }`
- Breakpoint in ROM page: `{ type: physical, mem_type: ROM, page: 1, offset: 0x200, enabled: true }`
- Watchpoint in Z80 address space: `{ type: address, value: 0xC000, size: 2, read: true, write: true }`
- Watchpoint in RAM page: `{ type: physical, mem_type: RAM, page: 2, offset: 0x200, size: 4, read: true, write: false }`

**File-based and Embedded Debug Data:**
- Both embedded and file-based entries (label_files, breakpoint_files) are supported.
- If both are present, the emulator MUST merge them, with embedded entries taking precedence in case of conflict (e.g., same address/page).

---

### Memory Section Note

- Any RAM page not listed in `memory.pages` MUST be initialized to all zero bytes (0x00).
- Unlisted ROM pages are a critical error and MUST cause the loader to fail or prompt for the missing ROM.

---

### Peripheral State Completeness

For perfect restoration, the snapshot must capture not only register values but also internal state for each peripheral. For example:
- **AY/YM Sound Chip:** Envelope generator state (phase, step counter, current volume, direction), noise generator state (shift register, counter).
- **WD1793 FDC:** State machine phase, timers, busy counter, and any other internal counters required for precise command continuation.

---

### emulator_config.features Example

The `features` section under `emulator_config` is intended for boolean or enumerated feature flags. Example:
```yaml
features:
  turbo_mode: true
  magic_button_enabled: false
```

---

### Versioning and Compatibility Policy

- Unknown top-level keys MUST be ignored for forward compatibility.
- Breaking changes MUST increment the `manifest_version`. A changelog MUST be maintained.
- Older emulators encountering a newer version MAY warn or refuse to load, but MUST NOT crash.
- If a loader encounters an unknown field, it MUST ignore it unless it is required for correct operation in the current version. 