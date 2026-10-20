// Renders an SVG with Qt's QSvgRenderer (what unreal-qt draws with) to a PNG: qtrender in.svg out.png [width] [layer]
// A layer (an element id: case, caps, legends, hints) is drawn alone, in its place on the picture
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <cstdio>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: qtrender in.svg out.png [width] [layer]\n");
        return 2;
    }
    QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));
    if (!svg.isValid())
    {
        std::fprintf(stderr, "invalid svg\n");
        return 1;
    }
    const int width = argc > 3 ? std::atoi(argv[3]) : 1600;
    const QSizeF size = svg.viewBoxF().size();
    QImage image(width, static_cast<int>(width * size.height() / size.width()), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    if (argc > 4)
    {
        const QString layer = QString::fromLocal8Bit(argv[4]);
        if (!svg.elementExists(layer))
        {
            std::fprintf(stderr, "no element %s\n", argv[4]);
            return 1;
        }
        const QRectF box = svg.viewBoxF();
        const QRectF bounds = svg.boundsOnElement(layer);
        const qreal scale = width / box.width();
        svg.render(&painter, layer,
                   QRectF((bounds.x() - box.x()) * scale, (bounds.y() - box.y()) * scale, bounds.width() * scale,
                          bounds.height() * scale));
    }
    else
    {
        svg.render(&painter);
    }
    painter.end();
    return image.save(QString::fromLocal8Bit(argv[2])) ? 0 : 1;
}
