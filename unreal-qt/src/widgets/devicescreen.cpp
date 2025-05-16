#include "devicescreen.h"
#include "ui_devicescreen.h"


#include <QDebug>
#include <QKeyEvent>
#include <QPainter>

#include "emulator/keyboardmanager.h"

#include <cmath>
static inline bool isFloatsEqual(float x, float y, float epsilon = 0.01f)
{
    bool result = false;

   if (fabsf(x - y) < epsilon)
      result = true;

   return result;
}

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

    ratio = static_cast<float>(width) / static_cast<float>(height);

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

void DeviceScreen::handleExternalKeyPress(QKeyEvent *event)
{
    keyPressEvent(event);
}

void DeviceScreen::handleExternalKeyRelease(QKeyEvent *event)
{
    keyReleaseEvent(event);
}

void DeviceScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter = QPainter(this);

    if (devicePixels != nullptr)
    {
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
        painter.setRenderHint(QPainter::LosslessImageRendering);
#endif
        int newWidth = event->rect().width();
        int newHeight = event->rect().height();

        float curRatio = static_cast<float>(newWidth) / static_cast<float>(newHeight);
        if (!isFloatsEqual(curRatio, ratio))
        {
            qDebug() << "width: " << newWidth << " height: " << newHeight << " ratio: " << curRatio;
        }

        painter.drawImage(event->rect(), *devicePixels, devicePixelsRect);
    }
}

void DeviceScreen::keyPressEvent(QKeyEvent *event)
{
    event->accept();

    // Don't react on auto-repeat
    if (!event->isAutoRepeat())
    {
        quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKey(event->key());

        // Skip unknown keys
        if (zxKey != 0)
        {
            KeyboardEvent* event = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_PRESSED);

            // Send valid key combinations to emulator instance
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(MC_KEY_PRESSED, event);
        }

        QString message = QString("DeviceScreen : keyPressEvent, key : 0x%1 (%2)").arg(event->key(), 2, 16).arg(event->key());
        qDebug() << message;
    }
}

void DeviceScreen::keyReleaseEvent(QKeyEvent *event)
{
    event->accept();

    // Don't react on auto-repeat
    if (!event->isAutoRepeat())
    {
        quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKey(event->key());

        // Skip unknown keys
        if (zxKey != 0)
        {
            KeyboardEvent* event = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_RELEASED);

            // Send valid key combinations to emulator instance
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(MC_KEY_RELEASED, event);
        }
    }

    QString message = QString("DeviceScreen : keyReleaseEvent, key : 0x%1 (%2)").arg(event->key(), 2, 16).arg(event->key());
    qDebug() << message;
}

void DeviceScreen::mousePressEvent(QMouseEvent *event)
{

}

void DeviceScreen::resizeEvent(QResizeEvent *event)
{
    //int oldWidth = event->oldSize().width();
    //int oldHeight = event->oldSize().height();

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

    QWidget::resizeEvent(event);
}


