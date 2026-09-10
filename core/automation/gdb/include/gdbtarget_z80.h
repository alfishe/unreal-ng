#pragma once

#include <cstdint>
#include <string>
#include <vector>

class EmulatorContext;

/// @brief Z80 target description and register codec for GDB RSP
///
/// Provides:
/// - target.xml generation for Z80 architecture
/// - Register serialization/deserialization per XML regnum order
/// - Model-aware peripheral pseudo-registers (paging latches, AY, etc.)
///
/// See TDD §4.2 for register mapping and §4.6 for clone peripherals.
class GDBTargetZ80
{
public:
    /// @brief Register entry in the target description
    struct RegisterDef
    {
        std::string name;
        int bitsize;
        int regnum;
        std::string type;    // "data_ptr", "code_ptr", or empty
        std::string feature; // Feature group name
    };

    /// @brief Generate target.xml for the current model
    /// @param ctx Emulator context for model-specific features
    /// @return Complete target XML string
    static std::string generateTargetXML(EmulatorContext* ctx);

    /// @brief Generate flattened target XML (no xi:include)
    static std::string generateFlatTargetXML(EmulatorContext* ctx);

    /// @brief Get the CPU feature XML (z80-cpu.xml annex)
    static std::string getCPUFeatureXML();

    /// @brief Get register definitions for the CPU feature
    static const std::vector<RegisterDef>& getCPURegisters();

    /// @brief Serialize all registers to hex string for 'g' packet
    /// @param ctx Emulator context to read registers from
    /// @return Hex-encoded register values in regnum order
    static std::string serializeRegisters(EmulatorContext* ctx);

    /// @brief Deserialize hex string and write to registers for 'G' packet
    /// @param ctx Emulator context to write registers to
    /// @param hex Hex-encoded register values
    /// @return true if successful
    static bool deserializeRegisters(EmulatorContext* ctx, const std::string& hex);

    /// @brief Read a single register for 'p' packet
    /// @param ctx Emulator context
    /// @param regnum Register number from target.xml
    /// @return Hex-encoded value, or "E02" if invalid regnum
    static std::string readRegister(EmulatorContext* ctx, int regnum);

    /// @brief Write a single register for 'P' packet
    /// @param ctx Emulator context
    /// @param regnum Register number
    /// @param hex Hex-encoded value
    /// @return "OK" or error code
    static std::string writeRegister(EmulatorContext* ctx, int regnum, const std::string& hex);

    /// @brief Get total register count for the current configuration
    static int getRegisterCount(EmulatorContext* ctx);

    /// @brief Get expected byte length of 'g' packet response
    static int getRegisterPacketLength(EmulatorContext* ctx);

private:
    static uint16_t readCPURegister(EmulatorContext* ctx, int regnum);
    static void writeCPURegister(EmulatorContext* ctx, int regnum, uint16_t value);
};
