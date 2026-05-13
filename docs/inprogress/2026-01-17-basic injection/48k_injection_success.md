# 48K BASIC Injection - Complete Technical Documentation

## Overview

This document describes the complete workflow for injecting and executing BASIC commands in 48K Spectrum mode via the `basic run` CLI command.

## Success Screenshot

![48K Injection Success](uploaded_image_1768637252537.png)

---

## Complete Workflow

### Step 1: Detect BASIC State

**Function**: `BasicEncoder::detectState(Memory* memory)`

**Logic**:
1. Check `Memory::GetROMPage()` to identify ROM type
   - Pages 1, 3 → 48K BASIC ROM → `BasicState::Basic48K`
   - Pages 0, 2 → 128K Editor ROM → Continue to check EDITOR_FLAGS

**Result**: Returns `BasicState::Basic48K` when in 48K mode

---

### Step 2: Tokenize the Command

**Function**: `BasicEncoder::tokenizeImmediate(const std::string& command)`

**Purpose**: Convert BASIC command string to byte sequence with keywords as tokens

**Algorithm**:
```
1. Convert command to uppercase for matching
2. For each position in the string:
   a. If inside string literal (between quotes): copy character as-is
   b. Otherwise, try to match against keyword table:
      - Check word boundaries (start/end of word)
      - If keyword matches: output token byte, skip keyword length
      - If keyword had trailing space AND next char is space: skip the space
   c. If no keyword matched: copy character as-is
3. Return byte vector
```

**Example**:
- Input: `"PRINT 123"`
- Output: `[0xF5, '1', '2', '3']` (4 bytes)
- Note: Space after PRINT is consumed (not stored)

**Critical Details**:
- PRINT token = 0xF5
- Keywords table has format `" KEYWORD "` with spaces
- Trailing space after keyword is skipped in output
- String literals are preserved (no tokenization inside quotes)

---

### Step 3: Read E_LINE Address

**System Variable**: `E_LINE` at address `0x5C59` (2 bytes, little-endian)

**Code**:
```cpp
uint16_t eLineAddr = memory->DirectReadFromZ80Memory(0x5C59) |
                     (memory->DirectReadFromZ80Memory(0x5C5A) << 8);
```

**Typical Value**: `0x5CCC` (23756)

---

### Step 4: Write Tokenized Data to E_LINE Buffer

**Memory Layout at E_LINE**:
```
E_LINE + 0: First token/character
E_LINE + 1: Second token/character
...
E_LINE + N: Last token/character
E_LINE + N+1: 0x0D (ENTER/carriage return)
E_LINE + N+2: 0x80 (End marker)
```

**Code**:
```cpp
// Write tokenized command
for (size_t i = 0; i < tokenized.size(); i++) {
    memory->DirectWriteToZ80Memory(eLineAddr + i, tokenized[i]);
}

// Terminate with ENTER (0x0D)
memory->DirectWriteToZ80Memory(eLineAddr + tokenized.size(), 0x0D);

// End marker (0x80)
memory->DirectWriteToZ80Memory(eLineAddr + tokenized.size() + 1, 0x80);
```

---

### Step 5: Update System Variables

**CRITICAL**: These must be set correctly or ROM will crash/reboot

#### WORKSP (0x5C61) - Workspace Pointer
```cpp
// Must point AFTER the 0x80 end marker
uint16_t workspVal = eLineAddr + tokenized.size() + 2;
memory->DirectWriteToZ80Memory(0x5C61, workspVal & 0xFF);
memory->DirectWriteToZ80Memory(0x5C62, (workspVal >> 8) & 0xFF);
```

**Why**: ROM uses WORKSP to know where temporary workspace begins. If wrong, parser reads garbage and produces "Nonsense in BASIC" error.

#### K_CUR (0x5C5B) - Cursor Position
```cpp
// Point to end of command (before 0x0D)
uint16_t kCurVal = eLineAddr + tokenized.size();
memory->DirectWriteToZ80Memory(0x5C5B, kCurVal & 0xFF);
memory->DirectWriteToZ80Memory(0x5C5C, (kCurVal >> 8) & 0xFF);
```

**Why**: Tells ROM where the editing cursor is positioned.

#### CH_ADD (0x5C5D) - Character Address
```cpp
// Point to start of line for LINE-SCAN to parse
memory->DirectWriteToZ80Memory(0x5C5D, eLineAddr & 0xFF);
memory->DirectWriteToZ80Memory(0x5C5E, (eLineAddr >> 8) & 0xFF);
```

**Why**: LINE-SCAN reads from this address when parsing the command.

---

### Step 6: Trigger Execution

**Function**: `BasicEncoder::injectEnter(Memory* memory)`

**Mechanism**:
```cpp
// Write ENTER key code to LAST_K (0x5C08)
memory->DirectWriteToZ80Memory(0x5C08, 0x0D);

// Set FLAGS bit 5 to signal new key available
uint8_t flags = memory->DirectReadFromZ80Memory(0x5C3B);
memory->DirectWriteToZ80Memory(0x5C3B, flags | 0x20);
```

**What Happens**:
1. ROM's keyboard handler sees bit 5 of FLAGS is set
2. Reads ENTER (0x0D) from LAST_K
3. Editor loop calls ED-ENTER
4. ED-ENTER triggers LINE-SCAN (0x1B17)
5. LINE-SCAN parses and executes the command

---

## System Variables Reference

| Variable | Address | Size | Purpose |
|----------|---------|------|---------|
| E_LINE | 0x5C59 | 2 | Start of edit line buffer |
| K_CUR | 0x5C5B | 2 | Cursor position in edit buffer |
| CH_ADD | 0x5C5D | 2 | Current character address for parser |
| WORKSP | 0x5C61 | 2 | Start of workspace |
| LAST_K | 0x5C08 | 1 | Last key pressed code |
| FLAGS | 0x5C3B | 1 | Bit 5 = new key available |

---

## Memory Layout Example

For command `PRINT 123`:

```
Address   Content   Description
--------  -------   -----------
0x5CCC    0xF5      PRINT token
0x5CCD    0x31      '1'
0x5CCE    0x32      '2'
0x5CCF    0x33      '3'
0x5CD0    0x0D      ENTER
0x5CD1    0x80      End marker
0x5CD2    ...       WORKSP starts here
```

System Variables:
- E_LINE = 0x5CCC
- K_CUR = 0x5CD0 (after '3', before 0x0D)
- CH_ADD = 0x5CCC (start of line)
- WORKSP = 0x5CD2 (after 0x80)

---

## Common Errors and Fixes

| Error | Cause | Fix |
|-------|-------|-----|
| Reboot on injection | WORKSP not set or wrong | Set WORKSP = E_LINE + len + 2 |
| "Nonsense in BASIC" | WORKSP points before 0x80 | Include both 0x0D and 0x80 in offset |
| Garbage on screen | Keywords not tokenized | Use tokenizeImmediate(), not ASCII |
| No execution | FLAGS bit 5 not set | Call injectEnter() after injection |

---

## Files Involved

- `core/src/debugger/analyzers/basic-lang/basicencoder.cpp` - Main implementation
- `core/src/debugger/analyzers/basic-lang/basicencoder.h` - Header file
- `core/src/emulator/spectrumconstants.h` - System variable addresses
- `core/automation/cli/src/commands/cli-processor-analyzer.cpp` - CLI handler
