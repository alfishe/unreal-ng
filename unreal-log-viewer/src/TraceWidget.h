#pragma once

#include <QWidget>
#include <QTimer>
#include <QTabWidget>
#include <QTableWidget>
#include <QLabel>

class TraceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TraceWidget(QWidget* parent = nullptr);

public slots:
    void attachToShm(void* base, size_t size);
    void detachFromShm();
    void onFrameReady();
    void setFifoActive(bool active);

private slots:
    void onRefresh();

private:
    void refreshCallTrace();
    void refreshOpcodeTrace();

    void* _shmBase = nullptr;
    size_t _shmSize = 0;
    QTimer* _refreshTimer = nullptr;

    QTabWidget* _tabs = nullptr;
    QTableWidget* _callTable = nullptr;
    QTableWidget* _opcodeTable = nullptr;
    QLabel* _callInfo = nullptr;
    QLabel* _opcodeInfo = nullptr;

    uint32_t _frameCounter = 0;
    bool _fifoActive = false;

    static constexpr int REFRESH_MS = 100;
    static constexpr int FALLBACK_MS = 500;
    static constexpr int FRAME_DIVISOR = 5;
};
