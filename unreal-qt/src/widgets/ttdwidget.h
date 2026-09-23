/**
 * @file ttdwidget.h
 * @brief Time Travel Debugging (TTD) control panel & timeline scrubber widget.
 */

#pragma once

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>

class Emulator;
class MainWindow;

/**
 * @brief TtdWidget - Time Travel Debugging control panel & timeline scrubber.
 *
 * Provides GUI controls for recording, loading/exporting .ttd files, clearing session history,
 * and interactive timeline scrubbing with immediate machine state repositioning & screen refresh.
 */
class TtdWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TtdWidget(MainWindow* mainWindow, QWidget* parent = nullptr);
    virtual ~TtdWidget() override = default;

    /// Update enabled state, status labels, and slider bounds from active emulator
    void updateState(std::shared_ptr<Emulator> activeEmulator);

    /// User preference / action toggle for widget visibility
    void setVisibleByUser(bool visible);
    bool isVisibleByUser() const { return _visibleByUser; }

    /// Exact height requested by the widget depending on user visibility and scrubber row state.
    int desiredHeight() const;
    int singleRowHeight() const;
    int doubleRowHeight() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void visibilityChanged(bool visible);
    void heightChanged();

public slots:
    void onRecordToggled();
    void onLoadSession();
    void onExportSession();
    void onClearSession();
    void onJumpStart();
    void onStepBack();
    void onStepForward();
    void onJumpEnd();
    void onResumeFromHere();
    void onSliderValueChanged(int value);
    void onSliderMoved(int value);


private:
    void updateTelemetry();
    void performSeekToFrame(uint64_t targetFrame);

    MainWindow* _mainWindow = nullptr;
    std::shared_ptr<Emulator> _activeEmulator = nullptr;
    QTimer* _telemetryTimer = nullptr;

    bool _visibleByUser = false;
    bool _isInternalSliderUpdate = false;

    // Control Row (Top)
    QWidget* _controlContainer = nullptr;
    QPushButton* _recordBtn = nullptr;
    QPushButton* _loadBtn = nullptr;
    QPushButton* _exportBtn = nullptr;
    QPushButton* _clearBtn = nullptr;
    QLabel* _statusLabel = nullptr;
    QToolButton* _closeBtn = nullptr;

    // Scrubber Row Container (Bottom)
    QWidget* _scrubberContainer = nullptr;
    QPushButton* _jumpStartBtn = nullptr;
    QPushButton* _stepBackBtn = nullptr;
    QSlider* _timelineSlider = nullptr;
    QPushButton* _stepForwardBtn = nullptr;
    QPushButton* _jumpEndBtn = nullptr;
    QPushButton* _resumeFromHereBtn = nullptr;
};
