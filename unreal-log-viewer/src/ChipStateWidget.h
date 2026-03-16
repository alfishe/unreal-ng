#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QLabel>
#include <QTabWidget>
#include <QMap>
#include <QGroupBox>

namespace monitoring { struct AYStateSnapshot; }
class SparklineWidget;
class EnvelopeShapeWidget;

class ChipStateWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChipStateWidget(QWidget* parent = nullptr);

public slots:
    void attachToShm(void* base, size_t size);
    void detachFromShm();
    void onFrameReady();
    void setFifoActive(bool active);

private slots:
    void onRefresh();

private:
    QWidget* createZ80Page();
    QWidget* createAYPage();
    QGroupBox* createAYChipPanel(int chipIndex);
    QWidget* createFDCPage();

    void refreshZ80();
    void refreshAY();
    void refreshFDC();
    void setField(const QString& key, const QString& value);
    void updateActivityColors();
    void formatAYRegLabels(int chipIdx, const monitoring::AYStateSnapshot& snap);

    void* _shmBase = nullptr;
    size_t _shmSize = 0;
    QTimer* _refreshTimer = nullptr;
    QTabWidget* _tabs = nullptr;

    // All value labels indexed by name
    QMap<QString, QLabel*> _fields;

    // AY #1 panel (hidden when no TurboSound)
    QGroupBox* _ay1Panel = nullptr;
    QElapsedTimer _ay1ActiveTimer;
    bool _ay1WasActive = false;

    bool _fifoActive = false;

    // Activity tracking for value-change indicators (Z80, FDC, AY decoded)
    struct ValueActivity
    {
        float heat = 0.0f;
        float peakHeat = 0.0f;  // Heat snapshot at last change (for smooth fade)
        qint64 lastChangeMs = 0;
        QString value;   // Current plain-text display value
    };

    // Helper: compute smooth indicator intensity from heat + elapsed time
    static float indicatorIntensity(const ValueActivity& act, qint64 now);

    // Helper: build HTML indicator square (<span>)
    static QString indicatorSquare(float intensity,
                                   double rRatio, double gRatio, double bRatio);

    // Indicator labels (separate grid column for alignment)
    QMap<QString, QLabel*> _indicators;

    // Sparkline mini-graphs for AY decoded values
    QMap<QString, SparklineWidget*> _sparklines;

    // Envelope shape visualization widgets
    QMap<QString, EnvelopeShapeWidget*> _envShapes;

    QMap<QString, ValueActivity> _valueActivity;
    QElapsedTimer _activityClock;

    // Previous AY register values for fallback change detection
    // (used when emulator doesn't provide per-register activity counters)
    uint8_t _prevAYRegs[2][16] = {};
    bool _prevAYRegsValid[2] = {false, false};

    static constexpr int REFRESH_MS = 50;
    static constexpr int FALLBACK_MS = 500;
    static constexpr qint64 AY1_HIDE_TIMEOUT_MS = 5000;
    static constexpr qint64 ACTIVITY_TIMEOUT_MS = 3000;
};
