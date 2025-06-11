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

/// @brief Constructs a DisassemblerTableModel with the given parent and emulator
/// @param emulator Pointer to the emulator instance
/// @param parent Parent QObject
DisassemblerTableModel::DisassemblerTableModel(Emulator* emulator, QObject* parent)
    : QAbstractTableModel(parent), m_emulator(emulator), m_currentPC(0), m_visibleStart(0), m_visibleEnd(0x1FF)
{
    // Initialize headers
    m_headers << "Address" << "Opcode" << "Label" << "Mnemonic" << "Annotation" << "Comment";

    // Notify the view that the model has been reset
    beginResetModel();

    // Load initial range if we have an emulator
    if (m_emulator)
    {
        setVisibleRange(m_visibleStart, m_visibleEnd);
    }

    endResetModel();

    qDebug() << "DisassemblerTableModel initialized with" << m_headers.size() << "columns";
}

/// @brief Destroys the DisassemblerTableModel
/// Cleans up any resources used by the model
DisassemblerTableModel::~DisassemblerTableModel()
{
    // Clear the emulator reference to prevent any access during destruction
    m_emulator = nullptr;
    m_decodedInstructionsCache.clear();
}

/// @brief Resets the disassembly model
/// Clears the cache and reloads the current visible range
void DisassemblerTableModel::reset()
{
    beginResetModel();
    m_decodedInstructionsCache.clear();
    m_visibleStart = 0;
    m_visibleEnd = 0x01FF;
    m_currentPC = 0;
    endResetModel();
}

/// @brief Refreshes the disassembly view by clearing and reloading the cache
/// This method is called when the emulator state changes and we need to update the disassembly
void DisassemblerTableModel::refresh()
{
    qDebug() << "DisassemblerTableModel::refresh() called";

    if (!m_emulator)
    {
        beginResetModel();
        m_decodedInstructionsCache.clear();
        endResetModel();
        return;
    }

    // Store current PC before refresh
    uint16_t currentPC = m_currentPC;

    // Clear the cache and reset the model
    beginResetModel();
    m_decodedInstructionsCache.clear();
    endResetModel();

    // If we don't have a valid PC yet, just return
    if (currentPC == 0xFFFF)
    {
        qDebug() << "No valid PC set, skipping disassembly";
        return;
    }

    // Force a full reload by clearing the cache
    m_decodedInstructionsCache.clear();

    // Calculate the range to load - centered around the current PC
    const uint16_t RANGE = 0x100;  // 256 bytes before and after PC
    uint16_t loadStart = (currentPC > RANGE) ? (currentPC - RANGE) : 0;
    uint16_t loadEnd = (currentPC < (0xFFFF - RANGE)) ? (currentPC + RANGE) : 0xFFFF;

    // Begin model reset for the actual data loading
    beginResetModel();
    
    // Load disassembly for the range
    loadDisassemblyRange(loadStart, loadEnd, currentPC);

    // End model reset
    endResetModel();

    qDebug() << "DisassemblerTableModel::refresh() completed with" << m_decodedInstructionsCache.size()
             << "instructions. Visible range: 0x" << QString::number(m_visibleStart, 16) << " to 0x"
             << QString::number(m_visibleEnd, 16);
}

/// @brief Sets the emulator instance to be used for disassembly
/// @param emulator Pointer to the emulator instance
/// This will clear the current cache and reload the disassembly for the new emulator
void DisassemblerTableModel::setEmulator(Emulator* emulator)
{
    // If setting to the same emulator, do nothing
    if (m_emulator == emulator)
    {
        return;
    }

    qDebug() << "setEmulator called with emulator:" << (emulator != nullptr);

    beginResetModel();
    // Clear existing data before changing the emulator
    m_decodedInstructionsCache.clear();
    m_emulator = emulator;
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
    if (m_emulator->GetContext() && m_emulator->GetContext()->pDebugManager)
    {
        qDebug() << "Setting initial visible range:" << QString::number(m_visibleStart, 16) << "to"
                 << QString::number(m_visibleEnd, 16);
        setVisibleRange(m_visibleStart, m_visibleEnd);
    }
    else
    {
        qWarning() << "Emulator context or debug manager not available";
        // Still need to set the visible range to update the view
        emit dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, columnCount() - 1));
    }
}

/// @brief Returns the number of rows in the model
/// @param parent Parent model index (unused)
/// @return Number of rows in the visible range
int DisassemblerTableModel::rowCount(const QModelIndex& /*parent*/) const
{
    return m_decodedInstructionsCache.size();
}

/// @brief Returns the number of columns in the model
/// @param parent Parent model index (unused)
/// @return Number of columns (fixed at 6: Address, Opcode, Label, Mnemonic, Annotation, Comment)
int DisassemblerTableModel::columnCount(const QModelIndex& /*parent*/) const
{
    return m_headers.size();
}

/// @brief Returns the data for the given role at the specified index
/// @param index Model index to get data for
/// @param role Qt item data role
/// @return QVariant containing the requested data
QVariant DisassemblerTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        qDebug() << "Invalid index requested";
        return QVariant();
    }

    if (!m_emulator)
    {
        qDebug() << "No emulator available for data request";
        return QVariant();
    }

    // Get the instruction for this row
    if (index.row() < 0 || index.row() >= m_decodedInstructionsCache.size())
    {
        qDebug() << "Row out of range:" << index.row() << "size:" << m_decodedInstructionsCache.size();
        return QVariant();
    }

    // Protection against empty cache or invalid row
    if (m_decodedInstructionsCache.isEmpty() || index.row() < 0 || index.row() >= m_decodedInstructionsCache.size())
    {
        qDebug() << "Invalid cache access - empty or row out of range. Row:" << index.row() 
                 << "Cache size:" << m_decodedInstructionsCache.size();
        return QVariant();
    }

    // Get the instruction at the current row
    auto it = m_decodedInstructionsCache.begin();
    try 
    {
        std::advance(it, index.row());
        if (it == m_decodedInstructionsCache.end())
        {
            qDebug() << "Failed to advance iterator to row:" << index.row();
            return QVariant();
        }
    }
    catch (const std::exception& e)
    {
        qCritical() << "Exception advancing iterator:" << e.what() << "Row:" << index.row() 
                   << "Cache size:" << m_decodedInstructionsCache.size();
        return QVariant();
    }

    const uint16_t addr = it.key();
    const auto& instr = it.value();

    if (role == Qt::UserRole)
    {
        return addr;  // Return the address for internal use
    }
    else if (role == Qt::BackgroundRole)
    {
        // Highlight the current PC row with a light yellow background
        return addr == m_currentPC ? QBrush(QColor(255, 255, 200)) : QVariant();
    }
    else if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        QString result;
        switch (index.column())
        {
            case 0:  // Address
                result = QString("%1").arg(addr, 4, 16, QChar('0')).toUpper();
                break;

            case 1:  // Opcode
            {
                std::ostringstream oss;
                for (size_t i = 0; i < instr.instructionBytes.size(); i++)
                {
                    if (i > 0)
                        oss << " ";
                    oss << StringHelper::ToHex(instr.instructionBytes[i], true);
                }
                result = QString::fromStdString(oss.str());
                break;
            }

            case 2:  // Label
                result = QString::fromStdString(instr.label);
                break;

            case 3:  // Mnemonic
                result = QString::fromStdString(instr.mnemonic);
                break;

            case 4:  // Annotation
                result = QString::fromStdString(instr.annotation);
                break;

            case 5:  // Comment
                result = QString::fromStdString(instr.comment);
                break;

            default:
                qDebug() << "Invalid column requested:" << index.column();
                return QVariant();
        }

        //        qDebug() << "Data requested for row:" << index.row()
        //                 << "col:" << index.column()
        //                 << "addr:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
        //                 << "value:" << result;

        return result;
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

/// @brief Returns the header data for the given section and orientation
/// @param section Section (column) index
/// @param orientation Header orientation (horizontal or vertical)
/// @param role Qt item data role
/// @return QVariant containing the header data
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

/// @brief Returns the item flags for the given index
/// @param index Model index to get flags for
/// @return Qt::ItemFlags for the item
Qt::ItemFlags DisassemblerTableModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

/// @brief Updates the current program counter (PC) and ensures its visibility in the disassembly view
/// @param pc The new program counter value to set
///
/// This method performs the following actions:
/// 1. Updates the internal PC state if it has changed
/// 2. Checks if the new PC is within the currently cached instructions
/// 3. If PC is not in cache, triggers a reload of the disassembly range centered on the PC
/// 4. If PC is near the edge of the current range, expands the visible range to keep PC centered
/// 5. Updates the view to highlight the current instruction
///
/// Events triggered:
/// - dataChanged(): When the old or new PC instructions need visual updates
/// - currentPCChanged(pc): After PC is updated, with the new PC value
/// - May trigger setVisibleRange() which emits layoutChanged() if the visible range changes
///
/// The method ensures smooth scrolling by maintaining a buffer of instructions around the PC
/// and only reloading when necessary for performance.
void DisassemblerTableModel::setCurrentPC(uint16_t pc)
{
    if (!m_emulator)
        return;
    if (pc == m_currentPC)
        return;

    uint16_t oldPC = m_currentPC;
    m_currentPC = pc;

    // Find rows for old and new PC to update highlighting
    int oldRow = findRowForAddress(oldPC);
    int newRow = findRowForAddress(pc);

    // Check if new PC is in our cache
    bool pcInCache = (newRow != -1);

    if (!pcInCache)
    {
        // PC not in cache, we need to reload
        qDebug() << "PC address 0x" << QString::number(pc, 16) << " not found in cache. Refreshing cache.";

        // Calculate a new range centered around the PC
        uint16_t rangeHalfSize = 0x100;
        uint16_t visibleStart = (pc >= rangeHalfSize) ? (pc - rangeHalfSize) : 0;
        uint16_t visibleEnd = (pc <= (0xFFFF - rangeHalfSize)) ? (pc + rangeHalfSize) : 0xFFFF;

        // Set the new visible range - this will trigger loadDisassemblyRange
        // which will load the new range into the cache
        setVisibleRange(visibleStart, visibleEnd);

        // Find the row again after the range is updated
        newRow = findRowForAddress(pc);
    }
    else
    {
        // PC is in cache, check if we're close to the boundaries
        uint16_t cacheStart = m_decodedInstructionsCache.firstKey();
        uint16_t cacheEnd = m_decodedInstructionsCache.lastKey();

        // Calculate the middle of the current cache
        uint16_t cacheMid = cacheStart + (cacheEnd - cacheStart) / 2;

        // If PC is in the first or last quarter of the cache, expand in that direction
        if (pc < cacheStart + (cacheMid - cacheStart) / 2 || pc > cacheMid + (cacheEnd - cacheMid) / 2)
        {
            // Calculate how much to expand (half the current range)
            uint16_t expandSize = (cacheEnd - cacheStart) / 2;
            uint16_t newStart = (pc > expandSize) ? (pc - expandSize) : 0;
            uint16_t newEnd = (pc < (0xFFFF - expandSize)) ? (pc + expandSize) : 0xFFFF;

            qDebug() << "PC is near cache boundary, expanding range to center it";

            // Update the visible range data in decoded instructions cache
            setVisibleRange(newStart, newEnd);
        }
    }

    // Update highlighting for old and new PC rows if they're visible
    if (oldRow != -1)
    {
        emit dataChanged(index(oldRow, 0), index(oldRow, columnCount() - 1));
    }

    if (newRow != -1)
    {
        emit dataChanged(index(newRow, 0), index(newRow, columnCount() - 1));
    }

    // Emit a signal that the PC value itself has changed
    emit currentPCChanged(pc);
}

/// @brief Sets the visible address range for the disassembly view
/// @param start Starting address of the visible range
/// @param end Ending address of the visible range
/// This will trigger loading of the specified range into the cache
void DisassemblerTableModel::setVisibleRange(uint16_t start, uint16_t end)
{
    qDebug() << "DisassemblerTableModel::setVisibleRange called with start: 0x" << QString::number(start, 16)
             << " end: 0x" << QString::number(end, 16);
    // Basic validation for the input range
    if (start > end)
    {
        qWarning() << "setVisibleRange: start address 0x" << QString::number(start, 16)
                   << " is greater than end address 0x" << QString::number(end, 16) << ". Clamping start to end.";
        start = end;  // Or handle error appropriately, e.g., by returning or throwing.
    }

    // Calculate the middle of the range - likely to be the PC address we're interested in
    uint16_t midPoint = start + (end - start) / 2;

    // Optimization: If the requested visible range is identical to the current one,
    // and the cache is not empty, we might avoid a full reset.
    // However, we must ensure that the key addresses (especially the middle of the range) are in the cache
    if (start == m_visibleStart && end == m_visibleEnd && !m_decodedInstructionsCache.isEmpty())
    {
        // Check if the middle address (likely the PC) is in the cache
        if (m_decodedInstructionsCache.contains(midPoint))
        {
            qDebug() << "DisassemblerTableModel::setVisibleRange: Range unchanged and cache contains key address 0x"
                     << QString::number(midPoint, 16) << ". Skipping full reset.";
            return;
        }
        else
        {
            qDebug() << "DisassemblerTableModel::setVisibleRange: Range unchanged but key address 0x"
                     << QString::number(midPoint, 16) << " not in cache. Forcing reload.";
        }
    }

    qDebug() << "DisassemblerTableModel: Updating visible range from 0x" << QString::number(m_visibleStart, 16) << "-0x"
             << QString::number(m_visibleEnd, 16) << " to 0x" << QString::number(start, 16) << "-0x"
             << QString::number(end, 16);

    // --- Critical section for model update ---
    beginResetModel();  // Signal views that the model is about to be drastically changed.

    m_visibleStart = start;
    m_visibleEnd = end;

    // We'll only clear the cache if we actually need to load new instructions
    // This prevents issues where we clear the cache but fail to populate it
    bool shouldClearCache = true;

    // If the PC is in the current range, make sure we don't lose it
    if (m_decodedInstructionsCache.contains(m_currentPC))
    {
        qDebug() << "Current PC 0x" << QString::number(m_currentPC, 16) << " is in cache before range update";
    }

    // Calculate the actual range to load into the cache, including padding.
    // The padding helps with smoother scrolling as the user approaches the edges of the visible area.
    // The user's previous code (from the diff) used a PADDING of 0x100.
    const uint16_t PADDING = 0x100;
    uint16_t loadStart = (m_visibleStart > PADDING) ? (m_visibleStart - PADDING) : 0;
    uint16_t loadEnd = (m_visibleEnd < (0xFFFF - PADDING)) ? (m_visibleEnd + PADDING) : 0xFFFF;

    // If the current PC is valid, ensure we load enough instructions around it
    // to keep it centered in the visible area
    if (m_currentPC != 0xFFFF)
    {
        // Calculate a range centered on the PC with enough padding for table height
        uint16_t pcCenteredStart = (m_currentPC > PADDING) ? (m_currentPC - PADDING) : 0;
        uint16_t pcCenteredEnd = (m_currentPC < (0xFFFF - PADDING)) ? (m_currentPC + PADDING) : 0xFFFF;

        // Expand our load range to include the PC-centered range
        loadStart = std::min(loadStart, pcCenteredStart);
        loadEnd = std::max(loadEnd, pcCenteredEnd);

        qDebug() << "Expanded load range to center PC 0x" << QString::number(m_currentPC, 16) << ": 0x"
                 << QString::number(loadStart, 16) << " - 0x" << QString::number(loadEnd, 16);
    }

    // Sanity check for padding calculation
    if (loadStart > loadEnd)
    {
        qWarning() << "setVisibleRange: loadStart 0x" << QString::number(loadStart, 16) << " is greater than loadEnd 0x"
                   << QString::number(loadEnd, 16) << ". Clamping loadStart.";
        loadStart = loadEnd;
    }

    qDebug() << "DisassemblerTableModel: Loading disassembly range 0x" << QString::number(loadStart, 16) << " - 0x"
             << QString::number(loadEnd, 16);

    if (m_emulator)
    {  // Only attempt to load if emulator is valid
        // Only clear the cache right before loading, not earlier
        if (shouldClearCache)
        {
            // Clear the cache before loading new data
            m_decodedInstructionsCache.clear();
            m_visibleStart = loadStart;
            m_visibleEnd = loadEnd;
        }

        // This will populate m_decodedInstructionsCache
        loadDisassemblyRange(loadStart, loadEnd);

        // Verify the cache was populated
        if (m_decodedInstructionsCache.isEmpty())
        {
            qWarning() << "Cache is still empty after loadDisassemblyRange (" << loadStart << " - " << loadEnd
                       << "). Trying direct disassembly.";
        }
        // If after loading, the cache is still empty or has too few instructions, try direct disassembly
        if (m_decodedInstructionsCache.isEmpty() || m_decodedInstructionsCache.size() < 30)
        {
            qDebug() << "Cache has too few instructions (" << m_decodedInstructionsCache.size()
                     << ") after loadDisassemblyRange. Trying direct disassembly.";
            loadDisassemblyRange(loadStart, loadEnd);
        }

        // If the PC should be in range but isn't in the cache, force add it
        if (m_currentPC >= loadStart && m_currentPC <= loadEnd && !m_decodedInstructionsCache.contains(m_currentPC))
        {
            qDebug() << "PC 0x" << QString::number(m_currentPC, 16)
                     << " should be in range but isn't in cache. Forcing add.";

            // Create a special instruction for the PC
            DecodedInstruction pcInstr;
            pcInstr.isValid = true;
            pcInstr.instructionAddr = m_currentPC;
            pcInstr.fullCommandLen = 1;

            // Try to read the byte at PC
            try
            {
                if (m_emulator && m_emulator->GetContext() && m_emulator->GetContext()->pMemory)
                {
                    uint8_t byte = m_emulator->GetContext()->pMemory->DirectReadFromZ80Memory(m_currentPC);
                    pcInstr.instructionBytes.push_back(byte);
                    pcInstr.mnemonic = "db " + StringHelper::ToHex(byte, true) + " (forced PC)";
                }
                else
                {
                    pcInstr.mnemonic = "??? (forced PC)";
                }
            }
            catch (...)
            {
                pcInstr.mnemonic = "??? (forced PC)";
            }

            // Add to cache
            m_decodedInstructionsCache[m_currentPC] = pcInstr;
        }
    }
    else
    {
        qWarning() << "DisassemblerTableModel::setVisibleRange: Emulator is null, cannot load disassembly range.";
        // The cache is already cleared, and endResetModel() will be called, resulting in an empty view.
    }

    endResetModel();  // Signal views that the model has been reset and they should refetch all data.
    // --- End critical section ---

    qDebug() << "DisassemblerTableModel::setVisibleRange completed. Cache size:" << m_decodedInstructionsCache.size()
             << ". Visible range: 0x" << QString::number(m_visibleStart, 16) << " to 0x"
             << QString::number(m_visibleEnd, 16);

    // The calls to dataChanged and layoutChanged that were previously here:
    // emit dataChanged(createIndex(0, 0), createIndex(rowCount() - 1, columnCount() - 1));
    // emit layoutChanged();
    // are no longer needed, as beginResetModel()/endResetModel() correctly notify the view of a full reset.
}

/// @brief Maps a memory address to its corresponding row in the disassembly view
/// @param address The memory address to locate in the disassembly
/// @return The row number (0-based) if found, or -1 if the address is not in the visible range
///
/// This method performs a fast lookup to find which instruction in the disassembly
/// corresponds to the given memory address. It handles several cases:
/// 1. Exact match: The address is the start of an instruction
/// 2. Contained within: The address falls within a multi-byte instruction
/// 3. Not found: The address is not part of any disassembled instruction
///
/// The search uses a binary search (O(log n) complexity) through the cached instructions.
/// The method is optimized for performance as it's called frequently during stepping
/// and scrolling operations.
///
/// Note: The returned row number corresponds to the position in the current
/// visible range, not the absolute instruction address. Use getRowForAbsoluteAddress()
/// if you need to map between view rows and instruction addresses.
int DisassemblerTableModel::findRowForAddress(uint16_t address) const
{
    int result = -1;

    if (!m_decodedInstructionsCache.isEmpty())
    {
        // Find the first instruction that starts at or after our target address
        auto it = m_decodedInstructionsCache.lowerBound(address);

        // Check for exact match
        if (it != m_decodedInstructionsCache.constEnd() && it.key() == address)
        {
            result = std::distance(m_decodedInstructionsCache.constBegin(), it);
        }
        // Handle case when address is before the first instruction
        else if (it == m_decodedInstructionsCache.constBegin())
        {
            result = (it.key() == address) ? 0 : -1;
        }
        // Check if address is within the previous instruction's range
        else
        {
            --it;  // Move to the previous instruction
            uint16_t instrStart = it.key();
            uint16_t instrSize = it.value().instructionBytes.size();
            instrSize = (instrSize == 0) ? 1 : instrSize;  // Minimum instruction size

            if (address >= instrStart && address < instrStart + instrSize)
            {
                result = std::distance(m_decodedInstructionsCache.constBegin(), it);
            }
        }
    }

    return result;
}

/// @brief Loads and disassembles the specified memory range if needed
/// @param start Starting address of the range to disassemble (inclusive)
/// @param end Ending address of the range to disassemble (inclusive)
///
/// This method performs the actual disassembly of the specified memory range
/// and populates the instruction cache with the results. It first checks if the
/// range is already loaded in the cache and only performs disassembly if needed.
///
/// The method handles edge cases including:
/// - Invalid or reversed address ranges
/// - Memory access errors during disassembly
/// - Cache population and management
void DisassemblerTableModel::loadDisassemblyRange(uint16_t start, uint16_t end, std::optional<uint16_t> pc)
{
    if (start > end)
    {
        qWarning() << "loadDisassemblyRange: Invalid range: start > end";
        return;
    }

    qDebug() << "loadDisassemblyRange called with start: 0x" << StringHelper::Format("%04X", start).c_str() << "end: 0x"
             << StringHelper::Format("%04X", end).c_str() << "(range size: " << (end - start + 1) << " bytes)";

    // Clear existing instructions if no emulator is available
    if (!m_emulator)
    {
        qDebug() << "No emulator available, clearing disassembly";
        beginResetModel();
        m_decodedInstructionsCache.clear();
        endResetModel();
        return;
    }

    // If PC is provided and within range, we'll handle it specially
    if (pc && *pc > start && *pc <= end)
    {
        // First disassemble from PC forward till end address
        disassembleForward(*pc, end);
        // Then disassemble backward from PC to start address
        disassembleBackward(*pc, start);
    }
    else
    {
        // Just disassemble the full range normally
        disassembleForward(start, end);
    }

    // Update the visible range to include the newly disassembled area
    m_visibleStart = std::min(m_visibleStart, start);
    m_visibleEnd = std::max(m_visibleEnd, end);

    qDebug() << StringHelper::Format("Disassembly complete. Cache size: %d, Visible range: 0x%04X-0x%04X",
                                     m_decodedInstructionsCache.size(), m_visibleStart, m_visibleEnd)
                    .c_str();
}

/// @brief Disassembles a range of memory addresses
/// @param start The starting address of the range to disassemble (inclusive)
/// @param end The ending address of the range to disassemble (inclusive)
///
/// @note start must be < end
void DisassemblerTableModel::disassembleForward(uint16_t start, uint16_t end)
{
    assert(start < end);

    qDebug() << StringHelper::Format("Disassembling forward from 0x%04X to 0x%04X", start, end).c_str();

    auto* disassembler = m_emulator->GetContext()->pDebugManager->GetDisassembler().get();
    auto* memory = m_emulator->GetMemory();

    uint16_t addr = start;

    while (addr <= end && addr >= start)
    {
        DecodedInstruction decodedInstruction = disassembleAt(addr, disassembler, memory);
        addr += decodedInstruction.fullCommandLen;
    }
}

/// @brief Disassembles instructions backward from the given PC address
/// @param pc The PC address to start disassembling from (exclusive)
/// @param end The ending address to stop disassembling at (inclusive)
///
/// This method disassembles instructions backward from PC-1 until it reaches the end address.
/// It handles multi-byte instructions by moving back by the instruction length after each disassembly.
void DisassemblerTableModel::disassembleBackward(uint16_t pc, uint16_t end)
{
    assert(pc > end);

    auto* disassembler = m_emulator->GetContext()->pDebugManager->GetDisassembler().get();
    auto* memory = m_emulator->GetMemory();

    qDebug() << StringHelper::Format("Disassembling backward from PC=0x%04X to 0x%04X", pc - 1, end).c_str();

    uint16_t addr = pc - 1;           // Start from the byte before PC
    while (addr >= end && addr < pc)  // Stop when we hit the end address or wrap around
    {
        DecodedInstruction decodedInstruction = disassembleAt(addr, disassembler, memory);
        addr -= decodedInstruction.fullCommandLen;

        // Protection against falling out of the address range
        addr = max(addr, end);
    }
}

DecodedInstruction DisassemblerTableModel::disassembleAt(uint16_t addr, Z80Disassembler* disassembler, Memory* memory)
{
    Z80Registers* registers = m_emulator->GetContext()->pCore->GetZ80();

    uint8_t buffer[Z80Disassembler::MAX_INSTRUCTION_LENGTH] = {0};
    size_t bytesToRead = std::min((size_t)(Z80Disassembler::MAX_INSTRUCTION_LENGTH), (size_t)(0x10000 - addr));

    try
    {
        // Read bytes from memory
        for (size_t i = 0; i < bytesToRead; i++)
        {
            buffer[i] = memory->DirectReadFromZ80Memory(addr + i);
        }

        // Disassemble the instruction
        uint8_t commandLen = 0;
        DecodedInstruction decodedInstruction;

        std::string disasm = disassembler->disassembleSingleCommandWithRuntime(buffer, sizeof(buffer), addr, &commandLen,
                                                                               registers, memory, &decodedInstruction);

        if (commandLen > 0 && commandLen <= Z80Disassembler::MAX_INSTRUCTION_LENGTH)
        {
            decodedInstruction.instructionBytes.assign(buffer, buffer + commandLen);
            decodedInstruction.mnemonic = disasm;
            decodedInstruction.instructionAddr = addr;
            decodedInstruction.fullCommandLen = commandLen;
            decodedInstruction.isValid = true;

            // Cache the instruction
            m_decodedInstructionsCache[addr] = decodedInstruction;
            qDebug() << StringHelper::Format("Disassembled at 0x%04X: %s %s | %s | %s",
                addr, 
                decodedInstruction.label.empty() ? "" : "[" + decodedInstruction.label + "[",
                decodedInstruction.mnemonic.c_str(),
                decodedInstruction.annotation.empty() ? "" : decodedInstruction.annotation.c_str()).c_str();
            return decodedInstruction;
        }
    }
    catch (const std::exception& e)
    {
        qCritical() << "Exception during disassembly at 0x" << StringHelper::Format("%04X", addr).c_str() << ":"
                    << e.what();
    }

    // If we get here, disassembly failed - create a DB instruction
    DecodedInstruction byteInstruction;
    byteInstruction.isValid = true;
    byteInstruction.instructionBytes.push_back(buffer[0]);
    byteInstruction.fullCommandLen = 1;
    byteInstruction.instructionAddr = addr;
    byteInstruction.mnemonic = StringHelper::Format("db 0x%02X", buffer[0]);

    m_decodedInstructionsCache[addr] = byteInstruction;
    return byteInstruction;
}

/// @brief Dumps the current state of the disassembly model to the debug output
/// Shows all cached instructions with their addresses and mnemonics
///
/// This method is useful for debugging purposes, allowing developers to inspect
/// the current state of the disassembly model.
void DisassemblerTableModel::dumpState() const
{
    qDebug() << "=== DisassemblerTableModel State ===";
    qDebug() << "Emulator:" << (m_emulator ? "Valid" : "Null");
    qDebug() << "Instructions count:" << m_decodedInstructionsCache.size();
    qDebug() << "Visible range:" << QString("0x%1 - 0x%2").arg(m_visibleStart, 4, 16, QChar('0'))
             << "(size:" << (m_visibleEnd - m_visibleStart + 1) << ")";
    qDebug() << "Current PC: 0x" << QString::number(m_currentPC, 16);

    // Dump first few instructions
    int count = 0;
    qDebug() << "Last 5 instructions:";
    for (auto it = m_decodedInstructionsCache.end(); it != m_decodedInstructionsCache.begin() && count < 5;)
    {
        --it;
        const auto& instr = it.value();
        qDebug() << "  0x" << QString::number(it.key(), 16) << ":" << QString::fromStdString(instr.mnemonic) << "("
                 << QString::fromStdString(instr.annotation) << ")";
        count++;
    }
    qDebug() << "===============================";
}