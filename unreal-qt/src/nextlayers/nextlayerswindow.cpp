#include "nextlayerswindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextreportquery.h"
#include "emulator/state/devicestate.h"
#include "emulator/state/statenodejson.h"
#include "emulator/video/next/nextlayerimage.h"
#include "emulator/video/next/screennext.h"

namespace
{
constexpr int kImageWidth = 640, kImageHeight = 512;
constexpr NextLayer::Kind kLayers[4] = {NextLayer::Kind::Ula, NextLayer::Kind::Layer2, NextLayer::Kind::Tilemap, NextLayer::Kind::Sprites};
const char* const kLayerNames[4] = {"ULA", "Layer 2", "Tilemap", "Sprites"};

QImage ChequerBoard()
{
    QImage image(kImageWidth, kImageHeight, QImage::Format_RGBA8888);
    for (int y = 0; y < kImageHeight; y++)
        for (int x = 0; x < kImageWidth; x++)
            image.setPixel(x, y, (((x >> 3) + (y >> 3)) & 1) ? qRgb(0x50, 0x50, 0x50) : qRgb(0x38, 0x38, 0x38));
    return image;
}
}  // namespace

NextLayersWindow::NextLayersWindow(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(tr("Next Layers"));
    buildUi();
    _timer = new QTimer(this);
    _timer->setInterval(100);
    connect(_timer, &QTimer::timeout, this, &NextLayersWindow::refresh);
}

void NextLayersWindow::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    auto* bar = new QHBoxLayout();
    _capture = new QCheckBox(tr("Capture"), this);
    _capture->setToolTip(tr("Record the layers while the frame is drawn (costs about 4 % of a frame)"));
    connect(_capture, &QCheckBox::toggled, this, &NextLayersWindow::applySwitch);
    bar->addWidget(_capture);
    _mode = new QComboBox(this);
    _mode->addItem(tr("Layers"), 0);
    _mode->addItem(tr("Composed"), 1);
    _mode->addItem(tr("Winner (ids)"), 2);
    _mode->setToolTip(tr("Layers: the ticked layers stacked ULA, Layer 2, tilemap, sprites (a fixed order, not the hardware priority); "
                         "Composed: the picture as shown; Winner: which layer won each pixel"));
    connect(_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        refresh();
        emit viewChanged();
    });
    bar->addWidget(_mode);
    for (int i = 0; i < 4; i++)
    {
        _layerBox[i] = new QCheckBox(tr(kLayerNames[i]), this);
        _layerBox[i]->setChecked(true);
        connect(_layerBox[i], &QCheckBox::toggled, this, [this]() {
            refresh();
            emit viewChanged();
        });
        bar->addWidget(_layerBox[i]);
    }
    bar->addStretch(1);
    layout->addLayout(bar);

    _view = new QLabel(this);
    _view->setMinimumSize(kImageWidth / 2, kImageHeight / 2);
    _view->setScaledContents(true);
    _view->setMouseTracking(true);
    _view->installEventFilter(this);
    layout->addWidget(_view, 1);

    _pixelText = new QPlainTextEdit(this);
    _pixelText->setReadOnly(true);
    _pixelText->setMaximumHeight(140);
    _pixelText->setPlaceholderText(tr("Move the mouse over the picture: the pixel inspector says which layer drew it and why"));
    layout->addWidget(_pixelText);

    _status = new QLabel(this);
    layout->addWidget(_status);
}

void NextLayersWindow::setBinding(EmulatorBinding* binding)
{
    _binding = binding;
}

EmulatorContext* NextLayersWindow::context() const
{
    return _binding ? _binding->context() : nullptr;  // for this handler only, never stored
}

int NextLayersWindow::mode() const
{
    return _mode->currentData().toInt();
}

void NextLayersWindow::setMode(int mode)
{
    const int index = _mode->findData(mode);
    if (index >= 0)
        _mode->setCurrentIndex(index);
}

bool NextLayersWindow::layerShown(int layer) const
{
    return layer >= 0 && layer < 4 && _layerBox[layer]->isChecked();
}

void NextLayersWindow::setLayerShown(int layer, bool shown)
{
    if (layer >= 0 && layer < 4)
        _layerBox[layer]->setChecked(shown);
}

void NextLayersWindow::applySwitch(bool on)
{
    if (ScreenNext* screen = NextLayer::NextScreen(context()))
        screen->SetLayerCapture(on);
}

void NextLayersWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    _lastSequence = ~0ull;
    refresh();
    _timer->start();
    emit visibilityChanged(true);
}

void NextLayersWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    _timer->stop();
    emit visibilityChanged(false);
}

void NextLayersWindow::refresh()
{
    EmulatorContext* ctx = context();
    ScreenNext* screen = NextLayer::NextScreen(ctx);
    if (!screen)
    {
        _status->setText(tr("The machine is not a ZX Spectrum Next."));
        _view->clear();
        _capture->setEnabled(false);
        return;
    }
    _capture->setEnabled(true);
    {
        QSignalBlocker blocker(_capture);
        _capture->setChecked(screen->LayerCaptureEnabled());
    }
    if (!screen->LayerCaptureEnabled())
    {
        _status->setText(tr("Capture is off: tick Capture."));
        return;
    }
    std::shared_ptr<const NextLayer::Frame> frame;
    std::string message;
    if (NextLayer::LastFrame(ctx, frame, message) != NextLayer::Status::Ok || !frame)
    {
        _status->setText(QString::fromStdString(message));
        return;
    }
    const int mode = _mode->currentData().toInt();
    QImage out;
    if (mode == 1)
    {
        const NextLayer::Image image = NextLayer::Capture(ctx, NextLayer::Kind::Composed, false);
        if (image.status == NextLayer::Status::Ok)
            out = QImage(image.rgba.data(), kImageWidth, kImageHeight, kImageWidth * 4, QImage::Format_RGBA8888).copy();
    }
    else if (mode == 2)
    {
        std::vector<uint8_t> rgba;
        NextLayer::Render(*frame, NextLayer::Kind::Ids, false, rgba);
        out = QImage(rgba.data(), kImageWidth, kImageHeight, kImageWidth * 4, QImage::Format_RGBA8888).copy();
    }
    else
    {
        out = ChequerBoard();
        QPainter painter(&out);
        for (int i = 0; i < 4; i++)
        {
            if (!_layerBox[i]->isChecked())
                continue;
            std::vector<uint8_t> rgba;
            NextLayer::Render(*frame, kLayers[i], true, rgba);
            const QImage layer(rgba.data(), kImageWidth, kImageHeight, kImageWidth * 4, QImage::Format_RGBA8888);
            painter.drawImage(0, 0, layer);
        }
    }
    _image = out;
    _view->setPixmap(QPixmap::fromImage(_image));
    _status->setText(tr("Frame %1 of the capture").arg(static_cast<qulonglong>(screen->LayerFramesCaptured())));
}

QString NextLayersWindow::inspect(const QPoint& p) const
{
    EmulatorContext* ctx = context();
    if (!ctx || p.x() < 0 || p.y() < 0 || p.x() >= kImageWidth || p.y() >= kImageHeight)
        return QString();
    NextPixelQuery query;
    query.x = static_cast<unsigned>(p.x() / 2);  // the grid is 320 pixels of two sub-pixels; lines are doubled
    query.sub = static_cast<unsigned>(p.x() & 1);
    query.y = static_cast<unsigned>(p.y() / 2);
    const StateNode report = DeviceState::NextPixel(ctx, query);
    return QString::fromStdString(StateNodeToJsonText(report));
}

void NextLayersWindow::showPixelAt(const QPoint& labelPoint)
{
    if (_view->width() <= 0 || _view->height() <= 0)
        return;
    const QPoint image(labelPoint.x() * kImageWidth / _view->width(), labelPoint.y() * kImageHeight / _view->height());
    const QString text = inspect(image);
    if (!text.isEmpty())
        _pixelText->setPlainText(tr("image (%1, %2)\n").arg(image.x()).arg(image.y()) + text);
}

bool NextLayersWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _view && event->type() == QEvent::MouseMove)
        showPixelAt(static_cast<QMouseEvent*>(event)->pos());
    return QWidget::eventFilter(watched, event);
}
