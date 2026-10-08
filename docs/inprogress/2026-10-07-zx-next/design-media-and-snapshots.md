# ZX Spectrum Next: media and snapshots

**Date:** 2026-10-07 · part of [README.md](README.md) · pipeline: [proposal](../2026-10-02-snapshot-pipeline/proposal.md)

## 1. Media manager

| Slot | Device | Notes |
|:--|:--|:--|
| `sd.next0` (`required`), `sd.next1` | `SdCardSpi` behind `#E7`/`#EB` | sources: `RawImage`, `HostFolderFat` (folder as FAT16/FAT32 card), session/read-only access; NR `#0A` bit 5 swaps which physical socket each slot is ([integration-next.md](../2026-09-28-storage-manager/integration-next.md) 2-3) |
| `sd.divmmc` | not used on the Next | the Next's DivMMC uses the same two cards |
| `tape` | EAR in via `#FE` bit 6 | existing tape manager; NextZXOS loads tapes via the 48K ROM trap path as usual |
| floppy | none; NextZXOS emulates +3 DSK images from the card | the FDC traps NR `#D8-#DA` (N10) |

No Next-specific change to the media manager is planned (storage-manager verdict). Media commands (`media` MCP tool,
WebAPI `/media`, CLI, Lua, Python, Qt drop targets) work by slot id. The folder-as-card requires FAT32 volumes with
at least 65526 clusters (the firmware's FatFs decides the type by cluster count).

## 2. Snapshot formats

| Format | Into the Next | From the Next | Source / status |
|:--|:--|:--|:--|
| NEX V1.0-1.2 | new `loadernex`: header ("Next" magic, RAM requirement, bank count and flags, loading screens, border, SP, PC, entry bank), banks in order 5,2,0,1,3,4,6,7,... (wiki; MAME `snapshot_nex.cpp`), screens via banks 5 (ULA) or 9-11 (Layer 2), starts at PC (0 = load only) | NEX save of the running machine: possible with caveats (jnext: "honest register limitations") | wiki NEX page (summary read); V1.3 opt-in (jnext) |
| `.snx` | the `.sna` loader (a 128K SNA that NextZXOS loads keeping handle 0 open) | no | ZEsarUX `snap.c` comment |
| `.sna`, `.z80` | existing loaders; the machine commits through the Next personality it is in (48K/128K/+3 or Pentagon timing) | `.sna` 48/128 in classic personalities | existing |
| `.szx` | existing SZX reader/writer for the matching classic machine id; Next-specific chunks: Fuse's SZX has none; a vendor chunk is Q9 | classic personalities only | [SZX design](../2026-09-29-szx-snapshots/design.md) |
| native | the TTD checkpoint written as a file (all blobs) | same | Q9; snapshot pipeline row #84 |

The Next appears in `snapshotcapture.cpp`'s list of models without a layout; N11 gives `MM_NEXT` a `SnapshotImage`
layout (RAM as 8K pages, the slot values, NextREG array) and a `SnapshotPolicy` that commits itself: a classic
snapshot goes through the machine's own port writes (`#7FFD`, `#DFFD`, `#1FFD`) after setting the personality.

## 3. MachineStateTransfer

Rules for `MM_NEXT` as source or target (owner rule: it works where physically possible):

| Pair | Verdict |
|:--|:--|
| classic 48K/128K/+2/+3 -> Next | possible: the Next in the matching personality; RAM into banks 0-7 by the classic map |
| Pentagon 128/512 -> Next | possible in Pentagon timing |
| Next -> classic | only when the Next is in a classic personality and uses no Next features (mismatches reported as lossy) |
| TSConf/ATM/Profi/Sprinter <-> Next | refused (different hardware) |

The exact layout registry entry is added in N11 (`machinestatetransferlayouts.cpp`); until then the capture reports
`capture_unsupported` as today.

## 4. Pipeline notes

Loading a snapshot while TTD records ends the session (the one TTD rule); the history stays browsable. NEX load
is a `TTDLoadKind::Snapshot`. Loading media wipes TTD history per the MCP instructions.
