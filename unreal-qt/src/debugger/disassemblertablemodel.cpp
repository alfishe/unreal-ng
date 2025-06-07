#include "disassemblertablemodel.h"

#include <QBrush>
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QtGui/QColor>
#include <iomanip>
#include <sstream>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"

DisassemblerTableModel::DisassemblerTableModel(Emulator* emulator, QObject* parent)
    : QAbstractTableModel(parent),
      m_emulator(emulator),
      m_startAddress(0),
      m_endAddress(0xFFFF),
      m_currentPC(0),
      m_visibleStart(0),
      m_visibleEnd(0x1FF)
{
    m_headers << "Address" << "Opcode" << "Label" << "Mnemonic" << "Annotation" << "Comment";

    // Load initial range if we have an emulator
    if (m_emulator)
    {
        setVisibleRange(m_visibleStart, m_visibleEnd);
    }
}

void DisassemblerTableModel::setEmulator(Emulator* emulator)
{
    qDebug() << "setEmulator called with emulator:" << (emulator != nullptr);

    beginResetModel();
    m_emulator = emulator;
    m_instructions.clear();
    m_currentPC = 0;
    endResetModel();

    // Clear the view if no emulator
    if (!m_emulator)
    {
        qDebug() << "Emulator set to null, clearing disassembly view";
        emit dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, columnCount() - 1));
        return;
    }

    // If we have a valid emulator, load the initial range
    qDebug() << "Setting initial visible range:" << QString::number(m_startAddress, 16) << "to"
             << QString::number(m_endAddress, 16);
    setVisibleRange(m_startAddress, m_endAddress);
}

DisassemblerTableModel::~DisassemblerTableModel() {}

int DisassemblerTableModel::rowCount(const QModelIndex&) const
{
    return m_instructions.size();
}

int DisassemblerTableModel::columnCount(const QModelIndex&) const
{
    return m_headers.size();
}

QVariant DisassemblerTableModel::data_impl(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !m_emulator)
    {
        return QVariant();
    }

    // Get the instruction for this row
    if (index.row() >= m_instructions.size() || index.row() < 0)
    {
        return QVariant();
    }

    // Get the address and instruction for this row
    auto it = m_instructions.begin();
    std::advance(it, index.row());
    if (it == m_instructions.end())
    {
        return QVariant();
    }

    uint16_t addr = it.key();
    const auto& instr = it.value();

    if (role == Qt::UserRole)
    {
        // Return the raw address for internal use
        return addr;
    }
    else if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
            case 0:  // Address
                return QString("%1").arg(addr, 4, 16, QChar('0')).toUpper();

            case 1:  // Opcode
            {
                std::ostringstream oss;
                for (size_t i = 0; i < instr.instructionBytes.size(); i++)
                {
                    if (i > 0)
                        oss << " ";
                    oss << StringHelper::ToHex(instr.instructionBytes[i], true);
                }
                return QString::fromStdString(oss.str());
            }
            break;

            case 2:  // Label
                return QString::fromStdString(instr.label);

            case 3:  // Mnemonic
                return QString::fromStdString(instr.mnemonic);

            case 4:  // Annotation
                return QString::fromStdString(instr.annotation);

            case 5:  // Comment
                return QString::fromStdString(instr.comment);
        }
    }
    else if (role == Qt::TextAlignmentRole)
    {
        // Right-align the address and opcode columns, left-align the rest
        return (index.column() <= 1) ? Qt::AlignRight : Qt::AlignLeft;
    }
    else if (role == Qt::FontRole)
    {
        // Use monospace font for better alignment
        QFont font("Monospace");
        font.setStyleHint(QFont::TypeWriter);
        return font;
    }
    else if (role == Qt::BackgroundRole && addr == m_currentPC)
    {
        // Highlight the current PC row
        return QVariant(QBrush(QColor(200, 230, 255)));
    }

    return QVariant();
}

QVariant DisassemblerTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        // Use the headers from m_headers list to ensure consistency
        if (section >= 0 && section < m_headers.size())
        {
            return m_headers[section];
        }
    }
    return QVariant();
}

Qt::ItemFlags DisassemblerTableModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void DisassemblerTableModel::setVisibleRange(uint16_t start, uint16_t end)
{
    qDebug() << "setVisibleRange called with start:" << QString::number(start, 16)
             << "end:" << QString::number(end, 16);

    // Don't do anything if the range hasn't changed
    if (start == m_visibleStart && end == m_visibleEnd)
    {
        qDebug() << "Range unchanged, skipping";
        return;
    }

    beginResetModel();

    // Store the visible range
    m_visibleStart = start;
    m_visibleEnd = end;

    // Calculate range to load with padding
    const uint16_t PADDING = 0x100;
    uint16_t loadStart = (start > PADDING) ? start - PADDING : 0;
    uint16_t loadEnd = (end < 0xFFFF - PADDING) ? end + PADDING : 0xFFFF;

    // Ensure the requested range is loaded
    ensureRangeLoaded(loadStart, loadEnd);

    // Clean up old instructions that are far outside the visible range
    if (!m_instructions.isEmpty())
    {
        auto it = m_instructions.begin();
        while (it != m_instructions.end())
        {
            uint16_t addr = it.key();
            if (addr < loadStart - PADDING || addr > loadEnd + PADDING)
            {
                it = m_instructions.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    endResetModel();

    // Notify views that the data has changed
    emit dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, columnCount() - 1));
}

void DisassemblerTableModel::setCurrentPC(uint16_t pc)
{
    if (pc == m_currentPC)
    {
        return;  // No change
    }

    // Store previous PC for updating the old row
    uint16_t oldPC = m_currentPC;
    m_currentPC = pc;

    // Find the rows for the old and new PC
    int oldRow = -1;
    int newRow = -1;

    // Calculate rows by finding the instruction that contains the PC
    for (auto it = m_instructions.begin(); it != m_instructions.end(); ++it)
    {
        uint16_t addr = it.key();
        const auto& instr = it.value();
        uint16_t instrEnd = addr + instr.instructionBytes.size();

        if (oldPC >= addr && oldPC < instrEnd)
        {
            oldRow = addr - m_visibleStart;
        }
        if (m_currentPC >= addr && m_currentPC < instrEnd)
        {
            newRow = addr - m_visibleStart;
        }

        if (oldRow != -1 && newRow != -1)
        {
            break;  // Found both rows
        }
    }

    // Update the view for the old and new PC rows
    if (oldRow >= 0)
    {
        emit dataChanged(createIndex(oldRow, 0), createIndex(oldRow, columnCount() - 1));
    }
    if (newRow >= 0)
    {
        emit dataChanged(createIndex(newRow, 0), createIndex(newRow, columnCount() - 1));
    }

    // Make sure the current PC is visible in the view
    if (m_currentPC < m_visibleStart || m_currentPC > m_visibleEnd)
    {
        // Center the view around the PC
        uint16_t halfVisible = (m_visibleEnd - m_visibleStart) / 2;
        uint16_t newStart = (m_currentPC > halfVisible) ? m_currentPC - halfVisible : 0;
        setVisibleRange(newStart, newStart + (m_visibleEnd - m_visibleStart));
    }
}

void DisassemblerTableModel::refresh()
{
    beginResetModel();
    endResetModel();
    ensureRangeLoaded(m_startAddress, m_endAddress);
}

void DisassemblerTableModel::reset()
{
    beginResetModel();
    m_instructions.clear();
    m_startAddress = 0;
    m_endAddress = 0xFFFF;
    m_currentPC = 0;
    endResetModel();
}

void DisassemblerTableModel::setHorizontalHeaderLabels(const QStringList& headers)
{
    qDebug() << "Setting header labels:" << headers;

    // Only update if the headers have actually changed
    if (m_headers != headers)
    {
        beginResetModel();
        m_headers = headers.toVector();
        endResetModel();

        // Emit headerDataChanged for all columns
        emit headerDataChanged(Qt::Horizontal, 0, m_headers.size() - 1);

        qDebug() << "Header labels updated, column count:" << m_headers.size();
    }
}

void DisassemblerTableModel::dumpState() const
{
    qDebug() << "=== DisassemblerTableModel State ===";
    qDebug() << "Emulator:" << (m_emulator ? "Valid" : "Null");
    qDebug() << "Instructions count:" << m_instructions.size();
    qDebug() << "Visible range:" << QString("0x%1 - 0x%2").arg(m_visibleStart, 4, 16, QChar('0'))
             << "(size:" << (m_visibleEnd - m_visibleStart + 1) << ")";
    qDebug() << "Current PC: 0x" << QString::number(m_currentPC, 16);

    // Dump first few instructions
    int count = 0;
    qDebug() << "First 5 instructions:";
    for (auto it = m_instructions.begin(); it != m_instructions.end() && count < 5; ++it, ++count)
    {
        const auto& instr = it.value();
        qDebug() << "  0x" << QString::number(it.key(), 16) << ":" << QString::fromStdString(instr.mnemonic)
                 << QString::fromStdString(instr.annotation) << "(bytes:" << instr.instructionBytes.size() << ")";
    }
    qDebug() << "===============================";
}

void DisassemblerTableModel::ensureRangeLoaded(uint16_t start, uint16_t end)
{
    if (!m_emulator || start > end)
    {
        return;
    }

    // Check if we need to load more data
    bool needLoad = false;
    uint16_t addr = start;
    while (addr <= end)
    {
        if (!m_instructions.contains(addr))
        {
            needLoad = true;
            break;
        }
        // Move to next instruction
        const auto& instr = m_instructions[addr];
        uint16_t instructionLength = instr.instructionBytes.empty() ? 1 : instr.instructionBytes.size();
        addr += instructionLength;

        // Safety check for invalid instruction length
        if (addr <= start)
        {
            addr++;  // Prevent infinite loop
        }
    }

    if (needLoad)
    {
        // Load with some padding for better performance
        const int padding = 8;
        uint16_t loadStart = start > padding ? start - padding : 0;
        uint16_t loadEnd = (end < 0xFFFF - padding) ? end + padding : 0xFFFF;
        loadDisassemblyRange(loadStart, loadEnd);
    }
}

void DisassemblerTableModel::loadDisassemblyRange(uint16_t start, uint16_t end)
{
    qDebug() << "loadDisassemblyRange called with start:" << QString::number(start, 16)
             << "end:" << QString::number(end, 16);

    // Clear existing instructions if no emulator is available
    if (!m_emulator)
    {
        qDebug() << "No emulator available, clearing disassembly";
        beginResetModel();
        m_instructions.clear();
        endResetModel();
        return;
    }

    if (start > end)
    {
        qDebug() << "Invalid range: start > end";
        return;
    }

    // Get the disassembler from the emulator
    auto* disassembler = m_emulator->GetContext()->pDebugManager->GetDisassembler().get();
    auto* memory = m_emulator->GetMemory();

    // Determine the actual range to load (expand by a few instructions for context)
    const int context = 4;  // Number of instructions to load before/after
    uint16_t loadStart = start > context ? start - context : 0;
    uint16_t loadEnd = (end < 0xFFFF - context) ? end + context : 0xFFFF;

    // Safety check to prevent infinite loops
    const size_t MAX_INSTRUCTIONS = 50;  // Maximum number of instructions to disassemble in one go
    size_t instructionCount = 0;
    uint16_t prevAddr = 0xFFFF;  // Track previous address to detect infinite loops

    // Load disassembly for the range
    uint16_t addr = loadStart;
    while (addr <= loadEnd && addr >= loadStart && instructionCount < MAX_INSTRUCTIONS)
    {
        // Check for infinite loop (same address twice in a row)
        if (addr == prevAddr)
        {
            qWarning() << "Infinite loop detected at address:" << QString("%1").arg(addr, 4, 16, QChar('0'));
            break;
        }
        prevAddr = addr;

        // Check if we've already processed this address
        if (m_instructions.contains(addr))
        {
            // Move to next instruction
            const auto& existingInstr = m_instructions[addr];
            uint16_t nextAddr =
                addr + (existingInstr.instructionBytes.empty() ? 1 : existingInstr.instructionBytes.size());

            // Prevent getting stuck on invalid instructions
            if (nextAddr <= addr)
            {
                qWarning() << "Invalid instruction size at address:" << QString("%1").arg(addr, 4, 16, QChar('0'));
                nextAddr = addr + 1;
            }

            addr = nextAddr;
            instructionCount++;
            continue;
        }

        // Read up to 4 bytes for the instruction
        uint8_t buffer[4] = {0};
        size_t bytesToRead = std::min(static_cast<size_t>(4), static_cast<size_t>(0x10000 - addr));

        try
        {
            // Read bytes from memory
            for (size_t i = 0; i < bytesToRead; i++)
            {
                buffer[i] = memory->DirectReadFromZ80Memory(addr + i);
            }

            // Disassemble the instruction
            uint8_t commandLen = 0;
            DecodedInstruction instr;

            // Disassemble with the correct method signature
            std::string disasm = disassembler->disassembleSingleCommandWithRuntime(buffer,          // buffer
                                                                                   sizeof(buffer),  // bufferSize
                                                                                   &commandLen,     // commandLen (out)
                                                                                   nullptr,         // pSymbolsProvider
                                                                                   memory,          // memory
                                                                                   &instr  // pInstruction (out)
            );

            // If we got a valid instruction
            if (commandLen > 0 && commandLen <= 4)
            {
                // Copy the actual bytes for display
                instr.instructionBytes.assign(buffer, buffer + commandLen);
                instr.mnemonic = disasm;

                // Add to our map
                m_instructions[addr] = instr;

                qDebug() << "Disassembled at" << QString("%1").arg(addr, 4, 16, QChar('0')) << ":"
                         << QString::fromStdString(instr.mnemonic) << "\"" << QString::fromStdString(instr.annotation)
                         << "\"";

                // Move to next instruction
                addr += commandLen;
                instructionCount++;
            }
            else
            {
                // Handle failed disassembly
                qWarning() << "Failed to disassemble at" << QString("%1").arg(addr, 4, 16, QChar('0')) << "(byte: 0x"
                           << QString::number(buffer[0], 16) << ")";

                // Create a DB instruction for the first byte
                DecodedInstruction byteInstr;
                byteInstr.isValid = true;
                byteInstr.instructionBytes.push_back(buffer[0]);
                byteInstr.fullCommandLen = 1;
                byteInstr.instructionAddr = addr;
                byteInstr.mnemonic = "db " + StringHelper::ToHex(buffer[0], true);

                m_instructions[addr] = byteInstr;
                addr++;
                instructionCount++;
            }

            // Additional safety checks
            if (addr == 0xFFFF)
            {
                qDebug() << "Reached end of memory";
                break;
            }
        }
        catch (const std::exception& e)
        {
            qCritical() << "Exception during disassembly at" << QString("%1").arg(addr, 4, 16, QChar('0')) << ":"
                        << e.what();
            addr++;  // Move to next byte to prevent infinite loop
            instructionCount++;
        }

        // Safety check to prevent address wrap-around or excessive instructions
        if (addr < loadStart || instructionCount >= MAX_INSTRUCTIONS)
        {
            if (instructionCount >= MAX_INSTRUCTIONS)
            {
                qWarning() << "Reached maximum instruction limit, stopping disassembly";
            }
            else if (addr < loadStart)
            {
                qWarning() << "Address wrap-around detected, stopping disassembly";
            }
            break;
        }
    }

    // Notify views that data has changed
    emit dataChanged(index(start, 0), index(end, columnCount() - 1));
}
