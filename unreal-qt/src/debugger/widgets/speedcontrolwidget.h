#pragma once

#include <memory>
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
#include <atomic>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;

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

/// @brief The pace ladder with audio player-style controls: a client of the core's paced run (execution::RunPacer,
/// core/src/emulator/execution/runpacer.h). The slider, the interval, play / pause and the step go through the
/// actions run.pace_set / run.pace_toggle / run.pace_step on the instance's actions worker (never blocking the GUI
/// thread); the widget shows the pacer's state from NC_RUN_PACE, so a deck dial, a script or the WebAPI moving the pace
/// moves the slider too. The pacer steps on its own thread and stops at a breakpoint.
class SpeedControlWidget : public QGroupBox
{
    Q_OBJECT

public:
    explicit SpeedControlWidget(QWidget* parent = nullptr);
    ~SpeedControlWidget() override;

    /// @brief Set the emulator instance to control
    void setEmulator(std::shared_ptr<Emulator> emulator);

    /// @brief Execute one step at the current granularity (run.pace_step)
    void executeStep();

    /// @brief Returns true if slider is at position 0 (full speed)
    bool isFullSpeed() const;

    /// @brief Returns true if the paced run runs
    bool isRunning() const;

    /// @brief Stop the paced run if it runs (the machine stays paused)
    void stop();

signals:
    /// @brief Emitted after each paced step so parent can refresh UI
    void stepped();

    /// @brief Emitted when the paced run starts/stops
    void runningChanged(bool running);

private slots:
    void onSliderChanged(int position);
    void onPlayPauseClicked();
    void onStepForwardClicked();
    void onStepBackwardClicked();

private:
    /// Show the pacer's state (GUI thread)
    void syncFromPacer();
    void updateLabel();
    /// Run a pace action on the instance's actions worker; a refusal goes to the label's tooltip
    void submit(const char* action, const StateNode& params = StateNode::Object());
    void subscribe();
    void unsubscribe();

    std::shared_ptr<Emulator> _emulator;
    QSlider* _slider = nullptr;
    QLabel* _label = nullptr;
    QToolButton* _stepBackButton = nullptr;
    QToolButton* _playPauseButton = nullptr;
    QSpinBox* _intervalSpinBox = nullptr;  // Auto-run interval in ms
    QToolButton* _stepForwardButton = nullptr;
    bool _running = false;
    bool _syncing = false;  ///< the controls follow the pacer: their signals do not submit
    uint64_t _paceObserver = 0;
    uint64_t _stepObserver = 0;
    std::shared_ptr<std::atomic<bool>> _stepPending = std::make_shared<std::atomic<bool>>(false);
};

#endif  // SPEEDCONTROLWIDGET_H
