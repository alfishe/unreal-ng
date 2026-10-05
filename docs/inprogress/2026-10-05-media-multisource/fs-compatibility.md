# Multi-source media: file-system compatibility (FAT16 / FAT32 / ISO 9660)

| | |
|---|---|
| **Date** | 2026-10-05 |
| **Status** | Analysis for review |
| **Question** | Can FAT16 and FAT32 disks be combined, optionally together with ISO 9660, and on what terms? |
| **Short answer** | **Yes for file-level composition in every direction, with clear size and name limits.** A FAT target can take files from FAT12/16/32 and ISO sources. An ISO target can take files from FAT and ISO sources. Partitions keep FAT16 and FAT32 side by side unchanged. Not possible: ISO as a hard-disk partition, a FAT12 target, and block-level merging of two file systems. |

## 1. Why file-level composition makes the mix possible

Two FAT volumes cannot be merged sector by sector. Each has its own boot sector, FAT, root
directory and cluster numbering, and they collide on the same LBAs. The design composes at the
**file** level instead ([architecture.md](architecture.md) §0):

- A source is only asked for its **tree** (names, sizes, times, attributes) and, per file, the
  **extents** where its bytes lie. Which file system the source uses stops mattering after this
  step.
- The target's metadata (boot sector, FAT, directories, or the ISO descriptors and path tables) is
  always **synthesized fresh**. In graft mode it is patched in place in the base, which must then be
  FAT16 or FAT32.

So "mixing FAT16 and FAT32" really means: **files from either are laid out under one new file
system**. The constraints come only from the target's limits and from name and time conversion.

## 2. Limits of each file system

| | FAT12 | FAT16 | FAT32 | ISO 9660 L1 | ISO 9660 L2 | Joliet (SVD) |
|---|---|---|---|---|---|---|
| Cluster / block count | < 4 085 | 4 085 – 65 524 | ≥ 65 525, ≤ 268 435 445 | 32-bit blocks | same | same |
| Unit size | 512 B – 4 KiB clusters | 512 B – 32 KiB clusters (64 KiB non-standard) | 512 B – 32 KiB clusters | 2 048 B blocks | same | same |
| Max volume (practical) | 16 MiB | **2 GiB** (4 GiB with 64 KiB clusters, not for ZX guests) | 2 TiB (32-bit sector count, 512 B sectors) | 8 TiB | same | same |
| Min volume | — | ~2 MiB | **~32 MiB** (65 525 clusters × 512 B) | — | — | — |
| Max file | 4 GiB − 1 (volume-bound) | 4 GiB − 1 (volume-bound: 2 GiB) | 4 GiB − 1 | 4 GiB − 1 per extent (L3 multi-extent beyond) | same | same |
| Root directory | fixed region | **fixed region**: 512 entries is the compatible value; long names use slots | a cluster chain, unlimited | extent | extent | extent |
| Entries per directory | 65 536 slots (2 MiB) | 65 536 slots | 65 536 slots | unlimited (bounded by extent size) | same | same |
| Names | 8.3 + LFN (255 UTF-16) | 8.3 + LFN | 8.3 + LFN | 8.3 d-chars `A-Z 0-9 _` + `;1` | 31 d-chars | 64 UCS-2 chars |
| Case | insensitive (8.3 upper; LFN case-preserving) | same | same | upper only | upper only | case-sensitive |
| Depth | unlimited (260-char paths on DOS) | same | same | 8 levels | 8 levels | 8 (enforced by us by default) |
| Timestamps | 1980–2107, 2 s | same | same | 1900–2155, 1 s, UTC offset | same | same |
| Attributes | R/H/S/A/D/V | same | same | hidden, directory, associated | same | same |

The FAT type always follows the cluster count, as strict readers (ChaN FatFs, ZX Next firmware,
`FatVolumeReader`) decide it. A "FAT16" whose cluster count is out of range is read as something
else, so the builder picks the cluster size to keep the count in range (as `HostFolderFat` does
today).

## 3. Source × target matrix

✓ = supported in v1 · ⚠ = supported with conditions (see notes) · ✗ = not supported (build fails with `NotSupported` and the reason)

| Source ↓ / Target → | FAT16 rebuild | FAT32 rebuild | FAT graft (as upper layer) | FAT graft (as **base**) | ISO 9660 + Joliet | MBR partition (passthrough) |
|---|---|---|---|---|---|---|
| Host folder | ✓ | ✓ | ✓ | ✗ (a folder has no layout) | ✓ | ✗ (compose it instead) |
| FAT12 image / partition | ✓ | ✓ | ✓ | ⚠ v2 (small volumes; no use case yet) | ✓ | ✓ |
| FAT16 image / partition | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| FAT32 image / partition | ⚠ 1 | ✓ | ✓ | ✓ | ✓ | ✓ |
| ISO 9660 (+ Joliet) image | ⚠ 2 | ⚠ 2 | ⚠ 2 | ✗ | ✓ | ✗ 3 |
| CHD hard disk (FAT inside) | ✓ | ✓ | ✓ | ✓ (read-only base; commit S3 ✗) | ✓ | ✓ |
| CHD CD / CUE+BIN data track (ISO inside) | ⚠ 2 | ⚠ 2 | ⚠ 2 | ✗ | ✓ | ✗ 3 |
| Audio CD tracks | ✗ | ✗ | ✗ | ✗ | ✗ 4 | ✗ |

Notes:

1. **FAT32 source → FAT16 target** works when the selected files fit FAT16: the union is at most
   about 2 GiB, the root has at most 512 slots, and no single file exceeds the volume. The
   validator names the first entry that breaks a limit. A typical use is pulling `/GAMES` out of a
   large FAT32 card into a small FAT16 disk for a guest that only reads FAT16.
2. **ISO source → FAT target** works for reading. Data comes from 2 048-byte blocks, each of which
   is four 512-byte sectors, so alignment is exact. Names come from Joliet when present, else the
   ISO name with `;1` stripped. ISO's hidden flag maps to FAT's hidden attribute. Guest writes to
   such files land in the change layer. Their owning layer is read-only, so S4 write-back
   **copies them up** into the writable layer ([flatten-strategies.md](flatten-strategies.md) §4).
   Multi-extent (Level 3) files become multi-extent `FileData`. Interleaved files (file unit size ≠
   0) are refused, as no ZX-era ISO uses them.
3. **ISO as a hard-disk partition** is refused. No ZX guest mounts ISO 9660 from an MBR partition,
   and 2 048-byte addressing on a 512-byte disk would need a translation no guest expects. Put the
   ISO in a CD slot, or compose its files into a FAT partition.
4. **Audio tracks** stay in `AudioFolderDisc` and the CUE / CHD paths. A data + audio composite
   (Enhanced CD) is a possible later extension.

## 4. Conversion rules between file systems

| Aspect | → FAT target | → ISO target |
|---|---|---|
| Long names | kept as LFN (UTF-16); 8.3 generated by `FatNameMapper` (unique `~N` tails, code page CP866 / CP1251) | Joliet name (UCS-2, truncated to 64 with a unique tail); ISO L1 name generated like an 8.3 name in d-characters |
| Short names from a FAT source | decoded with the **layer's** code page (`codepage:` per layer, default the target's), then re-mapped. An 8.3-only name stays the same when it is valid in the target code page | upper-cased, invalid characters become `_` |
| ISO source names | Joliet → LFN; ISO name (minus `;1` and a trailing `.`) → LFN when no Joliet | kept |
| Case collisions | `Readme.txt` and `README.TXT` are one entry; the upper layer wins (FR-11) | Joliet keeps both; the ISO L1 names get unique tails |
| Timestamps | clamped to 1980–2107, rounded down to 2 s; the report counts clamped entries | 1900–2155, UTC offset 0 |
| Attributes | FAT R/H/S/A kept; ISO hidden → H; host: none (A set, like `HostFolderFat`) | H → hidden flag; R/S/A dropped (reported once per build) |
| Volume label | descriptor `label`, else the bottom layer's label, else `UNREAL NG` | descriptor `label` → PVD volume id (d-chars) + Joliet volume id |
| Slack bytes past EOF in the last sector | zeroed in `dst` after the source read (a `memset` of the tail, no extra copy); this keeps output identical across sources whose slack differs | same |
| Empty files | first cluster 0, no data run | extent length 0 |
| Sparse / zero extents | `FileData::Zero` reads as zeros without touching the source | same |
| Deleted entries, LFN orphans, volume-label entries in a FAT source | ignored (never become files) | same |
| ISO associated files, version > 1 | ignored, reported | same |

## 5. Combination scenarios

| # | Scenario | Verdict | How |
|---|---|---|---|
| S-1 | FAT16 image + FAT32 image merged into one **FAT32** volume | ✓ | rebuild; both are layers; size = union + free |
| S-2 | the same into a **FAT16** volume | ⚠ | rebuild; only if the union fits FAT16 (≤ 2 GiB, root ≤ 512 slots) |
| S-3 | FAT16 base image (graft) + FAT32 image as an upper layer under `/DATA` | ✓ | graft; the upper layer's file system is irrelevant; needs the base's free clusters ≥ the upper data, and FAT16 root slots if mounted at `/` |
| S-4 | FAT32 base (graft) + host folders + an ISO's `/DEMOS` | ✓ | graft; the ISO's files are read-only extents in the base's free space |
| S-5 | FAT16 partition + FAT32 partition on one IDE disk | ✓ | partitions; each volume untouched (passthrough) or composed; types `#06`/`#0E` and `#0B`/`#0C` |
| S-6 | FAT partition + ISO partition on one IDE disk | ✗ | note 3; use an IDE CD unit for the ISO |
| S-7 | Two ISOs + a host folder into one **ISO** CD | ✓ | rebuild ISO; read-only medium |
| S-8 | FAT image + host folder into an **ISO** CD | ✓ | rebuild ISO |
| S-9 | ISO as the **base** with folder layers, target ISO | ✓ (as rebuild) | the ISO is the bottom layer of an ISO rebuild; its layout is not kept (no guest depends on ISO block positions). A bootable base keeps El Torito: the boot catalog is rebuilt with new LBAs, the boot images are read from the base by extent; a base without one can get it from a boot layer (D-6) |
| S-10 | Graft on a FAT16 base whose root is full (512 slots) with new files at `/` | ✗ graft, ✓ rebuild | `auto` falls back to rebuild and says why; `graft` fails with `DoesNotFit` |
| S-11 | FAT32 composite smaller than 32 MiB | ⚠ | FAT32 needs ≥ 65 525 clusters: the builder raises the volume to the minimum (free space), as `HostFolderFat` does, or fails if `size` is fixed below it |

## 6. Guest support and the default target

The target's file system must be one the guest can read. The facts below come from the guests'
sources and from what already runs on master (researched 2026-10-05). The emulator enforces them
through the slot's existing `MediaSlotDescriptor::fsCompatibility` / `defaultFs` (the rule of
BUGS.md #2), so a composite follows exactly the same per-slot rules as a folder volume.

| Guest (slot) | FAT12 | FAT16 | FAT32 | ISO 9660 | Evidence |
|---|---|---|---|---|---|
| NedoOS (ZX-Evo / ATM `sd.zc`, IDE, NeoGS SD) | ✓ | ✓ | ✓ | ✓ (CD mount, `cdplay`) | its disk driver is ChaN FatFs (`NOS/kernel/fatfsdrv.asm`, [baseconf-hardware-reference.md](../2026-09-15-atm-baseconf-highres-ports/baseconf-hardware-reference.md)), which reads all three; FAT16 boot proven by M1 ACC-3 |
| ERS boot menu (ZX-Evo) | — | ✓ | ✓ | `AUTORUN.ZX` | `ROM/bootsecfat.a80` carries both BPB layouts (FAT12/16 at `:4-28`, FAT32 at `:30-44`); FAT16 proven by M1 ACC-1/2 |
| Wild Commander (ZX-Evo) | — | ✓ | ✓ | — | listed as a FAT16 / FAT32 target in the IDE design ([2026-09-25-ide-hdd-design.md](../2026-09-21-profi/2026-09-25-ide-hdd-design.md) §7.4.4) |
| TS-BIOS + Wild Commander (TS-Conf `sd.zc`) | — | ✗ | ✓ | — | the slot declares `fsCompatibility = {Fat32}` (`portdecoder_tsconf.cpp`, `TsConfMedia_Test.SdSlotIsFat32Only`); a volume from sector 0, no MBR |
| Estex DSS (Sprinter IDE) | ✓ | ✓ | ✗ | — | DSS reads FAT12 / FAT16 only ([Sprinter hardware reference](../2026-09-28-sprinter/hardware-reference.md) §9.3: `fat_x.asm`, `DOSBOOT4.ASM:351-375`); partition types `#01 #04 #06 #0E`, extended `#05 #0F`; `#0B #0C` skipped; the boot loader reads **MBR entry 0 only**; boot code at LBA 1-3 ([sprinter-hdd recipe](../../../.recipe/media/sprinter-hdd.md)) |
| PQ-DOS (Profi IDE) | ✓ (boot floppy) | ✓ | not evidenced | — | `testdata/machines/profi/pqdos/pqdos-hdd-small.img`: MBR entry 0 type `#06`, FAT16; `pqdos1.fdi` FAT12; no FAT32 source or image found |
| NextZXOS / esxDOS (Next, later) | ✓ | ✓ | ✓ | — | [integration-next.md](../2026-09-28-storage-manager/integration-next.md) |
| NeoGS SD (loader, players) | — | ✓ | ✓ | — | [integration-neogs-sd.md](../2026-09-28-storage-manager/integration-neogs-sd.md) |

**Default target choice** (FR-2, `fs: auto`):

1. If the slot declares `fsCompatibility`, only those types are allowed. An explicit `fs` outside
   the set is refused (`BadRequest`, as for folders today).
2. Otherwise, or within the set: the slot's `defaultFs` if the union plus free space fits it, else
   the next allowed type (FAT16 → FAT32, the BUGS.md #2 rule); else `DoesNotFit`.
3. Graft: the base decides; a base whose type the slot does not allow is refused.
4. Partitions: each composed partition follows rules 1-2. Passthrough partitions are checked
   against `fsCompatibility` too, as inserted images are today.
5. Optical slot: ISO.

**Consequences for the slot descriptors** (work items for phase C2):

| Slot | Today | Needed |
|---|---|---|
| Sprinter `ide0.*`, `ide1.*` | no `fsCompatibility`: a FAT32 folder or composite is accepted, although DSS cannot read it and the Sprinter storage design says it must be refused | `fsCompatibility = {Fat16}` (FAT12 targets are a non-goal); `ComposeSprinter_Test.Fat32Refused` |
| Sprinter, partitions mode | — | the DSS partition must be **entry 0**; types `#0B`/`#0C` are pointless (DSS skips them), so the validator warns; a rebuilt DSS boot volume would lose the boot code at LBA 1-3: `build: auto` picks graft for a bootable DSS base; a rebuild carries the base's reserved boot sectors (D-6), and a composite with no DSS base gets the loader from a boot layer (`boot.reserved`) |
| Profi `ide0.*` | no `fsCompatibility` | keep FAT16 as `defaultFs`; no restriction (no evidence either way for FAT32); ACC-C4 checks FAT32 once and the result sets the descriptor |
| ZX-Evo `sd.zc`, IDE; NeoGS; Next | FAT16 default, both allowed | unchanged |
| TS-Conf `sd.zc` | `{Fat32}` | unchanged |

## 7. Verdict

- **FAT16 ↔ FAT32 mix: fully feasible.** Merge into either type if the limits allow, graft into
  either base, or keep each type as its own partition. The cost is one name and time conversion
  pass at build time; read speed and memory are the same as a single-source volume.
- **With ISO: feasible as a source everywhere and as a CD target.** It is not feasible as a
  hard-disk partition or as a graft base. CD targets are read-only, so they need no write
  attribution.
- **Risks** are in the guests, not the composition: a guest that only reads FAT16, a guest that
  mishandles one sector per cluster on large volumes (already avoided by the cluster-size rule), or
  a guest that depends on files being at fixed sectors. Graft mode exists for the last case.
