#include "disassemblerwidget.h"

#include "ui_disassemblerwidget.h"

#include "debuggerwindow.h"

#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"

DisassemblerWidget::DisassemblerWidget(QWidget *parent) : QWidget(parent), ui(new Ui::DisassemblerWidget)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    m_mainThread = QApplication::instance()->thread();

    m_debuggerWindow = static_cast<DebuggerWindow*>(parent);

    m_disassemblyTextEdit = ui->disassemblyTextEdit;
}

DisassemblerWidget::~DisassemblerWidget()
{
    //detach();
}

// Helper methods
Emulator* DisassemblerWidget::getEmulator()
{
    return m_debuggerWindow->getEmulator();
}

EmulatorContext* DisassemblerWidget::getEmulatorContext()
{
    return m_debuggerWindow->getEmulator()->GetContext();
}

Memory* DisassemblerWidget::getMemory()
{
    return m_debuggerWindow->getEmulator()->GetContext()->pMemory;
}

Z80Registers* DisassemblerWidget::getZ80Registers()
{
    return m_debuggerWindow->getEmulator()->GetContext()->pCore->GetZ80();
}

Z80Disassembler* DisassemblerWidget::getDisassembler()
{
    return m_debuggerWindow->getEmulator()->GetContext()->pDebugManager->GetDisassembler();
}

void DisassemblerWidget::setDisassemblerAddress(uint16_t pc)
{

    Memory& memory = *getMemory();
    Z80Registers* registers = getZ80Registers();
    Z80Disassembler& disassembler = *getDisassembler();

    uint8_t* pcPhysicalAddress = memory.MapZ80AddressToPhysicalAddress(pc);
    uint8_t commandLen = 0;
    DecodedInstruction decoded;

    std::string pcAddress = StringHelper::ToUpper(StringHelper::ToHexWithPrefix(pc, ""));
    std::string command = disassembler.disassembleSingleCommandWithRuntime(pcPhysicalAddress, 6, &commandLen, registers, &memory, &decoded);
    std::string hex = DumpHelper::HexDumpBuffer(pcPhysicalAddress, commandLen);
    std::string runtime;

    if (decoded.hasRuntime)
    {
        runtime = disassembler.getRuntimeHints(decoded);
        if (runtime.size() > 0)
        {
            runtime = " " + runtime;
        }
    }

    // Format value like: $15FB: CD 2C 16   call #162C
    std::string value = StringHelper::Format("$%s: %s   %s%s", pcAddress.c_str(), hex.c_str(), command.c_str(), runtime.c_str());
    m_disassemblyTextEdit->setPlainText(value.c_str());


    // DEBUG
    uint8_t z80Bank = 0;
    size_t read = memory.GetZ80BankReadAccessCount(z80Bank);
    size_t write = memory.GetZ80BankWriteAccessCount(z80Bank);
    size_t execute = memory.GetZ80BankExecuteAccessCount(z80Bank);
    std::string accessedValue = StringHelper::Format("%s\nBank 1:  read: %d\n         write: %d\n         execute: %d", value.c_str(), read, write, execute);
    m_disassemblyTextEdit->setPlainText(accessedValue.c_str());
}

void DisassemblerWidget::reset()
{
    m_disassemblyTextEdit->setPlainText("<Disassembly goes here>");
}

void DisassemblerWidget::refresh()
{

}
