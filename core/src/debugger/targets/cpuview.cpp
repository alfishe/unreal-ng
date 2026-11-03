#include "debugger/targets/cpuview.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>
#include <utility>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/targets/gscardtarget.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

namespace CpuView
{
namespace
{
const char* BoundaryName(uint8_t boundary)
{
    switch (boundary)
    {
        case Z80CpuBoundaryPrefixDd:
            return "prefix_dd";
        case Z80CpuBoundaryPrefixFd:
            return "prefix_fd";
        case Z80CpuBoundaryIntShadow:
            return "int_shadow";
        case Z80CpuBoundaryLdAIr:
            return "ld_a_ir";
        case Z80CpuBoundaryNmiAck:
            return "nmi_ack";
        default:
            return "none";
    }
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
}  // namespace

std::string Select(EmulatorContext* context, const std::string& name, DebugCpuId& cpu)
{
    if (!DebugManager::ParseCpuName(Lower(name), cpu))
        return "'cpu': main, gs or ms";
    if (IsCardCpu(cpu) && !Card(context, cpu))
        return std::string("cpu ") + DebugCpuName(cpu) + " not available: no card with a CPU in its slot";
    return {};
}

GeneralSoundCard* Card(EmulatorContext* context, DebugCpuId cpu)
{
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    GSCardTarget* target = debug && IsCardCpu(cpu) ? debug->GetCardTarget(cpu) : nullptr;
    return target ? target->Card() : nullptr;
}

StateNode Registers(EmulatorContext* context, DebugCpuId cpu)
{
    StateNode ret = StateNode::Object();
    GeneralSoundCard* card = Card(context, cpu);
    Z80CpuRegisters r{};
    if (!card || !card->cardRegisters(r))
        return ret;
    ret["cpu"] = std::string(DebugCpuName(cpu));

    StateNode main = StateNode::Object();
    main["af"] = static_cast<int>(r.af);
    main["bc"] = static_cast<int>(r.bc);
    main["de"] = static_cast<int>(r.de);
    main["hl"] = static_cast<int>(r.hl);
    ret["main"] = main;

    StateNode alt = StateNode::Object();
    alt["af_"] = static_cast<int>(r.afAlt);
    alt["bc_"] = static_cast<int>(r.bcAlt);
    alt["de_"] = static_cast<int>(r.deAlt);
    alt["hl_"] = static_cast<int>(r.hlAlt);
    ret["alternate"] = alt;

    StateNode index = StateNode::Object();
    index["ix"] = static_cast<int>(r.ix);
    index["iy"] = static_cast<int>(r.iy);
    ret["index"] = index;

    StateNode special = StateNode::Object();
    special["pc"] = static_cast<int>(r.pc);
    special["sp"] = static_cast<int>(r.sp);
    special["i"] = static_cast<int>(r.i);
    special["r"] = static_cast<int>(r.r);
    special["memptr"] = static_cast<int>(r.memptr);
    special["q"] = static_cast<int>(r.q);
    special["card_time"] = static_cast<int64_t>(card->cardTime());   // in the card's unit (GeneralSoundCard::cardTime)
    ret["special"] = special;

    StateNode interrupt = StateNode::Object();
    interrupt["iff1"] = static_cast<int>(r.iff1);
    interrupt["iff2"] = static_cast<int>(r.iff2);
    interrupt["im"] = static_cast<int>(r.im);
    interrupt["halted"] = r.halted != 0;
    interrupt["boundary"] = std::string(BoundaryName(r.boundary));
    ret["interrupt"] = interrupt;

    const uint8_t f = static_cast<uint8_t>(r.af & 0xFF);
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

StateNode FlatRegisters(EmulatorContext* context, DebugCpuId cpu)
{
    StateNode flat = StateNode::Object();
    const StateNode grouped = Registers(context, cpu);
    for (const auto& [group, members] : grouped.members)
    {
        if (group == "cpu")
            flat["cpu"] = members;
        if (group == "flags" || !members.isObject())
            continue;
        for (const auto& [name, value] : members.members)
            flat[name] = name == "iff1" || name == "iff2" ? StateNode(value.i != 0) : value;
    }
    return flat;
}

std::string SetRegister(Emulator* emulator, DebugCpuId cpu, const char* source, const std::string& name,
                        uint16_t value)
{
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    GeneralSoundCard* card = Card(context, cpu);
    Z80CpuRegisters probe{};
    if (!card || !card->cardRegisters(probe))
        return std::string("cpu ") + DebugCpuName(cpu) + " not available: no card with a CPU in its slot";
    if (!GSCardTarget::SetRegister(probe, name, value))
        return "unknown register or value out of range: " + name + " = " + std::to_string(value);
    // The registers as they are when the edit runs (the machine may have moved since the probe)
    std::string refused = emulator->EditMemoryFromTool(source, [&]() {
        Z80CpuRegisters regs{};
        if (GeneralSoundCard* now = Card(context, cpu); now && now->cardRegisters(regs) &&
                                                         GSCardTarget::SetRegister(regs, name, value))
            now->setCardRegisters(regs);
    });
    return refused;
}

bool GetRegister(EmulatorContext* context, DebugCpuId cpu, const std::string& name, uint16_t& value, bool* wide)
{
    GeneralSoundCard* card = Card(context, cpu);
    Z80CpuRegisters r{};
    if (!card || !card->cardRegisters(r))
        return false;
    const std::string key = Lower(name);
    const std::pair<const char*, uint16_t> table[] = {
        {"af", r.af}, {"bc", r.bc}, {"de", r.de}, {"hl", r.hl}, {"af'", r.afAlt}, {"bc'", r.bcAlt}, {"de'", r.deAlt},
        {"hl'", r.hlAlt}, {"ix", r.ix}, {"iy", r.iy}, {"sp", r.sp}, {"pc", r.pc}, {"memptr", r.memptr},
        {"a", static_cast<uint16_t>(r.af >> 8)}, {"f", static_cast<uint16_t>(r.af & 0xFF)},
        {"b", static_cast<uint16_t>(r.bc >> 8)}, {"c", static_cast<uint16_t>(r.bc & 0xFF)},
        {"d", static_cast<uint16_t>(r.de >> 8)}, {"e", static_cast<uint16_t>(r.de & 0xFF)},
        {"h", static_cast<uint16_t>(r.hl >> 8)}, {"l", static_cast<uint16_t>(r.hl & 0xFF)},
        {"ixh", static_cast<uint16_t>(r.ix >> 8)}, {"ixl", static_cast<uint16_t>(r.ix & 0xFF)},
        {"iyh", static_cast<uint16_t>(r.iy >> 8)}, {"iyl", static_cast<uint16_t>(r.iy & 0xFF)},
        {"i", r.i}, {"r", r.r}, {"im", r.im}, {"iff1", r.iff1}, {"iff2", r.iff2}};
    constexpr size_t kWide = 13;   // af .. memptr
    for (size_t k = 0; k < std::size(table); k++)
        if (key == table[k].first)
        {
            value = table[k].second;
            if (wide)
                *wide = k < kWide;
            return true;
        }
    return false;
}

bool Read(EmulatorContext* context, DebugCpuId cpu, uint16_t address, uint32_t length, std::vector<uint8_t>& out)
{
    GeneralSoundCard* card = Card(context, cpu);
    if (!card)
        return false;
    out.assign(length, 0xFF);
    for (uint32_t i = 0; i < length; i++)
        card->peekCardMemory(static_cast<uint16_t>(address + i), out[i]);
    return true;
}

std::string Write(Emulator* emulator, DebugCpuId cpu, const char* source, uint16_t address,
                  const std::vector<uint8_t>& bytes, size_t* written)
{
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    size_t landed = 0;
    if (written)
        *written = 0;
    if (!Card(context, cpu))
        return std::string("cpu ") + DebugCpuName(cpu) + " not available: no card with a CPU in its slot";
    const std::string refused = emulator->EditMemoryFromTool(source, [&]() {
        if (GeneralSoundCard* card = Card(context, cpu))
            for (size_t i = 0; i < bytes.size(); i++)
                landed += card->pokeCardMemory(static_cast<uint16_t>(address + i), bytes[i]) ? 1 : 0;
    });
    if (written)
        *written = landed;
    return refused;
}

StateNode Disasm(EmulatorContext* context, DebugCpuId cpu, uint16_t address, size_t count)
{
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    GeneralSoundCard* card = Card(context, cpu);
    if (!debug || !debug->GetDisassembler() || !card)
    {
        StateNode unavailable = StateNode::Object();
        unavailable["available"] = false;
        unavailable["description"] = std::string(card ? "Disassembler not available"
                                                      : "No card with a CPU in its slot");
        return unavailable;
    }
    Z80Disassembler* disasm = debug->GetDisassembler().get();
    count = std::clamp<size_t>(count, 1, 256);

    StateNode ret = StateNode::Object();
    ret["cpu"] = std::string(DebugCpuName(cpu));
    ret["address"] = static_cast<int>(address);
    ret["count"] = static_cast<unsigned>(count);
    StateNode instructions = StateNode::Array();
    uint16_t current = address;
    for (size_t i = 0; i < count && current >= address; ++i)
    {
        std::vector<uint8_t> buffer(4, 0xFF);
        for (uint16_t j = 0; j < 4; ++j)
            card->peekCardMemory(static_cast<uint16_t>(current + j), buffer[j]);
        uint8_t length = 0;
        DecodedInstruction decoded;
        const std::string mnemonic = disasm->disassembleSingleCommand(buffer, current, &length, &decoded);
        if (length == 0)
            length = 1;

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
        if ((decoded.hasJump || decoded.hasRelativeJump) && !decoded.hasIndirect)
            line["target"] = static_cast<int>(decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr);
        instructions.push(line);
        current = static_cast<uint16_t>(current + length);
    }
    ret["instructions"] = instructions;
    return ret;
}

StateNode Windows(EmulatorContext* context, DebugCpuId cpu)
{
    StateNode ret = StateNode::Object();
    GeneralSoundCard* card = Card(context, cpu);
    if (!card)
        return ret;
    ret["cpu"] = std::string(DebugCpuName(cpu));
    StateNode windows = StateNode::Array();
    for (uint32_t w = 0; w < 4; w++)
    {
        const uint16_t start = static_cast<uint16_t>(w * 0x4000);
        const GeneralSoundCard::CardPage page = card->mapCardAddress(start);
        StateNode window = StateNode::Object();
        window["start"] = static_cast<int>(start);
        window["size"] = 0x4000;
        window["kind"] = std::string(page.kind == GeneralSoundCard::CardPage::Rom   ? "rom"
                                     : page.kind == GeneralSoundCard::CardPage::Ram ? "ram"
                                                                                    : "none");
        if (page.kind != GeneralSoundCard::CardPage::None)
        {
            window["page"] = static_cast<int>(page.page);
            window["offset"] = static_cast<int>(page.page) * 0x4000;
        }
        windows.push(window);
    }
    ret["windows"] = windows;
    return ret;
}
}  // namespace CpuView
