#pragma once

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QLabel>

class FramebufferWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FramebufferWidget(QWidget* parent = nullptr);

public slots:
    void attachToShm(void* base, size_t size);
    void detachFromShm();
    void onFrameReady();
    void setFifoActive(bool active);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onRefresh();

private:
    void* _shmBase = nullptr;
    size_t _shmSize = 0;
    QTimer* _refreshTimer = nullptr;
    QImage _image;
    QLabel* _infoLabel = nullptr;
    bool _fifoActive = false;

    static constexpr int REFRESH_MS = 50;
    static constexpr int FALLBACK_MS = 500;
};
