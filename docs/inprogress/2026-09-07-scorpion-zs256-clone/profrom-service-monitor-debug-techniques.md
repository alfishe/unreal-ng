# ProfROM Service Monitor Debug Techniques

> **Status:** Reference documentation for debugging and testing  
> **Date:** 2026-09-13  
> **Related:** profrom-input-subsystem-and-driver-disassembly.md

---

## 1. Key Memory Addresses

### 1.1 Pointer Control Variable (`#E03B`)

Controls the Service Monitor's pointer subsystem. Bit field:

| Bit | Mask | Name | Description |
|:---:|:----:|:-----|:------------|
| 7 | `0x80` | Pointer Enable | Master enable for pointer subsystem |
| 6 | `0x40` | Joystick Enable | Kempston Joystick as pointer device |
| 5 | `0x20` | Mouse Enable | Kempston Mouse as pointer device |
| 4 | `0x10` | (reserved) | |
| 3 | `0x08` | (reserved) | |
| 2 | `0x04` | (reserved) | |
| 1 | `0x02` | (reserved) | |
| 0 | `0x01` | (reserved) | |

**Common values:**
- `0xC8` (200): Pointer ON, Joystick ON, Mouse OFF (default)
- `0xE8` (232): Pointer ON, Joystick ON, Mouse ON

### 1.2 Pointer Coordinates (`#E03C-#E03D`)

Current cursor position in screen coordinates:
- `#E03C`: X coordinate (0-255)
- `#E03D`: Y coordinate (0-191)

### 1.3 Raw Coordinate Latch (`#E12C-#E12D`)

Previous raw values from mouse port reads (used for delta computation):
- `#E12C`: Last X value from port `#FBDF`
- `#E12D`: Last Y value from port `#FFDF`

---

## 2. WebAPI Debug Commands

### 2.1 Read Control Variable

```bash
EMU_ID=$(curl -s http://localhost:8090/api/v1/emulator | jq -r '.emulators[0].id')
curl -s "http://localhost:8090/api/v1/emulator/$EMU_ID/memory/read/0xE03B?count=1" | jq '.data[0]'
```

### 2.2 Enable Mouse Pointer

```bash
EMU_ID=$(curl -s http://localhost:8090/api/v1/emulator | jq -r '.emulators[0].id')
curl -X POST "http://localhost:8090/api/v1/emulator/$EMU_ID/memory/write" \
  -H "Content-Type: application/json" \
  -d '{"address": "0xE03B", "data": [232]}'
```

### 2.3 Read Pointer Position

```bash
curl -s "http://localhost:8090/api/v1/emulator/$EMU_ID/memory/read/0xE03C?count=2" | jq '.data'
# Returns [X, Y]
```

### 2.4 Read Raw Latch Values

```bash
curl -s "http://localhost:8090/api/v1/emulator/$EMU_ID/memory/read/0xE12C?count=2" | jq '.data'
# Returns [lastX, lastY] from port reads
```

---

## 3. Unit Test Integration

### 3.1 Test: Mouse Detection at Boot

The Service Monitor polls mouse ports once at boot (frame ~110) for detection.
With `config.input.mouse=1` (KEMPSTON), the Mouse device returns:
- `#FADF` (buttons): `0xFF` (all released)
- `#FBDF` (X): `0x00` (initial)
- `#FFDF` (Y): `0x00` (initial)

**Assertion:** After boot, `#E12C` and `#E12D` should both be `0x00`.

### 3.2 Test: Mouse Enable/Disable via Control Bit

```cpp
// Enable mouse pointer
memory.WriteByte(0xE03B, memory.ReadByte(0xE03B) | 0x20);

// Verify mouse polling now occurs continuously
// (Mouse::recordPoll should be called each frame)

// Disable mouse pointer
memory.WriteByte(0xE03B, memory.ReadByte(0xE03B) & ~0x20);

// Verify mouse polling stops
```

### 3.3 Test: Pointer Movement

```cpp
// 1. Enable mouse: set bit 5 of #E03B
memory.WriteByte(0xE03B, 0xE8);

// 2. Inject mouse movement via MessageCenter
MouseMoveEvent* event = new MouseMoveEvent(10, 5, emulator->GetUUID());
messageCenter.Post(NC_MOUSE_MOVE, event, true);

// 3. Run several frames to let Service Monitor poll
emulator->RunFrames(5);

// 4. Read pointer position
uint8_t x = memory.ReadByte(0xE03C);
uint8_t y = memory.ReadByte(0xE03D);

// 5. Verify position changed
EXPECT_GT(x, 0);
EXPECT_GT(y, 0);
```

---

## 4. Debugging Checklist

When Service Monitor mouse input doesn't work:

1. **Check mouse configuration:**
   ```
   Core::Init - Mouse created, config.input.mouse=1, present=1
   ```
   If `present=0`, check `unreal.ini` has `Mouse=KEMPSTON`

2. **Check polling occurs:**
   ```
   Mouse::recordPoll - present=1, tState=..., frame=..., lastFrame=...
   ```
   If no polling, Service Monitor isn't reading mouse ports

3. **Check control variable:**
   Read `#E03B` - if bit 5 is clear, mouse is disabled in Service Monitor

4. **Check events flow:**
   ```
   DeviceScreen - posting NC_MOUSE_MOVE dx=... dy=...
   Mouse::OnMouseMove - dx=..., dy=..., x=...->..., y=...->...
   ```
   If posting but no OnMouseMove, check UUID filtering

5. **Check grab state:**
   ```
   transitionTo: X -> Y
   ```
   States: 0=Disabled, 1=Armed, 2=Engaged, 3=Released

---

## 5. Known Issues

### 5.1 Mouse Not Enabled by Default

The Service Monitor defaults to joystick-only pointer (`#E03B = 0xC8`).
Mouse must be enabled manually via:
- Service Monitor settings menu, or
- Direct memory write to `#E03B` (set bit 5)

### 5.2 Single Poll at Boot

The Service Monitor polls mouse ports ONCE during initialization (frame ~110).
Continuous polling only occurs when mouse is enabled (bit 5 of `#E03B`).
This is by design - the firmware conserves CPU cycles by not polling unused devices.
