#include "ulabeamwidget.h"

#include <QDebug>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

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

    // In a real implementation, we would get the actual beam position from the emulator
    // For now, we'll just update the widget

    // Calculate beam position based on current T-states
    // This is a simplified example - actual implementation would use emulator data
    if (_emulator->IsPaused())
    {
        // Get T-states from emulator
        CONFIG& config = _emulator->GetContext()->config;

        // Guard against uninitialized or invalid config
        if (config.t_line == 0 || config.frame == 0)
            return;

        // Get current T-state counter from Z80
        Z80* cpu = _emulator->GetContext()->pCore->GetZ80();
        if (cpu)
        {
            uint32_t tStates = cpu->t;

            // Calculate beam position based on T-states
            // This is a simplified calculation - actual would depend on specific ULA timing
            int tStatesPerLine = config.t_line;
            int currentLine = (tStates % config.frame) / tStatesPerLine;
            int currentPos = tStates % tStatesPerLine;

            _beamY = currentLine;
            _beamX = currentPos / 4;  // Divide by 4 to convert T-states to pixels (ULA draws 2 pixel per t-state)
        }
    }

    update();  // Trigger a repaint
}

void ULABeamWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate scale factors
    int totalWidth = width() - 20;    // Leave some margin
    int totalHeight = height() - 40;  // Leave margin for title

    int fullWidth = SCREEN_WIDTH + 2 * BORDER_WIDTH;
    int fullHeight = SCREEN_HEIGHT + 2 * BORDER_HEIGHT;

    _scaleX = static_cast<float>(totalWidth) / fullWidth;
    _scaleY = static_cast<float>(totalHeight) / fullHeight;

    // Use the smaller scale to maintain aspect ratio
    float scale = qMin(_scaleX, _scaleY);

    // Calculate centered position
    int screenX = (width() - fullWidth * scale) / 2;
    int screenY = 30 + (totalHeight - fullHeight * scale) / 2;

    // Draw border
    painter.setPen(Qt::black);
    painter.setBrush(QColor(0, 0, 0, 32));  // Semi-transparent black
    painter.drawRect(screenX, screenY, fullWidth * scale, fullHeight * scale);

    // Draw screen area
    painter.setBrush(QColor(0, 0, 0, 64));  // Darker for screen area
    painter.drawRect(screenX + BORDER_WIDTH * scale, screenY + BORDER_HEIGHT * scale, SCREEN_WIDTH * scale,
                     SCREEN_HEIGHT * scale);

    // Draw beam position if within visible area
    if (_beamX >= 0 && _beamX < fullWidth && _beamY >= 0 && _beamY < fullHeight)
    {
        int beamScreenX = screenX + _beamX * scale;
        int beamScreenY = screenY + _beamY * scale;

        // Draw a small circle for the beam
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::red);
        painter.drawEllipse(beamScreenX - 3, beamScreenY - 3, 6, 6);

        // Draw beam lines
        painter.setPen(QPen(QColor(255, 0, 0, 128), 1, Qt::DashLine));
        painter.drawLine(screenX, beamScreenY, screenX + fullWidth * scale, beamScreenY);
        painter.drawLine(beamScreenX, screenY, beamScreenX, screenY + fullHeight * scale);

        // Show coordinates
        painter.setPen(Qt::white);
        painter.drawText(5, height() - 5, QString("Beam: X=%1, Y=%2").arg(_beamX).arg(_beamY));
    }
}

void ULABeamWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refresh();
}
