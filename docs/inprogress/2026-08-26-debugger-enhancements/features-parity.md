# Debugger Basic Features Parity

Priority focus: usability, navigation, and core widget parity with xpeccy-plus.

## Gap Summary

| Feature | xpeccy-plus | unreal-qt | Priority |
|---------|-------------|-----------|----------|
| Stack depth | 9 entries (SP-2 to SP+E) | 4 entries | **High** |
| Stack → disassembly jump + back | Yes | Jump only, no back | **High** |
| Address history (back/forward) | Yes, unlimited | None | **High** |
| Marked addresses (1-5 slots) | Ctrl+1-5 mark, Alt+1-5 jump | None | **High** |
| Go to PC hotkey | Yes (dedicated key) | None | **High** |
| Follow operand address | Yes (jump to target) | None | **High** |
| Individual flag checkboxes | Yes, clickable to toggle | String only | **High** |
| IFF1/IFF2 display | Yes | IM only | Medium |
| Memory slot display (SLOT0-3) | Yes | MemoryPagesWidget exists | Medium |
| Signal indicators (DOS/ROM/INT) | Yes | None | Medium |
| Frame counter | Yes | None | Medium |
| Beam position (RAY X/Y) | Yes | UlaBeamWidget exists | Low |
| Port watch | Yes, configurable | None | Medium |

## 1. Enhanced Stack Widget

### Current State
- Shows 4 values: SP+0, SP+2, SP+4, SP+6
- Double-click → memory view
- Context menu with disassembly jump

### Proposed Changes

**a) Increase depth to 9 entries:**
```
SP-2: 0000   (value pushed last, about to be popped next)
SP+0: 8003   ← Current top of stack
SP+2: 5C00
SP+4: 0000
SP+6: 1234
SP+8: 5678
SP+A: ABCD
SP+C: 0000
SP+E: 0000
```

**b) Make values clickable links:**
- Single click → jump to address in disassembly (signal already exists)
- Maintain "back to PC" capability via disassembly history

**c) Visual indication:**
- Highlight values that look like valid return addresses (code area)
- Different style for SP-2 row (speculative)

### Mockup
```
+--Stack---------------------------+
| -2: 0000                         |
| +0: 8003 ←                       |
| +2: 5C00                         |
| +4: 0000                         |
| +6: 1234                         |
| +8: 5678                         |
| +A: ABCD                         |
| +C: 0000                         |
| +E: 0000                         |
+----------------------------------+
```

**Implementation:**
- Modify `stackwidget.ui` to add 5 more rows + SP-2
- Update `readStackIntoArray()` depth to 9
- Add row labels (-2, +0, +2... +E)

---

## 2. Disassembly Navigation Enhancements

### Current State
- Up/Down/PageUp/PageDown navigation
- Ctrl+G for go to address dialog
- No history, no marked addresses, no "follow" operand

### Proposed Features

**a) Address History (Back/Forward)**

Store visited addresses in a stack. Navigate with:
- `Backspace` or `Alt+Left` → Go back
- `Shift+Backspace` or `Alt+Right` → Go forward

```cpp
// Add to DisassemblerWidget
std::vector<uint16_t> m_addressHistory;
int m_historyPosition = -1;

void navigateBack();
void navigateForward();
void pushAddressToHistory(uint16_t addr);
```

**b) Marked Addresses (5 slots)**

Quick-access bookmarks:
- `Ctrl+1` through `Ctrl+5` → Mark current address in slot
- `Alt+1` through `Alt+5` → Jump to marked address
- Visual indicator showing which slots are used

```cpp
uint16_t m_markedAddresses[5] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
```

**c) Go to PC**

Dedicated hotkey to return to current PC:
- `Home` or `Ctrl+P` → Jump to PC, no history push

**d) Follow Operand**

Jump to target address of current instruction:
- `Enter` or `F` on a JP/CALL/JR instruction → Jump to target
- Works for: JP nn, CALL nn, JR e, JP (HL), DJNZ, etc.

```cpp
// In keyPressEvent
case Qt::Key_Return:
case Qt::Key_F:
    followOperandAddress();
    break;
```

### Mockup - Navigation Status Bar
```
+--Disassembly-------------------------------------------+
| [<] [>] [Home]  Marks: [1] [2] [ ] [ ] [ ]  Mode: Cmd  |
+--------------------------------------------------------+
| 8000  3E 05     LD A,#05                               |
|>8002  CD 00 90  CALL #9000    ← Enter to follow        |
| 8005  FE 0A     CP #0A                                 |
| 8007  20 F7     JR NZ,#8000                            |
+--------------------------------------------------------+
```

### Key Bindings Summary
| Key | Action |
|-----|--------|
| `Backspace` / `Alt+←` | History back |
| `Shift+Backspace` / `Alt+→` | History forward |
| `Home` / `Ctrl+P` | Go to PC |
| `Enter` / `F` | Follow operand |
| `Ctrl+G` | Go to address dialog |
| `Ctrl+1-5` | Mark address in slot |
| `Alt+1-5` | Jump to marked address |
| `B` | Toggle scroll mode |

---

## 3. Flags Widget Enhancement

### Current State
Shows flags as string: `SZ-H-PNC`

### Proposed Changes

**a) Individual clickable checkboxes:**
```
Flags
S Z - H - P N C
[x][ ]   [ ]   [ ][ ][ ]
```

Each checkbox:
- Shows current state
- Click to toggle (modify CPU state)
- Tooltip with flag meaning

**b) Alternative: Compact toggle row**
```
Flags: [S][Z][-][H][-][P][N][C]
       ●  ○     ○     ○  ○  ●
```

### Implementation
```cpp
// In RegistersWidget
QCheckBox* m_flagS;
QCheckBox* m_flagZ;
QCheckBox* m_flagH;
QCheckBox* m_flagP;
QCheckBox* m_flagN;
QCheckBox* m_flagC;

void updateFlagCheckboxes(uint8_t f);
void onFlagToggled(int flag);
```

---

## 4. IFF/IM Status Display with ISR Preview

### Current State
Shows `INT: 02` (IM mode only)

### Proposed Changes

**a) Show full interrupt state + ISR address:**

| IM Mode | ISR Address Calculation |
|---------|------------------------|
| IM 0 | N/A (data bus instruction) |
| IM 1 | Always `0x0038` |
| IM 2 | Word at `(I << 8) \| 0xFF` → actual handler address |

**b) Clickable ISR address:**
- Click → jump to ISR in disassembly
- Shows resolved address for IM2 (not just the vector location)

**c) Hover popup with ISR disassembly:**
- After 1 second hover, show popup with first ~20 instructions
- Cache disassembly (invalidate on memory write to ISR area)
- Max 100 bytes or until RET/RETI/RETN

### Mockup - Compact Display
```
+--Interrupt---------------------------------+
| IM2  EI  ISR: 8000 ←click                  |
+--------------------------------------------+
```

### Mockup - Hover Popup (after 1s)
```
+--ISR at 8000 (IM2: I=80, vector at 80FF)--+
| 8000  F5        PUSH AF                    |
| 8001  C5        PUSH BC                    |
| 8002  D5        PUSH DE                    |
| 8003  E5        PUSH HL                    |
| 8004  3A 00 5C  LD A,(5C00)                |
| 8007  FE 01     CP #01                     |
| 8009  20 05     JR NZ,#8010                |
| 800B  3E FF     LD A,#FF                   |
| 800D  32 00 5C  LD (5C00),A                |
| 8010  E1        POP HL                     |
| 8011  D1        POP DE                     |
| 8012  C1        POP BC                     |
| 8013  F1        POP AF                     |
| 8014  ED 4D     RETI                       |
+--------------------------------------------+
```

### Mockup - Full Display (optional expanded view)
```
+--Interrupt Status----------------------------------+
| Mode: [IM2]  IFF1: [x]  IFF2: [x]  (EI)           |
| I reg: 80    Vector: 80FF → Handler: 8000         |
|                                    [Jump to ISR]   |
+----------------------------------------------------+
```

### Implementation

```cpp
// In RegistersWidget or new InterruptStatusWidget

struct ISRCache {
    uint16_t address;
    uint8_t iRegister;      // For cache invalidation
    uint8_t imMode;
    std::vector<std::string> disassembly;
    bool valid;
};

class InterruptStatusWidget : public QWidget {
    ISRCache m_isrCache;
    QTimer* m_hoverTimer;
    QLabel* m_isrAddressLabel;  // Clickable
    
    uint16_t calculateISRAddress();
    void cacheISRDisassembly();
    void showISRPopup();
    void jumpToISR();
    
    uint16_t calculateISRAddress() {
        Z80Registers* regs = getZ80Registers();
        switch (regs->im) {
            case 0: return 0xFFFF;  // N/A
            case 1: return 0x0038;
            case 2: {
                uint16_t vectorAddr = (regs->i << 8) | 0xFF;
                Memory* mem = getMemory();
                uint8_t lo = mem->DirectReadFromZ80Memory(vectorAddr);
                uint8_t hi = mem->DirectReadFromZ80Memory(vectorAddr + 1);
                return (hi << 8) | lo;
            }
        }
        return 0xFFFF;
    }
    
    void cacheISRDisassembly() {
        uint16_t addr = calculateISRAddress();
        if (addr == 0xFFFF) return;
        
        // Check if cache is still valid
        Z80Registers* regs = getZ80Registers();
        if (m_isrCache.valid && 
            m_isrCache.address == addr &&
            m_isrCache.iRegister == regs->i &&
            m_isrCache.imMode == regs->im) {
            return;  // Cache hit
        }
        
        // Disassemble up to 100 bytes or RET/RETI/RETN
        m_isrCache.disassembly.clear();
        m_isrCache.address = addr;
        m_isrCache.iRegister = regs->i;
        m_isrCache.imMode = regs->im;
        
        auto& disasm = getDisassembler();
        uint16_t curAddr = addr;
        int bytes = 0;
        
        while (bytes < 100 && m_isrCache.disassembly.size() < 20) {
            DisassembledInstruction instr = disasm->disassembleInstruction(curAddr);
            m_isrCache.disassembly.push_back(instr.formatted);
            
            bytes += instr.length;
            curAddr += instr.length;
            
            // Stop at RET variants
            if (instr.mnemonic == "RET" || 
                instr.mnemonic == "RETI" || 
                instr.mnemonic == "RETN") {
                break;
            }
        }
        
        m_isrCache.valid = true;
    }
};
```

### Signals
```cpp
signals:
    void jumpToAddressInDisassembly(uint16_t addr);  // Connect to disassembly widget
```

### Cache Invalidation
- Invalidate when I register changes
- Invalidate when IM mode changes
- Optionally: invalidate on memory write to ISR area (requires write tracking)

---

## 5. System Status Panel

New widget showing machine state (like xpeccy's "misc" panel).

### Features
- Memory mapping: which pages in slots 0-3 (clickable → navigate)
- DOS mode indicator
- ROM/INT signal indicators  
- Frame counter
- Beam position with area indication

### 5.1 Memory Slot Navigation

Each slot should be clickable with context menu:

| Action | Target |
|--------|--------|
| "Show in Memory View" | Jump memory widget to slot start (0000/4000/8000/C000) |
| "Show in Disassembly" | Jump disassembly to slot start |
| "Show Page Start" | Jump to physical page start (e.g., RAM page 5 = offset 0x14000 in full RAM) |

**Right-click context menu:**
```
+--Slot 1: RAM 5------------------+
| Jump to 4000 in Disassembly     |
| Jump to 4000 in Memory View     |
| --------------------------------|
| Show full RAM page 5            |
+----------------------------------+
```

### 5.2 Beam Position Display

Beam coordinates need proper context - the raw (X, Y) values are in T-states/scanlines, but users need to understand WHERE the beam is:

**Coordinate systems:**
| Area | Description |
|------|-------------|
| **BORDER-TOP** | Above visible screen |
| **BORDER-LEFT** | Left of screen (including HSYNC/HBLANK) |
| **SCREEN** | Visible 256x192 area |
| **BORDER-RIGHT** | Right of screen |
| **BORDER-BOTTOM** | Below visible screen |
| **VBLANK** | Vertical blanking (offscreen) |

**Display format:**
```
Beam: SCREEN (128, 96)      ← pixel coordinates within 256x192
Beam: BORDER-LEFT (-32, 96) ← negative X = left border
Beam: BORDER-TOP (128, -8)  ← negative Y = top border  
Beam: VBLANK (---, 312)     ← during vertical blank
```

**Alternative: Framebuffer coordinates**
Show raw position in full frame (e.g., 448x312 for 128K timing):
```
Beam: 224, 160  [SCREEN: 128, 96]
```

### Mockup - Enhanced
```
+--Memory Mapping---------------------------+
| 0000: ROM 0  ←   | 4000: RAM 5  ←        |
| 8000: RAM 2  ←   | C000: RAM 0  ←        |
+--Signals----------------------------------+
| [●DOS] [ ROM ] [●INT]                    |
+--Timing-----------------------------------+
| Frame: 12345   T: 34567/69888            |
+--Beam-------------------------------------+
| Raw: 224, 160   Area: SCREEN (128, 96)   |
+-------------------------------------------+
```

### Mockup - Compact
```
+--System----------------------------------+
| ROM:0 | RAM:5 | RAM:2 | RAM:0   [DOS]   |
| Frame: 12345  Beam: SCR(128,96) T:34567 |
+------------------------------------------+
```

### Implementation

```cpp
class SystemStatusWidget : public QWidget {
    Q_OBJECT
    
    // Memory slot labels (clickable)
    ClickableLabel* m_slot0Label;  // 0000-3FFF
    ClickableLabel* m_slot1Label;  // 4000-7FFF
    ClickableLabel* m_slot2Label;  // 8000-BFFF
    ClickableLabel* m_slot3Label;  // C000-FFFF
    
    // Signal indicators
    QLabel* m_dosIndicator;
    QLabel* m_romIndicator;
    QLabel* m_intIndicator;
    
    // Timing
    QLabel* m_frameCounter;
    QLabel* m_tStates;
    
    // Beam position
    QLabel* m_beamRaw;
    QLabel* m_beamArea;
    
    // Screen timing constants (from current machine config)
    struct ScreenTiming {
        int borderTop;      // Scanlines before screen
        int borderBottom;   // Scanlines after screen
        int borderLeft;     // T-states before screen per line
        int borderRight;    // T-states after screen per line
        int screenWidth;    // 256 pixels
        int screenHeight;   // 192 lines
        int totalWidth;     // Full line in T-states (224 for 128K)
        int totalHeight;    // Full frame in lines (312 for 128K)
    };
    ScreenTiming m_timing;
    
    enum class BeamArea {
        Screen,
        BorderTop,
        BorderBottom,
        BorderLeft,
        BorderRight,
        VBlank,
        HBlank
    };
    
    struct BeamPosition {
        int rawX;           // T-state within line
        int rawY;           // Scanline
        BeamArea area;
        int screenX;        // -1 if not applicable
        int screenY;        // -1 if not applicable
    };
    
    BeamPosition calculateBeamPosition(int tStates);
    QString formatBeamArea(BeamArea area);
    
signals:
    void jumpToAddressInDisassembly(uint16_t addr);
    void jumpToAddressInMemoryView(uint16_t addr);
    void showMemoryBank(MemoryBankModeEnum type, uint8_t page);
    
public slots:
    void onSlot0Clicked();
    void onSlot1Clicked();
    void onSlot2Clicked();
    void onSlot3Clicked();
    void showSlotContextMenu(int slot, const QPoint& pos);
};

// Beam area calculation
BeamPosition SystemStatusWidget::calculateBeamPosition(int tStates) {
    BeamPosition pos;
    
    // Convert T-states to raw X/Y
    pos.rawY = tStates / m_timing.totalWidth;
    pos.rawX = tStates % m_timing.totalWidth;
    
    // Determine area and screen coordinates
    int screenStartY = m_timing.borderTop;
    int screenEndY = screenStartY + m_timing.screenHeight;
    int screenStartX = m_timing.borderLeft;
    int screenEndX = screenStartX + m_timing.screenWidth / 2;  // 2 T-states per pixel
    
    if (pos.rawY < screenStartY) {
        pos.area = BeamArea::BorderTop;
        pos.screenX = -1;
        pos.screenY = pos.rawY - screenStartY;  // Negative
    } else if (pos.rawY >= screenEndY) {
        if (pos.rawY >= m_timing.totalHeight - m_timing.borderBottom) {
            pos.area = BeamArea::VBlank;
        } else {
            pos.area = BeamArea::BorderBottom;
        }
        pos.screenX = -1;
        pos.screenY = pos.rawY - screenStartY;  // > 192
    } else {
        // Within screen scanlines
        pos.screenY = pos.rawY - screenStartY;
        
        if (pos.rawX < screenStartX) {
            pos.area = BeamArea::BorderLeft;
            pos.screenX = (pos.rawX - screenStartX) * 2;  // Negative
        } else if (pos.rawX >= screenEndX) {
            pos.area = BeamArea::BorderRight;
            pos.screenX = (pos.rawX - screenStartX) * 2;  // > 256
        } else {
            pos.area = BeamArea::Screen;
            pos.screenX = (pos.rawX - screenStartX) * 2;
        }
    }
    
    return pos;
}
```

### Machine-Specific Timing

Different machines have different screen timings:

| Machine | Total Lines | Border Top | Screen | Border Bottom | Line Width |
|---------|-------------|------------|--------|---------------|------------|
| 48K | 312 | 64 | 192 | 56 | 224 T |
| 128K | 311 | 63 | 192 | 56 | 228 T |
| Pentagon | 320 | 80 | 192 | 48 | 224 T |

Get timing from `EmulatorContext->pScreen->getTimingParams()` or similar.

---

## 6. Port Watch Widget

Dynamic, per-configuration port monitoring with weak decoding awareness.

### Features
- **Per-configuration port list** from PortManager
- Standard ports + model-specific ports
- Weak decoding display (show decode mask)
- Visual indicator when value changes
- User can add custom ports

### Port Decoding Examples

ZX Spectrum ports use weak decoding - only certain address bits matter:

| Port | Decode Mask | Matches | Description |
|------|-------------|---------|-------------|
| `xxxx xxxx xxxx xxx0` | `0x0001` | 0xFE, 0x00, 0x02... | Keyboard/Border |
| `0xxx xxxx xxxx xx0x` | `0x8002` | 0x7FFD | 128K Memory Paging |
| `11xx xxxx xxxx xx0x` | `0xC002` | 0xFFFD | AY Register Select |
| `10xx xxxx xxxx xx0x` | `0xC002` | 0xBFFD | AY Data Write |
| `0001 xxxx xxxx xx0x` | `0xF002` | 0x1FFD | +3 Paging |
| `xxxx xxxx 0001 1111` | `0x00FF` | 0x001F | Kempston Joystick |

### Port Registry per Machine

```cpp
// In core/src/emulator/ports/portregistry.h

struct PortDefinition {
    uint16_t port;          // Canonical port address
    uint16_t decodeMask;    // Which bits matter (1 = must match)
    uint8_t accessType;     // PORT_IN | PORT_OUT | PORT_BOTH
    const char* name;       // "Keyboard", "128K Paging", etc.
    const char* shortName;  // "KBD", "PAGE", "AY-REG", etc.
};

class PortRegistry {
public:
    // Get ports for current machine configuration
    static std::vector<PortDefinition> getPortsForMachine(MachineType machine);
    
    // Standard ports (all machines)
    static const std::vector<PortDefinition> STANDARD_PORTS;
    
    // Machine-specific additions
    static const std::vector<PortDefinition> PORTS_128K;
    static const std::vector<PortDefinition> PORTS_PLUS3;
    static const std::vector<PortDefinition> PORTS_PENTAGON;
    static const std::vector<PortDefinition> PORTS_SCORPION;
};

// Example definitions
const std::vector<PortDefinition> PortRegistry::STANDARD_PORTS = {
    {0x00FE, 0x0001, PORT_BOTH, "Keyboard/Border", "KBD"},
    {0x001F, 0x00FF, PORT_IN,   "Kempston Joystick", "KEMP"},
};

const std::vector<PortDefinition> PortRegistry::PORTS_128K = {
    {0x7FFD, 0x8002, PORT_OUT,  "128K Memory Paging", "PAGE"},
    {0xFFFD, 0xC002, PORT_OUT,  "AY Register Select", "AY-REG"},
    {0xBFFD, 0xC002, PORT_OUT,  "AY Data Write", "AY-DAT"},
    {0xFFFD, 0xC002, PORT_IN,   "AY Register Read", "AY-RD"},
};

const std::vector<PortDefinition> PortRegistry::PORTS_PLUS3 = {
    {0x1FFD, 0xF002, PORT_OUT,  "+3 Extended Paging", "+3PAGE"},
    {0x2FFD, 0xF002, PORT_OUT,  "+3 FDC Main Status", "FDC-ST"},
    {0x3FFD, 0xF002, PORT_BOTH, "+3 FDC Data", "FDC-DT"},
};
```

### Widget Implementation

```cpp
class PortWatchWidget : public QWidget {
    Q_OBJECT
    
    struct WatchedPort {
        PortDefinition definition;
        uint8_t lastInValue = 0xFF;
        uint8_t lastOutValue = 0xFF;
        bool hasInValue = false;
        bool hasOutValue = false;
        bool inChanged = false;   // Flash indicator
        bool outChanged = false;
        bool isCustom = false;    // User-added port
    };
    
    std::vector<WatchedPort> m_ports;
    MachineType m_currentMachine;
    
public slots:
    void onMachineChanged(MachineType machine);
    void onPortIn(uint16_t port, uint8_t value);
    void onPortOut(uint16_t port, uint8_t value);
    void addCustomPort();
    void removeSelectedPort();
    
private:
    void rebuildPortList();
    bool portMatches(uint16_t accessedPort, const PortDefinition& def);
};

// Check if accessed port matches definition (weak decoding)
bool PortWatchWidget::portMatches(uint16_t accessedPort, const PortDefinition& def) {
    return (accessedPort & def.decodeMask) == (def.port & def.decodeMask);
}

void PortWatchWidget::onPortOut(uint16_t port, uint8_t value) {
    for (auto& wp : m_ports) {
        if ((wp.definition.accessType & PORT_OUT) && 
            portMatches(port, wp.definition)) {
            wp.outChanged = (wp.hasOutValue && wp.lastOutValue != value);
            wp.lastOutValue = value;
            wp.hasOutValue = true;
        }
    }
    refresh();
}
```

### Mockup - Enhanced

```
+--Port Watch (128K)----------------------------------+
| Port   | Mask | IN   | OUT  | Name                  |
+--------+------+------+------+-----------------------+
| 00FE   | 0001 | BF   |  07  | Keyboard/Border       |
| 7FFD   | 8002 |  --  | *10  | 128K Memory Paging    |  ← * = changed
| FFFD   | C002 |  --  |  0E  | AY Register Select    |
| BFFD   | C002 |  --  |  00  | AY Data Write         |
+--------+------+------+------+-----------------------+
| [+ Add Custom Port]  [- Remove]  [Reset Values]     |
+-----------------------------------------------------+
```

### Mockup - Compact

```
+--Ports (128K)------------------+
| FE:BF/07   7FFD:--/*10  [...]  |
| FFFD:--/0E  BFFD:--/00         |
+---------------------------------+
```

### Add Custom Port Dialog

```
+--Add Custom Port---------------------------+
| Port Address: [____0x00____]               |
| Decode Mask:  [____0xFFFF__] (optional)    |
| Access:       [x] IN  [x] OUT              |
| Name:         [_____________________]      |
|                                            |
|              [Cancel]  [Add]               |
+--------------------------------------------+
```

### Port Mask Tooltip

Hover on mask column shows detailed decode info:

**Mockup - Tooltip for 7FFD (mask 0x8002):**
```
+--Port Decode: 7FFD--------------------------------+
| Canonical: 0x7FFD (32765)                         |
| Mask:      0x8002 (32770)                         |
|                                                   |
| Binary breakdown:                                 |
|   Addr: 0 1 1 1  1 1 1 1  1 1 1 1  1 1 0 1        |
|   Mask: 1 0 0 0  0 0 0 0  0 0 0 0  0 0 1 0        |
|         ↑                              ↑          |
|        A15=0                          A1=0        |
|                                                   |
| Matches: Any address where bit 15=0 and bit 1=0   |
| Examples: 7FFD, 3FFD, 5FFD, 1FFD, 7FF9...         |
|                                                   |
| Purpose: 128K Memory Paging Register              |
+---------------------------------------------------+
```

**Mockup - Tooltip for FE (mask 0x0001):**
```
+--Port Decode: 00FE--------------------------------+
| Canonical: 0x00FE (254)                           |
| Mask:      0x0001 (1)                             |
|                                                   |
| Binary breakdown:                                 |
|   Addr: 0 0 0 0  0 0 0 0  1 1 1 1  1 1 1 0        |
|   Mask: 0 0 0 0  0 0 0 0  0 0 0 0  0 0 0 1        |
|                                              ↑    |
|                                             A0=0  |
|                                                   |
| Matches: Any EVEN address (bit 0 = 0)             |
| Examples: 00FE, 0000, 0002, FFFE, 7FFE...         |
|                                                   |
| Purpose: ULA - Keyboard (IN) / Border (OUT)       |
+---------------------------------------------------+
```

### PortInfo Content System

Unified port information for all models:

```cpp
// core/src/emulator/ports/portinfo.h

struct PortInfo {
    uint16_t canonicalPort;
    uint16_t decodeMask;
    uint8_t accessType;           // PORT_IN | PORT_OUT
    
    // Identification
    const char* id;               // "ula", "128k_paging", "ay_reg", etc.
    const char* name;             // "ULA", "128K Memory Paging"
    const char* shortName;        // "ULA", "PAGE", "AY-R"
    
    // Documentation
    const char* purpose;          // Human description
    const char* decodeDescription; // "A0=0 (even addresses)"
    
    // Bit meanings for detailed tooltip
    struct BitInfo {
        uint8_t bit;              // 0-15
        bool mustBe;              // Required value (0 or 1)
        const char* meaning;      // "Memory bank select"
    };
    std::vector<BitInfo> significantBits;
    
    // OUT value bit meanings
    struct ValueBitInfo {
        uint8_t bit;
        const char* meaning;
    };
    std::vector<ValueBitInfo> outBits;
    std::vector<ValueBitInfo> inBits;
};

// Registry with all port definitions
class PortInfoRegistry {
public:
    static const PortInfoRegistry& instance();
    
    // Get info by port ID
    const PortInfo* getById(const std::string& id) const;
    
    // Get all ports for a machine
    std::vector<const PortInfo*> getForMachine(MachineType machine) const;
    
    // Format tooltip HTML
    QString formatTooltipHtml(const PortInfo& info) const;
    QString formatMaskBreakdown(uint16_t port, uint16_t mask) const;
    
private:
    std::unordered_map<std::string, PortInfo> m_ports;
    std::unordered_map<MachineType, std::vector<std::string>> m_machinePortIds;
};
```

### Example Port Definitions

```cpp
// Static port definitions
const PortInfo PORT_ULA = {
    .canonicalPort = 0x00FE,
    .decodeMask = 0x0001,
    .accessType = PORT_BOTH,
    .id = "ula",
    .name = "ULA",
    .shortName = "ULA",
    .purpose = "Keyboard input, border color output, tape/speaker",
    .decodeDescription = "A0=0 (any even address)",
    .significantBits = {
        {0, false, "Must be 0 for ULA select"}
    },
    .outBits = {
        {0, "Speaker"},
        {1, "Tape output"},
        {2, "MIC output"},
        {3, "Border bit 0"},
        {4, "Border bit 1"},
        {5, "Border bit 2"},
    },
    .inBits = {
        {0, "Key row bit 0"},
        {1, "Key row bit 1"},
        {2, "Key row bit 2"},
        {3, "Key row bit 3"},
        {4, "Key row bit 4"},
        {5, "Always 1"},
        {6, "Tape input"},
        {7, "Always 1"},
    }
};

const PortInfo PORT_128K_PAGING = {
    .canonicalPort = 0x7FFD,
    .decodeMask = 0x8002,
    .accessType = PORT_OUT,
    .id = "128k_paging",
    .name = "128K Memory Paging",
    .shortName = "PAGE",
    .purpose = "RAM bank selection, ROM selection, screen select",
    .decodeDescription = "A15=0, A1=0",
    .significantBits = {
        {15, false, "Must be 0"},
        {1, false, "Must be 0"}
    },
    .outBits = {
        {0, "RAM bank bit 0"},
        {1, "RAM bank bit 1"},
        {2, "RAM bank bit 2"},
        {3, "Screen select (0=5, 1=7)"},
        {4, "ROM select (0=128K, 1=48K)"},
        {5, "Paging disable (lock)"},
    }
};

const PortInfo PORT_AY_REG = {
    .canonicalPort = 0xFFFD,
    .decodeMask = 0xC002,
    .accessType = PORT_BOTH,
    .id = "ay_reg",
    .name = "AY Register Select",
    .shortName = "AY-R",
    .purpose = "Select AY-3-8912 register for read/write",
    .decodeDescription = "A15=1, A14=1, A1=0",
    .significantBits = {
        {15, true, "Must be 1"},
        {14, true, "Must be 1"},
        {1, false, "Must be 0"}
    },
    .outBits = {
        {0, "Register number bit 0"},
        {1, "Register number bit 1"},
        {2, "Register number bit 2"},
        {3, "Register number bit 3"},
    }
};
```

### Tooltip Rendering

```cpp
QString PortInfoRegistry::formatTooltipHtml(const PortInfo& info) const {
    QString html = "<div style='font-family: monospace;'>";
    
    // Header
    html += QString("<b>Port: %1</b> (%2)<br>")
        .arg(info.name)
        .arg(info.purpose);
    
    // Address info
    html += QString("<br>Canonical: 0x%1 (%2)<br>")
        .arg(info.canonicalPort, 4, 16, QChar('0')).toUpper()
        .arg(info.canonicalPort);
    html += QString("Mask: 0x%1 (%2)<br>")
        .arg(info.decodeMask, 4, 16, QChar('0')).toUpper()
        .arg(info.decodeMask);
    
    // Binary breakdown
    html += "<br><b>Decode:</b> " + QString(info.decodeDescription) + "<br>";
    html += formatMaskBreakdown(info.canonicalPort, info.decodeMask);
    
    // Bit meanings
    if (!info.outBits.empty()) {
        html += "<br><b>OUT bits:</b><br>";
        for (const auto& b : info.outBits) {
            html += QString("  D%1: %2<br>").arg(b.bit).arg(b.meaning);
        }
    }
    
    if (!info.inBits.empty()) {
        html += "<br><b>IN bits:</b><br>";
        for (const auto& b : info.inBits) {
            html += QString("  D%1: %2<br>").arg(b.bit).arg(b.meaning);
        }
    }
    
    html += "</div>";
    return html;
}

QString PortInfoRegistry::formatMaskBreakdown(uint16_t port, uint16_t mask) const {
    QString html = "<pre>";
    
    // Bit numbers
    html += "Bit:  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0<br>";
    
    // Port value
    html += "Addr: ";
    for (int i = 15; i >= 0; i--) {
        html += QString(" %1 ").arg((port >> i) & 1);
    }
    html += "<br>";
    
    // Mask
    html += "Mask: ";
    for (int i = 15; i >= 0; i--) {
        html += QString(" %1 ").arg((mask >> i) & 1);
    }
    html += "<br>";
    
    // Significance markers
    html += "      ";
    for (int i = 15; i >= 0; i--) {
        if ((mask >> i) & 1) {
            html += " ↑ ";
        } else {
            html += "   ";
        }
    }
    html += "</pre>";
    
    return html;
}
```

### Value Change Highlighting

- Normal: white/default background
- Just changed: yellow flash (fade over 500ms)
- No value yet: `--` in gray

### Customization

**Default behavior:**
- Show ALL ports for current machine model
- User can hide, show, or rearrange

**Per-profile persistence:**
```json
{
  "portWatch": {
    "hiddenPorts": ["001F"],           // Hidden standard ports
    "order": ["00FE", "7FFD", "FFFD", "BFFD"],  // Display order
    "customPorts": [
      {"port": "0xE3", "mask": "0xFF", "access": "both", "name": "DivMMC Control"}
    ]
  }
}
```

**Right-click on port row:**
```
+--Port FE (Keyboard)-------------+
| Hide this port                  |
| --------------------------------|
| Move Up                         |
| Move Down                       |
| --------------------------------|
| Reset to Default Order          |
| Show All Ports                  |
+---------------------------------+
```

**Drag & drop:** Allow reordering rows by drag

---

## 7. PreferenceManager (Persistence Abstraction)

All debugger preferences should go through a `PreferenceManager` class to allow switching backends (Qt → SDL/INI/JSON).

### Interface

```cpp
// unreal-qt/src/preferences/preferencemanager.h

class IPreferenceProvider {
public:
    virtual ~IPreferenceProvider() = default;
    
    virtual void setValue(const QString& key, const QVariant& value) = 0;
    virtual QVariant getValue(const QString& key, const QVariant& defaultValue = {}) const = 0;
    virtual bool contains(const QString& key) const = 0;
    virtual void remove(const QString& key) = 0;
    
    virtual void beginGroup(const QString& group) = 0;
    virtual void endGroup() = 0;
    
    virtual QStringList childKeys() const = 0;
    virtual QStringList childGroups() const = 0;
    
    virtual void sync() = 0;  // Force write to storage
};

class PreferenceManager {
public:
    static PreferenceManager& instance();
    
    void setProvider(std::unique_ptr<IPreferenceProvider> provider);
    IPreferenceProvider* provider() const { return m_provider.get(); }
    
    // Convenience methods
    template<typename T>
    void set(const QString& key, const T& value);
    
    template<typename T>
    T get(const QString& key, const T& defaultValue = {}) const;
    
    // Scoped group access
    class GroupScope {
    public:
        GroupScope(PreferenceManager& mgr, const QString& group);
        ~GroupScope();
    private:
        PreferenceManager& m_mgr;
    };
    
private:
    std::unique_ptr<IPreferenceProvider> m_provider;
};
```

### Qt Implementation (Default)

```cpp
// unreal-qt/src/preferences/qtpreferenceprovider.h

class QtPreferenceProvider : public IPreferenceProvider {
public:
    QtPreferenceProvider(const QString& organization = "UnrealSpeccy",
                         const QString& application = "unreal-qt");
    
    void setValue(const QString& key, const QVariant& value) override;
    QVariant getValue(const QString& key, const QVariant& defaultValue) const override;
    bool contains(const QString& key) const override;
    void remove(const QString& key) override;
    
    void beginGroup(const QString& group) override;
    void endGroup() override;
    
    QStringList childKeys() const override;
    QStringList childGroups() const override;
    
    void sync() override;
    
private:
    QSettings m_settings;
};
```

### JSON File Implementation (for SDL/portable)

```cpp
// For future SDL-only GUI or portable mode
class JsonPreferenceProvider : public IPreferenceProvider {
public:
    JsonPreferenceProvider(const QString& filePath);
    // ... implementation using QJsonDocument or nlohmann::json
private:
    QString m_filePath;
    QJsonObject m_root;
    QStringList m_groupStack;
};
```

### Usage in Widgets

```cpp
// In PortWatchWidget
void PortWatchWidget::savePreferences() {
    auto& prefs = PreferenceManager::instance();
    PreferenceManager::GroupScope scope(prefs, "PortWatch");
    
    prefs.set("hiddenPorts", m_hiddenPorts);
    prefs.set("portOrder", m_portOrder);
    prefs.set("customPorts", serializeCustomPorts());
}

void PortWatchWidget::loadPreferences() {
    auto& prefs = PreferenceManager::instance();
    PreferenceManager::GroupScope scope(prefs, "PortWatch");
    
    m_hiddenPorts = prefs.get<QStringList>("hiddenPorts");
    m_portOrder = prefs.get<QStringList>("portOrder");
    deserializeCustomPorts(prefs.get<QVariantList>("customPorts"));
}
```

### Preference Keys Structure

```
UnrealSpeccy/unreal-qt/
├── Debugger/
│   ├── Window/
│   │   ├── geometry
│   │   └── state
│   ├── PortWatch/
│   │   ├── hiddenPorts
│   │   ├── portOrder
│   │   └── customPorts
│   ├── Disassembly/
│   │   ├── markedAddresses
│   │   └── scrollMode
│   └── Registers/
│       └── flagsExpanded
├── Profiles/
│   └── <profile-name>/
│       └── ... profile-specific settings
└── General/
    ├── lastOpenedFile
    └── recentFiles
```

### Initialization

```cpp
// In main.cpp or application startup
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // Set up preference provider
    PreferenceManager::instance().setProvider(
        std::make_unique<QtPreferenceProvider>("UnrealSpeccy", "unreal-qt")
    );
    
    // Or for portable mode:
    // PreferenceManager::instance().setProvider(
    //     std::make_unique<JsonPreferenceProvider>("./config/preferences.json")
    // );
    
    // ...
}
```

### Integration with Breakpoints

Right-click on port row:
```
+--Port FE (Keyboard)-------------+
| Set IN Breakpoint               |
| Set OUT Breakpoint              |
| Set IN+OUT Breakpoint           |
| --------------------------------|
| Copy Port Address               |
| Copy Last IN Value              |
| Copy Last OUT Value             |
+---------------------------------+
```

---

## Implementation Phases

### Phase 1: Navigation (Highest Impact)
1. Address history with back/forward
2. Go to PC hotkey
3. Follow operand address
4. Marked addresses (5 slots)

**Effort:** 2-3 days

### Phase 2: Stack Widget
1. Increase depth to 9 entries
2. Add SP-2 row
3. Single-click to jump in disassembly

**Effort:** 1 day

### Phase 3: Flags/Interrupts Enhancement
1. Individual flag checkboxes with toggle
2. IFF1/IFF2 display
3. ISR address display (clickable → jump to disassembly)
4. ISR hover popup with cached disassembly

**Effort:** 2 days

### Phase 4: System Status
1. Memory slot display with click → navigate to memory/disassembly
2. Signals panel (DOS/ROM/INT)
3. Frame counter + T-states
4. Beam position with area detection (Screen/Border/VBlank)
5. Machine-specific timing constants

**Effort:** 2-3 days

### Phase 5: Port Watch
1. PortRegistry with per-machine port definitions
2. Weak decoding support (decode masks)
3. PortWatchWidget with dynamic port list
4. Value change highlighting
5. Custom port support with persistence
6. Right-click → set breakpoint integration

**Effort:** 3-4 days

---

## File Changes Summary

| File | Changes |
|------|---------|
| **Preferences (new)** | |
| `preferences/preferencemanager.h/cpp` | Singleton, provider abstraction |
| `preferences/ipreferenceprovider.h` | Interface for persistence backends |
| `preferences/qtpreferenceprovider.h/cpp` | QSettings implementation |
| **Debugger widgets** | |
| `disassemblerwidget.h/cpp` | History, marks, follow, go-to-PC |
| `disassemblerwidget.ui` | Navigation toolbar/status |
| `stackwidget.h/cpp` | 9 entries, SP-2, click behavior |
| `stackwidget.ui` | Additional rows |
| `registerswidget.h/cpp` | Flag checkboxes, IFF/ISR display |
| `registerswidget.ui` | Flag checkbox grid, IFF labels |
| `interruptstatuswidget.h/cpp` | ISR address + hover popup (optional separate widget) |
| `portwatchwidget.h/cpp` | New widget with PortRegistry |
| `portwatchwidget.ui` | New UI |
| `systemstatuswidget.h/cpp` | Memory slots, signals, beam |
| `debuggerwindow.cpp` | Wire new signals/slots |
| **Core (ports)** | |
| `core/.../portinfo.h` | PortInfo struct with bit meanings |
| `core/.../portinforegistry.h/cpp` | All port definitions + tooltip formatting |

---

## Keyboard Reference (xpeccy-plus)

For reference, xpeccy-plus uses these shortcuts:

| Shortcut | Action |
|----------|--------|
| `XCUT_TOPC` | Go to PC |
| `XCUT_SETPC` | Set PC to current line |
| `XCUT_JUMPTO` | Follow operand |
| `XCUT_RETFROM` | Go back in history |
| `XCUT_GOTOADR` | Go to address (edit mode) |
| `XCUT_SETBRK` | Toggle breakpoint |
| `Ctrl+1-5` | Mark address |
| `Alt+1-5` | Jump to mark |

Defined in shortcuts system, can be customized.
