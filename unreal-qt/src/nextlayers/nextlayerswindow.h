#pragma once

/// @file nextlayerswindow.h
/// @brief ZX Next: the per-layer view (design-layer-capture.md, L4): the layers of the last completed frame with a toggle each, the ids
/// map, a pixel inspector under the mouse and the capture switch.

#include <memory>

#include <QImage>
#include <QPoint>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTimer;
class EmulatorBinding;
class EmulatorContext;

class NextLayersWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NextLayersWindow(QWidget* parent = nullptr);

    void setBinding(EmulatorBinding* binding);

    /// The image the window shows now (tests): layers stacked bottom to top (ULA, Layer 2, tilemap, sprites) for the ticked ones
    QImage currentImage() const { return _image; }
    /// The inspector's text for a point of the image (640 x 512), or why there is none
    QString inspect(const QPoint& imagePoint) const;
    void refresh();

    /// The view's mode as the automation names it: 0 layers, 1 composed, 2 winner
    int mode() const;
    void setMode(int mode);
    /// The layer ticks: 0 ULA, 1 Layer 2, 2 tilemap, 3 sprites
    bool layerShown(int layer) const;
    void setLayerShown(int layer, bool shown);

signals:
    void visibilityChanged(bool visible);
    /// The mode or a layer tick changed
    void viewChanged();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    EmulatorContext* context() const;
    void buildUi();
    void applySwitch(bool on);
    void showPixelAt(const QPoint& labelPoint);

    EmulatorBinding* _binding = nullptr;
    QTimer* _timer = nullptr;
    QLabel* _status = nullptr;
    QLabel* _view = nullptr;
    QPlainTextEdit* _pixelText = nullptr;
    QCheckBox* _capture = nullptr;
    QCheckBox* _layerBox[4] = {};
    QComboBox* _mode = nullptr;
    QImage _image;
    uint64_t _lastSequence = ~0ull;
};
