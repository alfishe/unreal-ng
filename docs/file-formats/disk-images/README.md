# Disk Image Formats

Unreal-NG supports loading and saving disk images in multiple formats. All formats map to the internal **universal track model** which stores variable-length raw track streams with clock bitmap and sector index.

## Supported Formats

| Format | Extensions | Load | Save | Capabilities |
|--------|------------|:----:|:----:|--------------|
| [TRD](trd.md) | `.trd` | ✔ | ✔ | TR-DOS raw sectors (16×256 fixed) |
| [SCL](scl.md) | `.scl` | ✔ | ✔ | TR-DOS file archive |
| [UDI](udi.md) | `.udi` | ✔ | ✔ | Lossless native format (variable tracks, clock marks, FM) |
| [FDI](fdi.md) | `.fdi` | ✔ | ✔ | Sector-based with CRC flags |
| [DSK/EDSK](dsk.md) | `.dsk` | ✔ | ✔ | CPC format (EDSK supports variable sectors) |
| [TD0](td0.md) | `.td0` | ✔ | ✔ | Teledisk (advanced compression, FM, comments) |
| [MGT/IMG](mgt.md) | `.mgt`, `.img` | ✔ | ✔ | +D/DISCiPLE raw sectors (10×512) |
| [Raw PC floppy](rawpc.md) | `.img`, `.ima` | ✔ | ✔ | 720 KB (9×512, DD) / 1.44 MB (18×512, HD) sector dump |
| [MB-02](mb02.md) | `.mb2`, `.mbd` | ✔ | ✔ | MB-02 (BS-DOS) 1024-byte sectors in logical order, recognized by the boot sector |
| [MDOS](mdos.md) | `.d40`, `.d80` | ✔ | ✔ | Didaktik D40 / D80 (MDOS) 512-byte sectors in logical order, recognized by the boot sector |
| [Profi CP/M](pro.md) | `.pro` | ✔ | ✔ | Profi CP/M 5×1024 sector dump, cylinder 0 side 0 with sector IDs 1, 2, 3, 4, 9 |
| [Hobeta](hobeta.md) | `.$?` | ✔ | ✔ | Single TR-DOS file with header |
| HFE | `.hfe` | — | — | Planned (flux-level) |
| SCP | `.scp` | — | — | Planned (SuperCard Pro flux) |

## Hard disks and SD cards

Block media (IDE units, SD cards) take raw images (`.img`, `.hdd`, ...), HDF, HDI, fixed VHD and
[CHD](chd.md) (MAME's compressed hard-disk format, read and written). They are not track images: a
block medium is a list of 512-byte sectors.

## Detection

Formats with magic signatures are detected automatically:
- **UDI**: `UDI!` at offset 0
- **FDI**: `FDI` at offset 0
- **TD0**: `TD` or `td` at offset 0
- **DSK**: `MV - CPC` or `EXTENDED CPC DSK` at offset 0
- **SCL**: `SINCLAIR` at offset 0

Formats without signatures use size/extension heuristics:
- **Hobeta**: 17-byte header with valid checksum
- **TRD**: size in {163840, 327680, 655360} or TR-DOS volume signature
- **Raw PC floppy**: size 737280 or 1474560, checked before the TRD rule
- **MB-02**: the boot sector (`18 .. .. 02`, 2 sides, 1 sector a cluster) and its geometry matching the size, before the TRD and MGT rules
- **MDOS**: `SDOS` at 204 of the boot sector and its geometry matching the size, before the raw PC size rule
- **Profi CP/M**: the `.pro` extension and a size of 80..83 cylinders x 10 240 bytes (819 200 like an MGT: the extension decides)
- **MGT/IMG**: size 819200; `.mgt` = interleaved sides, `.img` = sequential

## Universal Track Model

All formats map to the internal model:
- **Variable track length**: 3125 (FM) to 6464 bytes (real drive dumps)
- **Clock bitmap**: marks sync bytes (A1/C2 address marks)
- **Sector index**: ID fields (C/H/R/N), data, CRC status, deleted marks
- **Encoding**: MFM (double density) or FM (single density)

UDI is the only format that stores this model losslessly. When saving to a lossy format (TRD, SCL), the emulator validates compatibility and re-targets to UDI if needed.

## See Also

- [MFM Data Encoding](mfm-data.md) — low-level track format
- [docs/beta-disk-interface/](../../beta-disk-interface/) — TR-DOS specifics
