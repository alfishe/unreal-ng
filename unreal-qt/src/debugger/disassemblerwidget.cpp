#include "disassemblerwidget.h"

#include <QHeaderView>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTableView>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "disassemblertablemodel.h"
#include "emulator/emulator.h"
#include "ui_disassemblerwidget.h"

DisassemblerWidget::DisassemblerWidget(QWidget* parent)
    : QWidget(parent),
      ui(new Ui::DisassemblerWidget),
      m_model(nullptr),
      m_emulator(nullptr),
      m_displayAddress(0),
      m_currentPC(0),
      m_updatingView(false),
      m_isDragging(false),
      m_scrollMode(ScrollMode::Command)
{
    ui->setupUi(this);
    initializeTable();
}

DisassemblerWidget::~DisassemblerWidget()
{
    delete ui;
}

void DisassemblerWidget::initializeTable()
{
    qDebug() << "Initializing disassembler table...";

    // Get the table view from UI
    QTableView* tableView = ui->disassemblyTable;

    // Create and set the model
    m_model = new DisassemblerTableModel(m_emulator, this);

    // Set the model first
    tableView->setModel(m_model);

    // Configure the view
    tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    tableView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tableView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Configure header
    QHeaderView* header = tableView->horizontalHeader();

    // Set resize modes for each column
    header->setSectionResizeMode(0, QHeaderView::Interactive);  // Address (16-bit hex $XXXX)
    header->setSectionResizeMode(1, QHeaderView::Interactive);  // Opcode (1-4 bytes hex)
    header->setSectionResizeMode(2, QHeaderView::Interactive);  // Label (text)
    header->setSectionResizeMode(3, QHeaderView::Interactive);  // Mnemonic (text)
    header->setSectionResizeMode(4, QHeaderView::Interactive);  // Annotation (text)
    header->setSectionResizeMode(5, QHeaderView::Stretch);      // Comment (text)

    // Set initial column widths
    tableView->setColumnWidth(0, 64);   // Address (fixed width for 16-bit hex)
    tableView->setColumnWidth(1, 112);  // Opcode (fixed width for 1-4 bytes hex)
    tableView->setColumnWidth(2, 112);  // Label (text, allow room for longer labels)
    tableView->setColumnWidth(3, 112);  // Mnemonic (text, needs most space)
    tableView->setColumnWidth(4, 112);  // Annotation (text, allow room for annotations)
    // Comments column will take remaining space
    
    // Configure table properties
    tableView->setAlternatingRowColors(true);
    tableView->setShowGrid(true);

    // Disable drag and drop
    tableView->setDragDropMode(QAbstractItemView::NoDragDrop);
    tableView->setDragEnabled(false);
    tableView->setDropIndicatorShown(false);

    // Configure headers
    header->setSectionsMovable(false);
    header->setStretchLastSection(true);  // Make the last column take remaining space

    // Configure vertical header
    QHeaderView* vHeader = tableView->verticalHeader();
    vHeader->setVisible(false);  // Hide vertical header
    vHeader->setDefaultSectionSize(18);
    vHeader->setMinimumSectionSize(1);

    // Connect signals
    connect(tableView->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &DisassemblerWidget::onCurrentRowChanged);

    qDebug() << "Table initialization complete, visible columns:" << tableView->horizontalHeader()->count();

    // If we have an emulator, update the view
    if (m_emulator)
    {
        qDebug() << "Initial update for emulator";
        updateVisibleRange();
    }
}

void DisassemblerWidget::setDisassemblerAddress(uint16_t pc)
{
    // Validate input and state
    if (pc >= MAX_ADDRESS || !m_model || !ui || !ui->disassemblyTable)
    {
        qDebug() << "Invalid parameters in setDisassemblerAddress";
        return;
    }

    qDebug() << "setDisassemblerAddress: 0x" << QString::number(pc, 16);

    // Update the current PC
    m_currentPC = pc;

    // Block signals temporarily to prevent recursive updates
    bool oldState = ui->disassemblyTable->blockSignals(true);

    try
    {
        // Update the model's current PC
        m_model->setCurrentPC(pc);

        // Calculate a range around the PC
        const uint16_t rangeSize = 0x100;  // 256 bytes range
        uint16_t start = (pc > rangeSize / 2) ? (pc - rangeSize / 2) : 0;
        uint16_t end = (pc < (0xFFFF - rangeSize / 2)) ? (pc + rangeSize / 2) : 0xFFFF;

        qDebug() << "Setting visible range to: 0x" << QString::number(start, 16) << " - 0x" << QString::number(end, 16);

        // Set the visible range in the model
        m_model->setVisibleRange(start, end);

        // Find the row that contains the PC
        int targetRow = -1;
        for (int row = 0; row < m_model->rowCount(); ++row)
        {
            QModelIndex idx = m_model->index(row, 0);
            QVariant addrData = m_model->data(idx, Qt::UserRole);
            if (addrData.isValid() && addrData.toUInt() == pc)
            {
                targetRow = row;
                break;
            }
        }

        // If we found the row, scroll to it
        if (targetRow >= 0)
        {
            QModelIndex idx = m_model->index(targetRow, 0);

            // Update the selection
            QItemSelectionModel* selectionModel = ui->disassemblyTable->selectionModel();
            if (selectionModel)
            {
                selectionModel->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows |
                                                QItemSelectionModel::Current);

                // Make sure the current index is set
                ui->disassemblyTable->setCurrentIndex(idx);
            }

            // Scroll to the address
            ui->disassemblyTable->scrollTo(idx, QAbstractItemView::PositionAtCenter);
            qDebug() << "Scrolled to row" << targetRow << "for PC 0x" << QString::number(pc, 16);
        }
        else
        {
            qDebug() << "Could not find row for PC 0x" << QString::number(pc, 16);
            // If we couldn't find the PC, just refresh the view
            m_model->refresh();
        }

        // Force an update of the view
        ui->disassemblyTable->viewport()->update();

        // Notify any connected components about the address change
        emit addressSelected(pc);
    }
    catch (const std::exception& e)
    {
        qWarning() << "Error in setDisassemblerAddress:" << e.what();
    }
    catch (...)
    {
        qWarning() << "Unknown error in setDisassemblerAddress";
    }

    // Restore signal blocking state
    ui->disassemblyTable->blockSignals(oldState);
}

void DisassemblerWidget::reset()
{
    if (m_model)
    {
        m_model->reset();
        updateVisibleRange();
    }
}

void DisassemblerWidget::refresh()
{
    if (!m_emulator || !m_model)
    {
        return;
    }
    qDebug() << "Refreshing disassembler widget";

    // Get the current scroll position
    QTableView* tableView = ui->disassemblyTable;
    int scrollValue = tableView->verticalScrollBar()->value();

    // Refresh the model
    m_model->refresh();

    // Force a full view update
    tableView->viewport()->update();
    tableView->updateGeometry();

    // Restore the scroll position
    tableView->verticalScrollBar()->setValue(scrollValue);

    // Ensure the current PC is visible
    if (m_currentPC != 0)
    {
        setDisassemblerAddress(m_currentPC);
    }

    qDebug() << "Refresh complete, column count:" << tableView->model()->columnCount();
}

void DisassemblerWidget::goToAddress(uint16_t address)
{
    setDisassemblerAddress(address);
}

void DisassemblerWidget::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() != 0)
    {
        QScrollBar* scrollBar = ui->disassemblyTable->verticalScrollBar();
        scrollBar->setValue(scrollBar->value() - event->angleDelta().y() / 120);
    }
}

void DisassemblerWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragStartPos = event->pos();
        m_isDragging = true;
    }
    QWidget::mousePressEvent(event);
}

void DisassemblerWidget::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && m_isDragging)
    {
        QScrollBar* scrollBar = ui->disassemblyTable->verticalScrollBar();
        int delta = m_dragStartPos.y() - event->pos().y();
        scrollBar->setValue(scrollBar->value() + delta);
        m_dragStartPos = event->pos();
    }
    QWidget::mouseMoveEvent(event);
}

void DisassemblerWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_isDragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void DisassemblerWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateVisibleRange();
}

void DisassemblerWidget::onCurrentRowChanged(const QModelIndex& current, const QModelIndex& previous)
{
    if (current.isValid() && m_model)
    {
        uint16_t address = static_cast<uint16_t>(current.row());
        emit addressSelected(address);
    }
}

void DisassemblerWidget::updateVisibleRange()
{
    if (!m_model || m_updatingView || !m_emulator || !ui->disassemblyTable)
    {
        qDebug() << "Cannot update visible range - missing model, emulator, or table";
        return;
    }

    m_updatingView = true;
    QTableView* tableView = ui->disassemblyTable;

    try
    {
        // Calculate the range to show - centered around current PC
        const uint16_t rangeSize = 0x100;  // Show 256 bytes by default
        uint16_t startAddr = (m_currentPC > rangeSize / 2) ? (m_currentPC - rangeSize / 2) : 0x0000;
        uint16_t endAddr = (m_currentPC < (0xFFFF - rangeSize / 2)) ? (m_currentPC + rangeSize / 2) : 0xFFFF;

        qDebug() << "Updating visible range to:" << QString("0x%1").arg(startAddr, 4, 16, QChar('0')) << "-"
                 << QString("0x%1").arg(endAddr, 4, 16, QChar('0')) << "(PC: 0x" << QString::number(m_currentPC, 16)
                 << ")";

        // Set the visible range in the model
        m_model->setVisibleRange(startAddr, endAddr);

        // Force the view to update
        tableView->viewport()->update();

        // Ensure the PC is visible and centered
        // Find the row with the current PC
        int pcRow = -1;
        for (int i = 0; i < m_model->rowCount(); ++i)
        {
            QModelIndex idx = m_model->index(i, 0);
            QVariant addrData = m_model->data(idx, Qt::UserRole);
            if (addrData.isValid() && addrData.toUInt() == m_currentPC)
            {
                pcRow = i;
                break;
            }
        }

        if (pcRow >= 0)
        {
            // Scroll to the PC and select it
            QModelIndex pcIndex = m_model->index(pcRow, 0);
            tableView->scrollTo(pcIndex, QAbstractItemView::PositionAtCenter);
            tableView->setCurrentIndex(pcIndex);
            qDebug() << "Scrolled to PC at row:" << pcRow;
        }
        else
        {
            qDebug() << "Could not find PC in current disassembly";
        }
    }
    catch (const std::exception& e)
    {
        qWarning() << "Error in updateVisibleRange:" << e.what();
    }
    catch (...)
    {
        qWarning() << "Unknown error in updateVisibleRange";
    }

    m_updatingView = false;
}

void DisassemblerWidget::setEmulator(Emulator* emulator)
{
    if (m_emulator == emulator)
    {
        return;  // No change
    }

    m_emulator = emulator;

    // Update the model with the new emulator
    if (m_model)
    {
        qDebug() << "Setting emulator on model";
        m_model->setEmulator(emulator);

        // Dump model state for debugging
        m_model->dumpState();

        // Reset the view to show the new emulator's state
        reset();

        // Enable/disable the view based on whether we have an emulator
        bool hasEmulator = (emulator != nullptr);
        ui->disassemblyTable->setEnabled(hasEmulator);

        if (hasEmulator)
        {
            // If we have a current PC, make sure it's visible
            qDebug() << "Setting disassembler address to current PC:" << QString::number(m_currentPC, 16);
            setDisassemblerAddress(m_currentPC);
        }
        else
        {
            // Clear the view when no emulator is available
            ui->disassemblyTable->clearSelection();
            ui->disassemblyTable->setDisabled(true);
        }
    }
}

// Format address as $XXXX
QString DisassemblerWidget::formatAddress(uint16_t address) const
{
    return QString("$%1").arg(address, 4, 16, QChar('0')).toUpper();
}