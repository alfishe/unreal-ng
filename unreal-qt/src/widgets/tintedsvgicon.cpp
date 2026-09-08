#include "tintedsvgicon.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>

TintedSvgIconEngine::TintedSvgIconEngine(const QString& svgPath, bool tint) : _svgPath(svgPath), _tint(tint) {}

void TintedSvgIconEngine::paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state)
{
    const qreal scale = painter->device() ? painter->device()->devicePixelRatio() : 1.0;
    painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
}

QPixmap TintedSvgIconEngine::pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state)
{
    return scaledPixmap(size, mode, state, 1.0);
}

QPixmap TintedSvgIconEngine::scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale)
{
    Q_UNUSED(state);

    QImage image(size * scale, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing, true);
        QSvgRenderer renderer(_svgPath);
        renderer.render(&p, image.rect());

        const QPalette::ColorGroup group = (mode == QIcon::Disabled) ? QPalette::Disabled : QPalette::Normal;
        if (_tint)
        {
            // Keep the SVG's alpha, replace its colour with the theme text colour
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(image.rect(), QApplication::palette().color(group, QPalette::WindowText));
        }
        else if (mode == QIcon::Disabled)
        {
            // Coloured icon: fade instead of recolouring
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            p.fillRect(image.rect(), QColor(0, 0, 0, 90));
        }
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(scale);
    return pixmap;
}

QIconEngine* TintedSvgIconEngine::clone() const
{
    return new TintedSvgIconEngine(_svgPath, _tint);
}

QIcon tintedSvgIcon(const QString& name, bool tint)
{
    return QIcon(new TintedSvgIconEngine(QStringLiteral(":/icons/%1.svg").arg(name), tint));
}
