#pragma once
#include "stdafx.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/// @brief Per-source-line assembly detail (used for the listing output)
struct AsmSourceLine
{
    uint16_t address = 0;           // Address of the first emitted byte
    std::string label;              // Label defined on this line (if any)
    std::string source;             // Original source text (comment stripped)
    std::vector<uint8_t> bytes;     // Emitted machine-code bytes
};

/// @brief Assembly error with source location
struct AsmErrorInfo
{
    int line = 0;                   // 1-based source line number (0 = general)
    std::string message;            // Human-readable error description
    std::string sourceLine;         // Offending source text
};

/// @brief Result of assembling a multi-line Z80 source text
struct AsmResult
{
    bool ok = false;
    AsmErrorInfo error;

    uint16_t startAddress = 0;      // ORG address
    uint16_t endAddress = 0;        // Address just past the last emitted byte
    std::vector<uint8_t> bytes;     // All emitted bytes in source order
    std::map<std::string, uint32_t> symbols;  // Labels + EQU constants (name -> value)
    std::vector<AsmSourceLine> lines;         // Per-line listing
};

/// @brief Two-pass Z80 assembler for automation (POST /assemble)
///
/// Supported (core subset, per MCP Phase 2 plan):
///   - all documented Z80 instructions (DD/FD prefixes included)
///   - pseudo-ops: ORG, EQU (=), DB/DEFB/BYTE, DW/DEFW/WORD, DS/DEFS/BLOCK
///   - labels with optional ':' (case-sensitive), '$' = current address
///   - literals: 0x1F, $1F, #1F, %0101, 0b0101, decimal, 'c' (with escapes)
///   - expressions: + - * / & | ^ << >> ~ unary- and parentheses
///   - ';' line comments
///
/// Pass 1 computes line sizes (value-independent) and label addresses;
/// pass 2 evaluates expressions and emits bytes. JR/DJNZ range and
/// address-overflow errors are reported in pass 2 only.
///
/// Deliberately context-free (no EmulatorContext) so core-tests can drive
/// it directly.
class Z80TextAssembler
{
public:
    Z80TextAssembler() = default;
    ~Z80TextAssembler() = default;

    /// Assemble source text at the given origin address
    /// @param source Multi-line Z80 assembly source
    /// @param org Start address for the first instruction
    AsmResult Assemble(const std::string& source, uint16_t org);

private:
    /// Parsed source line (comment stripped, operands split at top-level commas)
    struct LineParts
    {
        int lineNumber = 0;
        std::string label;
        std::string mnemonic;               // lower-case
        std::vector<std::string> operands;  // trimmed, quotes preserved
        std::string source;                 // original text without comment
    };

    /// Symbol table shared by both passes (labels + EQU values)
    std::map<std::string, uint32_t> _symbols;

    bool ParseSource(const std::string& source, std::vector<LineParts>& lines, AsmErrorInfo& error);
    bool SplitOperands(const std::string& text, std::vector<std::string>& operands);

    /// Encode one line. Pass 1 (pass == 1) only computes the size with relaxed
    /// checks; pass 2 evaluates expressions and performs range checks.
    bool EncodeLine(const LineParts& line, uint16_t address, int pass,
                    std::vector<uint8_t>& out, std::string& error);

    bool Evaluate(const std::string& expression, uint16_t currentAddress, int32_t& value, std::string& error);
};
