# ROMPrintDetector Implementation Plan

## Overview
Implement ROMPrintDetector as the first concrete analyzer using the AnalyzerManager framework. This will validate the entire analyzer infrastructure end-to-end.

## Phase 5a: Core ROMPrintDetector (CURRENT)

### Objectives
1. Create minimal working ROMPrintDetector using IAnalyzer interface
2. Implement breakpoint-based character capture
3. Integrate with AnalyzerManager lifecycle
4. Verify with unit tests

### Implementation Strategy

**Simplified First Version:**
- Focus ONLY on breakpoint-based capture (defer OCR)
- Use existing BasicEncoder for character decoding
- Minimal MessageCenter integration (defer full implementation)
- Store captured output in simple string buffer

### File Structure
```
core/src/debugger/analyzers/rom-print/
├── romprintdetector.h
└── romprintdetector.cpp

core/tests/debugger/analyzers/rom-print/
└── romprintdetector_test.cpp
```

### Class Design

```cpp
class ROMPrintDetector : public IAnalyzer {
public:
    ROMPrintDetector();
    ~ROMPrintDetector() override;
    
    // IAnalyzer interface
    std::string getName() const override { return "ROMPrintDetector"; }
    std::string getUUID() const override { return _uuid; }
    void onActivate(AnalyzerManager* manager) override;
    void onDeactivate() override;
    void onBreakpointHit(uint16_t address, BreakpointId bpId, Z80* cpu) override;
    
    // Query API
    std::string getNewOutput();           // Get output since last call
    std::string getFullHistory();         // Get complete history
    std::vector<std::string> getLines();  // Get all lines
    void clear();                         // Clear history
    
private:
    std::string _uuid;
    AnalyzerManager* _manager = nullptr;
    
    // Breakpoint tracking
    std::vector<BreakpointId> _breakpoints;
    
    // Output buffer
    std::string _fullHistory;
    std::vector<std::string> _lines;
    size_t _lastReadPosition = 0;
    
    // Character decoding (reuse BasicEncoder)
    std::string decodeCharacter(uint8_t code);
    void handleControlCode(uint8_t code, Z80* cpu);
    
    // Breakpoint addresses
    static constexpr uint16_t RST_10 = 0x0010;      // RST 0x10 - Print character
    static constexpr uint16_t PRINT_OUT = 0x09F4;   // PRINT-OUT routine
    static constexpr uint16_t PRINT_A_2 = 0x15F2;   // PRINT-A-2 implementation
};
```

### Integration Points

**1. AnalyzerManager Lifecycle:**
```cpp
void ROMPrintDetector::onActivate(AnalyzerManager* manager) {
    _manager = manager;
    
    // Request breakpoints for ROM print routines
    _breakpoints.push_back(_manager->requestExecutionBreakpoint(RST_10, "RST 0x10"));
    _breakpoints.push_back(_manager->requestExecutionBreakpoint(PRINT_OUT, "PRINT-OUT"));
    _breakpoints.push_back(_manager->requestExecutionBreakpoint(PRINT_A_2, "PRINT-A-2"));
}

void ROMPrintDetector::onDeactivate() {
    // AnalyzerManager automatically cleans up breakpoints
    _breakpoints.clear();
    _manager = nullptr;
}
```

**2. Character Capture:**
```cpp
void ROMPrintDetector::onBreakpointHit(uint16_t address, BreakpointId bpId, Z80* cpu) {
    // Get character from A register
    uint8_t charCode = cpu->a;
    
    // Decode and append to history
    std::string decoded = decodeCharacter(charCode);
    _fullHistory += decoded;
    
    // Handle newlines for line tracking
    if (charCode == 0x0D) {
        _lines.push_back(_currentLine);
        _currentLine.clear();
    } else {
        _currentLine += decoded;
    }
}
```

**3. Character Decoding:**
- Reuse BasicEncoder's token mapping
- Handle ASCII printable characters (0x20-0x7F)
- Handle ZX Spectrum tokens (0xA5-0xFF)
- Handle control codes (0x0D newline, etc.)

### Testing Strategy

**Unit Tests** (`romprintdetector_test.cpp`):
1. **Basic Lifecycle**
   - Register with AnalyzerManager
   - Activate/deactivate
   - Verify breakpoints created/cleaned up

2. **Character Capture**
   - Simulate breakpoint hits with different characters
   - Verify ASCII characters captured correctly
   - Verify tokens decoded correctly
   - Verify newlines create new lines

3. **Query API**
   - Test getNewOutput() returns only new content
   - Test getFullHistory() returns everything
   - Test getLines() returns line array
   - Test clear() resets state

**Integration Test** (deferred to Phase 5b):
- Use with TR-DOS FORMAT command
- Verify actual ROM output captured

### Dependencies

**Existing Code to Reuse:**
- [BasicEncoder](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/debugger/analyzers/basic-lang/basicencoder.h#34-35) - Token decoding (already exists)
- [AnalyzerManager](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/src/debugger/analyzers/analyzermanager.h#44-47) - Lifecycle and breakpoint management (Phase 1-4 ✅)
- [EmulatorTestHelper](file:///Volumes/TB4-4Tb/Projects/Test/unreal-ng/core/tests/_helpers/emulatortesthelper.h#9-34) - Test setup (Phase 3 ✅)

**New Code:**
- `ROMPrintDetector` class
- Unit tests

### Implementation Steps

1. **Create ROMPrintDetector skeleton** ✅
   - Implement IAnalyzer interface
   - Add basic member variables
   - Generate UUID

2. **Implement lifecycle methods**
   - onActivate() - request breakpoints
   - onDeactivate() - cleanup
   - Verify with unit test

3. **Implement character capture**
   - onBreakpointHit() - extract character from A register
   - decodeCharacter() - use BasicEncoder mapping
   - handleControlCode() - newlines, etc.
   - Verify with unit test

4. **Implement query API**
   - getNewOutput()
   - getFullHistory()
   - getLines()
   - clear()
   - Verify with unit tests

5. **Integration testing**
   - Create simple test that prints "HELLO"
   - Verify ROMPrintDetector captures it
   - Run with EmulatorTestHelper

### Success Criteria

- [ ] ROMPrintDetector implements IAnalyzer interface
- [ ] Registers/unregisters with AnalyzerManager successfully
- [ ] Breakpoints created on activation, cleaned up on deactivation
- [ ] Captures characters from ROM print routines
- [ ] Decodes ASCII and ZX Spectrum tokens correctly
- [ ] Query API returns correct output
- [ ] All unit tests pass
- [ ] Integration test captures real ROM output

## Phase 5b: Enhanced Features (DEFERRED)

- Screen OCR for direct screen writes
- MessageCenter broadcasting
- Advanced pattern matching (waitForText, etc.)
- Performance optimization
- Full TR-DOS FORMAT integration test

## Notes

**Why Start Simple:**
- Validate AnalyzerManager integration first
- Prove breakpoint-based capture works
- Get feedback before adding complexity
- Easier to debug minimal implementation

**Deferred Items:**
- OCR (not needed for initial validation)
- MessageCenter (can add later)
- Advanced query methods (YAGNI for now)
