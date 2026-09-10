#pragma once

#include <QIcon>
#include <QIconEngine>
#include <QString>

/// @brief Icon engine that renders a monochrome SVG tinted with the current
/// palette's text colour, so toolbar icons follow light/dark platform themes.
///
/// Rendering happens on demand at the requested device pixel ratio, so the
/// icons stay crisp on HiDPI displays and pick up palette changes automatically.
class TintedSvgIconEngine : public QIconEngine
{
public:
    /// @param svgPath Resource path of the SVG (e.g. ":/icons/start.svg")
    /// @param tint    Tint with the palette text colour; false keeps the SVG's own colours
    explicit TintedSvgIconEngine(const QString& svgPath, bool tint = true);

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override;
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override;
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override;
    QIconEngine* clone() const override;

private:
    QString _svgPath;
    bool _tint;
};

/// @brief Convenience: icon for the bundled ":/icons/<name>.svg".
QIcon tintedSvgIcon(const QString& name, bool tint = true);
