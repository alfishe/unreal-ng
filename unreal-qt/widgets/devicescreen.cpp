#include "devicescreen.h"
#include "ui_devicescreen.h"

#include <QDebug>
#include <QKeyEvent>
#include <QPainter>


DeviceScreen::DeviceScreen(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DeviceScreen)
{
    ui->setupUi(this);
}

DeviceScreen::~DeviceScreen()
{
    detach();

    delete ui;
}

void DeviceScreen::init(uint16_t width, uint16_t height, void* buffer)
{
    detach();

    devicePixelsRect = QRectF(0.0, 0.0, width, height);
    devicePixels = new QImage(static_cast<const unsigned char*>(buffer), width, height, QImage::Format_RGBA8888);
}

void DeviceScreen::detach()
{
    if (devicePixels)
    {
        delete devicePixels;
        devicePixels = nullptr;
    }
}

void DeviceScreen::refresh()
{
    update();
}

void DeviceScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter = QPainter(this);

    if (devicePixels != nullptr)
    {
        painter.setRenderHint(QPainter::LosslessImageRendering);

        painter.drawImage(rect(), *devicePixels, devicePixelsRect);
    }
}

void DeviceScreen::keyPressEvent(QKeyEvent *event)
{
    event->accept();

     qDebug() << "DeviceScreen : keyPressEvent , key : " << event->text();
}

void DeviceScreen::mousePressEvent(QMouseEvent *event)
{

}

void DeviceScreen::resizeEvent(QResizeEvent *event)
{
    static constexpr float ratio = 352.0 / 288.0;

    float width = static_cast<float>(event->size().width());
    float height = static_cast<float>(event->size().height());
    int newWidth;
    int newHeight;

    if (height * ratio < width)
    {
      newWidth = static_cast<int>(height * ratio);
      newHeight = static_cast<int>(height);
    }
    else
    {
        newWidth = static_cast<int>(width);
        newHeight = static_cast<int>(width / ratio);
    }

    resize(newWidth, newHeight);
}


