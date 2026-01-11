# Feature Toggle Design Evaluation

Comparing design docs against user's requirements. **Binary toggles only.**

---

## Revised Feature Set (Binary Toggles)

### Sound Features

| Feature | ID | Alias | Default | Effect |
|---------|-----|-------|---------|--------|
| **Sound Master** | `sound` | `snd` | ON | Skip all audio when OFF |
| **Sound HQ** | `soundhq` | `hq` | ON | FIR/oversampling when ON, bypass when OFF |

**Behavior Matrix:**

| `sound` | `soundhq` | Result | CPU Savings |
|---------|-----------|--------|-------------|
| OFF | - | Silent, no processing | ~18% |
| ON | OFF | Economy: direct chip output | ~15% |
| ON | ON | HQ: FIR + oversampling | baseline |

---

### Video Features (Deferred)

**User Requirements:**
- Headless: skip rendering for **inactive** emulators only
- UI-bound: **must render** when bound to DeviceScreen
- Videowall: **all render**, no inactive concept

**Recommendation:** Defer video toggle - requires activity-aware logic beyond simple binary on/off.

---

### Existing Debug Features (No Changes)

| Feature | ID | Alias | Notes |
|---------|-----|-------|-------|
| Debug Mode | `debugmode` | `dbg` | Master debug switch |
| Breakpoints | `breakpoints` | `bp` | Per-instruction checks |
| Memory Tracking | `memorytracking` | `mem` | Memory access stats |

---

## Summary

| User Need | Solution |
|-----------|----------|
| Disable sound completely | `sound off` |
| Economy/LQ mode | `sound on` + `soundhq off` |
| HQ mode (default) | `sound on` + `soundhq on` |
| Video for inactive headless | Defer - needs UI binding detection |
| Video for videowall | Defer - all must render |
