# Integration: IDE hard disks and the ATAPI CD (`ide0.master`, `ide0.slave`, `ide1.*`)

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Reviewed; applies with IDE rollout 1 (PLAN #13a), manager phase M6 |
| **Layers** | port decoder → port adapter → device → medium, with the slot and the manager beside them: [technical-design.md §1.1](technical-design.md#11-layers-from-the-guests-port-to-the-medium) |
| **IDE design** | [2026-09-25-ide-hdd-design.md](../2026-09-21-profi/2026-09-25-ide-hdd-design.md) (disk core §6, media §7, config §8, TTD §10) |
| **Boards** | Profi, Nemo (Pentagon), Nemo-A8, ZX-Evo NemoIDE (E6), SMUC (Scorpion), ATM; all through the shared `AtaChannel` |

## 1. What the user gets

- An IDE disk from an image (`.img`, `.hdf`, `.hdi`, `.vhd`) or from a PC **folder**: a FAT16
  volume below 2 GB.
- A CD from an ISO.
- All of it in the same media panel and the same `media` verbs.

## 2. The unit is the slot (review round 2, G2)

A hard disk and its medium are one thing: the disk *is* the image. A CD drive and its disc are two
things: the drive stays on the bus while discs change. WinUAE keeps the drive in the mount list and
the disc in a separate `cdimageN` slot. MAME configures each unit as `hdd` or `cdrom` (research §1;
`mame/src/devices/bus/spectrum/zxbus/nemoide.cpp:64-72`). Here **each IDE unit is one slot**, and its
configured device type sets the kind:

| `<slot>.device` (legacy `[HDD] CD0` / `CD1`) | Kind | Removable | On insert / eject |
|---|---|---|---|
| `disk` (default) | `Block` | no: insert / eject only while paused (the guest has no disk-change protocol) | the unit becomes a disk with that medium / no device |
| `cdrom` | `Optical` | yes, swap delay 3 s | the ATAPI drive stays on the bus; the disc changes; "medium changed" unit attention |

Ids: `ide0.master`, `ide0.slave`, and `ide1.*` for a second channel (Sprinter has two, MAME
`sprinter.cpp:1967-1969`). A CD sits on whichever unit is configured as `cdrom`: ZX-Evo, Nemo, SMUC
and ATM usually use the slave, Sprinter `ide0.slave`. The device type is machine configuration, not
a medium.

## 3. Code

| Where | Change |
|---|---|
| `AtaChannel` (IDE R1-1) | registers `ide0.master` / `ide0.slave`; `Attach` → `AtaDevice::Attach(IBlockDevice&, DriveConfig)` (IDE design §6.1 takes a `unique_ptr`; becomes non-owning, the manager owns) |
| `AtapiCdrom` (IDE R1-7) | serves a unit configured as `cdrom` (`Optical`); `Attach` / `Detach` raise the ATAPI "medium changed" unit attention |
| `MediaFormatRegistry` | adds HDF, HDI, VHD (IDE R1-5) and ISO (R1-7) as block / optical formats; `NativeGeometry()` from their headers |
| folder volumes | `HostFolderFat` with the IDE profile: FAT16 below 2 GB, geometry 16 heads × 63 sectors (xpeccy-plus `hdd.c:561-577`) unless configured |
| write policy | images `WriteThrough` by default (IDE design §6.4, UnrealSpeccy behavior); folders `Session`; `HD0RO=1` → `ReadOnly` → ABRT |
| `[HDD]` keys | mapped by `MediaConfig` (technical design §7); the IDE design's own parsing is not written |

## 4. TTD

IDE design §10.0 (rollout 1): the first IDE command register write ends a recording. Under the
common rule ([integration-ttd-snapshots.md](integration-ttd-snapshots.md)) this becomes: controller
state in the blob, and a write is a barrier. `AtaDevice` state is POD by design, so this is a small
step, and it can land together with the IDE core.

## 5. Tests

- IDE L3 backend tests (IDE design §12.3) move to `core/tests/emulator/io/storage/`, next to the
  block layer.
- New: Profi / Nemo / ZX-Evo HDD boot from a **folder** with the same file tree as the image fixture.
- New: a disc swap on a `cdrom` unit raises unit attention.
- New: ERS "D. CD boot" after a swap.
