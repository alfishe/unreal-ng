#include "gdbtarget_z80.h"
#include "gdbpacket.h"

#include <debugger/ports/portwrite.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/cpu/z80.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <emulator/ports/portdecoder.h>

#include <sstream>

namespace
{
    // CPU register definitions per TDD §4.2
    // Order matches target.xml regnum for g/G packet encoding
    const std::vector<GDBTargetZ80::RegisterDef> g_cpuRegisters = {
        {"af",  16, 0,  "",         "org.gnu.gdb.z80.cpu"},
        {"bc",  16, 1,  "",         "org.gnu.gdb.z80.cpu"},
        {"de",  16, 2,  "",         "org.gnu.gdb.z80.cpu"},
        {"hl",  16, 3,  "",         "org.gnu.gdb.z80.cpu"},
        {"sp",  16, 4,  "data_ptr", "org.gnu.gdb.z80.cpu"},
        {"pc",  16, 5,  "code_ptr", "org.gnu.gdb.z80.cpu"},
        {"ix",  16, 6,  "data_ptr", "org.gnu.gdb.z80.cpu"},
        {"iy",  16, 7,  "data_ptr", "org.gnu.gdb.z80.cpu"},
        {"af'", 16, 8,  "",         "org.gnu.gdb.z80.cpu"},
        {"bc'", 16, 9,  "",         "org.gnu.gdb.z80.cpu"},
        {"de'", 16, 10, "",         "org.gnu.gdb.z80.cpu"},
        {"hl'", 16, 11, "",         "org.gnu.gdb.z80.cpu"},
        {"ir",  16, 12, "",         "org.gnu.gdb.z80.cpu"},
        {"iff", 8,  13, "",         "org.gnu.gdb.z80.cpu"},
        {"im",  8,  14, "",         "org.gnu.gdb.z80.cpu"},
    };

    // Paging pseudo-registers (read/write port latches)
    // These allow GDB to inspect and modify memory paging state
    const std::vector<GDBTargetZ80::RegisterDef> g_pagingRegisters = {
        {"p7ffd", 8, 15, "", "org.gnu.gdb.z80.paging"},  // Port 0x7FFD - main paging
        {"p1ffd", 8, 16, "", "org.gnu.gdb.z80.paging"},  // Port 0x1FFD - +3 extended paging
        {"pfe",   8, 17, "", "org.gnu.gdb.z80.paging"},  // Port 0xFE - border/ear/mic
    };

    constexpr int PAGING_REG_BASE = 15;  // First paging register number
}

std::string GDBTargetZ80::generateTargetXML(EmulatorContext* /*ctx*/)
{
    return generateFlatTargetXML(nullptr);
}

std::string GDBTargetZ80::generateFlatTargetXML(EmulatorContext* /*ctx*/)
{
    std::ostringstream xml;

    xml << R"(<?xml version="1.0"?>
<!DOCTYPE target SYSTEM "gdb-target.dtd">
<target version="1.0">
  <architecture>z80</architecture>
  <feature name="org.gnu.gdb.z80.cpu">
)";

    for (const auto& reg : g_cpuRegisters)
    {
        xml << "    <reg name=\"" << reg.name << "\" bitsize=\"" << reg.bitsize << "\"";

        if (reg.regnum == 0)
        {
            xml << " regnum=\"0\"";
        }

        if (!reg.type.empty())
        {
            xml << " type=\"" << reg.type << "\"";
        }

        xml << "/>\n";
    }

    xml << R"(  </feature>
  <feature name="org.gnu.gdb.z80.paging">
)";

    for (const auto& reg : g_pagingRegisters)
    {
        xml << "    <reg name=\"" << reg.name << "\" bitsize=\"" << reg.bitsize << "\"";
        xml << "/>\n";
    }

    xml << R"(  </feature>
</target>
)";

    return xml.str();
}

std::string GDBTargetZ80::getCPUFeatureXML()
{
    std::ostringstream xml;

    xml << R"(<?xml version="1.0"?>
<!DOCTYPE feature SYSTEM "gdb-target.dtd">
<feature name="org.gnu.gdb.z80.cpu">
)";

    for (const auto& reg : g_cpuRegisters)
    {
        xml << "  <reg name=\"" << reg.name << "\" bitsize=\"" << reg.bitsize << "\"";

        if (reg.regnum == 0)
        {
            xml << " regnum=\"0\"";
        }

        if (!reg.type.empty())
        {
            xml << " type=\"" << reg.type << "\"";
        }

        xml << "/>\n";
    }

    xml << "</feature>\n";

    return xml.str();
}

const std::vector<GDBTargetZ80::RegisterDef>& GDBTargetZ80::getCPURegisters()
{
    return g_cpuRegisters;
}

std::string GDBTargetZ80::serializeRegisters(EmulatorContext* ctx)
{
    std::string result;

    for (const auto& reg : g_cpuRegisters)
    {
        uint16_t value = readCPURegister(ctx, reg.regnum);
        int bytes = reg.bitsize / 8;

        // Little-endian encoding
        for (int i = 0; i < bytes; i++)
        {
            result += GDBPacket::toHex((value >> (i * 8)) & 0xFF, 2);
        }
    }

    return result;
}

bool GDBTargetZ80::deserializeRegisters(EmulatorContext* ctx, const std::string& hex)
{
    if (!ctx)
    {
        return false;
    }

    size_t offset = 0;
    std::vector<uint16_t> values;
    for (const auto& reg : g_cpuRegisters)
    {
        int bytes = reg.bitsize / 8;
        int hexLen = bytes * 2;

        if (offset + hexLen > hex.size())
        {
            return false;
        }

        // Parse little-endian value
        uint16_t value = 0;
        for (int i = 0; i < bytes; i++)
        {
            auto byte = GDBPacket::parseHex(hex.substr(offset + i * 2, 2));
            if (!byte)
            {
                return false;
            }
            value |= static_cast<uint16_t>(*byte) << (i * 8);
        }

        values.push_back(value);
        offset += hexLen;
    }

    // One tool edit (Emulator::EditMemoryFromTool): while a session records, the new registers are part of the history
    editCPU(ctx, "GDB register write", [&] {
        for (size_t i = 0; i < values.size(); ++i)
            writeCPURegister(ctx, g_cpuRegisters[i].regnum, values[i]);
    });
    return true;
}

std::string GDBTargetZ80::readRegister(EmulatorContext* ctx, int regnum)
{
    int totalRegs = static_cast<int>(g_cpuRegisters.size() + g_pagingRegisters.size());
    if (regnum < 0 || regnum >= totalRegs)
    {
        return "E02";  // Invalid register number
    }

    int bytes;
    if (regnum < static_cast<int>(g_cpuRegisters.size()))
    {
        bytes = g_cpuRegisters[regnum].bitsize / 8;
    }
    else
    {
        int pagingIdx = regnum - static_cast<int>(g_cpuRegisters.size());
        bytes = g_pagingRegisters[pagingIdx].bitsize / 8;
    }

    uint16_t value = readCPURegister(ctx, regnum);

    std::string result;
    for (int i = 0; i < bytes; i++)
    {
        result += GDBPacket::toHex((value >> (i * 8)) & 0xFF, 2);
    }

    return result;
}

std::string GDBTargetZ80::writeRegister(EmulatorContext* ctx, int regnum, const std::string& hex)
{
    if (!ctx)
    {
        return "E0D";  // Read-only context
    }

    int totalRegs = static_cast<int>(g_cpuRegisters.size() + g_pagingRegisters.size());
    if (regnum < 0 || regnum >= totalRegs)
    {
        return "E02";
    }

    int bytes;
    if (regnum < static_cast<int>(g_cpuRegisters.size()))
    {
        bytes = g_cpuRegisters[regnum].bitsize / 8;
    }
    else
    {
        int pagingIdx = regnum - static_cast<int>(g_cpuRegisters.size());
        bytes = g_pagingRegisters[pagingIdx].bitsize / 8;
    }

    if (hex.size() != static_cast<size_t>(bytes * 2))
    {
        return "E01";
    }

    uint16_t value = 0;
    for (int i = 0; i < bytes; i++)
    {
        auto byte = GDBPacket::parseHex(hex.substr(i * 2, 2));
        if (!byte)
        {
            return "E01";
        }
        value |= static_cast<uint16_t>(*byte) << (i * 8);
    }

    // A CPU register is a tool edit; a paging pseudo-register is a port write, already one (PortWrite)
    if (regnum < static_cast<int>(g_cpuRegisters.size()))
        editCPU(ctx, "GDB register write", [&] { writeCPURegister(ctx, regnum, value); });
    else
        writeCPURegister(ctx, regnum, value);
    return "OK";
}

void GDBTargetZ80::editCPU(EmulatorContext* ctx, const char* source, const std::function<void()>& edit)
{
    if (ctx->pEmulator)
        ctx->pEmulator->EditMemoryFromTool(source, edit);
    else
        edit();
}

bool GDBTargetZ80::writeMemory(EmulatorContext* ctx, uint64_t addr, const std::vector<uint8_t>& bytes)
{
    Memory* memory = ctx ? ctx->pMemory : nullptr;
    if (!memory)
        return false;

    // Physical memory access (0x01PPAAAA): a page edit behind the CPU's back, TTD must see it like any other tool write
    if ((addr & 0xFF000000) == 0x01000000)
    {
        const uint8_t page = static_cast<uint8_t>((addr >> 16) & 0xFF);
        const uint16_t offset = static_cast<uint16_t>(addr & 0x3FFF);
        uint8_t* pageAddr = memory->RAMPageAddress(page);
        if (!pageAddr)
            return false;
        editCPU(ctx, "GDB page write", [&] {
            for (size_t i = 0; i < bytes.size(); i++)
                pageAddr[static_cast<uint16_t>((offset + i) & 0x3FFF)] = bytes[i];
            memory->MarkRamPageEdited(page);
        });
        return true;
    }

    editCPU(ctx, "GDB memory write", [&] {
        for (size_t i = 0; i < bytes.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>((addr + i) & 0xFFFF), bytes[i]);
    });
    return true;
}

int GDBTargetZ80::getRegisterCount(EmulatorContext* /*ctx*/)
{
    return static_cast<int>(g_cpuRegisters.size() + g_pagingRegisters.size());
}

int GDBTargetZ80::getRegisterPacketLength(EmulatorContext* /*ctx*/)
{
    int totalBytes = 0;
    for (const auto& reg : g_cpuRegisters)
    {
        totalBytes += reg.bitsize / 8;
    }
    return totalBytes * 2;  // Hex encoding
}

uint16_t GDBTargetZ80::readCPURegister(EmulatorContext* ctx, int regnum)
{
    if (!ctx || !ctx->pEmulator)
        return 0;

    // CPU registers (0-14)
    if (regnum < PAGING_REG_BASE)
    {
        Z80State* state = ctx->pEmulator->GetZ80State();
        if (!state)
            return 0;

        switch (regnum)
        {
            case 0:  return state->af;
            case 1:  return state->bc;
            case 2:  return state->de;
            case 3:  return state->hl;
            case 4:  return state->sp;
            case 5:  return state->pc;
            case 6:  return state->ix;
            case 7:  return state->iy;
            case 8:  return state->alt.af;
            case 9:  return state->alt.bc;
            case 10: return state->alt.de;
            case 11: return state->alt.hl;
            case 12: return state->ir_;
            case 13: return (state->iff1 ? 1 : 0) | (state->iff2 ? 2 : 0);
            case 14: return state->im;
            default: return 0;
        }
    }

    // Paging pseudo-registers (15+)
    EmulatorState& es = ctx->emulatorState;
    switch (regnum)
    {
        case 15: return es.p7FFD;   // Port 0x7FFD
        case 16: return es.p1FFD;   // Port 0x1FFD
        case 17: return es.pFE;     // Port 0xFE
        default: return 0;
    }
}

void GDBTargetZ80::writeCPURegister(EmulatorContext* ctx, int regnum, uint16_t value)
{
    if (!ctx || !ctx->pEmulator)
        return;

    // CPU registers (0-14)
    if (regnum < PAGING_REG_BASE)
    {
        Z80State* state = ctx->pEmulator->GetZ80State();
        if (!state)
            return;

        switch (regnum)
        {
            case 0:  state->af = value; break;
            case 1:  state->bc = value; break;
            case 2:  state->de = value; break;
            case 3:  state->hl = value; break;
            case 4:  state->sp = value; break;
            case 5:  state->pc = value; break;
            case 6:  state->ix = value; break;
            case 7:  state->iy = value; break;
            case 8:  state->alt.af = value; break;
            case 9:  state->alt.bc = value; break;
            case 10: state->alt.de = value; break;
            case 11: state->alt.hl = value; break;
            case 12: state->ir_ = value; break;
            case 13:
                state->iff1 = (value & 1) ? 1 : 0;
                state->iff2 = (value & 2) ? 1 : 0;
                break;
            case 14:
                state->im = value & 3;
                break;
            default:
                break;
        }
        return;
    }

    // Paging pseudo-registers (15+): a debugger port write through the decoder (PortWrite: no breakpoint fires,
    // a tool edit for TTD)
    uint8_t byteValue = static_cast<uint8_t>(value);
    switch (regnum)
    {
        case 15:  // Port 0x7FFD
            PortWrite::Write(ctx->pEmulator, 0x7FFD, byteValue, "gdb");
            break;
        case 16:  // Port 0x1FFD
            PortWrite::Write(ctx->pEmulator, 0x1FFD, byteValue, "gdb");
            break;
        case 17:  // Port 0xFE
            PortWrite::Write(ctx->pEmulator, 0x00FE, byteValue, "gdb");
            break;
        default:
            break;
    }
}
