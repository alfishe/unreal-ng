#pragma once
#include "stdafx.h"

#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <optional>
#include <filesystem>
#include <unordered_map>
#include "emulator/platform.h"
#include "emulator/memory/memory.h"

// Forward declarations
class ModuleLogger;
class EmulatorContext;

namespace unrealasm::symbols
{
class ISymbolCodec;
class SymbolIndex;
class SymbolStore;
struct Symbol;
struct SymbolSet;
}

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

    // The labels live in the main CPU's symbol store (symbols/architecture.md section 8): the set "user" holds what
    // AddLabel / UpdateLabel make and wins over every file; each loaded file is a set of its own, a later load above an
    // earlier one. The Label objects are a view of the resolved symbols, rebuilt after every change.
    struct View
    {
        std::map<std::string, std::shared_ptr<Label>> byName;
        std::unordered_map<uint16_t, std::shared_ptr<Label>> byAddress;   // the last one placed at the address
    };
    std::unique_ptr<unrealasm::symbols::SymbolStore> _store;
    std::shared_ptr<View> _view;
    int _nextFilePriority = 100;
    // AddLabel of a new name while the user set is on top only adds to the view; its records join the user set at the
    // next use of the store (Flush), so a script adding labels one by one does not rebuild the view each time
    mutable std::vector<unrealasm::symbols::Symbol> _pendingUser;
    bool _userOnTop = true;

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
    std::vector<std::shared_ptr<Label>> GetAllLabelsAtAddress(uint16_t address) const;

    // Filtered queries
    std::vector<std::shared_ptr<Label>> GetLabelsByModule(const std::string& module) const;
    std::vector<std::shared_ptr<Label>> GetLabelsByBank(uint16_t bank,
        std::optional<MemoryBankModeEnum> bankType = std::nullopt) const;
    std::vector<std::shared_ptr<Label>> GetLabelsByType(const std::string& type) const;
    std::vector<std::shared_ptr<Label>> GetLabelsInRange(uint16_t fromAddr, uint16_t toAddr) const;

    // Combined filter
    struct LabelFilter {
        std::optional<std::string> module;
        std::optional<uint16_t> bank;
        std::optional<MemoryBankModeEnum> bankType;
        std::optional<std::string> type;
        std::optional<uint16_t> addressFrom;
        std::optional<uint16_t> addressTo;
        bool activeOnly = false;
    };
    std::vector<std::shared_ptr<Label>> GetLabels(const LabelFilter& filter) const;

    // File operations
    bool LoadLabels(const std::string& path);
    bool LoadMapFile(const std::string& path);
    bool LoadSymFile(const std::string& path);
    bool SaveLabels(const std::string& path, FileFormat format = FileFormat::SYM) const;
    
    // Utility methods
    std::vector<std::shared_ptr<Label>> GetAllLabels() const;
    size_t GetLabelCount() const;

    // Symbol sets (the store behind the labels)
    static constexpr const char* USER_SET = "user";
    static constexpr int USER_SET_PRIORITY = 1000000;
    std::vector<unrealasm::symbols::SymbolSet> GetSymbolSets() const;
    std::shared_ptr<const unrealasm::symbols::SymbolIndex> GetSymbolIndex() const;
    bool SetSymbolSetEnabled(const std::string& id, bool enabled);
    bool SetSymbolSetPriority(const std::string& id, int priority);
    bool DropSymbolSet(const std::string& id);

    /// A symbol as the label it shows (the CPU address, the page as bank + bank offset, the kind or the file's own type
    /// word, "code" when none; the "label.*" traits keep what a label set by hand has beyond that); nullopt when the
    /// symbol has no main CPU address
    static std::optional<Label> ToLabel(const unrealasm::symbols::Symbol& symbol);
    /// A label as a symbol the store keeps (`base` gives the fields a label does not have); ToLabel gives it back as is
    static unrealasm::symbols::Symbol FromLabel(const Label& label, const unrealasm::symbols::Symbol& base);

protected:
    // Label files through the symbol module's codecs (unreal-asm, docs/inprogress/2026-10-05-unreal-asm/symbols/)
    static std::string CodecForExtension(const std::string& extension);
    bool ReadLabelFile(const std::string& path, std::vector<uint8_t>& bytes) const;
    bool ImportWith(const unrealasm::symbols::ISymbolCodec& codec, const std::vector<uint8_t>& bytes, const std::string& path);
    static unrealasm::symbols::Symbol ToSymbol(const Label& label);
    void Rebuild();
    void Flush() const;
    static void Notify();
    /// endregion </Methods>
};
