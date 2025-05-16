#ifndef DEVICESCREEN_H
#define DEVICESCREEN_H

#include <QImage>
#include <QWidget>

namespace Ui
{
    class DeviceScreen;
}

class DeviceScreen : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceScreen(QWidget *parent = nullptr);
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
    void handleExternalKeyPress(QKeyEvent *event);
    void handleExternalKeyRelease(QKeyEvent *event);

protected:
    void paintEvent(QPaintEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;
    using QWidget::heightForWidth;  // Bring method declaration from QWidget
    int heightForWidth(int width);

private:
    Ui::DeviceScreen* ui = nullptr;

    QRectF devicePixelsRect;
    QImage* devicePixels = nullptr;

    float ratio = 352.0f / 288.0f;
};

#endif // DEVICESCREEN_H
