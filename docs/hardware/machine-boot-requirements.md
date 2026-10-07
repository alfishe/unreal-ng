# Machine boot requirements

Reference for getting a specific machine model to actually boot something: which firmware it
runs, what it can boot from out of the box, what filesystem/geometry that medium needs, and which
files must be on it. This is not a design document — for the "why", see the per-family recipes
linked from each section and the design docs under `docs/inprogress/`.

Every boot source below is a [media slot](../features/media.md); insert media with the `media`
verb (MCP), `media insert` (CLI), `POST /api/v1/emulator/{id}/media/{slot}/insert` (WebAPI), or the
Qt media panel. Slot IDs and aliases are listed per machine in
[docs/features/media.md](../features/media.md#slots) and explained in
[.recipe/media/use-media-slots.md](../../.recipe/media/use-media-slots.md).

## Common pitfalls

These account for most "it won't boot" reports. Check them before suspecting the emulator.

1. **MBR vs. no-MBR is not a matter of taste — each boot path expects one specific layout, and
   the other one hangs.**
   - **IDE hard disks on ATM710 / ATM3 (ZX-Evo)** need an **MBR partition table with FAT
     partitions**, built by the in-ROM `hddfdisk` tool (or an image already laid out that way).
     Verified working example:
     [`testdata/machines/baseconf/hdd-images/README.md`](../../testdata/machines/baseconf/hdd-images/README.md)
     — after `B. HDD boot` the IDE unit issues real scattered-LBA `READ SECTORS` traffic and the
     CPU keeps executing, frame over frame.
   - **SD cards on TS-Conf / the ZX-Evo Z-Controller slot (`sd.zc`)** need the opposite: a **raw
     FAT16/FAT32 filesystem starting at sector 0, with no partition table at all**. Verified
     worked example (and its own caveat that the specific images have not been confirmed booting
     yet):
     [`testdata/machines/tsconf/wildcommander/README.md`](../../testdata/machines/tsconf/wildcommander/README.md).
   - **Mounting one where the other belongs is the classic mistake**: a TS-Conf-style raw-FAT SD
     image mounted on `ide0.master` of an ATM3 loads content the IDE boot path cannot parse — the
     machine parks in `DI`+`HALT` within a couple of frames and never touches the disk again
     (confirmed 2026-09-30, contrasted directly against the correct MBR+FAT image in the same
     README above). Conversely, an MBR-partitioned image put on `sd.zc` is not the flat filesystem
     TS-BIOS/xBIOS expect there.
2. **A CD-ROM image only mounts on a unit that is already configured as a CD-ROM drive.** An ISO
   never turns a hard-disk unit into a CD drive by itself; use the insert option `device=cdrom`
   on an **empty** unit, or the Qt media panel's prompt when you drop an ISO on a disk unit. Only
   ZX-Evo ships a CD drive by default (`[HDD] CD1=1` on the IDE slave); every other machine's IDE
   units are hard disks with no CD-ROM drive configured, so an ISO dropped on a Pentagon or Profi
   is refused ("no CD-ROM drive on this machine"), not silently mounted as a disk. See
   [docs/inprogress/2026-09-29-media-drop-targets/design.md](../inprogress/2026-09-29-media-drop-targets/design.md)
   §2 rule 3 and [docs/features/media.md](../features/media.md#hard-disks-and-the-cd-rom-drive-ide).
3. **TR-DOS/Beta-128 autostart only engages from drive A**, and only when the disk is TR-DOS
   format with a `.B` BASIC file on it (a `boot.B`, or the single `.B` file present, or — with
   several — the bundled commander). A disk in any other format, or in drive B, mounts but never
   autostarts. See [.recipe/run/autostart-disk.md](../../.recipe/run/autostart-disk.md).
4. **A dirty medium never silently leaves its slot.** If a previous session left unsaved writes
   on a slot you are about to reuse, `insert`/`swap` answers `dirty` (HTTP 409) until you add
   `save`, `export` or `discard` — this can look like "the new disk didn't take."
5. **An empty optical/IDE drive on ZX-Evo's "D. CD boot" or "B. HDD boot" just keeps retrying**
   with nothing inserted; it is not a hang, it is waiting for media.
6. **Each disk slot reads only certain FAT types, and the emulator refuses the others.** Sprinter and Profi IDE hard
   disks take FAT12 / FAT16 only (Estex DSS and PQ-DOS read no FAT32), the TS-Conf SD card FAT32 only, the other
   slots FAT16 and FAT32. A FAT32 image, folder or composite on a Sprinter or Profi disk is refused with the reason
   instead of mounting a disk the DOS cannot see. The table:
   [docs/features/media.md](../features/media.md#which-file-system-a-slot-takes).
7. **ROM choice is a config-file setting, not a runtime option.** Switching ROM trees (e.g. ATM3's
   `[EVO] Fpga=legacy` for the older `rom/zxevo.rom`) requires editing `unreal.ini` and creating a
   new instance — there is no live switch.

## Quick reference

| Model id | Full name | Firmware / ROM | Boots from (out of the box) | Required medium format |
|:--|:--|:--|:--|:--|
| `48K` | ZX-Spectrum 48K | `data/rom/48.rom` | tape (`tape`); Beta-128-style FDC is decoded on every Sinclair model in this build's shipped configs, so a TR-DOS floppy in `fdd.a` also autostarts | tape: `.tap`/`.tzx`/etc.; floppy: TR-DOS TRD (see [autostart-disk.md](../../.recipe/run/autostart-disk.md)) |
| `128k` | ZX-Spectrum 128K | `data/rom/128.rom` | tape, TR-DOS floppy `fdd.a`–`fdd.d` | same as above |
| `PLUS2` | ZX-Spectrum +2 (grey, Amstrad ROM) | `data/rom/plus2.rom` | tape, TR-DOS floppy (Beta-128 decoded the same way as other Sinclairs in this build) | same as above |
| `PLUS2A` | ZX-Spectrum +2A (the +3 without its floppy controller) | `data/rom/plus2a.rom` | tape; no native +3 FDC (that is the point of this model) | tape |
| `PLUS3` | ZX-Spectrum +3 | `data/rom/plus3.rom` | tape, built-in +3 FDC on `fdd.a`/`fdd.b` (present from power-on, no Beta-128 reset dance) | tape; `+3DOS` `.dsk` images insert directly |
| `PENTAGON` | Pentagon 128/512/1024 | `data/rom/pentagon.rom` (`ram_size` selects the 128/512/1024 decoder, not the ROM) | tape, TR-DOS floppy `fdd.a`–`fdd.d`; IDE hard disk `ide0.master`/`ide0.slave` (Nemo IDE, `[HDD] Scheme=NEMO`) | TR-DOS TRD for floppy; MBR+FAT for the Nemo IDE HDD (same rule as ATM, see pitfall 1) |
| `SCORPION` | ZS Scorpion 256/1024 | `data/rom/scorpion.rom` (Shadow Monitor "SOS" ROM, not the Sinclair 48K ROM) | tape, built-in Beta-128 TR-DOS floppy `fdd.a`–`fdd.d` (no interface to insert) | TR-DOS TRD |
| `PROFSCORP` | ZS Scorpion + ProfROM | `data/rom/scorpion.rom` + `data/rom/scorp_prof401.rom` (ProfROM, selected via `#7EFD`) | same as `SCORPION` | TR-DOS TRD |
| `PROFI` / `PROFI3` | Profi v5 / v3 | `data/rom/profi.rom` (v5) / `data/rom/profi/kramis-v03.rom` (v3); SYS/menu ROM, boots there first | tape, TR-DOS floppy `fdd.a`–`fdd.d`, IDE hard disk `ide0.master`/`ide0.slave` (`[HDD] Scheme=PROFI`) | TR-DOS TRD; hard disk: raw image, geometry from its ProfiHiDD header (16×16 default) — **not** MBR-dependent the way ATM/ZX-Evo is |
| `ATM710` | ATM-Turbo 2+ v7.10 | `data/rom/atm2.rom` (`sos`/`dos`/`128`/`sys` pages) | tape, TR-DOS floppy, IDE hard disk `ide0.master`/`ide0.slave` (`[HDD] Scheme=ATM`) via xBIOS | TR-DOS TRD; HDD: **MBR + FAT partitions built by `hddfdisk`** |
| `ATM3` | ZX-Evo (ATM Turbo 3, BaseConf) | `data/rom/zxevo-fe.rom` (official NedoPC BaseConf image; `[EVO] Fpga=legacy` switches to the older `data/rom/zxevo.rom`) | ERS menu: **Z.** TR-DOS floppy, **B.** IDE hard disk (NemoIDE, `[HDD] Scheme=NEMO-DIVIDE`), **D.** CD (`ide0.slave`, `CD1=1` by default), **5.** SD card (`sd.zc`, Z-Controller — NedoOS) | TR-DOS TRD; HDD: **MBR + FAT** (pitfall 1); CD: ISO 9660 with `AUTORUN.ZX` in the root; SD: **raw FAT16/FAT32, no MBR** (pitfall 1) |
| `TSL` (alias `TSCONF`) | TS-Conf (ZX-Evo, TS-Labs configuration) | `data/rom/zxevo.rom` (TS-BIOS, page 0) | TS-BIOS boot menu: TR-DOS/Beta-128 (virtual drives), SD card `sd.zc` ("BD boot.$c"), Nemo IDE `ide0.master`/`ide0.slave` (`[HDD] Scheme=NEMO-DIVIDE`) | SD: **raw FAT16/FAT32, no MBR** (pitfall 1); a blank CMOS starts in TS-BIOS Setup, not the boot menu — see below |
| `ZXPOLY-48K`/`-128K`/`-PENTAGON` | ZX-Poly (4 synced instances of a base model) | same ROM as the base model | same boot sources as the base model, plus `.zxp` (all 4 modules at once) and `.prom` (the ZX-Poly Test ROM) | same filesystem rules as the base model |
| `SPRINTER` | Peters Plus Sprinter Sp2000 | **not yet implemented** — planned BIOS 3.04 (3.06 selectable) + Estex DSS | design-only: floppy (Beta-128/WD1793), IDE ×2 (HDD + ATAPI CD), PC folder | design-only, see [Sprinter](#sprinter-spринter-not-yet-implemented) below |

Sources for every ROM path: each model's `[ROM]` section in `data/configs/<config-folder>/unreal.ini`
(config folder names, e.g. `pentagon128k`, `atm3`, `ts-conf`, do not always match the model id —
confirm with `GET /api/v1/emulator/{id}` → `config_folder`, per
[.recipe/_common/machines.md](../../.recipe/_common/machines.md) pitfalls).

## ZX-Spectrum-compatible: 48K, 128k, PLUS2, PLUS2A, PLUS3

Ground truth: [.recipe/machines/spectrum.md](../../.recipe/machines/spectrum.md).

- **Firmware**: one ROM file per model (`48.rom`, `128.rom`, `plus2.rom`, `plus2a.rom`,
  `plus3.rom` under `data/rom/`), no menu or setup step — the machine boots straight into BASIC.
- **Boot sources**: the tape deck (`tape` slot) on every model; TR-DOS floppy drives
  (`fdd.a`–`fdd.d`, aliases `A`–`D`) on `48K`/`128k`/`PLUS2`/`PLUS2A` because this build's shipped
  configs decode a Beta-128-style FDC on every Sinclair model regardless of whether real hardware
  had one (verified 2026-09-23 — see the pitfall in `.recipe/machines/spectrum.md`, "not
  PLUS3-exclusive"); `PLUS3` additionally has its own built-in +3 FDC present from power-on
  (`fdd.a`/`fdd.b` only — there is no drive C), which reads `+3DOS` `.dsk` images directly.
- **Filesystem parameters**: a tape is any file format the tape loaders read (`.tap`, `.tzx`,
  `.spc`, `.sta`, `.ltp`, `.zxt`) or a folder ([media.md](../features/media.md#tapes)); a TR-DOS
  floppy is a `.trd` image or a UDI image, autostart rules in
  [.recipe/run/autostart-disk.md](../../.recipe/run/autostart-disk.md).
- **Files that must be present**: for autostart, a TR-DOS disk needs at least one `.B` BASIC file
  in its catalog (ideally named `boot.B`); a tape needs nothing special beyond a standard header —
  `LOAD ""` reads whatever block comes first.
- No IDE board (`[HDD] Scheme=NONE`), so no hard disk or CD boot path on any of these five models.

## Pentagon (128/512/1024)

Ground truth: [.recipe/machines/pentagon.md](../../.recipe/machines/pentagon.md).

- **Firmware**: `data/rom/pentagon.rom`. `ram_size` (128/512/1024) picks the paging decoder, not
  a different ROM file.
- **Boot sources**: tape; TR-DOS floppy `fdd.a`–`fdd.d`; IDE hard disk on `ide0.master`
  (alias `hd`) / `ide0.slave`, board `[HDD] Scheme=NEMO` (Nemo IDE card, TR-DOS ports off). No CD
  drive by default (`CD0=0`/`CD1=0` in the shipped config) — an ISO needs the explicit
  `device=cdrom` switch on an empty unit first (see pitfall 2).
- **Filesystem parameters**: TR-DOS TRD for floppy; the Nemo IDE HDD path follows the same
  MBR+FAT-built-by-`hddfdisk` convention as ATM (pitfall 1) — Pentagon and ATM share the same
  xBIOS-family boot ROM lineage for HDD, though this has not been separately worked-example-verified
  the way the ATM3 image was; treat it as the same rule until proven otherwise.
- **Files that must be present**: TR-DOS autostart rules as above.

## Scorpion (`SCORPION`, `PROFSCORP`)

Ground truth: [.recipe/machines/scorpion.md](../../.recipe/machines/scorpion.md).

- **Firmware**: `data/rom/scorpion.rom` — a Shadow Monitor "SOS" service ROM, not the Sinclair
  48K ROM (`#7FFD` bit 4 selects SOS ROM vs. 128K ROM at boot). `PROFSCORP` additionally carries
  `data/rom/scorp_prof401.rom` (ProfROM), selected via `#7EFD` plane/page latches — the only
  decoder-level difference from plain `SCORPION`.
- **Boot sources**: tape; TR-DOS floppy `fdd.a`–`fdd.d` through the **built-in** Beta-128
  interface (`trdos_present` — no interface to insert, it just works). No IDE board by default
  (`[HDD] Scheme=NONE` in the shipped config; the media docs note `SMUC` — Scorpion's own SMUC IDE
  card — exists as a scheme option but ships on none of the default configs).
- **Filesystem parameters**: TR-DOS TRD.
- **Common trap**: `1024K` needs an explicit `ram_size: 1024` on create — the default is 256K,
  and 1024K-only software then fails its RAM probe silently.

## Profi 1024

Ground truth: [.recipe/machines/profi.md](../../.recipe/machines/profi.md),
[docs/hardware/profi-1024.md](profi-1024.md) (port-level reference).

- **Firmware**: `data/rom/profi.rom` (v5, `PROFI`) or `data/rom/profi/kramis-v03.rom` (v3, `PROFI3`, the
  factory BIOS V0.3 with TR-DOS 5.04T), each a 4-page ROM (SYS/menu page 0, TR-DOS page 1, 128K page 2, 48K
  BASIC page 3). The v3 board has no IDE, so it boots from tape or floppy only. The machine resets into the **SYS ROM with the DOS latch on**, not
  into 48K BASIC — this is a menu-driven boot, not straight-to-BASIC.
- **Boot sources**: tape; TR-DOS floppy `fdd.a`–`fdd.d`; IDE hard disk `ide0.master`/`ide0.slave`
  (`[HDD] Scheme=PROFI`) — the SYS ROM boots straight from the hard disk when one is present.
- **Filesystem parameters**: TR-DOS TRD for floppy. The hard disk is a **raw image** (no MBR
  requirement documented for the SYS ROM's own boot); its geometry comes from the disk's own ProfiHiDD header
  if present (16 heads × 16 sectors from the SYS ROM, 16×63 from Karabas-built images), or 16×16
  by default with no header at all. This is a different rule from ATM/ZX-Evo's MBR+FAT
  requirement — do not carry that assumption over to Profi.
- **PQ-DOS on `PROFI-PLUS`** (ROM BIOS Plus 0.41h1): the hard disk is **MBR + FAT16**, and the BIOS runs the
  **Z80 boot code in the MBR**, so a disk built without it does not start PQ-DOS
  (`testdata/machines/profi/pqdos/pqdos-hdd-small.img`: partition 1, type `#06`, active). PQ-DOS 2023-09 sees a
  second FAT16 partition as `D:` and **ignores a FAT32 partition**, so the Profi IDE slots take FAT12 / FAT16 only
  (FAT32 is refused). A composite with `partitions:` carries the MBR code of its first source disk, so PQ-DOS
  plus a host folder as `D:` works (checked by `ProfiPlusComposed_Test`).
- **Files that must be present**: TR-DOS autostart rules as above; hard disk content is whatever
  the SYS ROM's boot loader expects (not further documented in the sources read for this page).

## ATM Turbo (`ATM710`, `ATM3`)

Ground truth: [.recipe/machines/atm/](../../.recipe/machines/atm/README.md) (the most detailed
per-machine recipe — read it before automating anything ATM-specific).

- **Firmware**:
  - `ATM710`: `data/rom/atm2.rom` (four pages: `sos`/`dos`/`128`/`sys`).
  - `ATM3` (ZX-Evo): `data/rom/zxevo-fe.rom`, the official NedoPC BaseConf image, is the shipped
    default. `[EVO] Fpga=legacy` in `unreal.ini` switches to the older `data/rom/zxevo.rom` tree
    (older TR-DOS 5.04T layout, older ERS) — a config-file choice, not a runtime one. The EVO
    Reset Service (ERS) idles in its main menu around PC `#6117`, about 60 frames after reset.
- **Boot sources** (ERS menu letters in parentheses for `ATM3`):
  - Tape.
  - TR-DOS floppy `fdd.a`–`fdd.d` (**Z.** TR-DOS boot).
  - IDE hard disk `ide0.master`(`hd`)/`ide0.slave` — **B.** HDD boot. Board: `ATM3` uses the
    NemoIDE (`[HDD] Scheme=NEMO-DIVIDE`), `ATM710` its own ATM IDE (`Scheme=ATM`).
  - CD-ROM, ZX-Evo only, shipped on `ide0.slave` (`CD1=1` by default) — **D.** CD boot, runs the
    disc's `AUTORUN.ZX`. With the drive empty, the ERS keeps retrying until a disc is inserted.
  - SD card on the Z-Controller slot `sd.zc`, ZX-Evo only — **5.** SD card boot, boots NedoOS.
- **Filesystem parameters**:
  - TR-DOS TRD for floppy.
  - **IDE hard disk: MBR partition table with FAT partitions, built by the in-ROM `hddfdisk`
    tool.** This is pitfall 1 above — verified working example:
    [`testdata/machines/baseconf/hdd-images/README.md`](../../testdata/machines/baseconf/hdd-images/README.md).
    A TS-Conf-style raw-FAT image (no MBR) mounted here loads garbage and the machine hangs in
    `HALT` within a couple of frames.
  - **CD-ROM**: ISO 9660, read-only, needs `AUTORUN.ZX` (exact name, upper case; a trailing `;1`
    mastering suffix is tolerated) in the root directory, no larger than 34,816 bytes, loaded at
    `#6000`. Without `AUTORUN.ZX` present the loader falls back to `RST 0` (expected: back to the
    ERS menu — unverified on real hardware per
    [docs/inprogress/2026-09-29-zxevo-cd-boot/README.md](../inprogress/2026-09-29-zxevo-cd-boot/README.md)).
  - **SD card (NedoOS)**: **raw FAT16/FAT32 starting at sector 0, no MBR.** Same rule as TS-Conf's
    `sd.zc` (pitfall 1).
- **Files that must be present**:
  - NedoOS on the SD card needs `SD_BOOT.$C` in the root plus `bin/term.com` and `bin/cmd.com`; a
    minimal working fixture is
    [`testdata/machines/zxevo/nedoos/sdcard/`](../../testdata/machines/zxevo/nedoos/sdcard/). The
    NedoOS shell prompt after boot is `M:/bin>`. NedoOS reads the keyboard only from the AVR's
    PS/2 scan-code log, never the ZX matrix — automation must type PC-layout keys, not ZX keys.
  - The CD `AUTORUN.ZX` requirement is above.
  - A CP/M-mode boot (`#FF77` bit 9) is exercised by booting a CP/M disk image through the normal
    TR-DOS autostart path, not by a separate file requirement.

## TS-Conf (`TSL`, alias `TSCONF`)

Ground truth: [.recipe/machines/tsconf.md](../../.recipe/machines/tsconf.md).

- **Firmware**: `data/rom/zxevo.rom`, page 0, containing TS-BIOS. This is a **different BIOS**
  from ATM's ERS/xBIOS despite sharing the same underlying ZX-Evo hardware — TS-Conf has "its own
  BIOS without a CD boot" (per
  [docs/inprogress/2026-09-29-zxevo-cd-boot/README.md](../inprogress/2026-09-29-zxevo-cd-boot/README.md)).
  A **blank CMOS boots into the TS-BIOS Setup Utility** (text mode) instead of the boot menu —
  every fresh instance starts this way, since the shipped ts-conf config carries no NVRAM file.
  Press ENTER to change a Setup option and save the NVRAM; after a reset the machine boots the
  saved default.
- **Boot sources**: TR-DOS/Beta-128 (with virtual drives, gated by the DOS latch or `VG_OPEN`) —
  the same TRD floppy path as other clones; SD card on `sd.zc` (Z-Controller); Nemo IDE
  `ide0.master`/`ide0.slave` (`[HDD] Scheme=NEMO-DIVIDE`). No dedicated CD boot menu entry the way
  ATM3's ERS has one.
- **Filesystem parameters**: **SD card: raw FAT16/FAT32 filesystem starting at sector 0, no MBR**
  — MiSTer TSConf's own SD images need exactly this layout (pitfall 1). This is the layout TS-BIOS
  "Boot from SD" (menu key `SS+F12` in Setup to pick the boot device) expects.
- **Files that must be present** — Wild Commander (the TS-Conf shell) worked example: the Hobeta
  file `boot.$C` in the SD card's root (TS-BIOS's "Boot from SD" loads it directly — it is not an
  `.spg` program), which then reads `WC/wc.ini` to autoload plugins from the `WC/` folder and
  `WC/wc.mnu` for the F2 menu. Confirmed working end-to-end (test `BOOT-3`) with the Setup sequence
  documented in
  [`testdata/machines/tsconf/wildcommander/README.md`](../../testdata/machines/tsconf/wildcommander/README.md):
  blank CMOS → Setup → "Reset to" cycled 3× (ROM #00 → ROM #04 → RAM #F8 → `BD boot.$c`) → ENTER
  saves NVRAM → reset boots Wild Commander.
- `.spg` programs (TS-Conf SDK format, v1.0/v1.1) load directly via `load_software`/snapshot-load
  and switch any other model to `TSL` automatically (media kept, new emulator id returned).

## ZX-Poly (`ZXPOLY-48K`, `ZXPOLY-128K`, `ZXPOLY-PENTAGON`)

Ground truth: [.recipe/machines/zxpoly.md](../../.recipe/machines/zxpoly.md).

ZX-Poly is four synchronized instances of one base model (48K, 128K or Pentagon) — nothing about
firmware or the underlying boot rules changes from the base model's section above. What differs is
the media it accepts:

| Media | Loads as |
|:--|:--|
| `.zxp` | all four modules at once, locked, ready to run (refused on `ZXPOLY-48K` — needs 128K paging) |
| `.prom` | the ZX-Poly Test ROM: power-on, module 0 (CPU0) drives the rest |
| `.trd`/`.scl` | booted through TR-DOS on the master; the disk's own multiloader fills the slaves and locks the machine (`ZXPOLY-PENTAGON` only — this needs TR-DOS) |
| none | the bare machine: the master runs its normal ROM, the slaves wait |

`ZXPOLY-48K` runs replicated 48K software only, with no TR-DOS. `ZXPOLY-128K` runs `.zxp` files
and the Test ROM. `ZXPOLY-PENTAGON` (the default configuration) runs everything, including
multiloader disks.

## Sprinter (`SPRINTER`, not yet implemented)

**Design-only** — Sprinter has no factory port decoder yet and is not creatable
(`GET /api/v1/emulator/models` will not list it as `creatable`, per PLAN row #59 and
[.recipe/_common/machines.md](../../.recipe/_common/machines.md)). Everything below is quoted or
paraphrased from the design docs, not from a running implementation; do not treat it as verified
emulator behavior.

Sources:
[docs/inprogress/2026-09-28-sprinter/hardware-reference.md](../inprogress/2026-09-28-sprinter/hardware-reference.md)
§9, §10, §14 and
[docs/inprogress/2026-09-28-sprinter/roadmap-and-plan.md](../inprogress/2026-09-28-sprinter/roadmap-and-plan.md).

- **Firmware**: the real BIOS, community **3.07 BETA 1** by default since 2026-10-02 (3.04 and 3.06 selectable;
  [bios-versions.md](../inprogress/2026-09-28-sprinter/bios-versions.md)), plus the
  **Estex DSS** (disk operating system) that the BIOS chains into.
- **Planned boot sequence** (hardware-reference.md §14, from PLD-bitstream load through DSS):
  power-on loads the FPGA bitstream from ROM or fast RAM, then the BIOS runs POST, memory test,
  CMOS checks and IDE auto-detection, then reads the boot device from a CMOS byte (`#10`): floppy
  A/B, one of four IDE units (`#80`–`#83`), RAM disk, or ROM recovery (CD boot is BIOS-TT only).
  `OS_LOAD` reads LBA 1 (512 bytes; LBA 17×2048 for a CD) which must start with the literal string
  `"Starting...\0"`, then jumps into it. The DSS loader (`DOSBOOT4`) then reads the MBR (hard
  disk) or BPB (floppy), the first partition's boot sector, and `SYSTEM.DOS` from the FAT12/16
  root, before handing off to `SYSTEM.EXE`.
- **Planned boot sources**: floppy (WD1793-based Beta Disk interface, PC FAT12 720 KB/1.44 MB, or
  TR-DOS TRD via TR-DOS 5.04Em), two IDE channels × master/slave (hard disk + ATAPI CD, BIOS-TT
  extension), a PC folder (per PLAN row #59's summary — not detailed further in the hardware
  reference read for this page).
- **Planned filesystem constraints** (hardware-reference.md §9.3, §10): DSS reads **FAT12 and
  FAT16 only** (no FAT32). PC MBR partition types `#01`/`#04`/`#06`/`#0E` (primary),
  `#05`/`#0F` (extended, walked); the **DSS boot loader is stricter than the OS itself and checks
  only MBR partition entry 0** — the boot partition must be first in the table (found by reading
  source, **unverified on real hardware**). DSS floppies are FAT12 with a 3-reserved-sector boot
  area (`BOOT.EXE` removes one FAT copy and enlarges the reserved area to make room).
- **Explicitly uncertain in the sources**: whether `SYSTEM.EXE` runs `SYSTEM.BAT` automatically
  (the installer copies `SYSTEMX.BAT` → `SYSTEM.BAT`, suggesting yes, but this is unverified); the
  exact "Original ZX waits" contention pattern; several MAME-vs-manual disagreements listed in
  hardware-reference.md §7. Treat any Sprinter boot claim not in this list as unresearched.

## See also

- [docs/features/media.md](../features/media.md) — the full media-slot reference (verbs, options,
  error codes, every surface).
- [.recipe/media/use-media-slots.md](../../.recipe/media/use-media-slots.md) — the day-to-day
  recipe for inserting floppies, SD cards and folders.
- [.recipe/run/autostart-disk.md](../../.recipe/run/autostart-disk.md) — the one-call TR-DOS boot
  and its decision table.
- [.recipe/_common/machines.md](../../.recipe/_common/machines.md) — how to create and introspect
  a machine model, the authoritative model table, and the `creatable` flag.
