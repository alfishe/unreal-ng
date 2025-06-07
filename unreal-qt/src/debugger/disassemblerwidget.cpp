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
    // Create and set the model
    m_model = new DisassemblerTableModel(m_emulator, this);

    // Get the table view from UI
    QTableView* tableView = ui->disassemblyTable;

    // Set the model
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
    tableView->setColumnWidth(0, 80);   // Address (fixed width for 16-bit hex)
    tableView->setColumnWidth(1, 120);  // Opcode (fixed width for 1-4 bytes hex)
    tableView->setColumnWidth(2, 150);  // Label (text, allow room for longer labels)
    tableView->setColumnWidth(3, 200);  // Mnemonic (text, needs most space)
    tableView->setColumnWidth(4, 150);  // Annotation (text, allow room for annotations)
    // Comment column will take remaining space

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
    connect(tableView->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &DisassemblerWidget::onCurrentRowChanged);
    
    // Force the view to update its geometry
    tableView->resizeColumnsToContents();
    
    qDebug() << "Table initialization complete, visible columns:" << tableView->horizontalHeader()->count();

    // Initial load
    updateVisibleRange();
}

void DisassemblerWidget::setDisassemblerAddress(uint16_t pc)
{
    // Validate input and state
    if (pc >= MAX_ADDRESS || !m_model || !ui || !ui->disassemblyTable)
    {
        return;
    }

    qDebug() << "setDisassemblerAddress:" << QString("0x%1").arg(pc, 4, 16, QChar('0'));

    // Update the current PC
    m_currentPC = pc;

    // Block signals temporarily to prevent recursive updates
    bool oldState = ui->disassemblyTable->blockSignals(true);

    try
    {
        // Update the model's current PC
        m_model->setCurrentPC(pc);

        // Load a range around the PC if not already loaded
        const int range = 20;  // Number of instructions to load before/after
        uint16_t start = (pc > range) ? (pc - range) : 0;
        uint16_t end = (pc < (0xFFFF - range)) ? (pc + range) : 0xFFFF;

        qDebug() << "Loading disassembly range for PC:" << QString("0x%1 - 0x%2").arg(start, 4, 16, QChar('0')) << "to"
                 << QString("0x%1").arg(end, 4, 16, QChar('0'));

        m_model->loadDisassemblyRange(start, end);

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
                selectionModel->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }

            // Scroll to the address
            ui->disassemblyTable->scrollTo(idx, QAbstractItemView::PositionAtCenter);
            qDebug() << "Scrolled to row" << targetRow << "for PC" << QString("0x%1").arg(pc, 4, 16, QChar('0'));
        }
        else
        {
            qDebug() << "Could not find row for PC" << QString("0x%1").arg(pc, 4, 16, QChar('0'));
        }

        // Force an update of the view
        ui->disassemblyTable->viewport()->update();
    }
    catch (const std::exception& e)
    {
        // Log the error if needed
        qWarning() << "Error in setDisassemblerAddress:" << e.what();
    }
    catch (...)
    {
        qWarning() << "Unknown error in setDisassemblerAddress";
    }

    // Restore signal blocking state
    ui->disassemblyTable->blockSignals(oldState);

    // Dump model state for debugging
    m_model->dumpState();
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
    if (!m_emulator || !m_model) {
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
    
    // Make sure all columns are visible
    tableView->resizeColumnsToContents();
    
    // Restore the scroll position
    tableView->verticalScrollBar()->setValue(scrollValue);

    // Ensure the current PC is visible
    if (m_currentPC != 0) {
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
        return;
    }

    m_updatingView = true;

    QTableView* tableView = ui->disassemblyTable;
    QRect viewportRect = tableView->viewport()->rect();

    // Get the visible rows with some margin for smoother scrolling
    QModelIndex topLeft = tableView->indexAt(viewportRect.topLeft());
    QModelIndex bottomRight = tableView->indexAt(viewportRect.bottomRight());

    if (topLeft.isValid() && bottomRight.isValid())
    {
        // Get the addresses corresponding to visible rows
        QVariant topData = m_model->data(topLeft, Qt::UserRole);
        QVariant bottomData = m_model->data(bottomRight, Qt::UserRole);

        if (topData.isValid() && bottomData.isValid())
        {
            uint16_t firstVisibleAddr = topData.toUInt();
            uint16_t lastVisibleAddr = bottomData.toUInt();

            // Add some margin for smoother scrolling (in bytes, not rows)
            const uint16_t margin = 0x20;
            firstVisibleAddr = (firstVisibleAddr > margin) ? (firstVisibleAddr - margin) : 0;
            lastVisibleAddr = (lastVisibleAddr < (0xFFFF - margin)) ? (lastVisibleAddr + margin) : 0xFFFF;

            qDebug() << "Loading disassembly range:" << QString("0x%1 - 0x%2").arg(firstVisibleAddr, 4, 16, QChar('0'))
                     << "to" << QString("0x%1").arg(lastVisibleAddr, 4, 16, QChar('0'));

            // Only update if we have a valid range
            if (firstVisibleAddr <= lastVisibleAddr)
            {
                m_model->loadDisassemblyRange(firstVisibleAddr, lastVisibleAddr);

                // Force an update of the viewport to ensure any new data is displayed
                tableView->viewport()->update();
            }
        }
    }
    else if (m_currentPC != 0)
    {
        // If no rows are visible but we have a current PC, try to center on it
        setDisassemblerAddress(m_currentPC);
    }

    m_updatingView = false;
}

void DisassemblerWidget::setEmulator(Emulator* emulator)
{
    if (m_emulator == emulator)
    {
        return;  // No change
    }

    // Disconnect from previous emulator signals if any
    if (m_emulator)
    {
        // Disconnect any signals connected to the old emulator
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
            if (m_currentPC != 0)
            {
                qDebug() << "Setting disassembler address to current PC:" << QString::number(m_currentPC, 16);
                setDisassemblerAddress(m_currentPC);
            }
            else
            {
                // If no current PC but we have an emulator, show address 0
                qDebug() << "Setting disassembler address to 0";
                setDisassemblerAddress(0);
            }
        }
        else
        {
            // Clear the view when no emulator is available
            ui->disassemblyTable->clearSelection();
            ui->disassemblyTable->setDisabled(true);
        }
    }

    // Connect to new emulator signals if needed
    if (m_emulator)
    {
        // Connect to any signals from the new emulator
    }
}

// Format address as $XXXX
QString DisassemblerWidget::formatAddress(uint16_t address) const
{
    return QString("$%1").arg(address, 4, 16, QChar('0')).toUpper();
}