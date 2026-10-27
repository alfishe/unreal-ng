//
// screen_widget.cpp — Paints the ZX screen QImage scaled to widget size.
//

#include "screen_widget.h"

#include <QPainter>
#include <QPaintEvent>

namespace ttd {

ScreenWidget::ScreenWidget(QWidget* parent)
    : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(320, 288);
}

void ScreenWidget::setImage(const QImage& img) {
    _img = img;
    update();
}

QSize ScreenWidget::sizeHint() const {
    if (!_img.isNull())
        return _img.size();
    return {320, 288};
}

void ScreenWidget::paintEvent(QPaintEvent*) {
    if (_img.isNull())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // Scale preserving aspect ratio, centered
    QSize scaled = _img.size();
    scaled.scale(width(), height(), Qt::KeepAspectRatio);
    QRect target;
    target.setSize(scaled);
    target.moveCenter(rect().center());

    // Black background for letterbox
    p.fillRect(rect(), Qt::black);
    p.drawImage(target, _img);
}

} // namespace ttd
