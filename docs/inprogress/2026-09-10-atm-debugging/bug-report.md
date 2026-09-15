# ATM Video Mode Debugging Session - Bug Report

**Date:** 2026-09-10  
**Machine:** ATM-Turbo 2+ v7.10 512KB  
**Test case:** Loading `2048.scl` from TR-DOS  
**Symptom:** Black screen after game loads, CPU running through NOPs

---

## Bugs Found

### 1. Screen Capture Ignores ATM Video Mode Dimensions

**Location:** `core/src/emulator/video/screencapture.cpp`  
**Branch:** master (generic fix)

**Problem:**  
`extractScreenArea()` hardcodes 256x192 dimensions regardless of the active video mode. ATM16 mode uses 320x200 screen area within a 448x288 framebuffer.

```cpp
// Lines 13-14 - hardcoded constants
static constexpr uint16_t ZX_SCREEN_WIDTH = 256;
static constexpr uint16_t ZX_SCREEN_HEIGHT = 192;
```

**Impact:**  
- `CaptureMode::ScreenOnly` always captures 256x192, even in ATM16 (320x200), ATM HiRes (640x200), Pentagon 384x304, etc.
- For ATM modes, this captures the wrong region (centered extraction of wrong size)

**Fix:**  
Detect video mode from `FramebufferDescriptor::videoMode` and use appropriate dimensions from `rasterDescriptors[mode]`.screenWidth/screenHeight.

---

### 2. Memory Bank Reporting Returns UNMAPPABLE for ATM Pages

**Location:** `core/src/emulator/memory/memory.cpp` - `GetRAMPageFromAddress()`  
**Branch:** master (generic fix)

**Problem:**  
`GetRAMPageFromAddress()` cannot map ATM memory bank pointers back to RAM page numbers. Returns `MEMORY_UNMAPPABLE (0xFFFF/65535)` for bank1, bank2, bank3 even though memory is working correctly.

**Impact:**  
- API endpoints report `ram.bank1/2/3 = 65535` for ATM machines
- Confusing for debugging (looks like memory is unmapped when it's not)
- Memory reads/writes work correctly - this is purely a reporting bug

**Fix:**  
`GetRAMPageFromAddress()` needs to handle ATM memory manager's extended RAM mappings. The `_bank_read[]` pointers point to valid RAM but outside the range the function expects.

---

### 3. State/Screen Endpoint Doesn't Report ATM Video Modes

**Location:** `core/automation/webapi/src/api/state_screen_api.cpp`  
**Branch:** master (generic fix)

**Problem:**  
`getStateScreen()` and `getStateScreenMode()` return:
- `is_128k = false` for ATM machines (only checks SPECTRUM128/PENTAGON/PLUS3)
- `display_mode = "standard"` even when ATM16/ATMHR/ATMTX modes are active

**Impact:**  
- Incorrect state reporting via API
- Inconsistent with `/timing` endpoint which correctly shows `video_mode: "ATM16"`

**Fix:**  
- Add ATM models to the `is_128k` check (or better: rename to `has_banked_memory`)
- Read actual video mode from `Screen::GetVideoMode()` and report it

---

### 4. TTD Seek Doesn't Restore Framebuffer

**Location:** `core/src/debugger/ttd/` (TTD subsystem)  
**Branch:** master (generic fix)

**Problem:**  
After `ttd/seek` to a past frame, the CPU state is correctly restored but the framebuffer remains black. The video buffer contents are not restored or re-rendered.

**Impact:**  
- Screen captures after TTD seek show black screen
- Makes visual debugging with TTD impossible

**Expected behavior:**  
After seeking, either:
1. Restore the framebuffer snapshot from the checkpoint, OR
2. Run at least one frame of rendering to populate the framebuffer

**Fix:**  
TTD checkpoints need to include framebuffer state, or the seek operation should trigger a frame render before returning to "detached" state.

---

### 5. ATM16 Video Mode Rendering - Black Screen

**Location:** `core/src/emulator/video/zx/screenzx.cpp` - `DrawATMMode()`  
**Branch:** atm (ATM-specific fix needed)

**Problem:**  
When ATM16 mode is active:
- `timing` endpoint correctly reports `video_mode: "ATM16"` with 320x200/448x288 dimensions
- Framebuffer is allocated correctly (448x288)
- But the framebuffer contents are all black

**Possible causes:**
1. `DrawATMMode()` not being called during frame rendering
2. ATM video memory mapping not set up correctly (wrong RAM pages mapped)
3. Port FF77 / aFF77 state not properly triggering ATM mode rendering
4. Screen memory pointer calculation wrong for ATM layout

**Investigation needed:**
- Add logging to `DrawATMMode()` to verify it's being called
- Check `pFF77 & 7` value matches `FF77_16` (ATM16 mode selector)
- Verify ATM video RAM addresses are correctly calculated

---

### 6. Model Name Reporting (FIXED in this session)

**Location:** Multiple files in `core/automation/`  
**Branch:** master (already fixed)

**Problem:**  
Model name was hardcoded to "ZX Spectrum 48K" with manual if/else for a few models. ATM, Scorpion, Profi, etc. were not handled.

**Fix applied:**  
Added `Config::GetModelFullName(MEM_MODEL model)` and replaced all hardcoded model name logic in:
- `state_screen_api.cpp` (3 places)
- `state_memory_api.cpp` (3 places)
- `cli-processor-state.cpp` (6 places)

---

## Branch Assignment Summary

### Master Branch (Generic Fixes)
1. Screen capture dimensions - respect video mode
2. Memory bank reporting for extended memory
3. State/screen endpoint video mode reporting
4. TTD seek framebuffer restoration
5. Model name reporting (already fixed)

### ATM Branch (ATM-Specific Fixes)
1. ATM16 video mode rendering - investigate and fix black screen
2. Verify ATM memory manager integration with video system
3. Test with 2048.scl and other ATM demos

---

## Test Commands

```bash
# Load test case
# In emulator: ATM 7.10 512KB, load /Volumes/TB4-4Tb/Projects/Test/unreal-ng/testdata/machines/atm/2048.scl

# Via MCP - check state
inspect_state aspects=["timing", "memory_banks", "screen_image"]

# Capture full framebuffer (not just 256x192)
GET /api/v1/emulator/{id}/capture/screen?mode=full
```

## Related Files

- `core/src/emulator/video/screen.cpp` - video mode detection, raster descriptors
- `core/src/emulator/video/zx/screenzx.cpp` - ATM rendering (DrawATMMode)
- `core/src/emulator/video/screencapture.cpp` - screen capture
- `core/src/emulator/ports/models/portdecoder_atm710.cpp` - ATM port handling
- `core/src/emulator/memory/memory.cpp` - memory bank reporting
