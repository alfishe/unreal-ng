#include "floppydiskwidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QDebug>

FloppyDiskWidget::FloppyDiskWidget(QWidget *parent)
    : QWidget(parent)
{
    createUI();
    setMinimumSize(200, 200);
}

FloppyDiskWidget::~FloppyDiskWidget()
{
    // Clean up any resources if needed
}

void FloppyDiskWidget::createUI()
{
    QGridLayout* layout = new QGridLayout(this);
    
    _titleLabel = new QLabel("Floppy Disk", this);
    _titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(_titleLabel, 0, 0, 1, 2);
    
    // Create labels for disk information
    layout->addWidget(new QLabel("Drive:", this), 1, 0);
    _driveLabel = new QLabel("A", this);
    layout->addWidget(_driveLabel, 1, 1);
    
    layout->addWidget(new QLabel("Track:", this), 2, 0);
    _trackLabel = new QLabel("0", this);
    layout->addWidget(_trackLabel, 2, 1);
    
    layout->addWidget(new QLabel("Head:", this), 3, 0);
    _headLabel = new QLabel("0", this);
    layout->addWidget(_headLabel, 3, 1);
    
    layout->addWidget(new QLabel("Sector:", this), 4, 0);
    _sectorLabel = new QLabel("1", this);
    layout->addWidget(_sectorLabel, 4, 1);
    
    layout->addWidget(new QLabel("Motor:", this), 5, 0);
    _motorLabel = new QLabel("Off", this);
    layout->addWidget(_motorLabel, 5, 1);
    
    layout->addWidget(new QLabel("R/W:", this), 6, 0);
    _rwLabel = new QLabel("Idle", this);
    layout->addWidget(_rwLabel, 6, 1);
    
    // Add spacer to push content to the top
    layout->setRowStretch(7, 1);
    
    setLayout(layout);
}

void FloppyDiskWidget::setEmulator(Emulator* emulator)
{
    _emulator = emulator;
    reset();
}

void FloppyDiskWidget::reset()
{
    _currentDrive = 0;
    _currentTrack = 0;
    _currentHead = 0;
    _currentSector = 1;
    _motorOn = false;
    _isReading = false;
    _isWriting = false;
    
    refresh();
}

void FloppyDiskWidget::refresh()
{
    if (!_emulator)
        return;
    
    updateDiskInfo();
    update(); // Trigger a repaint
}

void FloppyDiskWidget::updateDiskInfo()
{
    if (!_emulator)
        return;
    
    // In a real implementation, we would get the actual floppy disk state from the emulator
    // For now, we'll just update the widget with the current state
    
    if (_emulator->IsPaused()) {
        // Get FDC information from emulator
        // This is a simplified example - actual implementation would use emulator's FDC data
        
        // For demonstration, we'll use some placeholder values
        // In a real implementation, we would get these from the emulator's FDC controller
        
        // Update labels
        _driveLabel->setText(QString("%1").arg(_currentDrive));
        _trackLabel->setText(QString("%1").arg(_currentTrack));
        _headLabel->setText(QString("%1").arg(_currentHead));
        _sectorLabel->setText(QString("%1").arg(_currentSector));
        _motorLabel->setText(_motorOn ? "On" : "Off");
        
        QString rwStatus = "Idle";
        if (_isReading) rwStatus = "Reading";
        if (_isWriting) rwStatus = "Writing";
        _rwLabel->setText(rwStatus);
        
        // Set colors based on state
        _motorLabel->setStyleSheet(_motorOn ? "color: green; font-weight: bold;" : "color: black;");
        _rwLabel->setStyleSheet(_isReading || _isWriting ? "color: red; font-weight: bold;" : "color: black;");
    }
}

void FloppyDiskWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw a visual representation of the disk
    int diskSize = qMin(width(), height() - 150) - 40;
    int diskX = (width() - diskSize) / 2;
    int diskY = 150;
    
    // Draw disk outline
    painter.setPen(QPen(Qt::black, 2));
    painter.setBrush(QColor(200, 200, 200));
    painter.drawEllipse(diskX, diskY, diskSize, diskSize);
    
    // Draw inner circle
    int innerSize = diskSize / 3;
    int innerX = diskX + (diskSize - innerSize) / 2;
    int innerY = diskY + (diskSize - innerSize) / 2;
    painter.setBrush(QColor(100, 100, 100));
    painter.drawEllipse(innerX, innerY, innerSize, innerSize);
    
    // Draw track indicator
    if (_currentTrack > 0) {
        int trackRadius = innerSize / 2 + (_currentTrack * (diskSize - innerSize) / 160);
        painter.setPen(QPen(Qt::red, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(
            diskX + (diskSize - trackRadius * 2) / 2,
            diskY + (diskSize - trackRadius * 2) / 2,
            trackRadius * 2,
            trackRadius * 2
        );
    }
    
    // Draw sector indicator
    if (_currentSector > 0) {
        painter.setPen(QPen(Qt::blue, 2));
        painter.setBrush(QColor(0, 0, 255, 64));
        
        // Calculate sector angle
        float sectorAngle = 360.0f / 18.0f; // Assuming 18 sectors per track
        float startAngle = (_currentSector - 1) * sectorAngle * 16;
        
        painter.drawPie(diskX, diskY, diskSize, diskSize, startAngle, sectorAngle * 16);
    }
    
    // Draw motor indicator
    if (_motorOn) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::green);
        painter.drawEllipse(width() - 20, 10, 10, 10);
    }
    
    // Draw R/W indicator
    if (_isReading || _isWriting) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(_isReading ? Qt::blue : Qt::red);
        painter.drawEllipse(width() - 20, 30, 10, 10);
    }
}

void FloppyDiskWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refresh();
}
