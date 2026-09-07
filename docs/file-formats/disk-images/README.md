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
| [Hobeta](hobeta.md) | `.$?` | ✔ | ✔ | Single TR-DOS file with header |
| HFE | `.hfe` | — | — | Planned (flux-level) |
| SCP | `.scp` | — | — | Planned (SuperCard Pro flux) |

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
