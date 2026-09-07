# Disk image loaders — common interface, detection and save policy

> Part of [2026-09-02-universal-track-model](README.md)

## 0. Status (2026-09-04)

Implemented: TRD, SCL, UDI, FDI, DSK/EDSK, TD0, MGT/IMG, Hobeta. Format selection is by extension in
`Emulator::LoadDisk` / `Emulator::SaveDisk`; a byte-signature registry (`DiskImageLoaderRegistry` below) is still a
design. Loaders share the same class shape but no common base class yet.

## 1. Interface

Existing loaders (`LoaderTRD`, `LoaderSCL`) are plain classes with `loadImage() / writeImage() / writeImage(path) /
getImage() / setImage()`. New loaders follow the same shape and additionally implement a small static contract so a
registry can pick the loader from bytes, not only from the extension:

```cpp
struct DiskFormatInfo
{
    const char* id;              // "trd", "scl", "fdi", "udi", "dsk", "td0", "mgt", "img", "hobeta"
    const char* description;
    std::vector<std::string> extensions;
    DiskFormatCapabilities caps;
};

struct DiskFormatCapabilities
{
    bool variableTrackLength;    // can store rawSize() != 6250
    bool clockMarks;             // per-byte sync information
    bool weakBits;
    bool arbitrarySectorIds;     // C/H/R may differ from physical, duplicates allowed
    bool variableSectorSize;     // 128/512/1024 and mixed
    bool idOnlySectors;          // ID field without data field
    bool crcErrors;              // can represent bad ID / data CRC
    bool deletedDataMark;
    bool fm;                     // single density tracks
    bool writable;               // loader implements writeImage
};

class IDiskImageLoader
{
public:
    virtual ~IDiskImageLoader() = default;
    virtual const DiskFormatInfo& info() const = 0;
    virtual bool loadImage() = 0;                       // file → new DiskImage (owned by caller afterwards)
    virtual bool writeImage(const std::string& path) = 0;
    virtual DiskImage* getImage() = 0;
    virtual void setImage(DiskImage*) = 0;
    virtual std::vector<std::string> lastWarnings() const;   // lossy-save and load diagnostics
};

class DiskImageLoaderRegistry
{
public:
    static bool detect(const uint8_t* head, size_t len, const std::string& ext, std::string& formatId);
    static std::unique_ptr<IDiskImageLoader> create(const std::string& formatId, EmulatorContext*, const std::string& path);
    static const std::vector<DiskFormatInfo>& formats();
};
```

`LoaderTRD` / `LoaderSCL` gain the `info()` implementation and are registered; their public methods are untouched.

## 2. Detection order

Signatures first, size heuristics last:

| Format | Signature (offset 0) | Fallback |
|--------|----------------------|----------|
| UDI | `"UDI!"` (`"udi!"` = compressed, rejected with a message) | — |
| FDI | `"FDI"` | — |
| TD0 | `"TD"` / `"td"` | — |
| DSK | `"MV - CPC"` / `"EXTENDED CPC DSK"` | — |
| SCL | `"SINCLAIR"` | — |
| Hobeta | none; header checksum at 15..16 validates | extension `$?` or `.$*` |
| TRD | none | size ∈ {163840, 327680, 655360} or a multiple of 4096 with a valid volume sector (TR-DOS signature 0x10 at track 0 sector 9 offset 0xE7) |
| MGT / IMG | none | size == 819200; MGT vs IMG decided by extension (`.mgt` interleaved sides, `.img` side-major) |
| HFE | `"HXCPICFE"` (v1) / `"HXCHFEV3"` (v3) | — |
| SCP | `"SCP"` | — |

## 3. Capability matrix

| Capability | TRD | SCL | FDI | UDI | DSK | EDSK | TD0 | MGT/IMG | Hobeta | HFE | SCP |
|------------|-----|-----|-----|-----|-----|------|-----|---------|--------|-----|-----|
| variable track length | – | – | – | ✔ | – | – | – | – | – | ✔ | ✔ |
| clock marks | – | – | – | ✔ | – | – | – | – | – | ✔ | ✔ |
| weak bits | – | – | – | – | – | ✔ (multi-copy) | – | – | – | – | ✔ (multi-rev) |
| arbitrary sector IDs | – | – | ✔ | ✔ | ✔ | ✔ | ✔ | – | – | ✔ | ✔ |
| variable sector size | – | – | ✔ | ✔ | ✔ (per track) | ✔ (per sector) | ✔ | – | – | ✔ | ✔ |
| ID-only sectors | – | – | ✔ | ✔ | – | ✔ (len 0) | ✔ | – | – | ✔ | ✔ |
| CRC errors | – | – | ✔ (flags) | ✔ (raw) | ✔ (ST1/ST2) | ✔ | ✔ (flag) | – | – | ✔ | ✔ |
| deleted DAM | – | – | ✔ | ✔ | ✔ | ✔ | ✔ | – | – | ✔ | ✔ |
| FM | – | – | – | ✔ | – | ✔ (rec. mode) | ✔ | – | – | ✔ | ✔ |
| write support | ✔ | ✔ | ✔ | ✔ | ✔ (EDSK) | ✔ | ✔ (uncompressed) | ✔ | ✔ (export file) | planned | planned (single rev) |

UDI is the only format that stores the model losslessly; it is the designated "save anything" target.

## 4. Save policy

`writeImage()` of a lossy format inspects the image first:

1. For every track compare `rawSize()`, encoding, sector list (C/H/R/N, order, CRC validity, DAM) with what the
   target format can express.
2. Anything it cannot express is collected into `lastWarnings()` (e.g. "track 12/0: 9 sectors of 512 bytes cannot
   be stored in TRD, sectors dropped").
3. **Strict formats refuse.** TRD and SCL (256-byte sectors, 16 per track, numbered 1..16) and MGT/IMG
   (10 × 512) return false without writing when the geometry does not match — they never truncate or pad.
   Descriptive formats (FDI, DSK, TD0) save what they can express and list the rest in `lastWarnings()`.
4. **Automatic re-target to UDI** (implemented in the Qt `MainWindow::saveDisk`): when the loader chosen by the
   original file's extension refuses, the image is saved to `<original-name>.udi` next to the original through
   `LoaderUDI` (lossless), the original file stays untouched, `DiskImage::getFilePath()` moves to the new path and
   the user is told why. The File → Save Disk menu also has "Save as .udi...". The same logic now lives in
   `Emulator::SaveDisk(drive, path, allowRetarget)` (returns `DiskSaveResult{saved, retargeted, savedPath, reason}`)
   and posts `NC_FDD_DISK_SAVE_RETARGETED` (payload `FDDDiskPayload` with `_reason`) so headless callers get the
   same behaviour; the Qt window calls it. Tests: `EmulatorSaveDisk_Test`.

TRD/SCL keep their exact current behaviour when the image is a plain TR-DOS disk (no warnings, identical bytes).

## 5. Bit-cell and flux formats

HFE and SCP are designed in [loader-hfe.md](loader-hfe.md), [loader-scp.md](loader-scp.md) and share the
[flux-decoder.md](flux-decoder.md) pipeline (flux → bit cells → byte stream + clock bitmap, and back for save).

## 6. Deferred formats

| Format | Reason |
|--------|--------|
| ISD (IS-DOS) | needs the IS-DOS geometry spec; same raw-sector structure as TRD with a different catalog; low demand |
| PRO (Profi) | Profi-DOS 5×1024 raw dump; fits the model (`TrackFormatSpec::ibm(5, 3)`), needs fixtures |
| DISCiPLE `.d80`, Opus `.opd/.opu` | raw sector dumps, trivial once MGT is done |
