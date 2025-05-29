#include "disassemblerwidget.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debuggerwindow.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "ui_disassemblerwidget.h"

DisassemblerWidget::DisassemblerWidget(QWidget* parent) : QWidget(parent), ui(new Ui::DisassemblerWidget)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    m_mainThread = QApplication::instance()->thread();
    m_debuggerWindow = static_cast<DebuggerWindow*>(parent);

    // Initialize member variables
    m_currentPC = 0;
    m_displayAddress = 0;
    m_scrollMode = ScrollMode::Command;  // Default to command mode
    m_isActive = false;                  // Default to detached state

    // Create a custom DisassemblyTextEdit and replace the default one
    DisassemblyTextEdit* customTextEdit = new DisassemblyTextEdit(this);
    customTextEdit->setReadOnly(true);
    customTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    customTextEdit->setFont(ui->disassemblyTextEdit->font());

    // Replace the original text edit with our custom one
    QLayout* layout = ui->disassemblyTextEdit->parentWidget()->layout();
    layout->replaceWidget(ui->disassemblyTextEdit, customTextEdit);
    ui->disassemblyTextEdit->setParent(nullptr);
    delete ui->disassemblyTextEdit;
    m_disassemblyTextEdit = customTextEdit;

    // Setup PC highlight format
    m_pcHighlightFormat.setBackground(QColor(255, 255, 0, 100));  // Light yellow background
    m_pcHighlightFormat.setForeground(QColor(0, 0, 0));           // Black text

    // Create scroll mode indicator label
    m_scrollModeIndicator = new QLabel(this);
    m_scrollModeIndicator->setFrameStyle(QFrame::Panel | QFrame::Raised);
    m_scrollModeIndicator->setAlignment(Qt::AlignCenter);
    m_scrollModeIndicator->setFixedSize(50, 24);
    m_scrollModeIndicator->setStyleSheet(
        "QLabel { background-color: #333; color: #0f0; font-weight: bold; border: 1px solid #555; }");

    // Create debugger state indicator label
    m_stateIndicator = new QLabel(this);
    m_stateIndicator->setFrameStyle(QFrame::Panel | QFrame::Raised);
    m_stateIndicator->setAlignment(Qt::AlignCenter);
    m_stateIndicator->setFixedSize(80, 24);
    m_stateIndicator->setStyleSheet(
        "QLabel { background-color: #333; color: #0f0; font-weight: bold; border: 1px solid #555; }");

    // Position the indicators in the top-right corner
    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout);
    if (mainLayout)
    {
        QHBoxLayout* topLayout = new QHBoxLayout();
        topLayout->addStretch();
        topLayout->addWidget(m_stateIndicator);
        topLayout->addSpacing(5);  // Add a small gap between indicators
        topLayout->addWidget(m_scrollModeIndicator);

        // Insert the top layout before the text edit
        mainLayout->insertLayout(0, topLayout);
    }

    // Update the indicators to show initial states
    updateScrollModeIndicator();
    updateDebuggerStateIndicator();

    // Connect signals for keyboard navigation
    connect(m_disassemblyTextEdit, &DisassemblyTextEdit::keyUpPressed, this, &DisassemblerWidget::navigateUp);
    connect(m_disassemblyTextEdit, &DisassemblyTextEdit::keyDownPressed, this, &DisassemblerWidget::navigateDown);
    connect(m_disassemblyTextEdit, &DisassemblyTextEdit::enterPressed, this, &DisassemblerWidget::returnToCurrentPC);
    connect(m_disassemblyTextEdit, &DisassemblyTextEdit::toggleScrollMode, this, &DisassemblerWidget::toggleScrollMode);
}

DisassemblerWidget::~DisassemblerWidget()
{
    // detach();
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

std::unique_ptr<Z80Disassembler>& DisassemblerWidget::getDisassembler()
{
    return m_debuggerWindow->getEmulator()->GetContext()->pDebugManager->GetDisassembler();
}

void DisassemblerWidget::setDisassemblerAddress(uint16_t pc)
{
    Memory& memory = *getMemory();
    Z80Registers* registers = getZ80Registers();
    Z80Disassembler& disassembler = *getDisassembler();

    // Store the current PC and display address
    m_currentPC = registers->pc;
    m_displayAddress = pc;

    uint8_t* pcPhysicalAddress = memory.MapZ80AddressToPhysicalAddress(pc);
    uint8_t commandLen = 0;
    DecodedInstruction decoded;
    std::stringstream ss;

    // Disassemble 10 instructions instead of just 4 to provide more context
    for (size_t i = 0; i < 10; i++)
    {
        std::string pcAddress = StringHelper::ToUpper(StringHelper::ToHexWithPrefix(pc, ""));
        std::string command = disassembler.disassembleSingleCommandWithRuntime(pcPhysicalAddress, 6, &commandLen,
                                                                               registers, &memory, &decoded);
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

        pcPhysicalAddress += decoded.fullCommandLen;
        pc += decoded.fullCommandLen;

        // Format value like: $15FB: CD 2C 16   call #162C
        ss << StringHelper::Format("$%s: %-11s   %s%s", pcAddress.c_str(), hex.c_str(), command.c_str(),
                                   runtime.c_str())
           << std::endl;
    }

    std::string value = ss.str();
    m_disassemblyTextEdit->setPlainText(value.c_str());

    // DEBUG info (keep this as it might be useful)
    uint8_t z80Bank = 0;
    size_t read = memory.GetZ80BankReadAccessCount(z80Bank);
    size_t write = memory.GetZ80BankWriteAccessCount(z80Bank);
    size_t execute = memory.GetZ80BankExecuteAccessCount(z80Bank);
    std::string accessedValue = StringHelper::Format("%s\nBank 1:  read: %d\n         write: %d\n         execute: %d",
                                                     value.c_str(), read, write, execute);
    m_disassemblyTextEdit->setPlainText(accessedValue.c_str());

    // Highlight the current PC instruction
    highlightCurrentPC();
}

void DisassemblerWidget::highlightCurrentPC()
{
    if (!getEmulator() || !getZ80Registers())
        return;

    QTextCursor cursor = m_disassemblyTextEdit->textCursor();
    cursor.movePosition(QTextCursor::Start);

    // Find the line containing the current PC
    QString pcHexString = QString::fromStdString(StringHelper::ToUpper(StringHelper::ToHexWithPrefix(m_currentPC, "")));
    QString searchString = "$" + pcHexString + ":";  // Format like: $15FB:

    // Search for the line with current PC
    bool found = m_disassemblyTextEdit->find(searchString);

    if (found)
    {
        // Get the cursor at the found position
        QTextCursor highlightCursor = m_disassemblyTextEdit->textCursor();

        // Select the entire line
        highlightCursor.movePosition(QTextCursor::StartOfLine);
        highlightCursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);

        // Apply the highlight format
        QTextEdit::ExtraSelection selection;
        selection.cursor = highlightCursor;
        selection.format = m_pcHighlightFormat;

        // Apply the selection
        QList<QTextEdit::ExtraSelection> extraSelections;
        extraSelections.append(selection);
        m_disassemblyTextEdit->setExtraSelections(extraSelections);

        // Make sure the highlighted line is visible
        m_disassemblyTextEdit->ensureCursorVisible();
    }
}

void DisassemblerWidget::updateScrollModeIndicator()
{
    // Update the indicator label based on current mode
    if (m_scrollMode == ScrollMode::Byte)
    {
        m_scrollModeIndicator->setText("BYTE");
    }
    else
    {
        m_scrollModeIndicator->setText("CMD");
    }
}

void DisassemblerWidget::updateDebuggerStateIndicator()
{
    // Check if we have a valid emulator
    Emulator* emulator = getEmulator();
    if (!emulator)
    {
        qDebug() << "DisassemblerWidget::updateDebuggerStateIndicator - No emulator available";
        m_stateIndicator->setText("DETACHED");
        m_stateIndicator->setStyleSheet(
            "QLabel { background-color: #333; color: #777; font-weight: bold; border: 1px solid #555; }");
        m_isActive = false;
        return;
    }
    
    // Check if the emulator is paused (active debugging state)
    EmulatorStateEnum state = emulator->GetState();
    bool isPaused = emulator->IsPaused(); // Use the IsPaused() method which handles all paused states
    
    // Convert state enum to string for debug output
    QString stateName;
    switch (state) {
        case EmulatorStateEnum::StateUnknown: stateName = "Unknown"; break;
        case EmulatorStateEnum::StateInitialized: stateName = "Initialized"; break;
        case EmulatorStateEnum::StateRun: stateName = "Run"; break;
        case EmulatorStateEnum::StatePaused: stateName = "Paused"; break;
        case EmulatorStateEnum::StateResumed: stateName = "Resumed"; break;
        case EmulatorStateEnum::StateStopped: stateName = "Stopped"; break;
        default: stateName = "Invalid"; break;
    }
    
    qDebug() << "DisassemblerWidget::updateDebuggerStateIndicator - Emulator state:" << stateName << "isPaused:" << isPaused;
    
    // Update the state indicator
    if (isPaused)
    {
        qDebug() << "DisassemblerWidget::updateDebuggerStateIndicator - Setting ACTIVE state";
        m_stateIndicator->setText("ACTIVE");
        m_stateIndicator->setStyleSheet(
            "QLabel { background-color: #333; color: #0f0; font-weight: bold; border: 1px solid #555; }");
        m_isActive = true;
    }
    else
    {
        qDebug() << "DisassemblerWidget::updateDebuggerStateIndicator - Setting DETACHED state";
        m_stateIndicator->setText("DETACHED");
        m_stateIndicator->setStyleSheet(
            "QLabel { background-color: #333; color: #777; font-weight: bold; border: 1px solid #555; }");
        m_isActive = false;
    }
}

void DisassemblerWidget::toggleScrollMode()
{
    // Toggle between byte and command scroll modes
    m_scrollMode = (m_scrollMode == ScrollMode::Byte) ? ScrollMode::Command : ScrollMode::Byte;

    // Update the indicator
    updateScrollModeIndicator();
}

uint16_t DisassemblerWidget::getNextCommandAddress(uint16_t currentAddress)
{
    if (!getEmulator() || !getMemory())
        return (currentAddress + 1) & 0xFFFF;

    Memory& memory = *getMemory();
    Z80Disassembler& disassembler = *getDisassembler();

    // Map the address to physical memory
    uint8_t* physicalAddress = memory.MapZ80AddressToPhysicalAddress(currentAddress);

    // Disassemble the current instruction to get its length
    uint8_t commandLen = 0;
    DecodedInstruction decoded;
    disassembler.disassembleSingleCommand(physicalAddress, 6, &commandLen, &decoded);

    // Calculate the next address by adding the command length
    return (currentAddress + decoded.fullCommandLen) & 0xFFFF;
}

uint16_t DisassemblerWidget::getPreviousCommandAddress(uint16_t currentAddress)
{
    // Finding the previous instruction is trickier since Z80 has variable-length instructions
    // A simple approach is to try disassembling from a few bytes back and see if we land at our address

    if (!getEmulator() || !getMemory())
        return (currentAddress - 1) & 0xFFFF;

    // Try up to 4 bytes back (longest Z80 instruction is 4 bytes)
    for (int i = 1; i <= 4; i++)
    {
        uint16_t testAddress = (currentAddress - i) & 0xFFFF;
        uint16_t nextAddr = getNextCommandAddress(testAddress);

        if (nextAddr == currentAddress)
        {
            // We found an instruction that leads to our current address
            return testAddress;
        }
    }

    // If we can't find a perfect match, just go back 1 byte as fallback
    return (currentAddress - 1) & 0xFFFF;
}

void DisassemblerWidget::navigateUp()
{
    if (!getEmulator() || !getZ80Registers())
        return;

    uint16_t prevAddress;

    if (m_scrollMode == ScrollMode::Byte)
    {
        // Byte mode: move back one byte
        prevAddress = m_displayAddress > 0 ? m_displayAddress - 1 : 0xFFFF;
    }
    else
    {
        // Command mode: move back one whole instruction
        prevAddress = getPreviousCommandAddress(m_displayAddress);
    }

    // Update the disassembly view
    setDisassemblerAddress(prevAddress);
}

void DisassemblerWidget::navigateDown()
{
    if (!getEmulator() || !getZ80Registers())
        return;

    uint16_t nextAddress;

    if (m_scrollMode == ScrollMode::Byte)
    {
        // Byte mode: move forward one byte
        nextAddress = m_displayAddress < 0xFFFF ? m_displayAddress + 1 : 0;
    }
    else
    {
        // Command mode: move forward one whole instruction
        nextAddress = getNextCommandAddress(m_displayAddress);
    }

    // Update the disassembly view
    setDisassemblerAddress(nextAddress);
}

void DisassemblerWidget::returnToCurrentPC()
{
    if (!getEmulator() || !getZ80Registers())
        return;

    // Get the current PC from the Z80 registers
    uint16_t currentPC = getZ80Registers()->pc;

    // Update the disassembly view to show the current PC
    setDisassemblerAddress(currentPC);
}

void DisassemblerWidget::reset()
{
    m_disassemblyTextEdit->setPlainText("<Disassembly goes here>");
    m_currentPC = 0;
    m_displayAddress = 0;

    // Reset to default scroll mode (Command)
    m_scrollMode = ScrollMode::Command;
    updateScrollModeIndicator();

    // Update debugger state indicator
    updateDebuggerStateIndicator();

    // Clear any highlights
    m_disassemblyTextEdit->setExtraSelections(QList<QTextEdit::ExtraSelection>());
}

void DisassemblerWidget::refresh()
{
    qDebug() << "DisassemblerWidget::refresh() called";
    
    // Update the disassembly view with current PC
    if (getEmulator() && getZ80Registers())
    {
        uint16_t currentPC = getZ80Registers()->pc;
        setDisassemblerAddress(currentPC);
    }

    // Update the debugger state indicator
    updateDebuggerStateIndicator();
}
