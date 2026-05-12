#include "memorypagesviswidget.h"

#include <QDebug>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"

MemoryPagesVisWidget::MemoryPagesVisWidget(QWidget* parent) : QWidget(parent)
{
    createUI();
    setMinimumSize(120, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

MemoryPagesVisWidget::~MemoryPagesVisWidget()
{
}

void MemoryPagesVisWidget::createUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);

    _titleLabel = new QLabel("Memory Pages", this);
    _titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(_titleLabel);

    // Scrollable area for page grid
    _scrollArea = new QScrollArea(this);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _scrollArea->setFrameShape(QFrame::NoFrame);

    _gridWidget = new QWidget();
    _gridLayout = new QGridLayout(_gridWidget);
    _gridLayout->setContentsMargins(0, 0, 0, 0);
    _gridLayout->setSpacing(2);

    _scrollArea->setWidget(_gridWidget);
    mainLayout->addWidget(_scrollArea, 1);

    setLayout(mainLayout);
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

    Memory* memory = _emulator->GetMemory();
    if (!memory)
        return;

    // Determine max RAM pages based on the emulator configuration
    CONFIG& config = _emulator->GetContext()->config;

    switch (config.mem_model)
    {
        case MEM_MODEL::MM_PENTAGON:
            if (config.ramsize == RAM_128)
                _maxRamPages = 8;
            else if (config.ramsize == RAM_512)
                _maxRamPages = 32;
            else
                _maxRamPages = config.ramsize / 16;
            break;
        default:
            _maxRamPages = config.ramsize / 16;
            break;
    }

    // Clear existing entries
    for (auto& entry : _pageEntries)
    {
        if (entry.label)
        {
            _gridLayout->removeWidget(entry.label);
            delete entry.label;
        }
    }
    _pageEntries.clear();
    _lastLabelTexts.clear();
    _lastLabelColors.clear();

    // Also remove section header labels (they are not in _pageEntries)
    QLayoutItem* item;
    while ((item = _gridLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
            delete item->widget();
        delete item;
    }

    // Set up font once
    QFont labelFont;
    labelFont.setPixelSize(10);

    constexpr int COLS = 2;
    constexpr int FIRST_ROM_PAGE = MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES;

    int row = 0;

    // --- ROM section header ---
    {
        QLabel* header = new QLabel("ROM", _gridWidget);
        header->setAlignment(Qt::AlignCenter);
        header->setStyleSheet("font-weight: bold; font-size: 10px; color: #c0a050; padding: 2px;");
        _gridLayout->addWidget(header, row, 0, 1, COLS);
        row++;
    }

    // --- ROM pages (first 4) ---
    for (int i = 0; i < ROM_PAGES_SHOWN; i++)
    {
        QLabel* label = new QLabel(QString("R%1").arg(i), _gridWidget);
        label->setAlignment(Qt::AlignCenter);
        label->setFrameStyle(QFrame::Panel | QFrame::Raised);
        label->setLineWidth(1);
        label->setMinimumHeight(24);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        label->setFont(labelFont);
        label->setAutoFillBackground(true);

        int col = i % COLS;
        _gridLayout->addWidget(label, row + i / COLS, col);
        label->installEventFilter(this);

        PageEntry entry;
        entry.label = label;
        entry.absPageIndex = FIRST_ROM_PAGE + i;
        entry.isROM = true;
        _pageEntries.append(entry);
        _lastLabelTexts.append(QString());
        _lastLabelColors.append(0);
    }
    row += (ROM_PAGES_SHOWN + COLS - 1) / COLS;

    // --- RAM section header ---
    {
        QLabel* header = new QLabel("RAM", _gridWidget);
        header->setAlignment(Qt::AlignCenter);
        header->setStyleSheet("font-weight: bold; font-size: 10px; color: #5080c0; padding: 2px;");
        _gridLayout->addWidget(header, row, 0, 1, COLS);
        row++;
    }

    // --- RAM pages ---
    for (int i = 0; i < _maxRamPages; i++)
    {
        QLabel* label = new QLabel(QString::number(i), _gridWidget);
        label->setAlignment(Qt::AlignCenter);
        label->setFrameStyle(QFrame::Panel | QFrame::Raised);
        label->setLineWidth(1);
        label->setMinimumHeight(24);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        label->setFont(labelFont);
        label->setAutoFillBackground(true);

        int col = i % COLS;
        _gridLayout->addWidget(label, row + i / COLS, col);
        label->installEventFilter(this);

        PageEntry entry;
        entry.label = label;
        entry.absPageIndex = i;  // RAM pages: absolute index == page number
        entry.isROM = false;
        _pageEntries.append(entry);
        _lastLabelTexts.append(QString());
        _lastLabelColors.append(0);
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

    MemoryAccessTracker& tracker = memory->GetAccessTracker();

    // Update the title with configuration info
    CONFIG& config = _emulator->GetContext()->config;
    _titleLabel->setText(QString("Memory Pages (%1K)").arg(config.ramsize));

    // Determine which absolute pages are currently mapped to Z80 banks
    int mappedPages[4] = {-1, -1, -1, -1};
    for (int bank = 0; bank < 4; bank++)
    {
        mappedPages[bank] = static_cast<int>(memory->GetPageForBank(bank));
    }

    constexpr int FIRST_ROM_PAGE = MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES;

    // Update each page entry
    for (int idx = 0; idx < _pageEntries.size(); idx++)
    {
        const PageEntry& entry = _pageEntries[idx];
        QLabel* label = entry.label;

        // Check if currently mapped to a Z80 bank
        bool isMapped = false;
        int mappedBank = -1;
        for (int bank = 0; bank < 4; bank++)
        {
            if (mappedPages[bank] == entry.absPageIndex)
            {
                isMapped = true;
                mappedBank = bank;
                break;
            }
        }

        // Check if page has activity via the tracker
        bool isAccessed = tracker.IsPageActive(static_cast<uint16_t>(entry.absPageIndex));

        // Compute text and color
        QString newText;
        QColor newColor;
        QColor newTextColor(Qt::black);

        // Logical page number for display
        int displayNum = entry.isROM ? (entry.absPageIndex - FIRST_ROM_PAGE) : entry.absPageIndex;
        QString pageLabel = entry.isROM ? QString("R%1").arg(displayNum) : QString::number(displayNum);

        if (isMapped)
        {
            newText = QString("%1\nBank %2").arg(pageLabel).arg(mappedBank);
            newColor = entry.isROM ? QColor(0xC0, 0xA0, 0x50) : QColor(0x80, 0x80, 0xFF);
            newTextColor = Qt::white;
        }
        else
        {
            newText = pageLabel;
            newColor = getColorForPage(entry.isROM, isAccessed, false);
        }

        QRgb newColorRgb = newColor.rgb();

        // Dirty-check: skip if nothing changed
        if (newText == _lastLabelTexts[idx] && newColorRgb == _lastLabelColors[idx])
            continue;

        if (newText != _lastLabelTexts[idx])
        {
            label->setText(newText);
            _lastLabelTexts[idx] = newText;
        }

        if (newColorRgb != _lastLabelColors[idx])
        {
            QPalette pal = label->palette();
            pal.setColor(QPalette::Window, newColor);
            pal.setColor(QPalette::WindowText, newTextColor);
            label->setPalette(pal);
            _lastLabelColors[idx] = newColorRgb;
        }
    }
}

QColor MemoryPagesVisWidget::getColorForPage(bool isROM, bool isAccessed, bool isMapped)
{
    if (isAccessed)
        return isROM ? QColor(200, 190, 160) : QColor(200, 200, 200);
    else
        return isROM ? QColor(140, 130, 100) : QColor(120, 120, 120);
}

void MemoryPagesVisWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
}

void MemoryPagesVisWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refresh();
}

bool MemoryPagesVisWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress)
    {
        QLabel* clickedLabel = qobject_cast<QLabel*>(watched);
        if (clickedLabel)
        {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            for (int i = 0; i < _pageEntries.size(); i++)
            {
                if (_pageEntries[i].label == clickedLabel)
                {
                    // Left-click = top free viewer (slot 0), Right-click = bottom free viewer (slot 1)
                    int viewerSlot = (mouseEvent->button() == Qt::RightButton) ? 1 : 0;
                    emit pageClickedForFreeViewer(_pageEntries[i].absPageIndex, viewerSlot);
                    return true;
                }
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}
