#include "bordertimingwidget.h"

#include <QDebug>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

BorderTimingWidget::BorderTimingWidget(QWidget* parent) : QWidget(parent)
{
    createUI();
    setMinimumSize(200, 200);

    // Initialize border history
    _borderValues.resize(MAX_HISTORY);
    _borderValues.fill(0);

    _tStates.resize(MAX_HISTORY);
    _tStates.fill(0);
}

BorderTimingWidget::~BorderTimingWidget()
{
    // Clean up any resources if needed
}

void BorderTimingWidget::createUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    _titleLabel = new QLabel("Border T-States", this);
    _titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(_titleLabel);

    // Add spacer to push content to the top
    layout->addStretch();

    setLayout(layout);
}

void BorderTimingWidget::setEmulator(Emulator* emulator)
{
    _emulator = emulator;
    reset();
}

void BorderTimingWidget::reset()
{
    _borderValues.fill(0);
    _tStates.fill(0);
    _currentFrameTStates = 0;

    if (_emulator)
    {
        CONFIG& config = _emulator->GetContext()->config;
        _totalFrameTStates = config.frame;
    }

    refresh();
}

void BorderTimingWidget::refresh()
{
    if (!_emulator)
        return;

    // In a real implementation, we would get the actual border timing data from the emulator
    // For now, we'll just update the widget with simulated data

    if (_emulator->IsPaused())
    {
        // Get current T-state counter from Z80
        Z80* cpu = _emulator->GetContext()->pCore->GetZ80();
        if (cpu)
        {
            _currentFrameTStates = cpu->t % _totalFrameTStates;

            // In a real implementation, we would get the border color history
            // For now, we'll just use the current border color
            uint8_t borderColor = 0x00;
            //_emulator->GetContext()GetPortDecoder()->GetBorderColor();

            // Shift history and add new value
            for (int i = 0; i < MAX_HISTORY - 1; i++)
            {
                _borderValues[i] = _borderValues[i + 1];
                _tStates[i] = _tStates[i + 1];
            }
            _borderValues[MAX_HISTORY - 1] = borderColor;
            _tStates[MAX_HISTORY - 1] = _currentFrameTStates;
        }
    }

    update();  // Trigger a repaint
}

void BorderTimingWidget::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    // Guard against division by zero (when emulator not set or config not loaded)
    if (_totalFrameTStates == 0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate dimensions
    int graphWidth = width() - 20;    // Leave some margin
    int graphHeight = height() - 80;  // Leave margin for title and bottom labels
    int graphX = 10;
    int graphY = 40;

    // Draw graph background
    painter.setPen(Qt::black);
    painter.setBrush(QColor(240, 240, 240));
    painter.drawRect(graphX, graphY, graphWidth, graphHeight);

    // Draw T-state scale
    painter.setPen(Qt::gray);
    for (int i = 0; i <= 10; i++)
    {
        int x = graphX + (i * graphWidth) / 10;
        painter.drawLine(x, graphY, x, graphY + graphHeight);

        // Draw scale label
        int tState = (i * _totalFrameTStates) / 10;
        painter.drawText(x - 15, graphY + graphHeight + 15, QString::number(tState));
    }

    // Draw current T-state marker
    int currentX = graphX + (_currentFrameTStates * graphWidth) / _totalFrameTStates;
    painter.setPen(QPen(Qt::red, 2));
    painter.drawLine(currentX, graphY, currentX, graphY + graphHeight);

    // Draw border color history
    if (_borderValues.size() > 1)
    {
        for (int i = 0; i < _borderValues.size() - 1; i++)
        {
            // Calculate x positions based on T-states
            int x1 = graphX + (_tStates[i] * graphWidth) / _totalFrameTStates;
            int x2 = graphX + (_tStates[i + 1] * graphWidth) / _totalFrameTStates;

            // Calculate y positions based on border values (0-7)
            int y1 = graphY + graphHeight - (_borderValues[i] * graphHeight) / 8;
            int y2 = graphY + graphHeight - (_borderValues[i + 1] * graphHeight) / 8;

            // Draw line segment
            painter.setPen(QPen(Qt::blue, 2));
            painter.drawLine(x1, y1, x2, y2);
        }
    }

    // Draw current values
    painter.setPen(Qt::black);
    painter.drawText(10, height() - 5,
                     QString("Current T-state: %1/%2, Border: %3")
                         .arg(_currentFrameTStates)
                         .arg(_totalFrameTStates)
                         .arg(_borderValues.last()));
}

void BorderTimingWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refresh();
}
