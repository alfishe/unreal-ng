#include "disassemblerwidget.h"

#include "ui_disassemblerwidget.h"

#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "debugger/disassembler/z80disasm.h"
#include "emulator/cpu/cpu.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"

DisassemblerWidget::DisassemblerWidget(QWidget *parent) : QWidget(parent), ui(new Ui::DisassemblerWidget)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    m_mainThread = QApplication::instance()->thread();

    m_disassemblyTextEdit = ui->disassemblyTextEdit;
}

DisassemblerWidget::~DisassemblerWidget()
{
    //detach();
}


void DisassemblerWidget::init(EmulatorContext* context)
{
    m_emulatorContext = context;
}

void DisassemblerWidget::detach()
{
    m_emulatorContext = nullptr;
}

void DisassemblerWidget::setDisassemblerAddress(uint16_t pc)
{
    if (m_emulatorContext == nullptr)
        return;

    Z80& z80 = *m_emulatorContext->pCPU->GetZ80();
    Memory& memory = *m_emulatorContext->pMemory;
    Z80Disassembler& disassembler = *m_emulatorContext->pDisassembler;

    uint8_t* pcPhysicalAddress = memory.RemapAddressToCurrentBank(z80.m1_pc);
    uint8_t commandLen = 0;
    std::string pcAddress = StringHelper::ToUpper(StringHelper::ToHexWithPrefix(z80.m1_pc, ""));
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
