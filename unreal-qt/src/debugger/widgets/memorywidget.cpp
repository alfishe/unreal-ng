#include "memorywidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDebug>

MemoryWidget::MemoryWidget(QWidget *parent)
    : QWidget(parent)
{
    // Initialize memory images
    for (int i = 0; i < 4; i++) {
        _memoryImage[i] = QImage(256, 64, QImage::Format_ARGB32);
        _memoryImage[i].fill(Qt::black);
        
        // Allocate memory for access tracking (16KB per bank)
        _readAccess[i] = new uint8_t[16384]();    // Zero-initialized
        _writeAccess[i] = new uint8_t[16384]();   // Zero-initialized
        _executeAccess[i] = new uint8_t[16384](); // Zero-initialized
    }
    
    createUI();
    setMinimumSize(512, 300);
}

MemoryWidget::~MemoryWidget()
{
    // Free memory for access tracking
    for (int i = 0; i < 4; i++) {
        delete[] _readAccess[i];
        delete[] _writeAccess[i];
        delete[] _executeAccess[i];
    }
}

void MemoryWidget::createUI()
{
    _layout = new QGridLayout(this);
    
    // Create bank labels
    for (int i = 0; i < 4; i++) {
        _bankLabels[i] = new QLabel(QString("Bank %1 (0x%2)").arg(i).arg(i * 0x4000, 4, 16, QChar('0')), this);
        _layout->addWidget(_bankLabels[i], i, 0);
        
        _imageLabels[i] = new QLabel(this);
        _imageLabels[i]->setScaledContents(true);
        _layout->addWidget(_imageLabels[i], i, 1);
    }
    
    // Create layer controls
    QHBoxLayout* controlLayout = new QHBoxLayout();
    _readLayerCheckbox = new QCheckBox("Show Read Access", this);
    _writeLayerCheckbox = new QCheckBox("Show Write Access", this);
    _executeLayerCheckbox = new QCheckBox("Show Execute Access", this);
    
    connect(_readLayerCheckbox, &QCheckBox::toggled, this, &MemoryWidget::toggleReadLayer);
    connect(_writeLayerCheckbox, &QCheckBox::toggled, this, &MemoryWidget::toggleWriteLayer);
    connect(_executeLayerCheckbox, &QCheckBox::toggled, this, &MemoryWidget::toggleExecuteLayer);
    
    controlLayout->addWidget(_readLayerCheckbox);
    controlLayout->addWidget(_writeLayerCheckbox);
    controlLayout->addWidget(_executeLayerCheckbox);
    controlLayout->addStretch();
    
    _layout->addLayout(controlLayout, 4, 0, 1, 2);
    
    setLayout(_layout);
}

void MemoryWidget::setEmulator(Emulator* emulator)
{
    _emulator = emulator;
    reset();
}

void MemoryWidget::reset()
{
    // Clear access tracking
    for (int i = 0; i < 4; i++) {
        memset(_readAccess[i], 0, 16384);
        memset(_writeAccess[i], 0, 16384);
        memset(_executeAccess[i], 0, 16384);
    }
    
    refresh();
}

void MemoryWidget::refresh()
{
    if (!_emulator)
        return;
    
    updateMemoryImage();
    update();
}

void MemoryWidget::updateMemoryImage()
{
    if (!_emulator)
        return;
    
    Memory* memory = _emulator->GetMemory();
    if (!memory)
        return;
    
    // For each bank (0-3)
    for (int bank = 0; bank < 4; bank++) {
        // Get the memory page connected to this bank
        uint8_t* pageAddress = memory->MapZ80AddressToPhysicalAddress(bank * 0x4000);
        
        if (!pageAddress) {
            _memoryImage[bank].fill(Qt::black);
            continue;
        }
        
        // Update bank label with actual page number
        int pageNumber = memory->GetPhysicalOffsetForZ80Bank(bank) / 0x4000;
        _bankLabels[bank]->setText(QString("Bank %1 (0x%2) - Page %3")
                                  .arg(bank)
                                  .arg(bank * 0x4000, 4, 16, QChar('0'))
                                  .arg(pageNumber));
        
        // Fill the image with memory data
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 256; x++) {
                int offset = y * 256 + x;
                if (offset < 16384) {
                    uint8_t value = pageAddress[offset];
                    
                    // Get base color for the byte value
                    QColor color = getColorForByte(value);
                    
                    // Apply access layers if enabled
                    if (_showReadLayer && _readAccess[bank][offset]) {
                        color = color.lighter(120);  // Make it lighter to indicate read
                    }
                    
                    if (_showWriteLayer && _writeAccess[bank][offset]) {
                        color = QColor(color.red(), color.green() * 0.8, color.blue() * 0.8);  // Reddish tint for writes
                    }
                    
                    if (_showExecuteLayer && _executeAccess[bank][offset]) {
                        color = QColor(color.red() * 0.8, color.green(), color.blue() * 0.8);  // Greenish tint for execution
                    }
                    
                    _memoryImage[bank].setPixelColor(x, y, color);
                }
            }
        }
        
        // Update the image label
        _imageLabels[bank]->setPixmap(QPixmap::fromImage(_memoryImage[bank]));
    }
}

QColor MemoryWidget::getColorForByte(uint8_t value)
{
    // Special colors for 0x00 and 0xFF
    if (value == 0x00)
        return QColor(0, 0, 128);  // Dark blue for 0x00
    
    if (value == 0xFF)
        return QColor(128, 0, 0);  // Dark red for 0xFF
    
    // Shades of gray for other values, scaled proportionally
    int gray = 80 + (value * 120) / 255;  // Scale to range 80-200
    return QColor(gray, gray, gray);
}

void MemoryWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    
    // Additional custom painting if needed
}

void MemoryWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refresh();
}

void MemoryWidget::toggleReadLayer(bool checked)
{
    _showReadLayer = checked;
    refresh();
}

void MemoryWidget::toggleWriteLayer(bool checked)
{
    _showWriteLayer = checked;
    refresh();
}

void MemoryWidget::toggleExecuteLayer(bool checked)
{
    _showExecuteLayer = checked;
    refresh();
}
