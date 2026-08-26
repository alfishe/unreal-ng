#include "gdbtarget_z80.h"
#include "gdbpacket.h"

#include <sstream>

namespace
{
    // CPU register definitions per TDD §4.2
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
    // TODO: Generate model-aware XML with peripheral features
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
    if (!ctx)
    {
        // Return zeros for all registers
        std::string result;
        for (const auto& reg : g_cpuRegisters)
        {
            int bytes = reg.bitsize / 8;
            for (int i = 0; i < bytes; i++)
            {
                result += "00";
            }
        }
        return result;
    }

    // TODO: Read actual register values
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
    // TODO: Add model-specific pseudo-registers
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

uint16_t GDBTargetZ80::readCPURegister(EmulatorContext* /*ctx*/, int regnum)
{
    // TODO: Implement actual register reading from EmulatorContext
    // For now return 0
    (void)regnum;
    return 0;
}

void GDBTargetZ80::writeCPURegister(EmulatorContext* /*ctx*/, int regnum, uint16_t value)
{
    // TODO: Implement actual register writing to EmulatorContext
    (void)regnum;
    (void)value;
}
