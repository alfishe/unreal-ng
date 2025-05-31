#include "disassemblerwidget.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QTextBlock>

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

    // Setup breakpoint highlight format
    m_breakpointFormat.setBackground(QColor(255, 0, 0, 100));     // Light red background
    m_breakpointFormat.setForeground(QColor(0, 0, 0));           // Black text

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

    // Create bank indicator label
    m_bankIndicator = new QLabel(this);
    m_bankIndicator->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_bankIndicator->setText("Bank: ROM");  // Default value

    // Position the indicators in the top-right corner
    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout);
    if (mainLayout)
    {
        QHBoxLayout* topLayout = new QHBoxLayout();

        // Add bank indicator on the left
        topLayout->addWidget(m_bankIndicator);

        // Add stretch to push other indicators to the right
        topLayout->addStretch();

        // Add state and scroll mode indicators on the right
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
    connect(m_disassemblyTextEdit, &DisassemblyTextEdit::goToAddressRequested, this, &DisassemblerWidget::showGoToAddressDialog);

    // Connect signals for mouse wheel navigation
    connect(m_disassemblyTextEdit, &DisassemblyTextEdit::wheelScrollUp, this, &DisassemblerWidget::navigateUp);
    connect(m_disassemblyTextEdit, &DisassemblyTextEdit::wheelScrollDown, this, &DisassemblerWidget::navigateDown);

    // Connect mouse click events for breakpoint toggling
    m_disassemblyTextEdit->viewport()->installEventFilter(this);
}

DisassemblerWidget::~DisassemblerWidget()
{
    // Remove event filter
    m_disassemblyTextEdit->viewport()->removeEventFilter(this);
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

BreakpointManager* DisassemblerWidget::getBreakpointManager() const
{
    return m_debuggerWindow->getEmulator()->GetBreakpointManager();
}

void DisassemblerWidget::setDisassemblerAddress(uint16_t pc)
{
    Memory& memory = *getMemory();
    Z80Registers* registers = getZ80Registers();
    Z80Disassembler& disassembler = *getDisassembler();

    // Clear the address map before generating new disassembly
    m_addressMap.clear();

    // Store the starting address to help with debugging
    int baseLineNumber = 0; // First line in the disassembly view

    // Store the current PC and display address
    m_currentPC = registers->pc;
    m_displayAddress = pc;

    // Update the bank indicator
    updateBankIndicator(pc);

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

        // Store the line number to address mapping for breakpoint handling
        // Map the actual line number in the text editor to the address
        m_addressMap[baseLineNumber + i] = pc;

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

        // Format value like: [B] $15FB: CD 2C 16   call #162C
        // Add a breakpoint column at the beginning - check for breakpoint at the CURRENT address
        // not the next one (pc already got incremented above)
        std::string breakpointMarker = hasBreakpointAtAddress(pc - decoded.fullCommandLen) ? "●" : " ";
        ss << StringHelper::Format("[%s] $%s: %-11s   %s%s", breakpointMarker.c_str(), pcAddress.c_str(), hex.c_str(), command.c_str(),
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

    // Highlight the current PC instruction and any breakpoints
    highlightCurrentPC();
    updateBreakpointHighlighting();
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
        QList<QTextEdit::ExtraSelection> extraSelections = m_disassemblyTextEdit->extraSelections();
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
    bool isPaused = emulator->IsPaused();

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
    // Toggle between byte and command modes
    if (m_scrollMode == ScrollMode::Byte)
    {
        m_scrollMode = ScrollMode::Command;
    }
    else
    {
        m_scrollMode = ScrollMode::Byte;
    }

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
    // We'll use a greedy approach to find the longest valid instruction that leads to our address

    if (!getEmulator() || !getMemory())
        return (currentAddress - 1) & 0xFFFF;

    // Store the best match found so far
    uint16_t bestMatch = (currentAddress - 1) & 0xFFFF;
    bool foundMatch = false;

    // Try up to 4 bytes back (longest Z80 instruction is 4 bytes)
    // Start from the farthest (longest instruction) and work our way back
    for (int i = 4; i >= 1; i--)
    {
        uint16_t testAddress = (currentAddress - i) & 0xFFFF;
        uint16_t nextAddr = getNextCommandAddress(testAddress);

        if (nextAddr == currentAddress)
        {
            // We found an instruction that leads to our current address
            // Since we're starting from the longest possible instructions,
            // this is the longest valid instruction
            return testAddress;
        }
    }

    // If we can't find a perfect match, just go back 1 byte as fallback
    return (currentAddress - 1) & 0xFFFF;
}

uint16_t DisassemblerWidget::findInstructionBoundaryBefore(uint16_t targetAddress)
{
    if (!getEmulator() || !getMemory())
        return (targetAddress - 1) & 0xFFFF;

    // Simple approach: Try to find an instruction that ends exactly at our target
    // Try up to 4 bytes back (longest Z80 instruction is 4 bytes)
    for (int i = 1; i <= 4; i++)
    {
        uint16_t testAddress = (targetAddress - i) & 0xFFFF;
        uint16_t nextAddr = getNextCommandAddress(testAddress);

        if (nextAddr == targetAddress)
        {
            // We found an instruction that leads to our target address
            return testAddress;
        }
    }

    // If we can't find a perfect match, just go back 1 byte as fallback
    return (targetAddress - 1) & 0xFFFF;
}

void DisassemblerWidget::navigateUp()
{
    if (!getEmulator() || !getZ80Registers())
        return;

    uint16_t prevAddress;

    if (m_scrollMode == ScrollMode::Byte)
    {
        // In byte mode, move back one byte
        if (m_displayAddress > 0)
        {
            // Move back one byte
            prevAddress = (m_displayAddress - 1) & 0xFFFF;
        }
        else
        {
            prevAddress = 0xFFFF;  // Wrap around to the end of memory
        }
    }
    else
    {
        // Command mode: silently find the previous command address
        // This ensures we jump directly to the previous instruction without intermediate steps
        prevAddress = getPreviousCommandAddress(m_displayAddress);
    }

    // Update the disassembly view with the found address
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

void DisassemblerWidget::updateBankIndicator(uint16_t address)
{
    if (!getEmulator() || !getMemory())
        return;

    Memory& memory = *getMemory();

    // Get the physical address for the current address
    uint8_t* physicalAddress = memory.MapZ80AddressToPhysicalAddress(address);
    std::string bankName = "Unknown";

    // Determine bank based on address range
    if (address < 0x4000)
    {
        // ROM 0 (0-16K)
        bankName = "ROM 0";
    }
    else if (address < 0x8000)
    {
        // ROM 1-N (16K-32K)
        uint8_t romPage = memory.GetROMPage();
        bankName = "ROM " + std::to_string(romPage);
    }
    else if (address < 0xC000)
    {
        // RAM banks (32K-48K)
        // Try to get the RAM page if the method is available
        uint8_t ramPage = 0;

        // Use different methods based on address range
        if (address >= 0x8000 && address < 0xA000)
        {
            ramPage = 2; // Common convention for this range
        }
        else if (address >= 0xA000 && address < 0xC000)
        {
            ramPage = 3; // Common convention for this range
        }

        bankName = "RAM " + std::to_string(ramPage);
    }
    else
    {
        // System RAM (48K-64K)
        bankName = "System RAM";
    }

    // Add the address range for clarity
    QString addressRangeStr;
    if (address < 0x4000)
        addressRangeStr = "0000-3FFF";
    else if (address < 0x8000)
        addressRangeStr = "4000-7FFF";
    else if (address < 0xC000)
        addressRangeStr = "8000-BFFF";
    else
        addressRangeStr = "C000-FFFF";

    // Update the bank indicator label with both bank name and address range
    m_bankIndicator->setText(QString("Bank: %1 (%2)").arg(QString::fromStdString(bankName), addressRangeStr));
}

void DisassemblerWidget::refresh()
{
    qDebug() << "DisassemblerWidget::refresh() called";

    // Update the disassembly view with current PC
    if (getEmulator() && getZ80Registers())
    {
        uint16_t currentPC = getZ80Registers()->pc;
        setDisassemblerAddress(currentPC);

        // Also directly update the bank indicator to ensure it's current
        updateBankIndicator(currentPC);
    }

    // Update the debugger state indicator
    updateDebuggerStateIndicator();
}

void DisassemblerWidget::refreshPreservingPosition(uint16_t addressToKeep)
{
    qDebug() << "DisassemblerWidget::refreshPreservingPosition() called with address:"
             << QString("0x%1").arg(addressToKeep, 4, 16, QLatin1Char('0')).toUpper();

    // Store the current address to preserve
    m_displayAddress = addressToKeep;

    // Refresh the disassembly view but keep the current address
    setDisassemblerAddress(addressToKeep);

    // Update the debugger state indicator
    updateDebuggerStateIndicator();

    // Update breakpoint highlighting
    updateBreakpointHighlighting();
}

// Breakpoint methods
bool DisassemblerWidget::hasBreakpointAtAddress(uint16_t address) const
{
    const BreakpointManager* bpManager = getBreakpointManager();
    if (!bpManager)
        return false;

    // Check all breakpoints to see if there's an execution breakpoint at this address
    const auto& allBreakpoints = bpManager->GetAllBreakpoints();
    for (const auto& pair : allBreakpoints)
    {
        const BreakpointDescriptor* bp = pair.second;
        if (bp->type == BRK_MEMORY &&
            (bp->memoryType & BRK_MEM_EXECUTE) &&
            bp->z80address == address &&
            bp->active)
        {
            return true;
        }
    }

    return false;
}

void DisassemblerWidget::updateBreakpointHighlighting()
{
    QList<QTextEdit::ExtraSelection> extraSelections = m_disassemblyTextEdit->extraSelections();

    // Get the document and iterate through its blocks (lines) safely
    QTextDocument* doc = m_disassemblyTextEdit->document();
    if (!doc) return;

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next())
    {
        // Get the text of the current line
        QString lineText = block.text();

        // Check if this line has a breakpoint marker
        if (lineText.startsWith("[●]"))
        {
            // Create a cursor for this block
            QTextCursor blockCursor(block);

            // Select the entire line
            blockCursor.movePosition(QTextCursor::StartOfLine);
            blockCursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);

            // Create and add the selection
            QTextEdit::ExtraSelection selection;
            selection.cursor = blockCursor;
            selection.format = m_breakpointFormat;
            extraSelections.append(selection);
        }
    }

    // Apply all the selections
    m_disassemblyTextEdit->setExtraSelections(extraSelections);
}

void DisassemblerWidget::toggleBreakpointAtAddress(uint16_t address)
{
    BreakpointManager* bpManager = getBreakpointManager();
    if (!bpManager)
        return;

    // Store current view address before toggling breakpoint
    uint16_t currentViewAddress = m_displayAddress;

    // Check if there's already a breakpoint at this address
    if (hasBreakpointAtAddress(address))
    {
        // Find the breakpoint ID and remove it
        const auto& allBreakpoints = bpManager->GetAllBreakpoints();
        for (const auto& pair : allBreakpoints)
        {
            const BreakpointDescriptor* bp = pair.second;
            if (bp->type == BRK_MEMORY &&
                (bp->memoryType & BRK_MEM_EXECUTE) &&
                bp->z80address == address)
            {
                bpManager->RemoveBreakpointByID(bp->breakpointID);
                break;
            }
        }
    }
    else
    {
        // Add a new execution breakpoint
        bpManager->AddCombinedMemoryBreakpoint(address, BRK_MEM_EXECUTE);
    }

    // Refresh the disassembly but maintain the current view position
    refreshPreservingPosition(currentViewAddress);
}

void DisassemblerWidget::handleBreakpointClick(int lineNumber)
{
    // Debug output to help diagnose issues
    qDebug() << "Clicked on line number:" << lineNumber;
    qDebug() << "Address map contains:" << m_addressMap.size() << "entries";

    // Check if the line number is valid and has an address mapping
    if (m_addressMap.find(lineNumber) != m_addressMap.end())
    {
        uint16_t address = m_addressMap[lineNumber];
        qDebug() << "Found address:" << QString::number(address, 16) << "for line" << lineNumber;
        toggleBreakpointAtAddress(address);
    }
    else
    {
        qDebug() << "No address mapping found for line" << lineNumber;

        // If we don't have an exact match, try to find the closest line
        // This helps with clicks that might be slightly off
        int closestLine = -1;
        int minDistance = 999;

        for (const auto& pair : m_addressMap)
        {
            int distance = std::abs(pair.first - lineNumber);
            if (distance < minDistance)
            {
                minDistance = distance;
                closestLine = pair.first;
            }
        }

        if (closestLine >= 0 && minDistance <= 1) // Only use if very close
        {
            qDebug() << "Using closest line:" << closestLine << "with address:" << QString::number(m_addressMap[closestLine], 16);
            toggleBreakpointAtAddress(m_addressMap[closestLine]);
        }
    }
}

void DisassemblerWidget::showGoToAddressDialog()
{
    bool ok;
    QString text = QInputDialog::getText(this, tr("Go to Address"),
                                         tr("Enter address (decimal or hex with 0x, $, or # prefix):"),
                                         QLineEdit::Normal, QString(), &ok);
    if (ok && !text.isEmpty())
    {
        uint16_t address = parseAddressInput(text);
        goToAddress(address);
    }
}

uint16_t DisassemblerWidget::parseAddressInput(const QString& input)
{
    QString trimmed = input.trimmed();
    bool ok;
    uint16_t address = 0;

    // Check for hex format with various prefixes
    if (trimmed.startsWith("0x", Qt::CaseInsensitive) ||
        trimmed.startsWith("$") ||
        trimmed.startsWith("#"))
    {
        // Remove the prefix
        if (trimmed.startsWith("0x", Qt::CaseInsensitive))
            trimmed = trimmed.mid(2);
        else
            trimmed = trimmed.mid(1);

        // Convert from hex
        address = trimmed.toUInt(&ok, 16);
    }
    else
    {
        // Check if the input looks like hex (contains a-f or A-F)
        static QRegularExpression hexPattern("[a-fA-F]");
        if (hexPattern.match(trimmed).hasMatch())
        {
            // If it contains hex digits, treat as hex
            address = trimmed.toUInt(&ok, 16);
            qDebug() << "Detected hex format without prefix:" << trimmed << "-> 0x" << QString::number(address, 16);
        }
        else
        {
            // Try decimal format
            address = trimmed.toUInt(&ok, 10);
        }
    }

    // Ensure the address is within valid range (0-65535)
    return address & 0xFFFF;
}

void DisassemblerWidget::goToAddress(uint16_t address)
{
    if (!getEmulator() || !getZ80Registers())
        return;

    // Update the disassembly view to show the specified address
    setDisassemblerAddress(address);
}

// Override event filter to handle mouse clicks for breakpoint toggling
bool DisassemblerWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_disassemblyTextEdit->viewport() && event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

        // Check if the click is in the breakpoint column (first few pixels)
        if (mouseEvent->position().x() < 20)
        {
            // Get the line number at the click position
            QTextCursor cursor = m_disassemblyTextEdit->cursorForPosition(mouseEvent->pos());
            int lineNumber = cursor.blockNumber();

            // Toggle breakpoint at this line
            handleBreakpointClick(lineNumber);

            return true; // Event handled
        }
    }

    // Pass the event to the parent class
    return QWidget::eventFilter(obj, event);
}
