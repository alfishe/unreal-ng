#include "debugsnapshot.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace DebugSnapshot
{
StateNode Registers(EmulatorContext* context)
{
    StateNode ret = StateNode::Object();
    Z80* z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
    if (!z80)
        return ret;

    StateNode main = StateNode::Object();
    main["af"] = static_cast<int>(z80->af);
    main["bc"] = static_cast<int>(z80->bc);
    main["de"] = static_cast<int>(z80->de);
    main["hl"] = static_cast<int>(z80->hl);
    ret["main"] = main;

    StateNode alt = StateNode::Object();
    alt["af_"] = static_cast<int>(z80->alt.af);
    alt["bc_"] = static_cast<int>(z80->alt.bc);
    alt["de_"] = static_cast<int>(z80->alt.de);
    alt["hl_"] = static_cast<int>(z80->alt.hl);
    ret["alternate"] = alt;

    StateNode index = StateNode::Object();
    index["ix"] = static_cast<int>(z80->ix);
    index["iy"] = static_cast<int>(z80->iy);
    ret["index"] = index;

    StateNode special = StateNode::Object();
    special["pc"] = static_cast<int>(z80->pc);
    special["sp"] = static_cast<int>(z80->sp);
    special["i"] = static_cast<int>(z80->i);
    special["r"] = static_cast<int>(Z80::RegisterR(z80));
    special["memptr"] = static_cast<int>(z80->memptr);
    special["q"] = static_cast<int>(z80->q);
    special["t"] = static_cast<uint64_t>(z80->t);   // CPU T-states since the frame's start
    ret["special"] = special;

    StateNode interrupt = StateNode::Object();
    interrupt["iff1"] = static_cast<int>(z80->iff1);
    interrupt["iff2"] = static_cast<int>(z80->iff2);
    interrupt["im"] = static_cast<int>(z80->im);
    interrupt["halted"] = z80->halted != 0;
    interrupt["boundary"] = std::string(Z80::BoundaryName(z80->boundary));   // what the next INT / NMI sampling sees
    ret["interrupt"] = interrupt;

    const uint8_t f = static_cast<uint8_t>(z80->af & 0xFF);
    StateNode flags = StateNode::Object();
    flags["s"] = (f & 0x80) ? 1 : 0;
    flags["z"] = (f & 0x40) ? 1 : 0;
    flags["y"] = (f & 0x20) ? 1 : 0;
    flags["h"] = (f & 0x10) ? 1 : 0;
    flags["x"] = (f & 0x08) ? 1 : 0;
    flags["pv"] = (f & 0x04) ? 1 : 0;
    flags["n"] = (f & 0x02) ? 1 : 0;
    flags["c"] = (f & 0x01) ? 1 : 0;
    ret["flags"] = flags;
    return ret;
}

StateNode Disasm(EmulatorContext* context, uint16_t address, size_t count)
{
    DebugManager* dbg = context ? context->pDebugManager : nullptr;
    Memory* memory = context ? context->pMemory : nullptr;
    Z80* z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
    if (!dbg || !dbg->GetDisassembler() || !memory || !z80)
    {
        StateNode unavailable = StateNode::Object();
        unavailable["available"] = false;
        unavailable["description"] = std::string("Disassembler not available");
        return unavailable;
    }
    Z80Disassembler* disasm = dbg->GetDisassembler().get();
    LabelManager* labels = dbg->GetLabelManager();
    count = std::clamp<size_t>(count, 1, 100);

    StateNode ret = StateNode::Object();
    ret["address"] = static_cast<int>(address);
    ret["count"] = static_cast<unsigned>(count);
    StateNode instructions = StateNode::Array();

    uint16_t current = address;
    for (size_t i = 0; i < count && current >= address; ++i)
    {
        // Direct (non-mutating) reads: the view must not strobe the ProfROM quadrant machine on #0000-#0003
        std::vector<uint8_t> buffer;
        for (int j = 0; j < 4; ++j)
            buffer.push_back(memory->DirectReadFromZ80Memory(static_cast<uint16_t>(current + j)));

        uint8_t length = 0;
        DecodedInstruction decoded;
        const std::string mnemonic = disasm->disassembleSingleCommandWithRuntime(buffer, current, &length, z80, memory, &decoded);
        if (length == 0)
            length = 1;   // at least advance by one

        StateNode line = StateNode::Object();
        line["address"] = static_cast<int>(current);
        std::string hex;
        for (uint8_t j = 0; j < length; ++j)
        {
            char byte[4];
            std::snprintf(byte, sizeof(byte), "%02X", buffer[j]);
            hex += byte;
        }
        line["bytes"] = hex;
        line["mnemonic"] = mnemonic;
        line["size"] = static_cast<int>(length);
        if (labels)
        {
            auto label = labels->GetLabelByZ80Address(current);
            if (label && !label->name.empty())
                line["label"] = label->name;
        }
        // Jump / call targets; an indirect one (JP (HL)) only when the runtime registers resolve it
        if (decoded.hasJump || decoded.hasRelativeJump)
        {
            const uint16_t target = decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr;
            if (!decoded.hasIndirect || decoded.hasRuntime)
            {
                line["target"] = static_cast<int>(target);
                if (labels)
                {
                    auto targetLabel = labels->GetLabelByZ80Address(target);
                    if (targetLabel && !targetLabel->name.empty())
                        line["targetLabel"] = targetLabel->name;
                }
            }
        }
        // The effective address of an (IX/IY+d) operand, with the runtime registers
        if (decoded.hasDisplacement && decoded.hasRuntime)
        {
            line["displacement"] = static_cast<int>(decoded.displacement);
            line["effectiveAddress"] = static_cast<int>(decoded.displacementAddr);
            if (labels)
            {
                auto effectiveLabel = labels->GetLabelByZ80Address(decoded.displacementAddr);
                if (effectiveLabel && !effectiveLabel->name.empty())
                    line["effectiveAddressLabel"] = effectiveLabel->name;
            }
        }
        instructions.push(line);
        current = static_cast<uint16_t>(current + length);
    }
    ret["instructions"] = instructions;
    return ret;
}
}  // namespace DebugSnapshot
