#pragma once
#include "stdafx.h"

#include "z80disasm.h"
#include "debugger/labels/labelmanager.h"

// Forward declarations
class EmulatorContext;

/// region <Types>

/// Decoded instruction augmented with additional fields:
/// - Z80 address
/// - Full host address
struct DisplayInstruction : public DecodedInstruction
{
    uint16_t addressZ80;        // Address in 64k Z80 address space
    uint8_t* addressHost;       // Physical host memory address

    uint16_t bank;              // RAM bank number
    uint16_t addressBank;       // Address in the bank

    /// Weak references (can become invalid anytime)
};

/// endregion </Types>


class DocumentDisasm
{
public:
    explicit DocumentDisasm(EmulatorContext* context);
    virtual ~DocumentDisasm();
    DisplayInstruction getInstructionForZ80Address(uint16_t address);

    // Label management
    bool LoadLabels(const std::string& path) { return _disassembler->GetLabelManager()->LoadLabels(path); }
    bool SaveLabels(const std::string& path, LabelManager::FileFormat format = LabelManager::FileFormat::SYM) const 
    { return _disassembler->GetLabelManager()->SaveLabels(path, format); }
    bool AddLabel(const std::string& name, uint16_t z80Address, uint32_t physicalAddress = 0,
                 const std::string& type = "", const std::string& module = "", 
                 const std::string& comment = "")
    { return _disassembler->GetLabelManager()->AddLabel(name, z80Address, physicalAddress, type, module, comment); }
    bool RemoveLabel(const std::string& name) { return _disassembler->GetLabelManager()->RemoveLabel(name); }
    void ClearAllLabels() { _disassembler->GetLabelManager()->ClearAllLabels(); }
    std::shared_ptr<Label> GetLabelByZ80Address(uint16_t address) const 
    { return _disassembler->GetLabelManager()->GetLabelByZ80Address(address); }
    std::shared_ptr<Label> GetLabelByPhysicalAddress(uint32_t address) const 
    { return _disassembler->GetLabelManager()->GetLabelByPhysicalAddress(address); }
    std::shared_ptr<Label> GetLabelByName(const std::string& name) const 
    { return _disassembler->GetLabelManager()->GetLabelByName(name); }
    std::vector<std::shared_ptr<Label>> GetAllLabels() const 
    { return _disassembler->GetLabelManager()->GetAllLabels(); }
    size_t GetLabelCount() const { return _disassembler->GetLabelManager()->GetLabelCount(); }

protected:
    std::unique_ptr<Z80Disassembler> _disassembler;
    EmulatorContext* _context;
};
