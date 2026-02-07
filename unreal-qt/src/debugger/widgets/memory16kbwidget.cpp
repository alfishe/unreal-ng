#include "memory16kbwidget.h"

#include <QDebug>
#include <QHelpEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QToolTip>
#include <QVBoxLayout>

#include "emulator/cpu/core.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/memory/rom.h"

QRgb Memory16KBWidget::s_colorLUT[256] = {};
bool Memory16KBWidget::s_colorLUTInitialized = false;

void Memory16KBWidget::initColorLUT()
{
    if (s_colorLUTInitialized)
        return;

    // Base memory value colors:
    // 0x00 = black, 0xFF = white, other = gray gradient
    s_colorLUT[0x00] = qRgb(0, 0, 0);       // Black
    s_colorLUT[0xFF] = qRgb(255, 255, 255); // White

    for (int i = 1; i < 255; i++)
    {
        int gray = 40 + (i * 175) / 255; // Gray gradient from 40 to 215
        s_colorLUT[i] = qRgb(gray, gray, gray);
    }

    s_colorLUTInitialized = true;
}

Memory16KBWidget::Memory16KBWidget(int bankIndex, QWidget* parent) : QWidget(parent), _bankIndex(bankIndex)
{
    initColorLUT();
    _memoryImage = QImage(IMAGE_WIDTH, IMAGE_HEIGHT, QImage::Format_RGB32);
    _memoryImage.fill(Qt::black);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    _titleLabel = new QLabel(this);
    _titleLabel->setAlignment(Qt::AlignCenter);
    _titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(_titleLabel);

    _imageLabel = new QLabel(this);
    _imageLabel->setMinimumSize(IMAGE_WIDTH, IMAGE_HEIGHT);
    _imageLabel->setMouseTracking(true);
    _imageLabel->installEventFilter(this);
    _imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    layout->addWidget(_imageLabel, 1);

    _countersLabel = new QLabel(this);
    _countersLabel->setAlignment(Qt::AlignCenter);
    _countersLabel->setStyleSheet("font-size: 10px; color: #888;");
    layout->addWidget(_countersLabel);

    setLayout(layout);
    setMinimumSize(280, 100);
    setMouseTracking(true);
}

Memory16KBWidget::~Memory16KBWidget() {}

void Memory16KBWidget::setEmulator(Emulator* emulator)
{
    _emulator = emulator;
    reset();
}

void Memory16KBWidget::setBankIndex(int bankIndex)
{
    _displayMode = DisplayMode::Z80Bank;
    _bankIndex = bankIndex;
    refresh();
}

void Memory16KBWidget::setPhysicalPage(int pageNumber)
{
    _displayMode = DisplayMode::PhysicalPage;
    _physicalPageNumber = pageNumber;
    refresh();
}

void Memory16KBWidget::reset()
{
    _memoryImage.fill(Qt::black);
    refresh();
}

void Memory16KBWidget::refresh()
{
    if (!_emulator)
        return;

    updateMemoryImage();
    updateCounterLabels();
    update();
}

void Memory16KBWidget::setShowReadOverlay(bool show)
{
    _showReadOverlay = show;
    refresh();
}

void Memory16KBWidget::setShowWriteOverlay(bool show)
{
    _showWriteOverlay = show;
    refresh();
}

void Memory16KBWidget::setShowExecuteOverlay(bool show)
{
    _showExecuteOverlay = show;
    refresh();
}

void Memory16KBWidget::setHideValues(bool hide)
{
    _hideValues = hide;
    refresh();
}

void Memory16KBWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
}

void Memory16KBWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (_imageLabel)
    {
        int labelWidth = _imageLabel->width();
        int labelHeight = _imageLabel->height();

        if (labelWidth > 0 && labelHeight > 0)
        {
            int newWidth = qMax(IMAGE_WIDTH, labelWidth);
            int newHeight = (newWidth * 3) / 4;

            if (newHeight > labelHeight)
            {
                newHeight = qMax(IMAGE_HEIGHT, labelHeight);
                newWidth = (newHeight * 4) / 3;
            }

            if (newWidth != _currentImageWidth || newHeight != _currentImageHeight)
            {
                _currentImageWidth = newWidth;
                _currentImageHeight = newHeight;
                _memoryImage = QImage(_currentImageWidth, _currentImageHeight, QImage::Format_RGB32);
                _memoryImage.fill(Qt::black);
            }
        }
    }

    refresh();
}

void Memory16KBWidget::updateMemoryImage()
{
    if (!_emulator)
        return;

    Memory* memory = _emulator->GetMemory();
    if (!memory)
        return;

    uint8_t* pageAddress = nullptr;
    uint16_t baseAddress = 0;
    bool isZ80Mode = (_displayMode == DisplayMode::Z80Bank);

    if (isZ80Mode)
    {
        baseAddress = _bankIndex * BANK_SIZE;
        pageAddress = memory->MapZ80AddressToPhysicalAddress(baseAddress);
    }
    else
    {
        // Physical page mode: directly access RAM page
        pageAddress = memory->RAMPageAddress(_physicalPageNumber);
    }

    if (!pageAddress)
    {
        _memoryImage.fill(Qt::black);
        if (isZ80Mode)
            _titleLabel->setText(QString("Bank %1 (0x%2) - N/A").arg(_bankIndex).arg(baseAddress, 4, 16, QChar('0')));
        else
            _titleLabel->setText(QString("RAM %1 - N/A").arg(_physicalPageNumber));
        _imageLabel->setPixmap(QPixmap::fromImage(_memoryImage));
        return;
    }

    // Set title
    if (isZ80Mode)
    {
        bool isROM = memory->GetMemoryBankMode(_bankIndex) == BANK_ROM;
        QString pageInfo;

        if (isROM)
        {
            EmulatorContext* ctx = _emulator->GetContext();
            ROM* rom = (ctx && ctx->pCore) ? ctx->pCore->GetROM() : nullptr;
            if (rom)
            {
                std::string title = rom->GetROMTitleByAddress(pageAddress);
                if (!title.empty())
                    pageInfo = QString::fromStdString(title);
                else
                    pageInfo = "ROM";
            }
            else
            {
                pageInfo = "ROM";
            }
        }
        else
        {
            int pageNumber = memory->GetPhysicalOffsetForZ80Bank(_bankIndex) / BANK_SIZE;
            pageInfo = QString("RAM %1").arg(pageNumber);
        }

        _titleLabel->setText(
            QString("Bank %1 (0x%2) - %3").arg(_bankIndex).arg(baseAddress, 4, 16, QChar('0')).arg(pageInfo));
    }
    else
    {
        _titleLabel->setText(QString("RAM %1").arg(_physicalPageNumber));
    }

    // Force COW detach before pixel loop
    _memoryImage.bits();

    bool needsOverlay = isZ80Mode && (_showReadOverlay || _showWriteOverlay || _showExecuteOverlay);
    const uint32_t* readCounters = nullptr;
    const uint32_t* writeCounters = nullptr;
    const uint32_t* execCounters = nullptr;

    if (needsOverlay)
    {
        MemoryAccessTracker& tracker = memory->GetAccessTracker();
        if (_showReadOverlay)
            readCounters = tracker.GetZ80ReadCountersPtr();
        if (_showWriteOverlay)
            writeCounters = tracker.GetZ80WriteCountersPtr();
        if (_showExecuteOverlay)
            execCounters = tracker.GetZ80ExecuteCountersPtr();
    }

    for (int y = 0; y < _currentImageHeight; y++)
    {
        int memY = (y * IMAGE_HEIGHT) / _currentImageHeight;
        QRgb* scanLine = reinterpret_cast<QRgb*>(_memoryImage.scanLine(y));
        for (int x = 0; x < _currentImageWidth; x++)
        {
            int memX = (x * IMAGE_WIDTH) / _currentImageWidth;
            int offset = memY * IMAGE_WIDTH + memX;
            if (offset < BANK_SIZE)
            {
                uint8_t value = pageAddress[offset];
                QRgb baseColor = _hideValues ? qRgb(64, 64, 64) : s_colorLUT[value];

                if (needsOverlay)
                {
                    uint16_t addr = baseAddress + offset;
                    uint32_t r = readCounters ? readCounters[addr] : 0;
                    uint32_t w = writeCounters ? writeCounters[addr] : 0;
                    uint32_t e = execCounters ? execCounters[addr] : 0;

                    if (r > 0 || w > 0 || e > 0)
                    {
                        int red = qRed(baseColor);
                        int green = qGreen(baseColor);
                        int blue = qBlue(baseColor);

                        if (e > 0)
                        {
                            int intensity = qMin(255, static_cast<int>(e));
                            green = qMin(255, green + intensity);
                        }
                        if (w > 0)
                        {
                            int intensity = qMin(255, static_cast<int>(w));
                            red = qMin(255, red + intensity);
                        }
                        if (r > 0)
                        {
                            int intensity = qMin(255, static_cast<int>(r));
                            blue = qMin(255, blue + intensity);
                        }
                        baseColor = qRgb(red, green, blue);
                    }
                }
                scanLine[x] = baseColor;
            }
        }
    }

    _imageLabel->setPixmap(QPixmap::fromImage(_memoryImage));
}

void Memory16KBWidget::updateCounterLabels()
{
    if (!_emulator)
    {
        _countersLabel->setText("R:- W:- X:-");
        return;
    }

    Memory* memory = _emulator->GetMemory();
    if (!memory)
    {
        _countersLabel->setText("R:- W:- X:-");
        return;
    }

    // Access counters are only available for Z80 bank mode
    if (_displayMode != DisplayMode::Z80Bank)
    {
        _countersLabel->setText("");
        return;
    }

    MemoryAccessTracker& tracker = memory->GetAccessTracker();

    uint32_t readCount = tracker.GetZ80BankReadAccessCount(_bankIndex);
    uint32_t writeCount = tracker.GetZ80BankWriteAccessCount(_bankIndex);
    uint32_t execCount = tracker.GetZ80BankExecuteAccessCount(_bankIndex);

    auto formatCount = [](uint32_t count) -> QString {
        if (count >= 1000000)
            return QString("%1M").arg(count / 1000000.0, 0, 'f', 1);
        if (count >= 1000)
            return QString("%1K").arg(count / 1000.0, 0, 'f', 1);
        return QString::number(count);
    };

    _countersLabel->setText(
        QString("R:%1 W:%2 X:%3").arg(formatCount(readCount)).arg(formatCount(writeCount)).arg(formatCount(execCount)));
}

int Memory16KBWidget::mapMouseToOffset(const QPoint& pos) const
{
    if (!_imageLabel || _imageLabel->width() <= 0 || _imageLabel->height() <= 0)
        return -1;

    QPoint localPos = _imageLabel->mapFrom(this, pos);
    if (!_imageLabel->rect().contains(localPos))
        return -1;

    int x = (localPos.x() * IMAGE_WIDTH) / _imageLabel->width();
    int y = (localPos.y() * IMAGE_HEIGHT) / _imageLabel->height();

    x = qBound(0, x, IMAGE_WIDTH - 1);
    y = qBound(0, y, IMAGE_HEIGHT - 1);

    return y * IMAGE_WIDTH + x;
}

bool Memory16KBWidget::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip)
    {
        QHelpEvent* helpEvent = static_cast<QHelpEvent*>(e);
        int offset = mapMouseToOffset(helpEvent->pos());

        if (offset >= 0 && offset < BANK_SIZE && _emulator)
        {
            Memory* memory = _emulator->GetMemory();
            if (memory)
            {
                uint16_t addr = (_bankIndex * BANK_SIZE) + offset;
                uint8_t value = memory->DirectReadFromZ80Memory(addr);

                MemoryAccessTracker& tracker = memory->GetAccessTracker();
                uint32_t readCount = tracker.GetZ80AddressReadCount(addr);
                uint32_t writeCount = tracker.GetZ80AddressWriteCount(addr);
                uint32_t execCount = tracker.GetZ80AddressExecuteCount(addr);

                QString text = QString("Addr: 0x%1 (%2)\nValue: 0x%3 (%4)\nR:%5 W:%6 X:%7")
                                   .arg(addr, 4, 16, QChar('0'))
                                   .arg(addr)
                                   .arg(value, 2, 16, QChar('0'))
                                   .arg(value)
                                   .arg(readCount)
                                   .arg(writeCount)
                                   .arg(execCount);

                QToolTip::showText(helpEvent->globalPos(), text, this);
                return true;
            }
        }
        QToolTip::hideText();
        e->ignore();
        return true;
    }
    return QWidget::event(e);
}

bool Memory16KBWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _imageLabel && event->type() == QEvent::ToolTip)
    {
        QHelpEvent* helpEvent = static_cast<QHelpEvent*>(event);
        QPoint localPos = helpEvent->pos();
        if (_imageLabel->width() <= 0 || _imageLabel->height() <= 0)
            return QWidget::eventFilter(watched, event);

        int x = (localPos.x() * IMAGE_WIDTH) / _imageLabel->width();
        int y = (localPos.y() * IMAGE_HEIGHT) / _imageLabel->height();

        x = qBound(0, x, IMAGE_WIDTH - 1);
        y = qBound(0, y, IMAGE_HEIGHT - 1);

        int offset = y * IMAGE_WIDTH + x;

        if (offset >= 0 && offset < BANK_SIZE && _emulator)
        {
            Memory* memory = _emulator->GetMemory();
            if (memory)
            {
                uint16_t addr = (_bankIndex * BANK_SIZE) + offset;
                uint8_t value = memory->DirectReadFromZ80Memory(addr);

                MemoryAccessTracker& tracker = memory->GetAccessTracker();
                uint32_t readCount = tracker.GetZ80AddressReadCount(addr);
                uint32_t writeCount = tracker.GetZ80AddressWriteCount(addr);
                uint32_t execCount = tracker.GetZ80AddressExecuteCount(addr);

                QString text = QString("Addr: 0x%1 (%2)\nValue: 0x%3 (%4)\nR:%5 W:%6 X:%7")
                                   .arg(addr, 4, 16, QChar('0'))
                                   .arg(addr)
                                   .arg(value, 2, 16, QChar('0'))
                                   .arg(value)
                                   .arg(readCount)
                                   .arg(writeCount)
                                   .arg(execCount);

                QToolTip::showText(helpEvent->globalPos(), text, _imageLabel);
                return true;
            }
        }
        QToolTip::hideText();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
