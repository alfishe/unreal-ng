# DiskImage & MFM Modernization Specification

## Current State Analysis

### DiskImage Structure (diskimage.h)

```cpp
// Current: Fixed 16 sectors of 256 bytes
struct RawSectorBytes {
    AddressMarkRecord address_mark;   // 5 bytes
    uint16_t address_crc;             // 2 bytes
    DataMarkRecord data_mark;         // 1 byte
    uint8_t data[256];                // Fixed size!
    uint16_t data_crc;                // 2 bytes
};

class Track {
    RawSectorBytes sectors[16];       // Fixed count!
    RawSectorBytes* sectorsOrderedRef[16];
    // ...
};
```

### Limitations

1. **Fixed sector count:** Always 16 sectors per track
2. **Fixed sector size:** Always 256 bytes
3. **Fixed interleave:** TR-DOS pattern hardcoded
4. **No geometry metadata:** Track doesn't know its own format
5. **No error injection:** Can't simulate bad sectors
6. **No raw mode:** Always assumes structured sectors

---

## Target State

### New TrackGeometry Structure

```cpp
struct SectorDescriptor {
    uint8_t sectorNumber;      // 1-255 (as in IDAM)
    uint8_t sizeCode;          // N field: 0=128, 1=256, 2=512, 3=1024
    uint16_t dataOffset;       // Offset in raw MFM data
    uint16_t idamOffset;       // Offset to IDAM in raw data
    
    bool hasCRCError;          // Intentional bad CRC (copy protection)
    bool isDeleted;            // F8 vs FB DAM
    bool isWeakBits;           // Reads differently each time
    
    uint16_t actualSize() const {
        return 128 << (sizeCode & 0x03);
    }
};

struct TrackGeometry {
    uint8_t cylinderNumber;
    uint8_t headNumber;
    uint8_t sectorCount;       // 1-26 variable
    uint16_t rawTrackLength;   // Total MFM bytes (varies with density)
    
    SectorDescriptor sectors[26];  // Max possible
    
    // Helper methods
    const SectorDescriptor* findSector(uint8_t sectorNo) const;
    std::vector<uint8_t> getSectorNumbers() const;
};
```

### New Track Class

```cpp
class Track {
public:
    // Raw MFM data storage
    std::vector<uint8_t> rawData;  // Variable size (typically 6250 for DD)
    
    // Geometry (detected or specified)
    TrackGeometry geometry;
    
    // Sector access (uses geometry)
    uint8_t* getDataForSector(uint8_t sectorNo);
    AddressMarkRecord* getIDForSector(uint8_t sectorNo);
    uint16_t getSectorSize(uint8_t sectorNo);
    
    // Geometry management
    void detectGeometry();           // Parse rawData to fill geometry
    void setGeometry(const TrackGeometry& geom);  // Manual override
    
    // Validation
    bool validateCRCs() const;
    std::vector<std::string> getDiagnostics() const;
    
    // Copy protection support
    void injectCRCError(uint8_t sectorNo);
    void setDeleted(uint8_t sectorNo, bool deleted);
    void setWeakBits(uint8_t sectorNo, bool weak);
    
private:
    // Sector size in bytes (calculated from geometry)
    uint16_t sectorSize(uint8_t sizeCode) const;
};
```

### Factory Methods

```cpp
class TrackFactory {
public:
    // Create standard track layouts
    static Track createTRDOSTrack(uint8_t cylinder);  // 16×256, 1:2 interleave
    static Track createIBMTrack(uint8_t cylinder);    // 9×512
    static Track createHDTrack(uint8_t cylinder);     // 18×512
    static Track createCustomTrack(const TrackGeometry& geom);
    
    // Parse from raw MFM data
    static Track createFromMFM(const uint8_t* data, size_t len);
};
```

---

## MFM Parser Modernization

### Current State

```cpp
class MFMParser {
public:
    static TrackParseResult parseTrack(const uint8_t* data, size_t length);
    static SectorParseResult parseSector(const uint8_t* data, size_t pos);
};
```

### Target State

```cpp
struct ParseConfig {
    bool validateCRC = true;
    bool detectWeakBits = false;
    bool allowDuplicates = false;
    bool allowMissingSectors = false;
    uint8_t expectedSectors = 0;     // 0 = auto-detect
    uint8_t expectedSectorSize = 1;  // N code, 0xFF = auto-detect
};

struct SectorInfo {
    uint8_t cylinderId;
    uint8_t headId;
    uint8_t sectorId;
    uint8_t sizeCode;
    
    uint16_t idamOffset;
    uint16_t damOffset;
    uint16_t dataOffset;
    
    uint16_t storedIdamCRC;
    uint16_t calculatedIdamCRC;
    bool idamCRCValid;
    
    uint16_t storedDataCRC;
    uint16_t calculatedDataCRC;
    bool dataCRCValid;
    
    bool isDeleted;
    bool hasWeakBits;
};

struct TrackInfo {
    std::vector<SectorInfo> sectors;
    uint8_t minSector;
    uint8_t maxSector;
    uint16_t gapBytes;
    bool isStandardLayout;
    std::string layoutDescription;
};

class MFMParser {
public:
    MFMParser();
    explicit MFMParser(const ParseConfig& config);
    
    // Main parsing
    TrackInfo parseTrack(std::span<const uint8_t> rawData);
    
    // Helpers
    static bool isSyncByte(uint8_t byte);
    static bool isAddressMark(uint8_t byte);
    static uint16_t calculateCRC(std::span<const uint8_t> data);
    
    // Validation
    bool validateTrack(const TrackInfo& info);
    std::string generateReport(const TrackInfo& info);
    
private:
    ParseConfig _config;
    
    SectorInfo parseSector(std::span<const uint8_t> data, size_t idamPos);
    size_t findNextSync(std::span<const uint8_t> data, size_t start);
};
```

---

## Migration Path

### Phase 1: Add New Structures (Non-Breaking)

```cpp
// Add alongside existing structures
struct SectorDescriptor { /* new */ };
struct TrackGeometry { /* new */ };

class Track {
    // Keep existing members
    RawSectorBytes sectors[16];
    
    // Add new members
    std::optional<TrackGeometry> _geometry;
    std::optional<std::vector<uint8_t>> _rawData;
    
    // New methods
    void detectGeometry();
};
```

### Phase 2: Migrate Internals

1. Make `sectors[]` array private
2. Route all access through geometry-aware methods
3. Add backwards-compatible accessors

### Phase 3: Deprecate Legacy

```cpp
[[deprecated("Use geometry-based access")]]
RawSectorBytes* getSector(uint8_t sectorNo);

// New method
uint8_t* getSectorData(uint8_t sectorNo);
SectorDescriptor getSectorInfo(uint8_t sectorNo);
```

### Phase 4: Remove Legacy

After all tests pass with new API, remove deprecated methods.

---

## Test Data Files

### Required Test Images

| Filename | Format | Purpose |
|----------|--------|---------|
| `trdos_blank.trd` | 16×256 | Standard TR-DOS |
| `ibm_pc_360k.img` | 9×512 | IBM PC format |
| `ibm_pc_720k.img` | 9×512 DS | IBM PC 720K |
| `hd_1440k.img` | 18×512 | High density |
| `missing_sector.trd` | Custom | Sector 5 missing |
| `duplicate_sector.trd` | Custom | Two sector 1s |
| `bad_crc.trd` | Custom | CRC error on sector 3 |
| `speedlock.trd` | Custom | Weak bits protection |

### Test Data Location

```
core/tests/testdata/fdc/
├── standard/
│   ├── trdos_blank.trd
│   ├── trdos_formatted.trd
│   └── trdos_with_files.trd
├── ibm/
│   ├── ibm_360k.img
│   └── ibm_720k.img
├── nonstandard/
│   ├── missing_sector.bin
│   ├── duplicate_sector.bin
│   └── bad_crc.bin
└── protection/
    ├── speedlock_sample.bin
    └── alkatraz_sample.bin
```

---

## API Examples

### Reading a TR-DOS Sector

```cpp
// Current (works but inflexible)
Track* track = disk->getTrackForCylinderAndSide(0, 0);
uint8_t* data = track->getDataForSector(1);

// New (geometry-aware)
Track* track = disk->getTrackForCylinderAndSide(0, 0);
auto info = track->getSectorInfo(1);
if (!info.idamCRCValid || !info.dataCRCValid) {
    // Handle error
}
uint8_t* data = track->getSectorData(1);
size_t size = info.actualSize();  // Could be 128/256/512/1024
```

### Detecting Track Format

```cpp
Track* track = disk->getTrackForCylinderAndSide(0, 0);
track->detectGeometry();

auto& geom = track->geometry;
std::cout << "Sectors: " << (int)geom.sectorCount << "\n";
std::cout << "Sector size: " << geom.sectors[0].actualSize() << " bytes\n";

for (int i = 0; i < geom.sectorCount; i++) {
    auto& sec = geom.sectors[i];
    std::cout << "Sector " << (int)sec.sectorNumber 
              << " at offset " << sec.dataOffset << "\n";
}
```

### Creating Non-Standard Track

```cpp
TrackGeometry geom;
geom.cylinderNumber = 0;
geom.headNumber = 0;
geom.sectorCount = 9;  // IBM PC format

for (int i = 0; i < 9; i++) {
    geom.sectors[i].sectorNumber = i + 1;
    geom.sectors[i].sizeCode = 2;  // 512 bytes
}

Track track = TrackFactory::createCustomTrack(geom);
```

---

## Backwards Compatibility

All existing tests must pass without modification during transition.

```cpp
// Guarantee: This must always work for TR-DOS images
Track* track = disk->getTrackForCylinderAndSide(0, 0);
uint8_t* sector1 = track->getDataForSector(1);
ASSERT_NE(sector1, nullptr);
ASSERT_EQ(track->getSectorSize(1), 256);  // TR-DOS default
```
