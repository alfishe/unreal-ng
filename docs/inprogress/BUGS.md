# Bug Registry

> ### 📌 Registry System Rules & Annotations
> 1. **Grouping & Numbering:** Bugs are grouped chronologically by their discovery date. The bug number (`#X`) increments sequentially within that specific date group.
> 2. **Retention Policy:** Closed bugs must be completely wiped from this tracker **no later than one week** after their `Date Fixed` to keep the file lean.
> 3. **Related Documents:** If a bug relates to functionality that is currently in progress, you must update the corresponding files in `docs/inprogress/*.md` and `PLAN.md` with relevant details or task dependencies.
> 4. **Methodology:** The tracking process (triage → confirmation → root cause → fix verification ladder) and the agent permission rules are defined in [`docs/testing/bug-tracking-guide.md`](../testing/bug-tracking-guide.md). Status markers: 🔴 Open → 🟠 Confirmed → 🔵 In Progress → 🟣 Fix Proposed → 🟢 Fixed (set by developer only).

---

2026-10-02
## 🟣 [Fix Proposed] #1: The halted Z80 fetches the HALT itself instead of the byte after it
* **Date Opened:** 2026-10-02
* **Date Fixed:** -
* **Commit ID:** -

### Description
While halted, the Z80 makes an idle opcode fetch every 4 T at the address **after** the `HALT` (its program
counter already points there) and discards the byte. unreal-ng (and unreal-z80) re-execute the `HALT` itself, so the
idle fetches go to the `HALT`'s own address. When the two addresses differ in contention, the interrupt is taken at
another T: HALT2INT v3 on the 48K, `HALT` at #7FFF (contended; the byte after it, #8000, is not), interrupt at 14335 /
14336 / 14562: the real early 48K shows R = #43 / #43 / #0B, unreal-ng #44 / #44 / #1C, and the program's header
reads "HALT: Unknown" instead of "Early".

### Root cause
`op_76` (`core/src/emulator/cpu/op_noprefix.cpp`) steps PC back, and every following step runs the full opcode
fetch (`Z80::m1_cycle`) at PC, the `HALT` address. The same in unreal-z80 (`op_76`, `Z80HaltT` reports the M1 at PC).
References: the hardware (HALT2INT photos and published screens) and MAME fetch HALT + 1; FUSE, Xpeccy, ZXMAK2 and
SkoolKit fetch the HALT, like unreal-ng.

### Requirements / Acceptance Criteria
- [x] The halted idle fetch goes to PC + 1 on the bus (contention, the +2A / +3 latch, ULA snow, the machine M1 hooks)
      while PC, the instruction-start work, R and the acknowledge stay as today
      ([design](2026-10-02-halt-fetch-address/design.md) §3-4). `Z80::HaltedM1`, branch `halt-fetch-fix`.
- [x] `Halt2Int_Test` passes with no known deviation (48K; the 128K program finds "Early"); host tests pin the idle
      fetch address on the 48K and 128K (`Contention48K_Test` / `Contention128K_Test.HaltedFetchGoesToTheByteAfterTheHalt`).
- [ ] No cost on the non-halted path (A/B, pending a quiet machine); unreal-z80 carries the same fix with every suite
      green (library branch `halt-fetch`, done), as do the vendored copies (General Sound, the Sprinter's Z84C15).

Design: [2026-10-02-halt-fetch-address](2026-10-02-halt-fetch-address/design.md). Plan: [PLAN.md](PLAN.md) #81.

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

## 🟢 [Fixed] #6: Native macOS MP4 Recording - Frame 2x Too Wide on TSConf
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-10-01
* **Commit ID:** `6c14bb57`

### Description
TS-Conf with the module player running, video recording started - native (macOS VideoToolbox), MP4, fullscreen. The video records fine **with sound**, but the file is **1440x576** where the natural frame is **720x288**: the horizontal resolution is doubled and the picture is stretched 2x horizontally. Recorded sample: `/Users/dev/Movies/unreal_20260930_205229.mp4` (emulator left paused for triage, file no longer present).

### Root cause
TS-Conf stores its fat pixels at **half height** internally: the 360x288-dot raster is kept at 720 (2 px/dot wide) x 288 (1 stored line per dot row). `RecordingManager` had no awareness of this and passed the raw `fb.width x fb.height` (720x288) straight through as the recording's target resolution - already wrong aspect before any user-chosen scale.

Separately, the GUI's recording dialog and "Quick Record" presets default to `SetScaleFactor(2)` (a "2x nearest-neighbor upscale, keeps ZX pixels crisp" convenience, `unreal-qt/src/debugger/widgets/videorecordingwidget.cpp:388-394`, `unreal-qt/src/mainwindow.cpp:3374`), which every encoder backend applies **uniformly to both axes** (`VideoToolboxEncoder::Start`, `core/recording/src/platform/macos/videotoolbox_encoder.mm:56-61`; the ffmpeg `scale=iw*N:ih*N` filter, `core/recording/src/encoders/ffmpeg_pipe_encoder.cpp:767-772`).

With no TS-Conf correction, `720x288` uniformly doubled by the GUI's default `scaleFactor=2` gives exactly the reported `1440x576`: the width got its (otherwise legitimate) 2x upscale, the height needed a 2x *aspect* correction first and never got one, so it reads as "width doubled, height not" even though the real defect is a missing height fix, not an added width one. A direct/test call to `RecordingManager::StartRecording` never touches `SetScaleFactor` (class default `1`), so that path was never visibly wrong - it just produced an uncorrected `720x288` with no upscale to make the mismatch visible.

### Fix
`core/src/emulator/video/videofamily.h`: `StoresHalfHeightLines(VideoModeEnum)` - true for the four TS-Conf modes (`M_TS16/M_TS256/M_TSTX/M_TSZX`), false elsewhere.

`core/recording/src/recordingmanager.cpp`: both `StartRecording` and `StartRecordingEx` double `_videoHeight` when `StoresHalfHeightLines(fb.videoMode)` before it ever reaches an encoder config, and `CaptureFrame` duplicates every captured row (720x288 -> 720x576) before handing the frame to `EncodeVideoFrame`. This is encoder-agnostic: it runs once, upstream of all three backends.

Checked that no backend double-applies or skips the fix:
- VideoToolbox (native) and the ffmpeg software path both read the already-corrected `config.videoWidth/videoHeight` as their un-scaled source size and apply the user's `scaleFactor` uniformly on top - a 2x "crisp pixels" recording of a TS-Conf session now comes out `1440x1152` (uniform 2x of the correct `720x576`), not `1440x576`.
- The GIF encoder ignores `scaleFactor` entirely (by design - chroma subsampling doesn't apply to GIF) and uses `config.videoWidth/videoHeight` directly, so it inherits the corrected `720x576` with no extra step.

### Verification
- New test `core/tests/emulator/recording/tsconf_aspect_test.cpp`: `StoresHalfHeightLines` classification (TS-Conf-only) and the row-doubling algorithm (byte-exact content, width untouched) - red before the fix, green after.
- Full rebuild (`ninja core-tests`, zero warnings) + `test-parallel`, twice, both green (no regression; a first pass accidentally broke `ScorpionTurbo_Test.TurboStrobePostsCpuFreqChanged` via an unrelated notification-throttle change bundled in the same working tree - reverted, see commit notes).
- Full default build (`ninja`, no target) also green, zero non-third-party warnings.
- **Found and fixed a second bug while verifying live**: `RecordingManager::CaptureFrame`'s first draft declared the stretched `FramebufferDescriptor` *inside* the `if (StoresHalfHeightLines(...))` block and kept using the `toEncode` pointer to it after that block closed - a dangling pointer to a destroyed stack local, read by `EncodeVideoFrame` right after. This is what produced "mostly red garbage, worse than before the fix" when actually recording Wild Commander in the running app (not caught by the geometry-only unit test or `test-parallel`, since neither exercises a real `RecordingManager` + encoder round trip). Fixed by hoisting `FramebufferDescriptor stretched;` to function scope next to the existing `cropped` local, same lifetime pattern already used for the crop paths.
- Re-verified live via WebAPI/MCP against the freshly built `unreal-qt` (TSL model, TR-DOS/TSTX boot screen - any TS-Conf framebuffer exercises the same path as Wild Commander): native H.264 recording at `scaleFactor=1` -> clean `720x576` frame (ffprobe + a decoded PNG, no corruption); at `scaleFactor=2` over 60 captured frames -> clean `1440x1152`, image content crisp and correctly proportioned, no garbage, no crash.
- Could not re-probe the *original* user sample - that file no longer exists on disk; the geometry fix is verified by the math above, the new unit test, and this fresh live recording.

### Requirements / Acceptance Criteria
- [x] Triage: probe the file (dimensions, SAR/DAR, pixel format) and read the VideoToolbox recorder's frame-size/scaling path.
- [x] Root cause: which stage doubles the width (capture with devicePixelRatio, a scale-to-even/16 step, or a SAR/PAR mix-up).
- [x] A native MP4 recording of a TS-Conf session has the right geometry (720-wide content stays 720 wide, or scales uniformly with the correct pixel aspect).
- [x] The same check for the other recording paths (GIF, software MP4) - they must not share the bug.

---

## 🟢 [Fixed] #3: Host Folder Insert Freezes the UI Thread
* **Date Opened:** 2026-09-30
* **Date Fixed:** 2026-10-01
* **Commit ID:** `47078607`

### Description
Inserting a host folder into an HDD / SD slot runs the folder scan and the volume build **on the Qt UI thread**. `MediaPanelWindow::run` (unreal-qt/src/media/mediapanelwindow.cpp:251) executes `MediaControl::Execute` synchronously, and `MediaManager::Insert` does its file I/O and `FolderSnapshot::Scan` "on the caller's thread" by design (core/src/emulator/media/mediamanager.cpp:149). A large folder (or a slow / network disk behind it) freezes the whole UI for the duration of the scan - no repaints, no input, no cancel.

### Root cause
Confirmed by trace: `MediaPanelWindow::insertInto` → `run()` → `MediaControl::Execute` → `MediaManager::Insert` → `MediaFormatRegistry::Open` → `FolderSnapshot::Scan` (recursive `std::filesystem::directory_iterator`, one syscall round-trip per entry, no batching) is a single synchronous call chain with no thread boundary and no cancellation hook anywhere in it. The existing `"async": "true"` option on insert is a red herring for this bug - it only skips `MediaControl::Finish`'s post-insert swap-delay wait (`WaitApplied`), never the scan/build itself, which runs unconditionally before `Finish` is even reached.

### Fix
Kept `core/` fully synchronous (every automation surface - WebAPI, CLI, MCP, Lua, Python - is unaffected and keeps blocking its own request thread, which has no UI to freeze). Added the async wrapper only where there is a UI to protect:

- **Core**: `cancelRequested`/`onProgress(entriesScanned, bytesScanned)` threaded end to end as optional `std::function` fields (`MediaRequest` → `InsertOptions` → `OpenRequest` → `FolderScanOptions` / `FolderDiskBuilder::BuildTrd`'s new parameters) - never serialized, so the wire protocols have no way to set them. `FolderSnapshot::Scan`'s walk checks `cancelRequested` once per entry and calls `onProgress` after every entry visited (bytes = total size of files accepted so far); cancelling unwinds every recursion level, not just the innermost one. New `MediaError::Cancelled` (HTTP 499).
- **GUI**: `MediaPanelWindow` gained the `std::thread` + `QMetaObject::invokeMethod(Qt::QueuedConnection)` worker idiom already used by `TapeExportAudioDialog`/`TapeImportAudioDialog` - no new threading pattern invented. An indeterminate `QProgressBar` + a status label show which slot and path are being scanned (slot name in bold), the entry count and a human-readable size (`QLocale::formattedDataSize`, KB/MB/GB). The scanning row in the slot table is tinted with `QPalette::Accent` (the system accent color - not `QPalette::Highlight`, which fades to gray whenever the window loses focus) so it is unambiguous which mount point a running scan belongs to. A toolbar button (reusing the existing Tools-menu "Media" `QAction`) opens the panel directly. A 1s watchdog timer compares the progress count across ticks and only cancels after `_insertStallTimeoutSeconds` (30, configurable) ticks with **zero** advancement - a slow-but-moving large folder is never aborted, only a genuinely stuck one. The worker takes its own `shared_ptr<Emulator>` (a fresh `EmulatorManager::GetEmulator(id)` lookup, not the GUI binding's raw pointer) so the emulator/context stays alive for the scan's duration regardless of concurrent unbind/rebind; the destructor cancels and joins unconditionally so the worker's completion lambda can never fire on a half-destroyed window.

Design: `docs/inprogress/2026-09-29-media-drop-targets/design.md` §11.

### Verification
- `core/tests/emulator/io/storage/hostfolder/foldersnapshot_test.cpp`: progress counting (entries and bytes), cancellation (exact call-count math verified), nested-recursion unwind (cancelling inside one subfolder never visits a sibling), a directory disappearing mid-walk failing the whole scan instead of truncating it.
- `core/tests/emulator/io/storage/hostfolder/folderdiskbuilder_test.cpp`: cancellation during the scan vs. during the TR-DOS file-read loop, distinguished by call count.
- Full rebuild (`ninja`, zero warnings) + `test-parallel`, both green.
- **Interactively verified** (built and run in an isolated git worktree, since the shared working tree had unrelated concurrent breakage at the time): inserting a real folder through the Media panel keeps the window responsive, shows the scanning row highlighted and the slot/path/entry-count/size in the status label, and completes correctly. Developer-confirmed live, including follow-up tweaks (byte/size display, row highlight color, bold slot name, the toolbar button, the 30s watchdog).

### Requirements / Acceptance Criteria
- [x] The folder scan and volume build for a GUI insert (Media Panel "Insert Folder..." and drag'n'drop) run on a **worker thread**: the Qt main thread stays responsive (event loop keeps running, window repaints, the user can keep working).
- [x] The UI is **notified on completion**: success -> the slot shows the medium; failure -> a readable error, surfaced like every other insert error (the same `report()`/`QMessageBox::warning` path every other verb already uses).
- [x] **Mid-scan source loss** (the host disk unmounted, the folder deleted, permission gone): no crash, no leaked background work, later inserts into the same slot work (verified by the lifetime design and thread join discipline). A structural I/O failure anywhere in the walk (the initial `directory_iterator` open, or `increment` failing partway through a directory) now fails the **whole** scan with a clear error instead of silently continuing past it - `Scanner` tracks this the same way as cancellation (`_ioError`/`IsIoError()`), unwinds every recursion level, and `FolderSnapshot::Scan` returns false with the `ec.message()`-derived error (mapped to `MediaError::UnreadableSource` at the `MediaFormatRegistry`/`FolderDiskBuilder` call sites, same as any other unreadable source). Policy exclusions (a symlink, the manifest's exclude list, a service file, a size/count limit) are unaffected and still just `Skip()` - only genuine filesystem errors fail the operation. Test: `FolderSnapshot_Test.DirectoryGoneMidWalkFailsInsteadOfTruncating` (deletes a subfolder from under the walk via the `onProgress` hook, deterministically, before the scanner recurses into it).
- [x] **Stall watchdog**: when the scan makes no progress for 30 s (configurable), it is aborted with an error to the UI instead of hanging forever.
- [x] The synchronous `Insert` semantics of the automation surfaces (WebAPI / MCP, their own HTTP threads) are preserved - `core/` is untouched for every caller that does not explicitly set the new callback fields.
- [x] Design note added to `docs/inprogress/2026-09-29-media-drop-targets/design.md` and `docs/inprogress/PLAN.md` (registry rule 3).

---
