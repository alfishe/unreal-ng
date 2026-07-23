#pragma once

#include <cstdint>
#include <vector>
#include <unordered_set>

#include "types.h"

class BlockTagMap;
class Memory;

/// @brief Analyzes code flow to identify UNKNOWN regions that are actually unreached CODE.
/// Used to inform region aggregation - does NOT modify the raw classifications.
class CodeFlowAnalyzer
{
public:
    struct AnalysisResult
    {
        uint32_t branchTargetsFound = 0;      // Branch targets pointing to UNKNOWN
        uint32_t unreachedCodeBytes = 0;      // UNKNOWN bytes identified as unreached CODE
        std::vector<bool> isUnreachedCode;    // Per-address: true if UNKNOWN but likely CODE
    };

    explicit CodeFlowAnalyzer(Memory* memory);

    /// @brief Analyze code flow without modifying tagMap.
    /// @param tagMap The tag map to analyze (read-only)
    /// @return Analysis results including per-address unreached code flags
    AnalysisResult Analyze(const BlockTagMap& tagMap);

private:
    Memory* _memory;

    // Z80 instruction analysis
    struct InstructionInfo
    {
        uint8_t length;           // Instruction length in bytes
        bool isUnconditionalJump; // JP nn, JR d, RET, etc.
        bool isConditionalJump;   // JP cc,nn, JR cc,d, CALL cc,nn, RET cc
        bool isCall;              // CALL nn or CALL cc,nn
        bool isRet;               // RET or RET cc
        int16_t relativeTarget;   // For JR: signed offset (-128 to +127)
        uint16_t absoluteTarget;  // For JP/CALL: absolute address
        bool hasTarget;           // Whether target is valid
    };

    /// @brief Decode instruction at address
    InstructionInfo decodeInstruction(uint16_t address);

    /// @brief Find all branch/call targets from CODE regions pointing to UNKNOWN
    std::unordered_set<uint16_t> findUnreachedBranchTargets(const BlockTagMap& tagMap);

    /// @brief Check if UNKNOWN region after CODE is padding (follows unconditional jump)
    bool isPaddingAfterJump(uint16_t codeEndAddress, const BlockTagMap& tagMap);

    /// @brief Mark unreached branch targets in result
    void markUnreachedCode(AnalysisResult& result, const BlockTagMap& tagMap,
                           const std::unordered_set<uint16_t>& targets);
};
