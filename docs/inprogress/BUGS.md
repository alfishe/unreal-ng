# Bug Registry

> ### 📌 Registry System Rules & Annotations
> 1. **Grouping & Numbering:** Bugs are grouped chronologically by their discovery date. The bug number (`#X`) increments sequentially within that specific date group.
> 2. **Retention Policy:** Closed bugs must be completely wiped from this tracker **no later than one week** after their `Date Fixed` to keep the file lean.
> 3. **Related Documents:** If a bug relates to functionality that is currently in progress, you must update the corresponding files in `docs/inprogress/*.md` and `PLAN.md` with relevant details or task dependencies.
> 4. **Methodology:** The tracking process (triage → confirmation → root cause → fix verification ladder) and the agent permission rules are defined in [`docs/testing/bug-tracking-guide.md`](../testing/bug-tracking-guide.md). Status markers: 🔴 Open → 🟠 Confirmed → 🔵 In Progress → 🟣 Fix Proposed → 🟢 Fixed (set by developer only).

---

2026-09-30
## 🟢 [Fixed] #1: TSConf Z-Controller FAT32 Compatibility Matrix
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-09-30
* **Commit ID:** `79985da7`

### Description
TSConf is expecting **FAT32 ONLY** formatted cards in the Z-controller slot, but the media manager mounts FAT16 by default.

### Requirements / Acceptance Criteria
- [x] Implement a **Media manager device registry compatibility matrix** to define expectations. (`SlotDescriptor::fsCompatibility`; `sd.zc` = `{FAT32}`, `defaultFs = Fat32`)
- [x] On insert/drag'n'drop events, verify requirements:
    - [x] **If Image:** Verify it is a compatible format (FAT32 for TSConf Z-Controller). (`ProbeFatType` on the opened medium; a FAT16 image is refused with `BadRequest`)
    - [x] **If Mounted Host Folder:** Ensure the virtual image is created as FAT32, not FAT16 (independent of folder content size). (the default clamps into the matrix; an explicit `fs=fat16` is a caller error)

---

## 🟢 [Fixed] #2: Media Manager FAT16 to FAT32 Auto-Switching >2GB
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-09-30
* **Commit ID:** `79985da7`

### Description
The Media manager is blindly creating FAT16 images for mounted host folders based on size. If the size is more than 2GB, it throws an error instead of switching to FAT32 format.

### Requirements / Acceptance Criteria
- [x] If device config meta allows both FAT16 and FAT32 formats, the Media manager must dynamically switch to FAT32 when folder size exceeds 2GB instead of throwing an error. (`OpenFolderVolume` retries as FAT32 over the FAT16 ceiling, with a report line)
- [x] If device config meta allows only single format - check by size if can be fulfilled and either fulfill or return an error. (the single-flavour slot keeps the honest `DoesNotFit`)

Verification: `MediaManager_Test.Fat32OnlySlotBuildsFoldersAsFat32AndChecksImages`, `MediaManager_Test.OversizedFolderSwitchesToFAT32WhenAllowed`, `TsConfMedia_Test.SdSlotIsFat32Only`.

---

## 🔴 [Open] #3: Host Folder Insert Freezes the UI Thread
* **Date Opened:** 2026-09-30
* **Date Fixed:** *Pending*
* **Commit ID:** *None*

### Description
Inserting a host folder into an HDD / SD slot runs the folder scan and the volume build **on the Qt UI thread**. `MediaPanelWindow::run` (unreal-qt/src/media/mediapanelwindow.cpp:251) executes `MediaControl::Execute` synchronously, and `MediaManager::Insert` does its file I/O and `FolderSnapshot::Scan` "on the caller's thread" by design (core/src/emulator/media/mediamanager.cpp:149). A large folder (or a slow / network disk behind it) freezes the whole UI for the duration of the scan - no repaints, no input, no cancel.

### Requirements / Acceptance Criteria
- [ ] The folder scan and volume build for a GUI insert (Media Panel "Insert Folder..." and drag'n'drop) run on a **worker thread**: the Qt main thread stays responsive (event loop keeps running, window repaints, the user can keep working).
- [ ] The UI is **notified on completion**: success -> the slot shows the medium; failure -> a readable error, surfaced like every other insert error.
- [ ] **Mid-scan source loss** (the host disk unmounted, the folder deleted, permission gone): the insert fails cleanly with a clear error - no crash, no half-inserted slot state, no leaked background work, later inserts into the same slot work.
- [ ] **Stall watchdog**: when the scan makes no progress for 60 s (configurable), it is aborted with an error to the UI instead of hanging forever ("everything froze and there is no progress at all").
- [ ] The synchronous `Insert` semantics of the automation surfaces (WebAPI / MCP, their own HTTP threads) are preserved - the async path is the GUI's, or `Insert` grows an async entry point (decide in the design).
- [ ] Design note added to `docs/inprogress/2026-09-29-media-drop-targets/design.md` and `docs/inprogress/PLAN.md` (registry rule 3).

---
