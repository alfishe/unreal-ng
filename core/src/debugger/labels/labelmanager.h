#pragma once
#include "stdafx.h"

#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <filesystem>
#include "emulator/platform.h"
#include "emulator/memory/memory.h"

// Forward declarations
class ModuleLogger;
class EmulatorContext;

/// @brief Structure representing a single label with address and type information
struct Label
{
    // Member variables
    std::string name;              // Symbol name (e.g., "main", "data_buffer")
    uint16_t address = 0;          // Z80 address space (0x0000-0xFFFF)
    uint16_t bank = UINT16_MAX;    // Memory bank number (0-254, 0xFFFF = any bank)
    uint16_t bankOffset = UINT16_MAX;  // Address within memory bank (0x0000-0x4000)
    MemoryBankModeEnum bankType = BANK_RAM; // Type of bank (RAM or ROM)
    std::string type;              // Symbol type ("code", "data", "const")
    std::string module;            // Module/segment name this label belongs to
    std::string comment;           // Optional comment or description
    bool active = true;            // Whether the label is currently active (can be toggled)

    Label() = default;
    ~Label() = default;

    // Helper methods for bank type
    bool isROM() const { return bankType == BANK_ROM; }
    bool isRAM() const { return bankType == BANK_RAM; }
    void setBankTypeROM() { bankType = BANK_ROM; }
    void setBankTypeRAM() { bankType = BANK_RAM; }
};

class LabelManager
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
    const uint16_t _SUBMODULE = PlatformDebuggerSubmodulesEnum::SUBMODULE_DEBUG_LABELS;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>


    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    
    // Maps for quick lookup in both directions
    std::map<uint16_t, std::shared_ptr<Label>> _labelsByZ80Address;
    std::map<std::string, std::shared_ptr<Label>> _labelsByName;
    
    // File format detection and parsing helpers
public:
    enum class FileFormat
    {
        UNKNOWN,
        MAP,        // Standard linker map file
        SYM,        // Simple symbol file
        VICE,       // VICE emulator symbol file
        SJASM,      // SJASM assembler symbol file
        Z88DK       // Z88DK symbol file
    };
    /// endregion </Fields>


    /// region <Constructors / destructors>
public:
    LabelManager(EmulatorContext* context);
    virtual ~LabelManager();
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    // Label management
    bool AddLabel(const std::string& name, uint16_t z80Address, uint16_t bank, uint16_t bankAddress, 
                 const std::string& type = "", const std::string& module = "", 
                 const std::string& comment = "", bool active = true);
    bool UpdateLabel(const Label& updatedLabel);
    bool RemoveLabel(const std::string& name);
    void ClearAllLabels();
    
    // Label lookup
    std::shared_ptr<Label> GetLabelByZ80Address(uint16_t address) const;
    std::shared_ptr<Label> GetLabelByName(const std::string& name) const;
    
    // File operations
    bool LoadLabels(const std::string& path);
    bool LoadMapFile(const std::string& path);
    bool LoadSymFile(const std::string& path);
    bool SaveLabels(const std::string& path, FileFormat format = FileFormat::SYM) const;
    
    // Utility methods
    std::vector<std::shared_ptr<Label>> GetAllLabels() const;
    size_t GetLabelCount() const;
    
protected:
    // File format detection and parsing
    FileFormat DetectFileFormat(const std::string& path) const;
    bool ParseMapFile(std::istream& input);
    bool ParseSymFile(std::istream& input);
    bool ParseViceSymFile(std::istream& input);
    bool ParseSJASMSymFile(std::istream& input);
    bool ParseZ88DKSymFile(std::istream& input);
    
    // Helper methods
    static std::string TrimWhitespace(const std::string& str);
    static std::vector<std::string> SplitString(const std::string& str, char delimiter);
    static bool IsHexDigit(char c);
    static uint16_t ParseHex16(const std::string& str);
    static uint32_t ParseHex32(const std::string& str);
    /// endregion </Methods>
};
