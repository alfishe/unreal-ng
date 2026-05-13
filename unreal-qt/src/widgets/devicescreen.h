#ifndef DEVICESCREEN_H
#define DEVICESCREEN_H

#include <QImage>
#include <QWidget>
#include <memory>

class Emulator;  // Forward declaration

namespace Ui
{
class DeviceScreen;
}

class DeviceScreen : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceScreen(QWidget* parent = nullptr);
    ~DeviceScreen() override;

public:
    void init(uint16_t width, uint16_t height, void* buffer);
    void detach();

public:
    QSize sizeHint() const override
    {
        return QSize(352, 288);
    }

public slots:
    void refresh();
    void handleExternalKeyPress(QKeyEvent* event);
    void handleExternalKeyRelease(QKeyEvent* event);
    void prepareForShutdown();  // Block refreshes during shutdown

public:
    void setEmulator(std::shared_ptr<Emulator> emulator)
    {
        _emulator = emulator;
    }

protected:
    void paintEvent(QPaintEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

    void resizeEvent(QResizeEvent* event) override;
    using QWidget::heightForWidth;  // Bring method declaration from QWidget
    int heightForWidth(int width);

private:
    Ui::DeviceScreen* ui = nullptr;

    QRectF devicePixelsRect;
    QImage* devicePixels = nullptr;

    float ratio = 352.0f / 288.0f;

    std::shared_ptr<Emulator> _emulator = nullptr;  // Reference to emulator for UUID tagging
    bool _isShuttingDown = false;  // Flag to block refreshes during shutdown
};

#endif  // DEVICESCREEN_H
