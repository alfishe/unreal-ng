#include "floppydiskwidget.h"

#undef signals
#include "../../../core/src/emulator/io/fdc/fdd.h"
#include "../../../core/src/emulator/io/fdc/wd1793.h"
#define signals Q_SIGNALS

#include <QDebug>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>

FloppyDiskWidget::FloppyDiskWidget(QWidget* parent) : QWidget(parent)
{
    createUI();
    setMinimumSize(150, 150);
}

FloppyDiskWidget::~FloppyDiskWidget()
{
    // Clean up any resources if needed
}

void FloppyDiskWidget::createUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(2);

    _titleLabel = new QLabel("Floppy Disk", this);
    _titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(_titleLabel);

    // Compact horizontal metadata layout
    QHBoxLayout* infoLayout = new QHBoxLayout();
    infoLayout->setSpacing(8);

    // Drive info compact: "A: T00 H0 S01"
    _driveLabel = new QLabel("A:", this);
    _driveLabel->setStyleSheet("font-weight: bold;");
    infoLayout->addWidget(_driveLabel);

    _trackLabel = new QLabel("T00", this);
    infoLayout->addWidget(_trackLabel);

    _headLabel = new QLabel("H0", this);
    infoLayout->addWidget(_headLabel);

    _sectorLabel = new QLabel("S01", this);
    infoLayout->addWidget(_sectorLabel);

    infoLayout->addStretch();

    _motorLabel = new QLabel("Motor: Off", this);
    infoLayout->addWidget(_motorLabel);

    _rwLabel = new QLabel("Idle", this);
    _rwLabel->setMinimumWidth(50);
    infoLayout->addWidget(_rwLabel);

    mainLayout->addLayout(infoLayout);

    // Spacer - push disk visualization area
    mainLayout->addStretch(1);

    setLayout(mainLayout);
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
    update();  // Trigger a repaint
}

void FloppyDiskWidget::updateDiskInfo()
{
    if (!_emulator)
        return;

    EmulatorContext* context = _emulator->GetContext();
    if (!context)
        return;

    const WD1793* fdc = context->pBetaDisk;
    if (!fdc)
    {
        _titleLabel->setText("Floppy Disk (No FDC)");
        _driveLabel->setText("-:");
        _trackLabel->setText("T--");
        _headLabel->setText("H-");
        _sectorLabel->setText("S--");
        _motorLabel->setText("Motor: -");
        _rwLabel->setText("-");
        return;
    }

    _titleLabel->setText("Floppy Disk");

    _currentTrack = fdc->getTrackRegister();
    _currentSector = fdc->getSectorRegister();

    uint8_t status = fdc->getStatusRegister();
    bool busy = (status & WD1793::WDS_BUSY) != 0;

    uint8_t beta128Status = fdc->getBeta128Status();
    (void)beta128Status;

    FDD* drive = const_cast<WD1793*>(fdc)->getDrive();
    if (drive)
    {
        _motorOn = drive->getMotor();
        _currentHead = drive->getSide() ? 1 : 0;
    }
    else
    {
        _motorOn = false;
        _currentHead = 0;
    }

    WD1793::WD_COMMANDS lastCmd = fdc->getLastDecodedCommand();
    _isReading = busy && (lastCmd == WD1793::WD_CMD_READ_SECTOR || lastCmd == WD1793::WD_CMD_READ_TRACK ||
                          lastCmd == WD1793::WD_CMD_READ_ADDRESS);
    _isWriting = busy && (lastCmd == WD1793::WD_CMD_WRITE_SECTOR || lastCmd == WD1793::WD_CMD_WRITE_TRACK);

    static const char* driveLetters[] = {"A:", "B:", "C:", "D:"};
    _driveLabel->setText(driveLetters[_currentDrive & 0x03]);
    _trackLabel->setText(QString("T%1").arg(_currentTrack, 2, 10, QChar('0')));
    _headLabel->setText(QString("H%1").arg(_currentHead));
    _sectorLabel->setText(QString("S%1").arg(_currentSector, 2, 10, QChar('0')));
    _motorLabel->setText(_motorOn ? "Motor: On" : "Motor: Off");

    QString rwStatus = "Idle";
    if (_isReading)
        rwStatus = "Read";
    else if (_isWriting)
        rwStatus = "Write";
    else if (busy)
        rwStatus = "Busy";
    _rwLabel->setText(rwStatus);

    _motorLabel->setStyleSheet(_motorOn ? "color: green; font-weight: bold;" : "");

    if (_isReading)
        _rwLabel->setStyleSheet("color: blue; font-weight: bold;");
    else if (_isWriting)
        _rwLabel->setStyleSheet("color: red; font-weight: bold;");
    else if (busy)
        _rwLabel->setStyleSheet("color: orange; font-weight: bold;");
    else
        _rwLabel->setStyleSheet("");
}

void FloppyDiskWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate disk position - header is now compact (title + one line of metadata)
    const int headerHeight = 50;  // Compact header area
    int availableHeight = height() - headerHeight - 10;
    int diskSize = qMin(width() - 20, availableHeight);
    if (diskSize < 50)
        return;  // Too small to draw

    int diskX = (width() - diskSize) / 2;
    int diskY = headerHeight;

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
    if (_currentTrack > 0)
    {
        int trackRadius = innerSize / 2 + (_currentTrack * (diskSize - innerSize) / 160);
        painter.setPen(QPen(Qt::red, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(diskX + (diskSize - trackRadius * 2) / 2, diskY + (diskSize - trackRadius * 2) / 2,
                            trackRadius * 2, trackRadius * 2);
    }

    // Draw sector indicator
    if (_currentSector > 0)
    {
        painter.setPen(QPen(Qt::blue, 2));
        painter.setBrush(QColor(0, 0, 255, 64));

        // Calculate sector angle
        float sectorAngle = 360.0f / 18.0f;  // Assuming 18 sectors per track
        float startAngle = (_currentSector - 1) * sectorAngle * 16;

        painter.drawPie(diskX, diskY, diskSize, diskSize, startAngle, sectorAngle * 16);
    }

    // Draw motor indicator (top right corner)
    if (_motorOn)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::green);
        painter.drawEllipse(width() - 20, 10, 10, 10);
    }

    // Draw R/W indicator
    if (_isReading || _isWriting)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(_isReading ? Qt::blue : Qt::red);
        painter.drawEllipse(width() - 20, 25, 10, 10);
    }
}

void FloppyDiskWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refresh();
}
