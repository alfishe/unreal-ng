# Integration: TSConf SD card (`sd.zc` on TSConf)

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Reviewed; applies at TSConf phase 6 (PLAN #41) |
| **Layers** | port decoder → port adapter → device → medium, with the slot and the manager beside them: [technical-design.md §1.1](technical-design.md#11-layers-from-the-guests-port-to-the-medium) |
| **TSConf design** | [technical-design.md](../2026-09-27-tsconf/technical-design.md) §3.11 (storage) |

## 1. What the user gets

TS-BIOS, TR-DOS through the vdos, NedoOS and Wild Commander on TSConf, booted from an image or a
PC folder. TSConf's `VirtualFatBlockStore` ("FAT32 over a host folder", xpeccy semantics) **is**
`HostFolderFat`: it is not built twice. What the TSConf design asked for (read-only by default)
becomes a `Session` default, exportable, as for every folder volume.

## 2. The slot

| Field | Value |
|---|---|
| id | `sd.zc`: the same id as on ZX-Evo, because it is the same Z-Controller. A model switch ZX-Evo ↔ TSConf keeps the card (M4) |
| kind | `Block` |
| removable | yes, swap delay 500 ms |
| second card | `sd.zc2` is reserved for the FPGA option `SD_CARD2` (`#77` bit 3), off in shipped builds |
| accepts folder | yes; FAT16 by default, FAT32 by parameter; xpeccy-plus always builds FAT32 (research §2), so its TSConf folder setups map to `fs=fat32` |
| default access | `Session` |
| registered by | the TSConf port decoder |

## 3. Code

| Piece | Source |
|---|---|
| `ZControllerSpi` | shared, already on master (E5) |
| `SdCardSpi` + `attach()` | shared (M1) |
| TSConf DMA to / from SPI | TSConf-specific: the DMA engine calls `ZControllerSpi::WriteData / ReadData` in a loop, or `SdCardSpi::exchange` directly (TSConf design §3.11) |
| `HostFolderFat` | shared (M1) |
| TSConf `ISdBlockStore`, `FileBlockStore`, `VirtualFatBlockStore` | **not built**: `IBlockDevice`, `RawImage`, `HostFolderFat` |
| Gluk extension (SD present / WP in CMOS) | `EvoAvr::SetSdStatus`, driven by the slot, as on ZX-Evo |

## 4. TTD and tests

- TTD follows the common rule ([integration-ttd-snapshots.md](integration-ttd-snapshots.md)).
  The DMA path makes multi-sector reads fast but changes nothing: the medium only changes at a
  write.
- Tests: TSConf VFAT-1…3 run against `HostFolderFat` (TSConf plan).
- Tests: TS-BIOS boot from a folder.
- Tests: a ZX-Evo → TSConf model switch keeps `sd.zc` with its session writes.
