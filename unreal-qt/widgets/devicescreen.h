#ifndef DEVICESCREEN_H
#define DEVICESCREEN_H

#include <QImage>
#include <QWidget>

namespace Ui {
class DeviceScreen;
}

class DeviceScreen : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceScreen(QWidget *parent = nullptr);
    ~DeviceScreen();

public:
    void init(uint16_t width, uint16_t height, void* buffer);
    void detach();

public slots:
    void refresh();

protected:
    void paintEvent(QPaintEvent *event);

    void keyPressEvent(QKeyEvent *event);
    void keyReleaseEvent(QKeyEvent *event);
    void mousePressEvent(QMouseEvent *event);

    void resizeEvent(QResizeEvent *event);
    int heightForWidth(int width);

private:
    Ui::DeviceScreen* ui = nullptr;

    QRectF devicePixelsRect;
    QImage* devicePixels = nullptr;

    float ratio = 352.0f / 288.0f;
};

#endif // DEVICESCREEN_H
