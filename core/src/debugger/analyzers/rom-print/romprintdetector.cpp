#include "romprintdetector.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "emulator/cpu/z80.h"
#include <sstream>
#include <iomanip>

// Generate UUID for this analyzer instance
static std::string generateUUID() {
    static int counter = 0;
    std::stringstream ss;
    ss << "romprintdetector-" << std::hex << std::setw(8) << std::setfill('0') << counter++;
    return ss.str();
}

ROMPrintDetector::ROMPrintDetector()
    : _uuid(generateUUID())
{
}

ROMPrintDetector::~ROMPrintDetector()
{
    // Cleanup handled by AnalyzerManager
}

void ROMPrintDetector::onActivate(AnalyzerManager* manager)
{
    _manager = manager;
    
    // Request breakpoints for ROM print routines
    // These will be automatically cleaned up on deactivation
    // Second parameter is analyzer ID (from IAnalyzer::_registrationId)
    BreakpointId bp1 = _manager->requestExecutionBreakpoint(RST_10, _registrationId);
    BreakpointId bp2 = _manager->requestExecutionBreakpoint(PRINT_OUT, _registrationId);
    BreakpointId bp3 = _manager->requestExecutionBreakpoint(PRINT_A_2, _registrationId);
    
    if (bp1 != BRK_INVALID) _breakpoints.push_back(bp1);
    if (bp2 != BRK_INVALID) _breakpoints.push_back(bp2);
    if (bp3 != BRK_INVALID) _breakpoints.push_back(bp3);
}

void ROMPrintDetector::onDeactivate()
{
    // AnalyzerManager automatically cleans up breakpoints
    _breakpoints.clear();
    _manager = nullptr;
    
    // Suppress unused field warning
    (void)_breakpoints;
}

void ROMPrintDetector::onBreakpointHit(uint16_t address, Z80* cpu)
{
    // Suppress unused parameter warning
    (void)address;
    
    if (!cpu) return;
    
    // Get character from A register
    uint8_t charCode = cpu->a;
    
    // Decode and append to history
    std::string decoded = decodeCharacter(charCode);
    _fullHistory += decoded;
    
    // Handle newlines for line tracking
    if (charCode == 0x0D) {  // CR - newline
        _lines.push_back(_currentLine);
        _currentLine.clear();
    } else if (charCode >= 0x20) {  // Printable character
        _currentLine += decoded;
    }
    // Ignore other control codes for now
}

std::string ROMPrintDetector::getNewOutput()
{
    if (_lastReadPosition >= _fullHistory.length()) {
        return "";
    }
    
    std::string newOutput = _fullHistory.substr(_lastReadPosition);
    _lastReadPosition = _fullHistory.length();
    return newOutput;
}

std::vector<std::string> ROMPrintDetector::getNewLines()
{
    std::vector<std::string> newLines;
    
    for (size_t i = _lastLineIndex; i < _lines.size(); i++) {
        newLines.push_back(_lines[i]);
    }
    
    _lastLineIndex = _lines.size();
    return newLines;
}

void ROMPrintDetector::clear()
{
    _fullHistory.clear();
    _currentLine.clear();
    _lines.clear();
    _lastReadPosition = 0;
    _lastLineIndex = 0;
}

std::string ROMPrintDetector::decodeCharacter(uint8_t code)
{
    // Handle ASCII printable characters (0x20-0x7F)
    if (code >= 0x20 && code < 0x7F) {
        return std::string(1, static_cast<char>(code));
    }
    
    // Handle newline
    if (code == 0x0D) {
        return "\n";
    }
    
    // Handle ZX Spectrum tokens (0xA5-0xFF)
    // Use BasicEncoder's keyword mapping
    if (code >= 0xA5) {
        // Search for token in BasicEncoder::BasicKeywords
        for (const auto& pair : BasicEncoder::BasicKeywords) {
            if (pair.second == code) {
                return pair.first;
            }
        }
    }
    
    // Unknown character - return hex representation
    std::stringstream ss;
    ss << "[0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(code) << "]";
    return ss.str();
}

void ROMPrintDetector::handleControlCode(uint8_t code, Z80* cpu)
{
    // Suppress unused parameter warnings
    (void)code;
    (void)cpu;
    
    // Control code handling can be expanded later
    // For now, we just decode them in decodeCharacter()
}
