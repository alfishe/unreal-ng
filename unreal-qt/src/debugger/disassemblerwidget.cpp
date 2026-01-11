#include "disassemblerwidget.h"

#include <QHeaderView>
#include <QMouseEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTableView>
#include <QTextBlock>
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
      m_scrollMode(ScrollMode::Command),
      m_resizeTimer(this)
{
    ui->setupUi(this);
    initializeTable();
    connect(&m_resizeTimer, &QTimer::timeout, this, &DisassemblerWidget::onResizeTimer);
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

    // Force the view to update its layout
    tableView->doItemsLayout();  // This triggers row height calculation

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
    // Comments column will take the remaining space

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

    // Enable grid for the table
    // tableView->setShowGrid(true);
    // tableView->setGridStyle(Qt::SolidLine);

    // Style the header with borders
    QString headerStyle =
        "QHeaderView::section {"
        "    background: transparent;"
        "    border: none;"
        "    border-right: 1px solid #c0c0c0;"
        "    border-bottom: 1px solid #c0c0c0;"
        "    padding: 4px;"
        "    margin: 0;"
        "}";
    tableView->horizontalHeader()->setStyleSheet(headerStyle);

    // Calculate header height based on font metrics
    // We need to ensure the header is tall enough to show the text + border
    QFontMetrics fm(tableView->font());
    int headerHeight = fm.height() + 8;  // Add some padding
    tableView->horizontalHeader()->setFixedHeight(headerHeight);

    // Connect signals
    connect(tableView->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &DisassemblerWidget::onCurrentRowChanged);

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
    if (!m_model || !ui->disassemblyTable)
    {
        qDebug() << "DisassemblerWidget::setDisassemblerAddress - model or table UI not set.";
        return;
    }

    bool oldSignalState = ui->disassemblyTable->blockSignals(true);

    try
    {
        // 1. Inform the model of the new PC.
        // Model will do all the work to update the visible range and highlight the new PC.
        m_model->setCurrentPC(pc);

        // Now, find the row for the PC in the new dataset.
        int targetRow = m_model->findRowForAddress(pc);
        qDebug() << "DisassemblerWidget: PC 0x" << QString::number(pc, 16) << "found at model row:" << targetRow;

        if (targetRow != -1)
        {
            QModelIndex pcIndex = m_model->index(targetRow, 0);
            if (pcIndex.isValid())
            {
                ui->disassemblyTable->scrollTo(pcIndex, QAbstractItemView::PositionAtCenter);
                ui->disassemblyTable->setCurrentIndex(pcIndex);  // Sets the current item for keyboard navigation
                if (ui->disassemblyTable->selectionModel())
                {  // Ensure selection model is valid
                    ui->disassemblyTable->selectionModel()->select(
                        pcIndex,
                        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);  // Selects the entire row
                }
            }
            else
            {
                qDebug() << "DisassemblerWidget: PC 0x" << QString::number(pc, 16) << "found at row" << targetRow
                         << "but model index is invalid.";
            }
        }
        else
        {
            qDebug() << "DisassemblerWidget: PC 0x" << QString::number(pc, 16)
                     << "not found in model after range update.";
        }
    }
    catch (const std::exception& e)
    {
        qCritical() << "Exception in setDisassemblerAddress:" << e.what();
    }
    catch (...)
    {
        qWarning() << "Unknown error in setDisassemblerAddress";
    }

    // Restore signal blocking state
    ui->disassemblyTable->blockSignals(oldSignalState);
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

    // Refresh the model - this will reset the model and reload instructions
    m_model->refresh();

    // Force a full view update
    tableView->viewport()->update();
    tableView->updateGeometry();

    // Restore the scroll position
    tableView->verticalScrollBar()->setValue(scrollValue);

    // Ensure the current PC is visible without triggering another full range update
    if (m_currentPC != 0)
    {
        // Find the row for the current PC
        int pcRow = m_model->findRowForAddress(m_currentPC);
        if (pcRow != -1)
        {
            QModelIndex pcIndex = m_model->index(pcRow, 0);
            if (pcIndex.isValid())
            {
                // Scroll to PC and select it
                tableView->scrollTo(pcIndex, QAbstractItemView::PositionAtCenter);
                tableView->setCurrentIndex(pcIndex);
                if (tableView->selectionModel())
                {
                    tableView->selectionModel()->select(
                        pcIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                }
            }
        }
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

    // Get the row height to calculate how many rows fit in the visible area
    int rowHeight = ui->disassemblyTable->rowHeight(0);
    if (rowHeight <= 0)
    {
        rowHeight = ui->disassemblyTable->verticalHeader()->defaultSectionSize();
    }

    // Calculate how many rows would be shown with the new size
    int currentRows = m_lastSize.isValid() ? m_lastSize.height() / rowHeight : 0;
    int newRows = event->size().height() / rowHeight;

    // Only update if we can show at least 10 more rows than before
    // or if we've never updated before (m_lastSize is invalid)
    if (!m_lastSize.isValid() || abs(newRows - currentRows) >= 10)
    {
        // Restart the timer - this will delay the update until resizing stops
        m_resizeTimer.stop();
        m_resizeTimer.setSingleShot(true);
        m_resizeTimer.start(100);  // 100ms delay
    }

    m_lastSize = event->size();
}

void DisassemblerWidget::onResizeTimer()
{
    // Only update if we have a valid model and the widget is visible
    if (m_model && isVisible())
    {
        updateVisibleRange();
    }
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
        // The model's setVisibleRange method now centers around the PC with 256 bytes in each direction
        // We just need to trigger it with any range, and it will automatically center on PC

        // For logging purposes, calculate how many rows are visible in the table
        int tableHeight = tableView->height();
        int rowHeight = tableView->rowHeight(0);
        if (rowHeight <= 0)
        {
            // Fallback if still is not calculated
            rowHeight = tableView->verticalHeader()->defaultSectionSize();
        }

        int visibleRows = tableHeight / rowHeight;
        if (visibleRows <= 0)
            visibleRows = 20;  // Fallback if calculation fails

        qDebug() << "Table height:" << tableView->height() << "Row height:" << tableView->rowHeight(0)
                 << "Visible rows:" << visibleRows;

        qDebug() << "Updating disassembly with PC at:" << QString::number(m_currentPC, 16);

        // Trigger the model to update its range centered on PC
        // The actual range values don't matter as the model will override them
        // to center around the PC with 256 bytes in each direction
        m_model->setVisibleRange(0, 0);

        // Force the view to update
        tableView->viewport()->update();

        // Use the model's findRowForAddress method to find the PC row
        int pcRow = m_model->findRowForAddress(m_currentPC);

        if (pcRow >= 0)
        {
            // Scroll to the PC and select it
            QModelIndex pcIndex = m_model->index(pcRow, 0);
            tableView->scrollTo(pcIndex, QAbstractItemView::PositionAtCenter);
            tableView->setCurrentIndex(pcIndex);

            // Also select the row to highlight it
            if (tableView->selectionModel())
            {
                tableView->selectionModel()->select(pcIndex,
                                                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }

            qDebug() << "Scrolled to PC at row:" << pcRow << "with address 0x" << QString::number(m_currentPC, 16);
        }
        else
        {
            qDebug() << "Could not find PC 0x" << QString::number(m_currentPC, 16) << " in current disassembly";

            // If PC not found, force a model refresh which will rebuild the cache
            qDebug() << "PC not found in cache, forcing model refresh";
            m_model->refresh();

            // Try to find PC again after refresh
            pcRow = m_model->findRowForAddress(m_currentPC);
            if (pcRow >= 0)
            {
                QModelIndex pcIndex = m_model->index(pcRow, 0);
                tableView->scrollTo(pcIndex, QAbstractItemView::PositionAtCenter);
                tableView->setCurrentIndex(pcIndex);

                if (tableView->selectionModel())
                {
                    tableView->selectionModel()->select(
                        pcIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                }

                qDebug() << "Found PC after model refresh at row:" << pcRow;
            }
            else
            {
                qDebug() << "Still could not find PC after model refresh";
            }
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

    // HOTFIX: Only proceed with disassembly if emulator is in a ready state
    // During initialization the emulator is not yet fully set up
    if (m_emulator)
    {
        EmulatorStateEnum state = m_emulator->GetState();
        bool isReadyState = (state == StateRun || state == StateResumed || state == StatePaused);

        if (!isReadyState)
        {
            qDebug() << "DisassemblerWidget::setEmulator - emulator state" << state
                     << "not ready, deferring all operations";
            // Just store the reference, don't trigger any disassembly
            if (m_model)
            {
                m_model->setEmulator(nullptr);  // Clear model safely
            }
            ui->disassemblyTable->setEnabled(false);
            return;
        }
    }

    // Update the model with the new emulator (only if ready state)
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