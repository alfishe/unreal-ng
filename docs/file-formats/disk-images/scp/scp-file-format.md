# SCP (SuperCard Pro) File Format Documentation

## Overview

The SCP (SuperCard Pro) format is a raw flux capture format used for storing magnetic flux transitions from floppy disks. It was developed by Jim Drew for the SuperCard Pro hardware and is widely supported by various floppy disk imaging tools.

## File Structure

### 1. File Header (16 bytes)

The SCP file begins with a 16-byte header containing metadata about the disk image:

```c
struct SCPHeader {
    char signature[3];     // "SCP" - file signature
    uint8_t version;       // Format version (0 = v2.0)
    uint8_t disk_type;     // Disk type identifier
    uint8_t revolutions;   // Number of revolutions per track
    uint8_t start_track;   // First track number
    uint8_t end_track;     // Last track number
    uint8_t flags;         // Header flags
    uint8_t cell_width;    // 16-bit cell width
    uint8_t heads;         // Number of heads (0=auto)
    uint8_t resolution;    // Capture resolution (0=25ns)
    uint32_t checksum;     // Checksum of remaining data
};
```

#### Header Flags (SCPHeaderFlags)

```c
enum SCPHeaderFlags {
    INDEXED       = 1<<0,  // Image used index mark to cue tracks
    TPI_96        = 1<<1,  // Drive is 96 TPI, otherwise 48 TPI
    RPM_360       = 1<<2,  // Drive is 360RPM, otherwise 300RPM
    NORMALISED    = 1<<3,  // Flux has been normalized, otherwise raw
    READWRITE     = 1<<4,  // Image is read/write capable, otherwise read-only
    FOOTER        = 1<<5,  // Image contains extension footer
    EXTENDED_MODE = 1<<6,  // Extended type for other media
    FLUX_CREATOR  = 1<<7   // Created by non-SuperCard Pro device
};
```

#### Disk Types

The `disk_type` field identifies the type of disk:

```c
enum DiskType {
    c64 = 0x00,           // Commodore 64
    amiga = 0x04,         // Amiga
    amigahd = 0x08,       // Amiga HD
    atari800_sd = 0x10,   // Atari 800 SD
    atari800_dd = 0x11,   // Atari 800 DD
    atari800_ed = 0x12,   // Atari 800 ED
    atarist_ss = 0x14,    // Atari ST SS
    atarist_ds = 0x15,    // Atari ST DS
    appleii = 0x20,       // Apple II
    appleiipro = 0x21,    // Apple II Pro
    apple_400k = 0x24,    // Apple 400K
    apple_800k = 0x25,    // Apple 800K
    apple_1m44 = 0x26,    // Apple 1.44MB
    ibmpc_360k = 0x30,    // IBM PC 360K
    ibmpc_720k = 0x31,    // IBM PC 720K
    ibmpc_1m2 = 0x32,     // IBM PC 1.2MB
    ibmpc_1m44 = 0x33,    // IBM PC 1.44MB
    trs80_sssd = 0x40,    // TRS-80 SSSD
    trs80_ssdd = 0x41,    // TRS-80 SSDD
    trs80_dssd = 0x42,    // TRS-80 DSSD
    trs80_dsdd = 0x43,    // TRS-80 DSDD
    ti_99_4a = 0x50,      // TI-99/4A
    roland_d20 = 0x60,    // Roland D20
    amstrad_cpc = 0x70,   // Amstrad CPC
    other_320k = 0x80,    // Other 320K
    other_1m2 = 0x81,     // Other 1.2MB
    other_720k = 0x84,    // Other 720K
    other_1m44 = 0x85,    // Other 1.44MB
    tape_gcr1 = 0xe0,     // Tape GCR1
    tape_gcr2 = 0xe1,     // Tape GCR2
    tape_mfm = 0xe2,      // Tape MFM
    hdd_mfm = 0xf0,       // Hard Disk MFM
    hdd_rll = 0xf1        // Hard Disk RLL
};
```

### 2. Track Lookup Table (TLUT) - 672 bytes

Following the header is a Track Lookup Table (TLUT) containing 168 32-bit offsets (672 bytes total):

```c
struct TLUT {
    uint32_t track_offsets[168];  // File offsets to track data
};
```

- Each entry contains the file offset to the corresponding track's data
- Offset 0 indicates the track is not present
- Offsets must be >= 0x2b0 (688) to be valid
- The TLUT is used to locate track data quickly

#### Populating the TLUT

The TLUT must be populated during SCP file creation. Here's the process:

**Step 1: Calculate Track Data Offsets**

```python
def calculate_tlut_offsets(tracks):
    """Calculate TLUT offsets for all tracks."""
    tlut = [0] * 168  # Initialize all entries to 0
    
    # Start position after header and TLUT
    current_offset = 0x10 + 672  # Header (16) + TLUT (672)
    
    # Add extension block if present
    if has_extension_block:
        current_offset += extension_block_size
    
    # Calculate offset for each track
    for track_num in sorted(tracks.keys()):
        if track_num < 168:  # Valid track range
            tlut[track_num] = current_offset
            # Move to next track position
            track_size = calculate_track_size(tracks[track_num])
            current_offset += track_size
    
    return tlut
```

**Step 2: Write TLUT to File**

```python
def write_tlut(file, tlut):
    """Write TLUT to SCP file."""
    for offset in tlut:
        file.write(struct.pack('<I', offset))
```

**Example TLUT Population:**

```
Track 0: offset = 0x2b0 (688)
Track 1: offset = 0x1234 (4660) 
Track 2: offset = 0x5678 (22136)
...
Track 81: offset = 0xABCD (43981)
Tracks 82-167: offset = 0 (not present)
```

**Validation Rules:**

1. **Minimum Offset**: All valid tracks must have offset >= 0x2b0
2. **No Overlap**: Track offsets must not overlap
3. **Sequential Order**: Tracks should be written in sequential order
4. **Unused Tracks**: Set offset to 0 for tracks not present

### 3. Extension Block (Optional)

Some SCP files contain an extension block starting at offset 0x2b0:

```c
struct ExtensionBlock {
    char signature[4];     // "EXTS"
    uint32_t length;       // Length of extension data
    // Extension data follows...
};
```

#### Write Splice Information (WRSP)

Within the extension block, there may be a WRSP (WRite SPlice) chunk:

```c
struct WRSPChunk {
    char signature[4];     // "WRSP"
    uint32_t length;       // Length of WRSP data
    uint32_t flags;        // Flags (currently unused, must be 0)
    uint32_t splice_positions[168];  // Write splice positions for each track
};
```

- `splice_positions[i]` contains the write splice/overlap position for track i
- Values are in SCP ticks (40MHz clock)
- Zero indicates the track is unused

### 4. Track Data Structure

Each track's data follows a specific structure that varies based on the number of revolutions. Here's the detailed breakdown:

#### Determining Number of Revolutions

The number of revolutions is **fixed for the entire disk** and is specified in the **file header**:

```c
struct SCPHeader {
    // ... other fields ...
    uint8_t revolutions;   // Number of revolutions per track (fixed for entire disk)
    // ... other fields ...
};
```

**Key Points:**
- **All tracks** have the same number of revolutions
- **Fixed at capture time** - cannot vary between tracks
- **Typical values**: 1, 2, 3, or 5 revolutions
- **Common practice**: 3 revolutions for reliability (capture multiple rotations)

**Example:**
```
File header revolutions = 3
→ All tracks have exactly 3 revolutions
→ Each track has 3 revolution headers (12 bytes each)
→ Each revolution has its own flux data
```

#### Track Header (4 bytes)

```c
struct TrackHeader {
    char signature[3];     // "TRK" - track signature
    uint8_t track_number;  // Track number (0-167)
};
```

#### Revolution Headers (12 bytes per revolution)

For each revolution, there's a revolution header:

```c
struct RevolutionHeader {
    uint32_t time_to_index;        // Time to index mark (ticks)
    uint32_t num_flux_transitions; // Number of flux transitions
    uint32_t offset_to_flux_data;  // Offset to flux data within track
};
```

**Revolution Header Details:**

- **time_to_index**: Time from start of revolution to index mark (in 40MHz ticks)
- **num_flux_transitions**: Number of flux transitions in this revolution
- **offset_to_flux_data**: Byte offset from start of track data to flux data

**Time Units and Conversion:**

All timing values in SCP files use **40MHz ticks** as the base unit:

- **Base Frequency**: 40MHz (40,000,000 Hz)
- **Tick Duration**: 25ns (1/40MHz)
- **Time Conversion**: `nanoseconds = ticks × 25.0`

**Example Time Calculations:**

```
Time to index: 8001612 ticks
= 8001612 × 25ns = 200,040,300ns = 200.04ms

For a typical DD floppy disk:
- Rotation speed: 300 RPM = 5 Hz
- Revolution time: 200ms
- Index mark timing: ~200ms (matches our example)
```

#### Flux Data Structure

The flux data follows the revolution headers and contains the actual flux transition timing:

```c
struct FluxData {
    uint16_t intervals[];  // Flux interval values (2 bytes each)
};
```

**What Flux Data Represents:**

Flux data encodes the **time intervals between magnetic flux transitions** on the disk surface. Each 16-bit value represents the time (in 40MHz ticks) between consecutive flux reversals.

**Flux Transitions:**
- **Magnetic domains** on the disk surface have north/south polarity
- **Flux transition** = change from one polarity to the opposite
- **Flux interval** = time between consecutive transitions
- **No transition** = no change in magnetic polarity

**ASCII Timing Diagram:**

```
Magnetic Polarity:  N S N S N S N S N S N S N S N S
Flux Transitions:   ↓ ↑ ↓ ↑ ↓ ↑ ↓ ↑ ↓ ↑ ↓ ↑ ↓ ↑ ↓ ↑
Time Intervals:     t1 t2 t3 t4 t5 t6 t7 t8 t9 t10...

Flux Data: [t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, ...]
```

**Example: MFM Encoding Pattern**

```
MFM Bit Pattern:     1  0  1  1  0  0  1  0
Flux Transitions:    ↓  ↑  ↓  ↑  ↓  ↑  ↓  ↑
Time Intervals:      t1 t2 t3 t4 t5 t6 t7 t8

Where:
- t1 = time from start to first transition
- t2 = time between first and second transition  
- t3 = time between second and third transition
- ... and so on
```

**Real-World Example:**

```
Typical DD MFM timing:
- Bit cell: 2000ns (80 ticks at 40MHz)
- Short interval: 1000ns (40 ticks) - for "0" bits
- Long interval: 2000ns (80 ticks) - for "1" bits

Flux data might look like:
[40, 80, 40, 80, 40, 40, 80, 40, 80, 40, ...]
(ticks) (ticks) (ticks) (ticks) (ticks) ...
```

**Key Points:**
- **Each 16-bit value** = time to next flux transition
- **No absolute timing** - only relative intervals
- **Cumulative time** = sum of all intervals
- **Index mark** = special transition used for synchronization

**Complete Track Data Layout:**

```
Track Header (4 bytes):
  [0-2]   "TRK" signature (3 bytes)
  [3]     Track number (1 byte)

Revolution Headers (12 bytes per revolution):
  [4-7]   Time to index mark (4 bytes, little-endian)
  [8-11]  Number of flux transitions (4 bytes, little-endian)
  [12-15] Offset to flux data (4 bytes, little-endian)
  [16-19] Time to index mark (revolution 2)
  [20-23] Number of flux transitions (revolution 2)
  [24-27] Offset to flux data (revolution 2)
  ... (continues for all revolutions)

Flux Data (variable size):
  [offset] 16-bit flux intervals (2 bytes each, little-endian)
```

### 5. Flux Transition Encoding

#### Basic Encoding Scheme

Flux transitions are encoded as 16-bit values representing time intervals in 40MHz ticks:

- **Base Unit**: 1 tick = 25ns (1/40MHz)
- **Range**: 1-65535 ticks (25ns to 1.638ms)
- **Extended Range**: Values > 65535 use special encoding

#### Extended Range Encoding

For intervals larger than 65535 ticks, SCP uses a special encoding scheme:

```c
// Extended range encoding algorithm
uint32_t accumulated_value = 0;
for each 16-bit value in flux_data:
    if (value == 0):
        accumulated_value += 65536;
        continue;
    else:
        final_interval = accumulated_value + value;
        accumulated_value = 0;
```

**Common Rule:**
Each `0x0000` prefix adds `0x10000` (65536) to the accumulator. Multiple `0x0000` values can be chained to encode arbitrarily large intervals.

**Example Extended Range Encoding:**

```
Raw intervals: [70000, 80000, 90000] ticks

Encoding process:
1. 70000 ticks:
   - 70000 > 65535, so we need extended encoding
   - 70000 = 65536 + 4464
   - Encoded as: [0x0000, 0x1110] (0 + 4464)
   - Where 0x1110 = 4464 in little-endian

2. 80000 ticks:
   - 80000 > 65535, so we need extended encoding  
   - 80000 = 65536 + 14464
   - Encoded as: [0x0000, 0x3880] (0 + 14464)
   - Where 0x3880 = 14464 in little-endian

3. 90000 ticks:
   - 90000 > 65535, so we need extended encoding
   - 90000 = 65536 + 24464
   - Encoded as: [0x0000, 0x5F80] (0 + 24464)
   - Where 0x5F80 = 24464 in little-endian

Final encoded sequence:
[0x0000, 0x1110, 0x0000, 0x3880, 0x0000, 0x5F80]
```

**Decoding process:**
```
1. Read 0x0000: value = 0, add 65536 to accumulator = 65536
2. Read 0x1110: value = 4464, final_interval = 65536 + 4464 = 70000
3. Read 0x0000: value = 0, add 65536 to accumulator = 65536  
4. Read 0x3880: value = 14464, final_interval = 65536 + 14464 = 80000
5. Read 0x0000: value = 0, add 65536 to accumulator = 65536
6. Read 0x5F80: value = 24464, final_interval = 65536 + 24464 = 90000
```

**Example: 1-Second Interval encoding**

```
1 second = 1,000,000,000 nanoseconds
1 tick = 25 nanoseconds (40MHz)
1 second = 1,000,000,000 / 25 = 40,000,000 ticks

Encoding 40,000,000 ticks:
- 40,000,000 = 65536 × 610 + 28,160
- 610 = 65536 × 0 + 610
- So: 40,000,000 = 65536 × 610 + 28,160

Encoded as: [0x0000 repeated 610 times, 0x6E00]
Where 0x6E00 = 28,160 in little-endian

Decoding:
1. Read 610 × 0x0000: accumulator = 610 × 65536 = 39,971,840
2. Read 0x6E00: value = 28,160
3. Final interval = 39,971,840 + 28,160 = 40,000,000 ticks
```

#### Flux Decoding Algorithm

Here's the complete algorithm for decoding flux intervals:

```python
def decode_flux_intervals(flux_data):
    """
    Decode SCP flux intervals from raw byte data.
    
    Args:
        flux_data: Raw bytes containing flux interval data
        
    Returns:
        List of flux intervals in ticks
    """
    flux_list = []
    val = 0
    
    # Process 2 bytes at a time (16-bit values)
    for i in range(0, len(flux_data), 2):
        if i + 1 >= len(flux_data):
            break
            
        # Read 16-bit value (little-endian)
        x = (flux_data[i] << 8) | flux_data[i + 1]
        
        if x == 0:
            # Special case: add 65536 to accumulated value
            val += 65536
            continue
        else:
            # Normal case: add to accumulated value and store
            flux_list.append(val + x)
            val = 0
    
    return flux_list
```

### 6. Concrete Examples

#### Example 1: Simple Track Structure

Consider a track with 2 revolutions and 5 flux transitions per revolution:

```
Track Header:
  [0-2]   "TRK" (0x54 0x52 0x4B)
  [3]     Track number 0 (0x00)

Revolution 1 Header:
  [4-7]   Time to index: 8001612 ticks (0x7A 0x18 0x4C 0x00)
  [8-11]  Num transitions: 5 (0x05 0x00 0x00 0x00)
  [12-15] Offset to flux: 16 (0x10 0x00 0x00 0x00)

Revolution 2 Header:
  [16-19] Time to index: 8001612 ticks (0x7A 0x18 0x4C 0x00)
  [20-23] Num transitions: 5 (0x05 0x00 0x00 0x00)
  [24-27] Offset to flux: 26 (0x1A 0x00 0x00 0x00)

Flux Data (Revolution 1):
  [16-17] Interval 1: 2000 ticks (0xD0 0x07)
  [18-19] Interval 2: 1500 ticks (0xDC 0x05)
  [20-21] Interval 3: 3000 ticks (0xB8 0x0B)
  [22-23] Interval 4: 1000 ticks (0xE8 0x03)
  [24-25] Interval 5: 2500 ticks (0xC4 0x09)

Flux Data (Revolution 2):
  [26-27] Interval 1: 2100 ticks (0xD4 0x08)
  [28-29] Interval 2: 1400 ticks (0xD8 0x05)
  [30-31] Interval 3: 3100 ticks (0xBC 0x0C)
  [32-33] Interval 4: 1100 ticks (0xEC 0x04)
  [34-35] Interval 5: 2400 ticks (0xC0 0x09)
```

#### Example 2: Extended Range Encoding

Consider a track with large intervals:

```
Raw intervals: [100000, 120000, 80000] ticks

Encoded as:
  [0x0000, 0x86A0]  # 0 + 34464 = 34464, then 34464 + 65536 = 100000
  [0x0000, 0xD4A0]  # 0 + 54464 = 54464, then 54464 + 65536 = 120000
  [0x0000, 0x38A0]  # 0 + 14464 = 14464, then 14464 + 65536 = 80000
```

#### Example 3: Real Track Analysis

From our test.scp file, track 0 revolution 0:

```
Header Analysis:
  - Track number: 0
  - Revolutions: 2 (from file header)
  - Time to index: 8001612 ticks (200.04ms)
  - Num transitions: 4000797
  - Flux data size: 8001846 bytes

Flux Interval Statistics:
  - Total time: 19,607,460,125 ns (19.6 seconds)
  - Average interval: 4900.89 ns (4.9μs)
  - Min interval: 200.0 ns
  - Max interval: 3,239,725.0 ns (3.24ms)
  - Standard deviation: 15,247.04 ns

Timing Analysis:
  - 40MHz sampling = 25ns per tick
  - Average interval = 4900.89ns / 25ns = 196 ticks
  - This suggests DD (Double Density) timing
  - Expected bit cell: ~2000ns (80 ticks)
```

### 7. Track Data Validation

#### Validation Rules

1. **Track Signature**: Must be "TRK" (0x54 0x52 0x4B)
2. **Track Number**: Must match TLUT index
3. **Revolution Count**: Must match header revolutions field
4. **Flux Data Size**: Must be even (16-bit values)
5. **Offset Validation**: Flux data offsets must be within track bounds

#### Error Detection

```python
def validate_track_data(track_data, track_num, revolutions):
    """Validate track data structure."""
    
    # Check track signature
    if track_data[:3] != b'TRK':
        raise ValueError("Invalid track signature")
    
    # Check track number
    if track_data[3] != track_num:
        raise ValueError(f"Track number mismatch: expected {track_num}, got {track_data[3]}")
    
    # Check revolution headers
    header_size = 4 + revolutions * 12
    if len(track_data) < header_size:
        raise ValueError("Track data too short for revolution headers")
    
    # Validate flux data offsets
    for rev in range(revolutions):
        offset = 4 + rev * 12
        flux_offset = struct.unpack('<I', track_data[offset+8:offset+12])[0]
        if flux_offset >= len(track_data):
            raise ValueError(f"Invalid flux data offset for revolution {rev}")
    
    return True
```

### 8. Performance Considerations

#### Memory Usage

- **Large Files**: SCP files can be several GB in size
- **Flux Data**: Each track can contain millions of transitions
- **Processing**: Consider streaming for large files

#### Optimization Strategies

1. **Lazy Loading**: Load track data only when needed
2. **Caching**: Cache decoded flux intervals
3. **Parallel Processing**: Process multiple tracks simultaneously
4. **Compression**: Consider compressing flux data for storage

### 9. Footer (Optional)

If the FOOTER flag is set, the file ends with a footer block:

```c
struct Footer {
    uint16_t app_name_length;      // Length of application name
    char app_name[];               // Application name (null-terminated)
    uint32_t drive_manufacturer;   // Drive manufacturer
    uint32_t drive_model;          // Drive model
    uint32_t drive_serial;         // Drive serial number
    uint32_t creator_name;         // Creator name
    uint32_t footer_offs;          // Application name offset
    uint32_t comments;             // Comments
    uint64_t creation_time;        // Creation timestamp
    uint64_t modification_time;    // Modification timestamp
    uint8_t app_version;           // Application version
    uint8_t hardware_version;      // Hardware version
    uint8_t firmware_version;      // Firmware version
    uint8_t format_version;        // Format version (0x24 = v2.4)
    char signature[4];             // "FPCS"
};
```

#### Footer Implementation

**Step 1: Check if Footer is Required**

```python
def should_write_footer(flags):
    """Check if footer should be written based on header flags."""
    return (flags & SCPHeaderFlags.FOOTER) != 0
```

**Step 2: Create Footer Data**

```python
def create_footer(app_name="GreaseWeazle", drive_info=None):
    """Create footer data structure."""
    footer = bytearray()
    
    # Application name (null-terminated)
    app_name_bytes = app_name.encode('utf-8') + b'\x00'
    footer += struct.pack('<H', len(app_name_bytes))
    footer += app_name_bytes
    
    # Drive information (optional)
    if drive_info:
        footer += struct.pack('<I', drive_info.get('manufacturer', 0))
        footer += struct.pack('<I', drive_info.get('model', 0))
        footer += struct.pack('<I', drive_info.get('serial', 0))
    else:
        footer += struct.pack('<III', 0, 0, 0)  # Default values
    
    # Creator and comments
    footer += struct.pack('<II', 0, 0)  # creator_name, comments
    
    # Timestamps
    current_time = int(time.time())
    footer += struct.pack('<QQ', current_time, current_time)
    
    # Version information
    footer += struct.pack('<BBBB', 0, 0, 0, 0x24)  # versions
    
    # Signature
    footer += b'FPCS'
    
    return footer
```

**Step 3: Write Footer to File**

```python
def write_footer(file, footer_data):
    """Write footer to end of SCP file."""
    file.write(footer_data)
```

**Example Footer Structure:**

```
Footer Data:
├── App name length: 12 (0x0C)
├── App name: "GreaseWeazle\0"
├── Drive manufacturer: 0
├── Drive model: 0  
├── Drive serial: 0
├── Creator name: 0
├── Comments: 0
├── Creation time: 1640995200 (2022-01-01 00:00:00)
├── Modification time: 1640995200 (2022-01-01 00:00:00)
├── App version: 0
├── Hardware version: 0
├── Firmware version: 0
├── Format version: 0x24 (v2.4)
└── Signature: "FPCS"
```

**Footer Validation:**

```python
def validate_footer(footer_data):
    """Validate footer structure."""
    if len(footer_data) < 4:
        return False
    
    # Check signature
    signature = footer_data[-4:]
    if signature != b'FPCS':
        return False
    
    # Check minimum size
    if len(footer_data) < 8:  # At least app_name_length + signature
        return False
    
    return True
```

**Reading Footer:**

```python
def read_footer(file):
    """Read footer from end of SCP file."""
    # Seek to end of file
    file.seek(0, 2)  # Seek to end
    file_size = file.tell()
    
    # Read footer (assume max 1024 bytes)
    footer_size = min(1024, file_size)
    file.seek(file_size - footer_size)
    footer_data = file.read(footer_size)
    
    # Find FPCS signature
    signature_pos = footer_data.rfind(b'FPCS')
    if signature_pos == -1:
        return None
    
    # Extract footer
    footer = footer_data[signature_pos-32:signature_pos+4]
    return footer
```

## Timing and Sampling

### Sampling Frequency

- **Base Frequency**: 40MHz (40,000,000 Hz)
- **Tick Duration**: 25ns (1/40MHz)
- **Resolution**: 25ns per tick

### Flux Interval Conversion

To convert flux intervals to real time:

```python
def ticks_to_nanoseconds(ticks):
    return ticks * 25.0  # 25ns per tick

def nanoseconds_to_ticks(nanoseconds):
    return int(nanoseconds / 25.0)
```

## File Layout Summary

```
Offset    Size    Description
0x000     16      File Header
0x010     672     Track Lookup Table (TLUT)
0x2b0     var     Extension Block (optional)
0x2b0+    var     Track Data
...       ...     Additional tracks
EOF-var   var     Footer (optional)
```

## Track Data Layout

For each track:

```
Track Header (4 bytes):
  - "TRK" signature (3 bytes)
  - Track number (1 byte)

Revolution Headers (12 bytes per revolution):
  - Time to index (4 bytes)
  - Number of flux transitions (4 bytes)
  - Offset to flux data (4 bytes)

Flux Data (variable):
  - 16-bit flux intervals (2 bytes each)
```

## Validation and Error Handling

### Checksum Calculation

The header checksum is calculated as:

```python
def calculate_checksum(data):
    return sum(data[16:]) & 0xffffffff
```

#### Detailed Checksum Algorithm

The SCP checksum is an **unsigned 32-bit sum** of all data bytes starting from offset 16 (after the header) to the end of the file.

**Step-by-Step Process:**

```python
def calculate_scp_checksum(file_data):
    """
    Calculate SCP file checksum.
    
    Args:
        file_data: Complete SCP file as bytes
        
    Returns:
        32-bit unsigned checksum value
    """
    # Skip header (first 16 bytes)
    data_to_sum = file_data[16:]
    
    # Sum all bytes as unsigned 32-bit values
    checksum = 0
    for byte in data_to_sum:
        checksum += byte  # Each byte is 0-255 (unsigned)
    
    # Return unsigned 32-bit result (0 to 0xFFFFFFFF)
    return checksum & 0xffffffff
```

**Important Notes:**
- **Unsigned arithmetic**: All additions are performed as unsigned 32-bit operations
- **No overflow**: The `& 0xffffffff` mask ensures 32-bit unsigned result
- **Range**: Checksum values range from 0 to 0xFFFFFFFF (4,294,967,295)
- **No sign bit**: The checksum is never negative

**Example Calculation:**

```
SCP File Structure:
┌──────────────────────────────────────────────────────┐
│ Header (16 bytes) │ TLUT (672 bytes) │ Track Data... │
└──────────────────────────────────────────────────────┘
↑                    ↑
Offset 0             Offset 16 (start of checksum data)
```

**Checksum Data Includes:**
- **TLUT** (672 bytes)
- **Extension Block** (if present)
- **All Track Data** (variable size)
- **Footer** (if present)

**Example with Real Data:**

```python
# Example SCP file structure
header = b'SCP\x00\x80\x03\x00\x51\x23\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
tlut = b'\x00' * 672  # Placeholder TLUT
track_data = b'TRK\x00...'  # Track data

# Calculate checksum
checksum_data = tlut + track_data  # Data starting from offset 16
checksum = sum(checksum_data) & 0xffffffff

print(f"Checksum: 0x{checksum:08x}")
```

**Validation Process:**

```python
def validate_scp_checksum(file_data):
    """
    Validate SCP file checksum.
    
    Args:
        file_data: Complete SCP file as bytes
        
    Returns:
        True if checksum is valid, False otherwise
    """
    # Extract checksum from header
    header_checksum = struct.unpack('<I', file_data[12:16])[0]
    
    # Calculate actual checksum
    calculated_checksum = calculate_scp_checksum(file_data)
    
    # Compare
    return header_checksum == calculated_checksum
```

**Common Checksum Issues:**

1. **File Corruption**: Checksum mismatch indicates file corruption
2. **Incomplete Data**: Missing track data causes checksum failure
3. **Header Corruption**: Damaged header affects checksum validation
4. **Extension Block**: Missing or corrupted extension data

**Error Handling:**

```python
def read_scp_with_checksum(filename):
    """Read SCP file with checksum validation."""
    with open(filename, 'rb') as f:
        data = f.read()
    
    # Validate checksum
    if not validate_scp_checksum(data):
        print("WARNING: SCP file checksum mismatch!")
        print("File may be corrupted or incomplete.")
        # Continue reading but warn user
    
    return data
```

**Performance Considerations:**

- **Large Files**: Checksum calculation can be slow for large files
- **Streaming**: For very large files, consider streaming checksum calculation
- **Caching**: Cache checksum results for repeated validation

**Alternative Implementation (Streaming):**

```python
def calculate_checksum_streaming(filename):
    """Calculate checksum for large files without loading entire file."""
    checksum = 0
    
    with open(filename, 'rb') as f:
        # Skip header
        f.seek(16)
        
        # Read and sum remaining data in chunks
        while True:
            chunk = f.read(8192)  # 8KB chunks
            if not chunk:
                break
            for byte in chunk:
                checksum += byte
    
    return checksum & 0xffffffff
```

### Common Validation Rules

1. **Signature Check**: File must start with "SCP"
2. **TLUT Validation**: Track offsets must be >= 0x2b0 or 0
3. **Track Number Validation**: Track headers must match TLUT index
4. **Revolution Count**: Must match header revolutions field
5. **Flux Data Size**: Must match revolution header count

## Implementation Notes

### GreaseWeazle Specific Features

1. **Index Cuing**: Automatically cues tracks at index marks
2. **Revolution Handling**: Supports multiple revolutions per track
3. **Splice Points**: Tracks write splice positions for accurate writing
4. **Legacy Support**: Handles old single-sided image formats
5. **Extension Blocks**: Supports extended metadata via EXTS blocks

#### Index Cuing Explained

**What is an Index Mark?**

An index mark is a **physical reference point** on a floppy disk that indicates the start of a track revolution:

```
Floppy Disk Layout:
┌────────────────────────────────────┐
│  ┌─────────────────────────────┐   │
│  │        Track Data           │   │
│  │                             │   │
│  │  ┌─────┐                    │   │
│  │  │INDEX│ ← Physical hole    │   │
│  │  │MARK │   in disk          │   │
│  │  └─────┘                    │   │
│  │                             │   │
│  └─────────────────────────────┘   │
└────────────────────────────────────┘
```

**Index Cuing Process:**

When a floppy disk rotates, the drive detects the index mark and uses it as a **synchronization reference**:

```
Timeline of Disk Rotation:
┌───────────────────────────────────────────────────────┐
│ Revolution 1    │ Revolution 2    │ Revolution 3      │
│ ┌─────────────┐ │ ┌─────────────┐ │ ┌─────────────┐   │
│ │INDEX→Data   │ │ │INDEX→Data   │ │ │INDEX→Data   │   │
│ └─────────────┘ │ └─────────────┘ │ └─────────────┘   │
└───────────────────────────────────────────────────────┘
```

**What "Index Cuing" Means:**

1. **Synchronization**: The capture hardware waits for the index mark before starting to record flux transitions
2. **Consistent Start Point**: Each revolution starts at the same physical location (index mark)
3. **Reliable Timing**: The `time_to_index` field in revolution headers indicates when the index mark occurs
4. **Multiple Revolutions**: Each revolution is captured from index mark to index mark

**Index-Cued vs Non-Index-Cued:**

```
Index-Cued (INDEXED flag set):
┌─────────────────────────────────────────────────────┐
│ Rev1: [INDEX at t=0] → Data → [INDEX at t=200ms]    │
│ Rev2: [INDEX at t=0] → Data → [INDEX at t=200ms]    │
│ Rev3: [INDEX at t=0] → Data → [INDEX at t=200ms]    │
└─────────────────────────────────────────────────────┘

Non-Index-Cued (INDEXED flag not set):
┌─────────────────────────────────────────────────────┐
│ Rev1: [random start] → Data → [INDEX at random time]│
│ Rev2: [random start] → Data → [INDEX at random time]│
│ Rev3: [random start] → Data → [INDEX at random time]│
└─────────────────────────────────────────────────────┘
```

**Example from SCP Data:**

```
Revolution Header:
- time_to_index: 8001612 ticks (200.04ms)
- num_flux_transitions: 4000797
- offset_to_flux_data: 16

This means:
- Index mark occurs 200.04ms after revolution start
- 4,000,797 flux transitions follow
- Total revolution time ≈ 200ms (matches 300 RPM)

Note: In index-cued tracks, the index mark is at the START of the revolution (t=0),
but the time_to_index field still indicates the time to the NEXT index mark.
```

**Benefits of Index Cuing:**

1. **Consistency**: All revolutions start at the same physical point
2. **Reliability**: Reduces timing variations between revolutions
3. **Compatibility**: Most floppy disk software expects index-cued data
4. **Accuracy**: Enables precise track writing back to disk

**Non-Index Cued vs Index Cued:**

```
Non-Index Cued:
┌──────────────────────────────────────────────────────┐
│ Rev1: [random start] → Data → [random end]           │
│ Rev2: [random start] → Data → [random end]           │
│ Rev3: [random start] → Data → [random end]           │
└──────────────────────────────────────────────────────┘

Index Cued:
┌──────────────────────────────────────────────────────┐
│ Rev1: [INDEX] → Data → [INDEX]                       │
│ Rev2: [INDEX] → Data → [INDEX]                       │
│ Rev3: [INDEX] → Data → [INDEX]                       │
└──────────────────────────────────────────────────────┘
```

**GreaseWeazle Implementation:**

GreaseWeazle automatically handles index cuing by:
- Waiting for index mark detection before capture
- Recording the exact time to index mark
- Ensuring all revolutions are properly synchronized
- Setting the INDEXED flag in the SCP header

### Performance Considerations

1. **Memory Usage**: Large files can consume significant memory
2. **Seeking**: Use TLUT for efficient track access
3. **Flux Decoding**: Optimize interval decoding for large datasets
4. **Checksum**: Validate file integrity on load

## Example Usage

### Reading an SCP File

```python
def read_scp_file(filename):
    with open(filename, 'rb') as f:
        # Read header
        header = f.read(16)
        sig, version, disk_type, revolutions = struct.unpack('<3sBBBB', header[:8])
        
        # Read TLUT
        tlut = struct.unpack('<168I', f.read(672))
        
        # Process tracks
        for track_num, offset in enumerate(tlut):
            if offset != 0 and offset >= 0x2b0:
                f.seek(offset)
                track_data = read_track(f, revolutions)
                yield track_num, track_data
```

### Writing an SCP File

```python
def write_scp_file(filename, tracks, disk_type=0x80):
    with open(filename, 'wb') as f:
        # Write header
        header = struct.pack('<3sBBBBBBBBBI',
                           b'SCP', 0, disk_type, 3, 0, 81, 35, 0, 0, 0, 0)
        f.write(header)
        
        # Write TLUT (placeholder)
        f.write(b'\x00' * 672)
        
        # Write track data
        for track_num, track_data in tracks.items():
            write_track(f, track_num, track_data)
```

## References

- [SCP Image Specification](https://www.cbmstuff.com/downloads/scp/scp_image_specs.txt)
- [GreaseWeazle SCP Implementation](https://github.com/keirf/Greaseweazle)
- [SuperCard Pro Hardware](https://www.cbmstuff.com/proddetail.php?prod=SCP)

---

*This documentation is based on the GreaseWeazle SCP implementation and the official SCP specification.*