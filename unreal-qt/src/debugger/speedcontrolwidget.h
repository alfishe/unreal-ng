#pragma once
#ifndef SPEEDCONTROLWIDGET_H
#define SPEEDCONTROLWIDGET_H

#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <vector>

class Emulator;

/// @brief How a preset executes its step
enum class StepType
{
    FullSpeed,   // Resume normal execution
    Frame,       // Call RunFrame()
    TStates      // Call RunTStates(N)
};

/// @brief Per-preset configuration for the speed control slider
struct PresetConfig
{
    const char* label;       // Display label
    StepType stepType;       // How to execute
    int fixedIntervalMs;     // Fixed timer interval (0 = use spinner, -1 = no timer)
    int spinnerMin;          // Spinner minimum (ms), 0 = disabled
    int spinnerMax;          // Spinner maximum (ms), 0 = disabled
    int spinnerStep;         // Spinner step increment (ms)

    // T-state calculation: result = (F * frameNum + L * lineNum) / divisor
    // F = t-states per frame, L = t-states per scanline
    int frameNum = 0;        // Frame multiplier numerator
    int lineNum = 0;         // Scanline multiplier numerator
    int divisor = 1;         // Divisor for fractional values
};

/// @brief Draws evenly-spaced vertical tick marks aligned to a companion QSlider
class TickMarkWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TickMarkWidget(int tickCount, QSlider* slider, QWidget* parent = nullptr)
        : QWidget(parent), _tickCount(tickCount), _slider(slider)
    {
        setFixedHeight(10);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (_tickCount < 2 || !_slider)
            return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        // Map the slider's left/right edges into our coordinate space
        QPoint sliderTopLeft = _slider->mapTo(parentWidget(), QPoint(0, 0));
        QPoint myTopLeft = mapTo(parentWidget(), QPoint(0, 0));
        int sliderLeft = sliderTopLeft.x() - myTopLeft.x();
        int sliderRight = sliderLeft + _slider->width();

        // Inset by handle half-width to align with groove endpoints
        const int handleHalf = 7;
        const int left = sliderLeft + handleHalf;
        const int right = sliderRight - handleHalf;
        const int usable = right - left;

        if (usable <= 0)
            return;

        const int midIndex = (_tickCount - 1) / 2;
        const QColor majorColor = palette().color(QPalette::Text);
        const QColor minorColor = palette().color(QPalette::Mid);

        for (int i = 0; i < _tickCount; ++i)
        {
            bool isMajor = (i == 0 || i == _tickCount - 1 || i == midIndex);
            int x = left + (usable * i) / (_tickCount - 1);
            int tickHeight = isMajor ? height() : height() / 2;

            QPen pen(isMajor ? majorColor : minorColor);
            pen.setWidth(1);
            p.setPen(pen);
            p.drawLine(x, 0, x, tickHeight - 1);
        }
    }

private:
    int _tickCount;
    QSlider* _slider;
};

/// @brief Step granularity selector with audio player-style controls.
/// Provides configurable speed presets from full-speed to 1 T-state.
/// QTimer orchestrates repeated calls to existing Emulator step methods.
class SpeedControlWidget : public QGroupBox
{
    Q_OBJECT

public:
    explicit SpeedControlWidget(QWidget* parent = nullptr);

    /// @brief Set the emulator instance to control
    void setEmulator(Emulator* emulator);

    /// @brief Execute one step at the current granularity
    void executeStep();

    /// @brief Returns true if slider is at position 0 (full speed)
    bool isFullSpeed() const;

    /// @brief Returns true if the auto-step timer is running
    bool isRunning() const;

    /// @brief Stop auto-stepping if active
    void stop();

    /// @brief Access the preset collection
    static const std::vector<PresetConfig>& presets();

signals:
    /// @brief Emitted after each step so parent can refresh UI
    void stepped();

    /// @brief Emitted when auto-stepping starts/stops
    void runningChanged(bool running);

private slots:
    void onSliderChanged(int position);
    void onPlayPauseClicked();
    void onStepForwardClicked();
    void onStepBackwardClicked();
    void onTimerTick();

private:
    void updateLabel();
    unsigned getTStatesForPreset(int position) const;
    int getTimerIntervalMs(int position) const;

    Emulator* _emulator = nullptr;
    QSlider* _slider = nullptr;
    QLabel* _label = nullptr;
    QToolButton* _stepBackButton = nullptr;
    QToolButton* _playPauseButton = nullptr;
    QSpinBox* _intervalSpinBox = nullptr;  // Auto-run interval in ms
    QToolButton* _stepForwardButton = nullptr;
    QTimer* _timer = nullptr;
};

#endif  // SPEEDCONTROLWIDGET_H
