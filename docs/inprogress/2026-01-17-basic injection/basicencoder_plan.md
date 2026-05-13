# BasicEncoder Implementation Plan

## Goal
Create a `BasicEncoder` class that provides the inverse operations of `BasicExtractor`:
1. **Tokenize**: Convert plain text BASIC syntax to tokenized format
2. **Inject**: Load tokenized program into emulator memory with system variable correction
3. **Test**: Comprehensive unit and integration tests including round-trip verification

## Design

### Class Structure

```cpp
class BasicEncoder
{
public:
    // Tokenize plain text BASIC program to binary format
    std::vector<uint8_t> tokenize(const std::string& basicText);
    
    // Inject tokenized program into emulator memory
    // Returns true on success, false on failure
    bool injectIntoMemory(Memory* memory, const std::vector<uint8_t>& tokenizedProgram);
    
    // Convenience: tokenize and inject in one call
    bool loadProgram(Memory* memory, const std::string& basicText);
    
private:
    // Helper: Parse a single line of BASIC text
    std::vector<uint8_t> tokenizeLine(uint16_t lineNumber, const std::string& lineText);
    
    // Helper: Find and replace BASIC keywords with tokens
    std::vector<uint8_t> replaceKeywords(const std::string& text);
    
    // Helper: Update ZX Spectrum system variables after program load
    void updateSystemVariables(Memory* memory, uint16_t progStart, uint16_t progEnd);
};
```

### Tokenization Algorithm

1. **Parse lines**: Split input by newlines, extract line numbers
2. **Keyword matching**: Longest-match-first for tokens (e.g., "GO TO" before "GO")
3. **String handling**: Preserve quoted strings as-is
4. **Number encoding**: Store ASCII digits, optionally add 0x0E hidden format
5. **Line format**:
   ```
   [Line Number: 2 bytes BE] [Line Length: 2 bytes LE] [Line Data...] [0x0D]
   ```

### Memory Injection

Standard ZX Spectrum memory layout after BASIC load:
```
PROG (23635)  → Start of BASIC program
VARS (23627)  → Start of variables (= end of program)
E_LINE (23641) → Editor line buffer
STKEND (23653) → End of used memory
```

Injection steps:
1. Write tokenized program starting at PROG address (typically 0x5C53 + offset)
2. Update VARS = PROG + program_length
3. Update E_LINE, WORKSP, STKBOT, STKEND for empty variable/stack state
4. Set NXTLIN, CH_ADD for ready-to-run state

## Implementation Files

### [NEW] basicencoder.h
- Class declaration
- Token lookup table (inverse of BasicExtractor::BasicTokens)
- System variable constants (reuse from spectrumconstants.h)

### [NEW] basicencoder.cpp
- Tokenization logic
- Memory injection with system variable updates

### [NEW] basicencoder_test.cpp
- Unit tests for tokenization
- Unit tests for memory injection
- Integration test: Pentagon 128K + load + extract + verify round-trip
- Integration test: Load "PRINT" program, verify screen memory

## Test Strategy

### Unit Tests
1. **Tokenize single line**: `"10 PRINT \"HELLO\""` → verify bytes
2. **Tokenize multi-line**: Full program with loops, conditionals
3. **Keyword edge cases**: "PRINT", "LPRINT", "GO TO", "GO SUB"
4. **String preservation**: Quotes, special characters
5. **Number handling**: Integers, floats, expressions

### Integration Tests
1. **Round-trip test**:
   ```cpp
   string original = "10 PRINT \"TEST\"\\n20 GOTO 10\\n";
   encoder.loadProgram(memory, original);
   string extracted = extractor.extractFromMemory(memory);
   EXPECT_EQ(normalize(original), normalize(extracted));
   ```

2. **Execution test**:
   ```cpp
   encoder.loadProgram(memory, "10 PRINT \"X\"");
   emulator.RunForCycles(100000); // Execute PRINT
   // Verify "X" appears in screen memory at 0x4000
   ```

3. **System variables test**:
   ```cpp
   encoder.loadProgram(memory, program);
   uint16_t prog = readWord(memory, PROG);
   uint16_t vars = readWord(memory, VARS);
   EXPECT_EQ(vars - prog, tokenizedSize);
   ```

## Verification Criteria

- [ ] Tokenize simple BASIC programs correctly
- [ ] Handle all 93 BASIC tokens (0xA3-0xFF)
- [ ] Preserve string literals
- [ ] Generate correct line number/length headers
- [ ] Inject program at correct memory address
- [ ] Update all relevant system variables (PROG, VARS, E_LINE, STKEND)
- [ ] Round-trip test passes (encode → inject → extract → compare)
- [ ] Execution test passes (load PRINT program → run → verify screen)

## Notes

> [!IMPORTANT]
> The tokenizer must handle keyword precedence correctly. For example:
> - "GO TO" (2 words) → 0xEC (single token)
> - "GO SUB" (2 words) → 0xED (single token)  
> - "GOTO" (1 word) → should still tokenize as 0xEC

> [!NOTE]
> For simplicity, the initial implementation will:
> - Store numbers as ASCII digits only (skip 0x0E hidden format)
> - Use standard memory layout (PROG at 0x5CCB for 48K)
> - Not handle UDG or extended tokens
