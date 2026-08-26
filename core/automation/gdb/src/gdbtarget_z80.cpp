#include "gdbtarget_z80.h"
#include "gdbpacket.h"

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/cpu/z80.h>

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

        writeCPURegister(ctx, reg.regnum, value);
        offset += hexLen;
    }

    return true;
}

std::string GDBTargetZ80::readRegister(EmulatorContext* ctx, int regnum)
{
    if (regnum < 0 || regnum >= static_cast<int>(g_cpuRegisters.size()))
    {
        return "E02";  // Invalid register number
    }

    const auto& reg = g_cpuRegisters[regnum];
    uint16_t value = readCPURegister(ctx, regnum);
    int bytes = reg.bitsize / 8;

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

    if (regnum < 0 || regnum >= static_cast<int>(g_cpuRegisters.size()))
    {
        return "E02";
    }

    const auto& reg = g_cpuRegisters[regnum];
    int bytes = reg.bitsize / 8;

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

    writeCPURegister(ctx, regnum, value);
    return "OK";
}

int GDBTargetZ80::getRegisterCount(EmulatorContext* /*ctx*/)
{
    return static_cast<int>(g_cpuRegisters.size());
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

void GDBTargetZ80::writeCPURegister(EmulatorContext* ctx, int regnum, uint16_t value)
{
    if (!ctx || !ctx->pEmulator)
        return;

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
}
