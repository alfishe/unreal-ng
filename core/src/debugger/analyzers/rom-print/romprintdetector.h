#pragma once

#include "debugger/analyzers/ianalyzer.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include <string>
#include <vector>
#include <cstdint>

// Forward declarations
class AnalyzerManager;
class Z80;

/// ROMPrintDetector: Captures and decodes ROM print output via breakpoints
/// 
/// This analyzer monitors ZX Spectrum ROM print routines and captures
/// all printed characters, decoding them from ZX Spectrum character codes
/// to readable text.
///
/// **Usage:**
/// 1. Register with AnalyzerManager
/// 2. Activate to start capturing
/// 3. Query captured output via getNewOutput() or getFullHistory()
/// 4. Deactivate when done
///
/// **Breakpoint Locations:**
/// - 0x0010 (RST 0x10) - Print character routine
/// - 0x09F4 (PRINT-OUT) - Main print routine  
/// - 0x15F2 (PRINT-A-2) - Actual print implementation
///
class ROMPrintDetector : public IAnalyzer
{
public:
    ROMPrintDetector();
    ~ROMPrintDetector() override;
    
    // IAnalyzer interface
    std::string getName() const override { return "ROMPrintDetector"; }
    std::string getUUID() const override { return _uuid; }
    
    void onActivate(AnalyzerManager* manager) override;
    void onDeactivate() override;
    void onBreakpointHit(uint16_t address, Z80* cpu) override;
    
    // Query API
    
    /// Get new output since last call to getNewOutput()
    /// @return New text captured since last query
    std::string getNewOutput();
    
    /// Get complete captured history
    /// @return All text captured since activation
    std::string getFullHistory() const { return _fullHistory; }
    
    /// Get all captured lines
    /// @return Vector of complete lines (newline-terminated)
    std::vector<std::string> getLines() const { return _lines; }
    
    /// Get new lines since last call to getNewOutput()
    /// @return Vector of new complete lines
    std::vector<std::string> getNewLines();
    
    /// Clear all captured history
    void clear();
    
private:
    std::string _uuid;
    AnalyzerManager* _manager = nullptr;
    
    // Breakpoint tracking
    std::vector<uint16_t> _breakpoints;
    
    // Output buffer
    std::string _fullHistory;
    std::string _currentLine;
    std::vector<std::string> _lines;
    size_t _lastReadPosition = 0;
    size_t _lastLineIndex = 0;
    
    // Character decoding
    std::string decodeCharacter(uint8_t code);
    void handleControlCode(uint8_t code, Z80* cpu);
    
    // Breakpoint addresses
    static constexpr uint16_t RST_10 = 0x0010;      // RST 0x10 - Print character
    static constexpr uint16_t PRINT_OUT = 0x09F4;   // PRINT-OUT routine
    static constexpr uint16_t PRINT_A_2 = 0x15F2;   // PRINT-A-2 implementation
};
