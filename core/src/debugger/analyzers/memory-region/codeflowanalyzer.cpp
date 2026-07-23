#include "codeflowanalyzer.h"
#include "blocktagmap.h"
#include "emulator/memory/memory.h"

CodeFlowAnalyzer::CodeFlowAnalyzer(Memory* memory)
    : _memory(memory)
{
}

CodeFlowAnalyzer::AnalysisResult CodeFlowAnalyzer::Analyze(const BlockTagMap& tagMap)
{
    AnalysisResult result{};
    result.isUnreachedCode.resize(65536, false);

    if (!_memory)
        return result;

    // Find branch targets in UNKNOWN regions
    auto targets = findUnreachedBranchTargets(tagMap);
    result.branchTargetsFound = static_cast<uint32_t>(targets.size());

    // Mark those bytes as unreached code (for aggregation to use)
    markUnreachedCode(result, tagMap, targets);

    return result;
}

std::unordered_set<uint16_t> CodeFlowAnalyzer::findUnreachedBranchTargets(const BlockTagMap& tagMap)
{
    std::unordered_set<uint16_t> targets;

    // Scan all CODE addresses for branch/jump instructions
    for (uint32_t addr = 0; addr < 65536; addr++)
    {
        if (tagMap.getType(static_cast<uint16_t>(addr)) != BlockType::CODE)
            continue;

        auto info = decodeInstruction(static_cast<uint16_t>(addr));
        if (!info.hasTarget)
            continue;

        uint16_t targetAddr = info.absoluteTarget;
        if (info.relativeTarget != 0)
        {
            // JR instruction: target = PC + 2 + signed_offset
            targetAddr = static_cast<uint16_t>(addr + 2 + info.relativeTarget);
        }

        // If target is in UNKNOWN region, it's unreached code
        if (tagMap.getType(targetAddr) == BlockType::UNKNOWN)
        {
            targets.insert(targetAddr);
        }
    }

    return targets;
}

void CodeFlowAnalyzer::markUnreachedCode(AnalysisResult& result, const BlockTagMap& tagMap,
                                          const std::unordered_set<uint16_t>& targets)
{
    for (uint16_t target : targets)
    {
        if (tagMap.getType(target) != BlockType::UNKNOWN)
            continue;

        // Trace forward from target, marking as unreached CODE until we hit:
        // - Already-classified region
        // - Unconditional jump/ret (end of basic block)
        // - Invalid instruction (likely data)

        uint32_t addr = target;
        while (addr < 0x10000 && tagMap.getType(static_cast<uint16_t>(addr)) == BlockType::UNKNOWN)
        {
            auto info = decodeInstruction(static_cast<uint16_t>(addr));

            // Sanity check: if instruction seems invalid, stop
            if (info.length == 0 || info.length > 4)
                break;

            // Mark these bytes as unreached code
            for (uint8_t i = 0; i < info.length && (addr + i) < 0x10000; i++)
            {
                uint16_t byteAddr = static_cast<uint16_t>(addr + i);
                if (!result.isUnreachedCode[byteAddr] && tagMap.getType(byteAddr) == BlockType::UNKNOWN)
                {
                    result.isUnreachedCode[byteAddr] = true;
                    result.unreachedCodeBytes++;
                }
            }

            // Stop at unconditional terminator
            if (info.isUnconditionalJump || (info.isRet && !info.isConditionalJump))
                break;

            addr += info.length;
        }
    }
}

bool CodeFlowAnalyzer::isPaddingAfterJump(uint16_t codeEndAddress, [[maybe_unused]] const BlockTagMap& tagMap)
{
    if (codeEndAddress == 0)
        return false;

    auto info = decodeInstruction(codeEndAddress);
    return info.isUnconditionalJump || (info.isRet && !info.isConditionalJump);
}

CodeFlowAnalyzer::InstructionInfo CodeFlowAnalyzer::decodeInstruction(uint16_t address)
{
    InstructionInfo info{};
    info.length = 1;
    info.hasTarget = false;

    uint8_t opcode = _memory->DirectReadFromZ80Memory(address);

    switch (opcode)
    {
        // === Unconditional Jumps ===
        case 0xC3: // JP nn
            info.length = 3;
            info.isUnconditionalJump = true;
            info.absoluteTarget = _memory->DirectReadFromZ80Memory(address + 1) |
                                  (_memory->DirectReadFromZ80Memory(address + 2) << 8);
            info.hasTarget = true;
            break;

        case 0x18: // JR d
            info.length = 2;
            info.isUnconditionalJump = true;
            info.relativeTarget = static_cast<int8_t>(_memory->DirectReadFromZ80Memory(address + 1));
            info.absoluteTarget = static_cast<uint16_t>(address + 2 + info.relativeTarget);
            info.hasTarget = true;
            break;

        case 0xC9: // RET
            info.length = 1;
            info.isUnconditionalJump = true;
            info.isRet = true;
            break;

        case 0xE9: // JP (HL)
            info.length = 1;
            info.isUnconditionalJump = true;
            break;

        // === Conditional Jumps (JP cc,nn) ===
        case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        case 0xE2: case 0xEA: case 0xF2: case 0xFA:
            info.length = 3;
            info.isConditionalJump = true;
            info.absoluteTarget = _memory->DirectReadFromZ80Memory(address + 1) |
                                  (_memory->DirectReadFromZ80Memory(address + 2) << 8);
            info.hasTarget = true;
            break;

        // === Conditional Jumps (JR cc,d) ===
        case 0x20: case 0x28: case 0x30: case 0x38:
            info.length = 2;
            info.isConditionalJump = true;
            info.relativeTarget = static_cast<int8_t>(_memory->DirectReadFromZ80Memory(address + 1));
            info.absoluteTarget = static_cast<uint16_t>(address + 2 + info.relativeTarget);
            info.hasTarget = true;
            break;

        // === CALL instructions ===
        case 0xCD: // CALL nn
            info.length = 3;
            info.isCall = true;
            info.absoluteTarget = _memory->DirectReadFromZ80Memory(address + 1) |
                                  (_memory->DirectReadFromZ80Memory(address + 2) << 8);
            info.hasTarget = true;
            break;

        case 0xC4: case 0xCC: case 0xD4: case 0xDC:
        case 0xE4: case 0xEC: case 0xF4: case 0xFC:
            info.length = 3;
            info.isCall = true;
            info.isConditionalJump = true;
            info.absoluteTarget = _memory->DirectReadFromZ80Memory(address + 1) |
                                  (_memory->DirectReadFromZ80Memory(address + 2) << 8);
            info.hasTarget = true;
            break;

        // === Conditional RET ===
        case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        case 0xE0: case 0xE8: case 0xF0: case 0xF8:
            info.length = 1;
            info.isRet = true;
            info.isConditionalJump = true;
            break;

        // === RST instructions ===
        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF:
            info.length = 1;
            info.isCall = true;
            info.absoluteTarget = opcode & 0x38;
            info.hasTarget = true;
            break;

        // === DJNZ ===
        case 0x10:
            info.length = 2;
            info.isConditionalJump = true;
            info.relativeTarget = static_cast<int8_t>(_memory->DirectReadFromZ80Memory(address + 1));
            info.absoluteTarget = static_cast<uint16_t>(address + 2 + info.relativeTarget);
            info.hasTarget = true;
            break;

        // === Extended opcodes ===
        case 0xED:
        {
            uint8_t op2 = _memory->DirectReadFromZ80Memory(address + 1);
            info.length = 2;
            if (op2 == 0x4D || op2 == 0x45) { info.isUnconditionalJump = true; info.isRet = true; }
            else if ((op2 & 0xC7) == 0x43) info.length = 4;
            break;
        }

        case 0xDD: case 0xFD:
        {
            uint8_t op2 = _memory->DirectReadFromZ80Memory(address + 1);
            info.length = 2;
            if (op2 == 0xE9) info.isUnconditionalJump = true;
            else if (op2 == 0xCB) info.length = 4;
            else if ((op2 & 0xC0) == 0x40 || (op2 & 0xC7) == 0x46) info.length = 3;
            else if (op2 == 0x21 || op2 == 0x22 || op2 == 0x2A || op2 == 0x36) info.length = 4;
            break;
        }

        case 0xCB:
            info.length = 2;
            break;

        default:
            if ((opcode & 0xC0) == 0x40 && opcode != 0x76) info.length = 1;
            else if ((opcode & 0xC7) == 0x06) info.length = 2;
            else if ((opcode & 0xCF) == 0x01) info.length = 3;
            else if ((opcode & 0xC7) == 0xC6) info.length = 2;
            break;
    }

    return info;
}
