#include "memorypagesviswidget.h"

#include <QDebug>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

MemoryPagesVisWidget::MemoryPagesVisWidget(QWidget* parent) : QWidget(parent)
{
    createUI();
    setMinimumSize(120, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

MemoryPagesVisWidget::~MemoryPagesVisWidget()
{
    // Clean up any resources if needed
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
            _gridLayout->removeWidget(label);
            delete label;
        }
    }
    _pageLabels.clear();
    _lastLabelTexts.clear();
    _lastLabelColors.clear();

    // Reset accessed pages tracking
    _accessedPages.resize(_maxPages);
    _accessedPages.fill(false);

    // Set up font once (replaces per-frame setStyleSheet)
    QFont labelFont;
    labelFont.setPixelSize(10);

    // Create page labels
    const int COLS = 2;  // 2 columns for narrower side panel
    for (int i = 0; i < _maxPages; i++)
    {
        QLabel* label = new QLabel(QString::number(i), _gridWidget);
        label->setAlignment(Qt::AlignCenter);
        label->setFrameStyle(QFrame::Panel | QFrame::Raised);
        label->setLineWidth(1);
        label->setMinimumHeight(24);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        label->setFont(labelFont);
        label->setAutoFillBackground(true);  // Required for QPalette background

        int row = i / COLS;
        int col = i % COLS;

        _gridLayout->addWidget(label, row, col);
        label->installEventFilter(this);  // Catch mouse clicks on labels
        _pageLabels.append(label);
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

    // Update the title with configuration info
    CONFIG& config = _emulator->GetContext()->config;
    _titleLabel->setText(QString("Memory Pages (%1K)").arg(config.ramsize));

    // Check which pages are currently mapped to Z80 address space
    uint8_t currentPages[4] = {0};
    for (int bank = 0; bank < 4; bank++)
    {
        int pageNumber = memory->GetPhysicalOffsetForZ80Bank(bank) / 0x4000;
        currentPages[bank] = pageNumber;

        if (pageNumber < _maxPages)
        {
            _accessedPages[pageNumber] = true;
        }
    }

    // Update labels — only touch widgets whose text or color actually changed
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

        // Compute desired text and color
        QString newText;
        QColor newColor;
        QColor newTextColor(Qt::black);  // default text color

        if (isMapped)
        {
            newText = QString("%1\nBank %2").arg(i).arg(mappedBank);
            newColor = QColor(0x80, 0x80, 0xFF);
            newTextColor = Qt::white;
        }
        else
        {
            newText = QString::number(i);
            newColor = getColorForPage(i, _accessedPages[i]);
        }

        QRgb newColorRgb = newColor.rgb();

        // Dirty-check: skip if nothing changed
        if (newText == _lastLabelTexts[i] && newColorRgb == _lastLabelColors[i])
            continue;

        // Apply changes
        if (newText != _lastLabelTexts[i])
        {
            label->setText(newText);
            _lastLabelTexts[i] = newText;
        }

        if (newColorRgb != _lastLabelColors[i])
        {
            QPalette pal = label->palette();
            pal.setColor(QPalette::Window, newColor);
            pal.setColor(QPalette::WindowText, newTextColor);
            label->setPalette(pal);
            _lastLabelColors[i] = newColorRgb;
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
            for (int i = 0; i < _pageLabels.size(); i++)
            {
                if (_pageLabels[i] == clickedLabel)
                {
                    // Left-click = top free viewer (slot 0), Right-click = bottom free viewer (slot 1)
                    int viewerSlot = (mouseEvent->button() == Qt::RightButton) ? 1 : 0;
                    emit pageClickedForFreeViewer(i, viewerSlot);
                    return true;
                }
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}
