#include "memorypagesviswidget.h"

#include <QDebug>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>

MemoryPagesVisWidget::MemoryPagesVisWidget(QWidget* parent) : QWidget(parent)
{
    createUI();
    setMinimumSize(200, 300);
}

MemoryPagesVisWidget::~MemoryPagesVisWidget()
{
    // Clean up any resources if needed
}

void MemoryPagesVisWidget::createUI()
{
    _layout = new QGridLayout(this);

    _titleLabel = new QLabel("Memory Pages", this);
    _titleLabel->setAlignment(Qt::AlignCenter);
    _layout->addWidget(_titleLabel, 0, 0, 1, 4);

    setLayout(_layout);
}

void MemoryPagesVisWidget::setEmulator(Emulator* emulator)
{
    _emulator = emulator;
    reset();
}

void MemoryPagesVisWidget::reset()
{
    if (!_emulator)
        return;

    // Determine max pages based on the emulator configuration
    Memory* memory = _emulator->GetMemory();
    if (!memory)
        return;

    // Get the memory model to determine max pages
    CONFIG& config = _emulator->GetContext()->config;

    // Determine max pages based on memory model
    switch (config.mem_model)
    {
        case MEM_MODEL::MM_PENTAGON:
            if (config.ramsize == RAM_128)
                _maxPages = 8;  // Pentagon 128K has 8 RAM pages
            else if (config.ramsize == RAM_512)
                _maxPages = 32;  // Pentagon 512K has 32 RAM pages
            else
                _maxPages = config.ramsize / 16;  // Each page is 16KB
            break;
        default:
            _maxPages = config.ramsize / 16;  // Default calculation
            break;
    }

    // Clear existing page labels
    for (QLabel* label : _pageLabels)
    {
        if (label)
        {
            _layout->removeWidget(label);
            delete label;
        }
    }
    _pageLabels.clear();

    // Reset accessed pages tracking
    _accessedPages.resize(_maxPages);
    _accessedPages.fill(false);

    // Create page labels
    const int COLS = 4;
    for (int i = 0; i < _maxPages; i++)
    {
        QLabel* label = new QLabel(QString::number(i), this);
        label->setAlignment(Qt::AlignCenter);
        label->setFrameStyle(QFrame::Panel | QFrame::Raised);
        label->setLineWidth(2);
        label->setMinimumSize(40, 30);

        int row = (i / COLS) + 1;  // +1 because title is in row 0
        int col = i % COLS;

        _layout->addWidget(label, row, col);
        _pageLabels.append(label);
    }

    refresh();
}

void MemoryPagesVisWidget::refresh()
{
    if (!_emulator)
        return;

    updatePageDisplay();
}

void MemoryPagesVisWidget::updatePageDisplay()
{
    if (!_emulator)
        return;

    Memory* memory = _emulator->GetMemory();
    if (!memory)
        return;

    // Update the title with configuration info
    CONFIG& config = _emulator->GetContext()->config;
    _titleLabel->setText(QString("Memory Pages (%1K)").arg(config.ramsize));

    // Check which pages are currently mapped to Z80 address space
    uint8_t currentPages[4] = {0};
    for (int bank = 0; bank < 4; bank++)
    {
        // Get the page number for this bank
        int pageNumber = memory->GetPhysicalOffsetForZ80Bank(bank) / 0x4000;
        currentPages[bank] = pageNumber;

        // Mark this page as accessed
        if (pageNumber < _maxPages)
        {
            _accessedPages[pageNumber] = true;
        }
    }

    // Update all page labels
    for (int i = 0; i < _maxPages && i < _pageLabels.size(); i++)
    {
        QLabel* label = _pageLabels[i];

        // Check if this page is currently mapped to a Z80 bank
        bool isMapped = false;
        int mappedBank = -1;
        for (int bank = 0; bank < 4; bank++)
        {
            if (currentPages[bank] == i)
            {
                isMapped = true;
                mappedBank = bank;
                break;
            }
        }

        // Set the label text and style
        if (isMapped)
        {
            label->setText(QString("%1\nBank %2").arg(i).arg(mappedBank));
            label->setStyleSheet("background-color: #8080FF; color: white;");
        }
        else
        {
            label->setText(QString::number(i));

            // Different color for accessed vs. never accessed pages
            QColor color = getColorForPage(i, _accessedPages[i]);
            label->setStyleSheet(QString("background-color: %1;").arg(color.name()));
        }
    }
}

QColor MemoryPagesVisWidget::getColorForPage(int pageIndex, bool isAccessed)
{
    if (isAccessed)
    {
        return QColor(200, 200, 200);  // Light gray for accessed pages
    }
    else
    {
        return QColor(120, 120, 120);  // Darker gray for never accessed pages
    }
}

void MemoryPagesVisWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    // Additional custom painting if needed
}

void MemoryPagesVisWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refresh();
}

void MemoryPagesVisWidget::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);

    // Handle clicking on a page label to show it in memory view
    // This would be implemented in a full version
}
