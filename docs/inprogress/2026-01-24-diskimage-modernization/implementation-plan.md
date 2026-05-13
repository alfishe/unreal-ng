# DiskImage Modernization - Implementation Plan

> **Work Item:** 2026-01-24-diskimage-modernization  
> **Status:** Planning  
> **Priority:** High (Prerequisite for WD1793 full coverage)  
> **Estimated Effort:** 8-12 days  

## Goal Statement

Modernize the DiskImage subsystem to support:
- All WD1793 sector sizes and track layouts
- Write protection with configurable behavior
- Change tracking and persistence
- Centralized disk management via DiskManager
- Safe and unsafe save modes
- Format conversion capabilities

---

## Table of Contents

1. [Current State Analysis](#current-state-analysis)
2. [Feature Requirements](#feature-requirements)
3. [Architecture Overview](#architecture-overview)
4. [Phase Breakdown](#phase-breakdown)
5. [Open Questions](#open-questions)

---

## Current State Analysis

### DiskImage Limitations

| Limitation | Impact |
|------------|--------|
| Fixed 16 sectors/track | Can't load IBM PC, HD, or protected disks |
| Fixed 256 bytes/sector | Only TR-DOS format works |
| No write protection API | Can't prevent writes |
| No change tracking | Can't detect modifications |
| No save mechanism | Images never persist |
| Hardcoded interleave | Non-standard layouts fail |
| No per-sector metadata | No CRC error injection |

### Current File Structure

```
core/src/emulator/io/fdc/
├── diskimage.h          # DiskImage and Track classes
├── diskimage.cpp        # Implementation
├── fdd.h/cpp            # Drive abstraction
├── wd1793.h/cpp         # Controller
└── mfm_parser.h         # MFM parsing utilities
```

---

## Sector Addressing Convention

> **CRITICAL:** All internal DiskImage methods use **physical (0-based)** addressing.
> TR-DOS and other logical (1-based) addressing requires explicit conversion.

### Naming Rules

| Convention | Index Range | Method Naming | Use Case |
|------------|-------------|---------------|----------|
| **Physical** | 0-15 | `getSector(idx)`, `getDataForSector(idx)` | Internal storage, raw access |
| **Logical** | 1-16 | `getSectorLogical(sectorNo)`, `getDataForSectorLogical(sectorNo)` | TR-DOS API compatibility |

### Implementation

```cpp
// Physical (0-based) - DEFAULT
uint8_t* getDataForSector(uint8_t idx) {
    uint8_t index = idx & 0x0F;
    return sectorsOrderedRef[index]->data;
}

// Logical (1-based) - EXPLICIT CONVERSION
uint8_t* getDataForSectorLogical(uint8_t sectorNo) {
    // Convert 1-16 → 0-15
    return getDataForSector(sectorNo & 0x0F);
}
```

### Refactoring Tasks

- [ ] Add `getSectorLogical()` wrapper
- [ ] Add `getIDForSectorLogical()` wrapper
- [ ] Add `getDataForSectorLogical()` wrapper
- [ ] Update documentation in `diskimage.h`
- [ ] Ensure all WD1793 callers use correct convention

### F1: Write Protection (Priority: P1)

| Requirement | Description |
|-------------|-------------|
| F1.1 | Per-image write protect flag |
| F1.2 | API to get/set write protect status |
| F1.3 | WD1793 honors write protect (returns status bit) |
| F1.4 | UI indicator for protected disks |

### F2: Non-Standard Track Layouts (Priority: P1)

| Requirement | Description |
|-------------|-------------|
| F2.1 | Variable sectors per track (1-256) |
| F2.2 | Variable sector sizes (128, 256, 512, 1024) |
| F2.3 | Floating sector positions (arbitrary IDAM locations) |
| F2.4 | Mixed sector sizes on same track |
| F2.5 | Duplicate sector numbers (copy protection) |
| F2.6 | Missing sector numbers (copy protection) |
| F2.7 | Intentional CRC errors (copy protection) |
| F2.8 | Deleted data marks (F8 DAM) |

### F3: Change Tracking (Priority: P1)

| Requirement | Description |
|-------------|-------------|
| F3.1 | Dirty flag per image |
| F3.2 | Dirty flag per track |
| F3.3 | Dirty flag per sector |
| F3.4 | Change timestamp tracking |
| F3.5 | CRC32/hash of original content |
| F3.6 | CRC32/hash of current content |
| F3.7 | Per-sector write heatmap |
| F3.8 | Change detection callback |

### F4: DiskManager (Priority: P1)

| Requirement | Description |
|-------------|-------------|
| F4.1 | Centralized image registry |
| F4.2 | Image lifecycle management |
| F4.3 | Multi-instance support (videowall) |
| F4.4 | Shared image access (read-only sharing) |
| F4.5 | Image locking (exclusive access) |
| F4.6 | Save queue management |
| F4.7 | Observer pattern for state changes |

### F5: Persistence / Save (Priority: P1)

| Requirement | Description |
|-------------|-------------|
| F5.1 | Save to original file |
| F5.2 | Save to new file |
| F5.3 | Auto-save option |
| F5.4 | Save-on-exit behavior |
| F5.5 | Discard changes option |
| F5.6 | Save protection modes (see below) |

#### Save Protection Modes

| Mode | Behavior | Use Case |
|------|----------|----------|
| `Transparent` | Save without asking | Power users |
| `Confirm` | Prompt before save | Default |
| `SafeOnly` | Only save to new file | Archivists |
| `ReadOnly` | Never save changes | Testing |

### F6: Format Conversion (Priority: P2)

| Requirement | Description |
|-------------|-------------|
| F6.1 | Read via loader to universal format |
| F6.2 | Write to any supported format |
| F6.3 | Supported formats: TRD, SCL, FDI, UDI, TD0, IMG |
| F6.4 | Format conversion API |
| F6.5 | Batch conversion tool |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      DiskManager                             │
│  - Image registry                                           │
│  - Multi-instance coordination                              │
│  - Save queue                                               │
└──────────────────────┬──────────────────────────────────────┘
                       │
         ┌─────────────┴─────────────┐
         │                           │
         ▼                           ▼
┌─────────────────┐         ┌─────────────────┐
│   DiskImage     │         │   DiskImage     │
│  (Image 0)      │         │  (Image 1)      │
└────────┬────────┘         └─────────────────┘
         │
         ▼
┌─────────────────────────────────────────────┐
│                Track (per cyl/side)         │
│  - TrackGeometry                            │
│  - Raw MFM data                             │
│  - Change tracking                          │
└────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────┐
│             SectorDescriptor                │
│  - Sector number, size, offsets             │
│  - CRC status, deleted mark, dirty          │
└─────────────────────────────────────────────┘
```

---

## Phase Breakdown

### Phase 1: Core Data Structures (2-3 days)

**Objective:** Refactor DiskImage internals for flexibility.

#### 1.1 SectorDescriptor

```cpp
struct SectorDescriptor {
    // Identity
    uint8_t sectorNumber;      // 1-255 (from IDAM)
    uint8_t sizeCode;          // N field: 0=128, 1=256, 2=512, 3=1024
    
    // Position in raw track
    uint16_t idamOffset;       // Offset to FE byte
    uint16_t damOffset;        // Offset to FB/F8 byte
    uint16_t dataOffset;       // Offset to first data byte
    
    // Status
    bool idamCRCValid;
    bool dataCRCValid;
    bool intentionalCRCError;  // Copy protection
    bool isDeleted;            // F8 DAM
    
    // Change tracking
    bool dirty;
    uint64_t lastWriteTimestamp;
    uint32_t originalCRC32;
    uint32_t currentCRC32;
    
    // Helpers
    uint16_t dataSize() const { return 128 << (sizeCode & 0x03); }
};
```

#### 1.2 TrackGeometry

```cpp
struct TrackGeometry {
    uint8_t cylinderNumber;
    uint8_t headNumber;
    uint8_t sectorCount;       // Actual sectors found
    uint16_t rawTrackLength;   // MFM bytes (6250 typical)
    
    std::vector<SectorDescriptor> sectors;
    
    // Methods
    const SectorDescriptor* findSector(uint8_t sectorNo) const;
    std::vector<uint8_t> getSectorNumbers() const;
    bool hasDuplicateSectors() const;
    bool hasAllSectors(uint8_t expected) const;
};
```

#### 1.3 Track Refactoring

```cpp
class Track {
public:
    // Raw MFM data
    std::vector<uint8_t> rawData;
    
    // Geometry (auto-detected or manual)
    TrackGeometry geometry;
    
    // Change tracking
    bool dirty = false;
    uint64_t lastWriteTimestamp = 0;
    
    // Sector access
    uint8_t* getSectorData(uint8_t sectorNo);
    const SectorDescriptor* getSectorInfo(uint8_t sectorNo);
    
    // Geometry
    void detectGeometry();
    void setGeometry(const TrackGeometry& geom);
    
    // Modification
    void writeSector(uint8_t sectorNo, const uint8_t* data, size_t len);
    void markDirty();
};
```

**Deliverables:**
- [ ] `SectorDescriptor` structure
- [ ] `TrackGeometry` structure
- [ ] Refactored `Track` class
- [ ] Unit tests for new structures

---

### Phase 2: Write Protection (1 day)

**Objective:** Implement write protect with WD1793 integration.

#### 2.1 DiskImage Write Protect

```cpp
class DiskImage {
    bool _writeProtected = false;
    
public:
    bool isWriteProtected() const { return _writeProtected; }
    void setWriteProtected(bool protect) { _writeProtected = protect; }
    
    // Write operations check this
    bool canWrite() const { return !_writeProtected; }
};
```

#### 2.2 WD1793 Integration

```cpp
// In WD1793::cmdWriteSector()
if (diskImage && diskImage->isWriteProtected()) {
    _statusRegister |= WDS_WRITEPROTECT;
    raiseIntrq();
    return;
}
```

**Deliverables:**
- [ ] Write protect API on DiskImage
- [ ] WD1793 honors write protect status
- [ ] Tests for write protect behavior

---

### Phase 3: Change Tracking (2-3 days)

**Objective:** Track all modifications with heatmap support.

#### 3.1 ChangeTracker Class

```cpp
class ChangeTracker {
public:
    struct SectorChange {
        uint8_t cylinder;
        uint8_t head;
        uint8_t sector;
        uint64_t timestamp;
        uint32_t previousCRC;
        uint32_t newCRC;
    };
    
    // Change log
    std::vector<SectorChange> changeLog;
    
    // Heatmap (per sector write count)
    std::map<std::tuple<uint8_t,uint8_t,uint8_t>, uint32_t> writeHeatmap;
    
    // Image-level tracking
    bool isDirty() const;
    size_t getChangeCount() const;
    std::vector<std::tuple<uint8_t,uint8_t,uint8_t>> getDirtySectors() const;
    
    // Notifications
    using ChangeCallback = std::function<void(const SectorChange&)>;
    void setChangeCallback(ChangeCallback cb);
    
    // Reset
    void clearDirtyFlags();
    void snapshotAsOriginal();  // Mark current state as "original"
};
```

#### 3.2 Integration

```cpp
// In Track::writeSector()
void Track::writeSector(uint8_t sectorNo, const uint8_t* data, size_t len) {
    auto* sector = geometry.findSector(sectorNo);
    if (!sector) return;
    
    // Track change
    uint32_t oldCRC = sector->currentCRC32;
    
    // Write data
    std::memcpy(rawData.data() + sector->dataOffset, data, len);
    
    // Update tracking
    sector->currentCRC32 = calculateCRC32(data, len);
    sector->dirty = (sector->currentCRC32 != sector->originalCRC32);
    sector->lastWriteTimestamp = getCurrentTimestamp();
    
    dirty = true;
    
    // Notify
    if (_changeTracker) {
        _changeTracker->recordChange(geometry.cylinderNumber, 
                                      geometry.headNumber, 
                                      sectorNo, oldCRC, sector->currentCRC32);
    }
}
```

**Deliverables:**
- [ ] `ChangeTracker` class
- [ ] Per-sector dirty flags
- [ ] CRC32 tracking (original vs current)
- [ ] Write heatmap
- [ ] Change notification callback
- [ ] Tests for change tracking

---

### Phase 4: DiskManager (2-3 days)

**Objective:** Centralized disk image management.

#### 4.1 DiskManager Class

```cpp
class DiskManager {
public:
    // Singleton or per-context
    static DiskManager& instance();
    
    // Image lifecycle
    std::shared_ptr<DiskImage> loadImage(const std::string& path);
    std::shared_ptr<DiskImage> createBlankImage(uint8_t cylinders, uint8_t sides);
    void unloadImage(const std::string& path);
    
    // Registry
    std::vector<std::string> getLoadedImages() const;
    std::shared_ptr<DiskImage> getImage(const std::string& path) const;
    
    // Multi-instance sharing
    bool isImageInUse(const std::string& path) const;
    bool lockImageForExclusiveAccess(const std::string& path);
    void unlockImage(const std::string& path);
    
    // Persistence
    enum class SaveMode { Transparent, Confirm, SafeOnly, ReadOnly };
    void setSaveMode(SaveMode mode);
    SaveMode getSaveMode() const;
    
    bool saveImage(const std::string& path);
    bool saveImageAs(const std::string& originalPath, const std::string& newPath);
    bool discardChanges(const std::string& path);
    
    // Pending saves
    std::vector<std::string> getImagesWithPendingChanges() const;
    
    // Observers
    class IObserver {
    public:
        virtual void onImageLoaded(const std::string& path) = 0;
        virtual void onImageUnloaded(const std::string& path) = 0;
        virtual void onImageModified(const std::string& path) = 0;
        virtual void onImageSaved(const std::string& path) = 0;
    };
    void addObserver(IObserver* observer);
    void removeObserver(IObserver* observer);
    
private:
    struct ImageEntry {
        std::shared_ptr<DiskImage> image;
        std::string originalPath;
        bool locked = false;
        std::string lockedBy;  // Emulator instance ID
    };
    std::map<std::string, ImageEntry> _images;
    SaveMode _saveMode = SaveMode::Confirm;
    std::vector<IObserver*> _observers;
};
```

#### 4.2 Emulator Integration

```cpp
// In Emulator or FDD
void FDD::insertDisk(const std::string& path) {
    auto image = DiskManager::instance().loadImage(path);
    if (image) {
        _diskImage = image;
        // Optionally lock for exclusive access
    }
}

void FDD::ejectDisk() {
    if (_diskImage) {
        auto& dm = DiskManager::instance();
        if (_diskImage->isDirty()) {
            // Handle based on save mode
        }
        dm.unloadImage(_originalPath);
        _diskImage = nullptr;
    }
}
```

**Deliverables:**
- [ ] `DiskManager` class
- [ ] Image registry
- [ ] Multi-instance locking
- [ ] Observer pattern
- [ ] Emulator integration

---

### Phase 5: Persistence / Save (2-3 days)

**Objective:** Implement save functionality with protection.

#### 5.1 Save Operations

```cpp
class DiskManager {
    bool saveImage(const std::string& path) {
        auto it = _images.find(path);
        if (it == _images.end()) return false;
        
        auto& entry = it->second;
        if (!entry.image->isDirty()) return true;  // Nothing to save
        
        switch (_saveMode) {
            case SaveMode::ReadOnly:
                return false;  // Never save
                
            case SaveMode::SafeOnly:
                return false;  // Must use saveImageAs
                
            case SaveMode::Confirm:
                // This should trigger UI confirmation
                // For now, just save
                // Fall through
                
            case SaveMode::Transparent:
                return doSave(entry.originalPath, entry.image);
        }
    }
    
private:
    bool doSave(const std::string& path, std::shared_ptr<DiskImage> image) {
        // Determine format from extension
        auto format = detectFormat(path);
        
        // Get appropriate saver
        auto saver = getSaverForFormat(format);
        if (!saver) return false;
        
        // Save
        bool success = saver->save(path, image);
        
        if (success) {
            image->clearDirtyFlags();
            image->snapshotAsOriginal();
            notifyObservers(&IObserver::onImageSaved, path);
        }
        
        return success;
    }
};
```

#### 5.2 Save Confirmation UI Hook

```cpp
// Interface for UI to implement
class ISaveConfirmationHandler {
public:
    enum class UserChoice { Save, SaveAs, Discard, Cancel };
    
    virtual UserChoice confirmSave(
        const std::string& imagePath,
        size_t changedSectors,
        bool isOriginalReadOnly
    ) = 0;
    
    virtual std::string getSaveAsPath(const std::string& originalPath) = 0;
};
```

**Deliverables:**
- [ ] Save to original file
- [ ] Save to new file
- [ ] Save mode configuration
- [ ] UI confirmation hook
- [ ] Format-aware saving via loaders

---

### Phase 6: Format Conversion (2 days)

**Objective:** Universal internal format with multi-format I/O.

#### 6.1 Format Registry

```cpp
class DiskFormatRegistry {
public:
    // Loaders (read)
    void registerLoader(const std::string& extension, 
                        std::shared_ptr<IDiskLoader> loader);
    std::shared_ptr<IDiskLoader> getLoader(const std::string& path);
    
    // Savers (write)
    void registerSaver(const std::string& extension,
                       std::shared_ptr<IDiskSaver> saver);
    std::shared_ptr<IDiskSaver> getSaver(const std::string& path);
    
    // Conversion
    bool convert(const std::string& sourcePath, 
                 const std::string& destPath);
};
```

#### 6.2 Supported Formats

| Format | Extension | Read | Write | Notes |
|--------|-----------|------|-------|-------|
| TR-DOS | .trd | ✅ | ✅ | 16×256, 80-track |
| SCL | .scl | ✅ | ✅ | Archived files |
| FDI | .fdi | ✅ | 🚧 | Full disk image |
| UDI | .udi | 🚧 | 🚧 | Universal |
| TD0 | .td0 | 🚧 | ❌ | Teledisk |
| IMG | .img | ✅ | ✅ | Raw sector dump |

**Deliverables:**
- [ ] Format registry
- [ ] Loader/saver interface
- [ ] TRD saver (TRD loader exists)
- [ ] IMG loader/saver
- [ ] Conversion API

---

## Open Questions

I have the following questions before proceeding:

### Q1: Multi-Instance Sharing

> When multiple emulator instances access the same disk image:
> - Should one instance get exclusive write access while others are read-only?
> - Or should we use a copy-on-write approach?
> - What happens if the user modifies while another instance has it loaded?

### Q2: Auto-Save Behavior

> For auto-save:
> - Save on every sector write? (Expensive, but safe)
> - Save periodically (every N seconds)?
> - Save on disk eject?
> - Save on emulator close?

### Q3: Heatmap Persistence

> Should the write heatmap be:
> - Session-only (reset on load)?
> - Stored with the image (requires metadata file)?
> - User preference?

### Q4: Backup Strategy

> Before overwriting original file:
> - Create backup file (path.bak)?
> - Keep N versions?
> - User preference?

### Q5: SD Card / HDD Management

> You mentioned "sd card and hdd management" - can you elaborate?
> - Is this for emulating +3 disk systems or Plus D?
> - Or for managing images stored on USB/SD on the host system?
> - Different from floppy management?

### Q6: Format Detection

> For files without clear extension:
> - Detect by magic bytes?
> - Detect by file size?
> - Ask user?

### Q7: Test Images

> Do we have or need to create test images for:
> - Non-standard sector counts (9, 18, etc.)?
> - Copy protection patterns?
> - Intentional CRC errors?

---

## Success Criteria

| Criteria | Description |
|----------|-------------|
| SC1 | All standard TR-DOS operations work |
| SC2 | Write-protected disks show status, reject writes |
| SC3 | Modified images can be saved/discarded |
| SC4 | Multiple emulator instances can share images |
| SC5 | Non-standard layouts (IBM, protected) load correctly |
| SC6 | Change tracking identifies modified sectors |
| SC7 | Format conversion between TRD/SCL/IMG works |

---

## Related Documents

- [WD1793 Test Coverage Plan](../2026-01-24-wd1793-test-coverage/implementation-plan.md)
- [WD1793 Documentation](../../WD1793/)
- [MFM Format Specification](../../WD1793/MFM_Format.md)
