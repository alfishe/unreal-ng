#include "disassemblerwidget.h"

#include "ui_disassemblerwidget.h"

#include "debuggerwindow.h"

#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "emulator/cpu/cpu.h"
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

Z80Disassembler* DisassemblerWidget::getDisassembler()
{
    return m_debuggerWindow->getEmulator()->GetContext()->pDebugManager->GetDisassembler();
}

void DisassemblerWidget::setDisassemblerAddress(uint16_t pc)
{

    Memory& memory = *getMemory();
    Z80Disassembler& disassembler = *getDisassembler();

    uint8_t* pcPhysicalAddress = memory.RemapAddressToCurrentBank(pc);
    uint8_t commandLen = 0;
    std::string pcAddress = StringHelper::ToUpper(StringHelper::ToHexWithPrefix(pc, ""));
    std::string command = disassembler.disassembleSingleCommand(pcPhysicalAddress, 6, &commandLen);
    std::string hex = DumpHelper::HexDumpBuffer(pcPhysicalAddress, commandLen);

    std::string value = StringHelper::Format("$%s: %s   %s", pcAddress.c_str(), hex.c_str(), command.c_str());
    m_disassemblyTextEdit->setPlainText(value.c_str());
}

void DisassemblerWidget::reset()
{
    m_disassemblyTextEdit->setPlainText("<Disassembly goes here>");
}

void DisassemblerWidget::refresh()
{

}
