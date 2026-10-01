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

## 🟢 [Fixed] #4: TSConf Wild Commander - Parsed Modules Play No Sound
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-09-30
* **Commit ID:** `d391d73f`

### Description
TS-Conf, Wild Commander (`wc-improved-v1.11i.img`) with a host folder of modules mounted. Opening any non-archived module (.asc / .pt2 and others) shows the player screen with **correctly parsed tags**, but **no music plays**. Suspect: the music ports (AY / TSFM / GS / MoonSound) are not wired in the TS-Conf port decoder.

### Triage (2026-09-30, live instance over MCP / WebAPI)
- Ports are alive: a 30-frame port trace shows the player (PC `0xC4CD-0xC516`) writing a full AY register dump every frame - `#FFFD/#BFFD`, `had_handler: true`, decode arm `Ay`. The TSFM's second SSG carries the song's registers (active channels, volumes).
- `sound_played_since_reset: false` is a stub ("not tracked", devicestate.cpp:124) - a red herring.
- An armed 2 s audio capture collected **0 samples** while running: SoundManager::handleFrameEnd returns before the analyzer tap when `turbo_mode && !turbo_mode_audio` (soundmanager.cpp:754) - and `Core::EnableTurboMode` **always mutes audible output**.
- Instance settings at the time: `turbo_mode = true`, `turbo_active = true`, `speed = 2`, `turbo_audio = false`.
- **Decisive test**: turbo off + speed 1x -> a 2 s capture returns `dominant_hz: 1764.75`, RMS 0.083 - the module plays.

### Root cause
Turbo mode was on - by design it mutes all audio and skips the host-audio path entirely. The likely trigger: turbo is bound to **bare Tab** (menumanager.cpp:624), a key a file manager invites constantly. The port decoder, Wild Commander and the TSFM are correct.

### Requirements / Acceptance Criteria
- [x] Triage on the live instance: audio hardware config, port trace for AY port activity (#BFFD/#FFFD) while the player runs. (activity confirmed)
- [x] Root cause: player never writes the ports, or the decoder swallows/misroutes them, or the chip is not fitted. (none of these: turbo mute)
- [x] A module played from Wild Commander produces audio (AY path at minimum). (works with turbo off; the trap is gone with the rebind below)
- [x] Proposal: rebind Turbo Mode off bare Tab (a guest-app key) - e.g. `Ctrl+Tab` / `F11` - and/or surface the turbo state on the player screen (status bar already shows it). Decision: developer. (rebound to Ctrl+Tab in `d391d73f`)

---

## 🟢 [Fixed] #5: "28 MHz", Changing Frequency, 2x-Fast Time, Flicker on TSConf
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-09-30
* **Commit ID:** `d391d73f`

### Description
The machine appears to jump to a turbo frequency of ~28 MHz, the number keeps changing, emulated time flows about 2x faster than real time, and the picture flickers. TS-Conf is expected to support at most 14 MHz.

### Triage (2026-09-30)
- 28 MHz is arithmetic, not a phantom clock: host speed multiplier was **2**, Wild Commander toggles SYS_CONFIG 3.5 <-> 14 MHz around SD I/O every frame (port trace, PC `0x6BD5`), so the displayed effective clock jumps **7 <-> 28 MHz**. At host 8x the same toggle reads 28 <-> 112.
- "Time flows 2x faster" = the host speed multiplier 2 (turbo active runs unthrottled on top).
- The flicker = turbo mode's render decimation.
- The hardware turbo composition itself is correct: `ApplyHardwareTurboNow` rescales the in-frame position, recomputes frame timing and keeps the INT video-locked (50 Hz), so SYS_CONFIG 7/14 MHz does not change song tempo.
- SYS_CONFIG[1:0] combo 3 is deliberately clamped to 14 MHz (`kRatio[4] = {1, 2, 4, 4}`, portdecoder_tsconf.cpp:972) - the TS-BIOS setup only offers 3.5 / 7 / 14, and nothing in the shipped firmware sets 28.

### Requirements / Acceptance Criteria
- [x] Verify what the 4th SYS_CONFIG combo does. (clamped to 14 - matches the BIOS; the tslabs spec's 28 MHz combo is never issued by shipped firmware)
- [x] Verify frame/INT timing under hw turbo. (frame budget scales with the ratio; INT stays video-locked)
- [x] Proposal: same as #4 - the turbo shortcut + turbo state visibility. The frequency display could also show the composition (e.g. "14 MHz x2") instead of a bare product. (turbo rebound to Ctrl+Tab; the label shows the half-second range, e.g. "3.5-14 MHz", at 2 Hz - `d391d73f`)

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
