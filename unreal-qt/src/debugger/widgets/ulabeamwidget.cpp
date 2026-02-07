#include "ulabeamwidget.h"

#include <QDebug>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "emulator/video/screen.h"

ULABeamWidget::ULABeamWidget(QWidget* parent) : QWidget(parent)
{
    createUI();
    setMinimumSize(200, 200);
}

ULABeamWidget::~ULABeamWidget()
{
    // Clean up any resources if needed
}

void ULABeamWidget::createUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    _titleLabel = new QLabel("ULA Beam Position", this);
    _titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(_titleLabel);

    // Add spacer to push content to the top
    layout->addStretch();

    setLayout(layout);
}

void ULABeamWidget::setEmulator(Emulator* emulator)
{
    _emulator = emulator;
    reset();
}

void ULABeamWidget::reset()
{
    _beamX = 0;
    _beamY = 0;
    refresh();
}

void ULABeamWidget::refresh()
{
    if (!_emulator)
        return;

    CONFIG& config = _emulator->GetContext()->config;
    if (config.t_line == 0 || config.frame == 0)
        return;

    Z80* cpu = _emulator->GetContext()->pCore->GetZ80();
    if (cpu)
    {
        _currentTstate = cpu->t;
        int tStatesPerLine = config.t_line;
        _currentLine = (_currentTstate % config.frame) / tStatesPerLine;
        _linePosition = _currentTstate % tStatesPerLine;

        _beamY = _currentLine;
        _beamX = _linePosition / 4;
    }

    updateScreenImage();
    update();
}

void ULABeamWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int totalWidth = width() - 20;
    int totalHeight = height() - 60;

    int fullWidth = SCREEN_WIDTH + 2 * BORDER_WIDTH;
    int fullHeight = SCREEN_HEIGHT + 2 * BORDER_HEIGHT;

    _scaleX = static_cast<float>(totalWidth) / fullWidth;
    _scaleY = static_cast<float>(totalHeight) / fullHeight;

    float scale = qMin(_scaleX, _scaleY);

    int screenX = (width() - fullWidth * scale) / 2;
    int screenY = 30 + (totalHeight - fullHeight * scale) / 2;

    if (!_screenImage.isNull())
    {
        QRect destRect(screenX, screenY, fullWidth * scale, fullHeight * scale);
        painter.drawImage(destRect, _screenImage);
    }
    else
    {
        painter.setPen(Qt::black);
        painter.setBrush(QColor(160, 160, 160));
        painter.drawRect(screenX, screenY, fullWidth * scale, fullHeight * scale);
    }

    painter.setPen(QPen(Qt::darkGray, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(screenX + BORDER_WIDTH * scale, screenY + BORDER_HEIGHT * scale, SCREEN_WIDTH * scale,
                     SCREEN_HEIGHT * scale);

    if (_beamX >= 0 && _beamX < fullWidth && _beamY >= 0 && _beamY < fullHeight)
    {
        int beamScreenX = screenX + _beamX * scale;
        int beamScreenY = screenY + _beamY * scale;

        painter.setPen(QPen(QColor(255, 0, 0, 128), 1, Qt::DashLine));
        painter.drawLine(screenX, beamScreenY, screenX + fullWidth * scale, beamScreenY);
        painter.drawLine(beamScreenX, screenY, beamScreenX, screenY + fullHeight * scale);

        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::red);
        painter.drawEllipse(beamScreenX - 3, beamScreenY - 3, 6, 6);

        painter.setPen(Qt::white);
        QString infoText = QString("T-state: %1 | Line: %2 | Pos: %3 | Beam: (%4, %5)")
                               .arg(_currentTstate)
                               .arg(_currentLine)
                               .arg(_linePosition)
                               .arg(_beamX)
                               .arg(_beamY);
        painter.drawText(5, height() - 5, infoText);
    }
}

void ULABeamWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refresh();
}

void ULABeamWidget::updateScreenImage()
{
    if (!_emulator)
        return;

    FramebufferDescriptor fb = _emulator->GetFramebuffer();
    if (!fb.memoryBuffer || fb.width == 0 || fb.height == 0)
        return;

    int fullWidth = SCREEN_WIDTH + 2 * BORDER_WIDTH;
    int fullHeight = SCREEN_HEIGHT + 2 * BORDER_HEIGHT;

    if (_screenImage.width() != fullWidth || _screenImage.height() != fullHeight)
        _screenImage = QImage(fullWidth, fullHeight, QImage::Format_RGB32);

    uint32_t* src = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    for (int y = 0; y < fullHeight && y < fb.height; y++)
    {
        QRgb* destLine = reinterpret_cast<QRgb*>(_screenImage.scanLine(y));
        for (int x = 0; x < fullWidth && x < fb.width; x++)
        {
            uint32_t pixel = src[y * fb.width + x];
            int r = (pixel >> 16) & 0xFF;
            int g = (pixel >> 8) & 0xFF;
            int b = pixel & 0xFF;
            int gray = (r + g + b) / 3;
            int faded = 128 + gray / 4;
            destLine[x] = qRgb(faded, faded, faded);
        }
    }
}
