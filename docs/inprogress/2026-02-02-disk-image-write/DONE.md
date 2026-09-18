# DONE — Disk image write support (2026-02-02)

**Status:** complete.

## What landed
- Disk image writing (TDD + walkthrough): sector writes persist to the image, save-disk
  with format-aware save policy and UDI re-target for non-TRD-shaped content.

## Evidence
- `Emulator::SaveDisk` with loader registry save paths (`NC_FDD_DISK_SAVE_RETARGETED`),
  TRD/SCL byte-identical round-trip gates (universal track model README §6, both green).

## Follow-ups
- None.
