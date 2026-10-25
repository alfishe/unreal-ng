// Qt 6 Widgets, own CPU raster + dirty rectangles.
//
// The question this variant answers: how cheap can a debugger GUI be with no GPU at all, on Qt Widgets?
//   - every view keeps its own QImage (RGB32, device pixels) and changes only the pixels whose data changed:
//     a hex cell is redrawn when its byte or its "written this frame" state changes, a disassembly row when its text
//     changes, a timeline column when its bar height changes;
//   - text is not laid out per frame: glyphs are rendered once per (ink, background) pair into an atlas and copied;
//   - Qt only receives the dirty rectangles: root.repaint(region) paints them from the views' images and flushes
//     that region of the backing store, so the X server copies only what changed.
// Panels and titles are ordinary QPainter widgets that paint once.

#include "../common/harness.h"
#include "../common/layout.h"
#include "../qtcommon/rasterviews.h"

#include <QApplication>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <QRegion>
#include <QTimer>
#include <QWidget>

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace bench;
namespace L = bench::layout;

static QColor C(uint32_t rgb) { return QColor::fromRgb((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

class Panel : public QWidget
{
public:
    Panel(QWidget* parent, const L::Rect& r, QString title) : QWidget(parent), _title(std::move(title))
    {
        setGeometry(int(r.x), int(r.y), int(r.w), int(r.h));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(C(L::kLine));
        p.setBrush(C(L::kPanel));
        p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1, height() - 1), 6, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(C(0x1C2129));
        p.drawRect(QRectF(1, 1, width() - 2, L::kTitleH - 1));
        p.setPen(C(L::kTitle));
        p.drawText(QRectF(8, 0, width() - 16, L::kTitleH), Qt::AlignVCenter | Qt::AlignLeft, _title);
    }

private:
    QString _title;
};

/// A view = an opaque child of its panel; it shows its RasterView's image and repaints only the changed region
class View : public QWidget
{
public:
    View(Panel* panel, const L::Rect& r, std::unique_ptr<raster::RasterView> rv) : QWidget(panel), _rv(std::move(rv))
    {
        setGeometry(int(L::kPad), int(L::kTitleH + L::kPad), int(r.w - 2 * L::kPad), int(r.h - L::kTitleH - 2 * L::kPad));
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    QRegion Frame() { return _rv->Frame(size(), float(devicePixelRatioF())); }

protected:
    void paintEvent(QPaintEvent* e) override
    {
        QPainter p(this);
        const float k = _rv->Scale();
        for (const QRect& r : e->region())
            p.drawImage(QRectF(r), _rv->Image(), QRectF(r.x() * k, r.y() * k, r.width() * k, r.height() * k));
    }

private:
    std::unique_ptr<raster::RasterView> _rv;
};

/// Paint and flush the pending update()s now, so the frame is complete when this returns
static void FlushNow(QWidget& root)
{
    const QByteArray mode = qgetenv("BENCH_PAINT_MODE");
    if (mode == "posted")
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
    else
    {
        QEvent ev(QEvent::UpdateRequest);
        QCoreApplication::sendEvent(&root, &ev);
    }
}

int main(int argc, char** argv)
{
    const HarnessOptions pre = ParseOptions(argc, argv, "qt-widgets-raster");
    if (pre.scale != 1.0f)
        qputenv("QT_SCALE_FACTOR", QByteArray::number(pre.scale));
    QApplication app(argc, argv);
    Harness h(ParseOptions(argc, argv, "qt-widgets-raster"));
    const HarnessOptions& opt = h.Options();

    QFontDatabase::addApplicationFont(L::kFontPath);
    QFont font(L::kFontFamily);
    font.setPixelSize(int(L::kFontPx));
    font.setStyleHint(QFont::Monospace);
    app.setFont(font);

    QWidget root;
    root.setFixedSize(kWindowW, kWindowH);
    root.setAutoFillBackground(true);
    QPalette pal = root.palette();
    pal.setColor(QPalette::Window, C(L::kBg));
    root.setPalette(pal);

    View* views[4] = {};
    if (opt.View(kViewDisasm))
        views[0] = new View(new Panel(&root, L::kDisasm, "Disassembly"), L::kDisasm, std::make_unique<raster::DisasmRaster>(h.Data()));
    if (opt.View(kViewHex))
        views[1] = new View(new Panel(&root, L::kHex, "Memory"), L::kHex, std::make_unique<raster::HexRaster>(h.Data()));
    if (opt.View(kViewImages))
        views[2] = new View(new Panel(&root, L::kImages, "Screen / layers"), L::kImages, std::make_unique<raster::ImagesRaster>(h.Data()));
    if (opt.View(kViewTimeline))
        views[3] = new View(new Panel(&root, L::kTimeline, "Timeline"), L::kTimeline, std::make_unique<raster::TimelineRaster>(h.Data()));
    root.show();
    root.raise();
    root.activateWindow();  // in front and unobscured: macOS throttles occluded windows
    h.SetDisplay(float(root.devicePixelRatioF()), "qt-raster");  // Widgets always paint on the CPU

    long long dirtyPx = 0, frames = 0;
    std::function<void()> step = [&]() {
        const Phase ph = h.Update();
        if (ph == Phase::Done)
        {
            char notes[160];
            std::snprintf(notes, sizeof notes, "own CPU raster + dirty rects; mean repainted area %.0f px per frame",
                          frames ? double(dirtyPx) / frames : 0.0);
            h.Finish(notes);
            app.quit();
            return;
        }
        if (ph == Phase::Idle)
        {
            QTimer::singleShot(h.MsUntilPhaseEnd(), Qt::PreciseTimer, step);
            return;
        }
        h.TickWorkload();
        h.FrameBegin();
        float newFont = 0, newScale = 0;
        h.PendingSwitch(newFont, newScale);  // font: the views see L::T() change and redraw; scale: no run-time API
        QRegion dirty;
        for (View* v : views)
            if (v)
            {
                const QRegion r = v->Frame();
                v->update(r);
                dirty += r.translated(v->mapTo(&root, QPoint(0, 0)));
            }
        h.MarkRenderStart();
        FlushNow(root);
        for (const QRect& r : dirty)
            dirtyPx += (long long)r.width() * r.height();
        frames++;
        h.FrameEnd();
        if (h.WantScreenshot())
        {
            const QImage img = root.grab().toImage().convertToFormat(QImage::Format_RGBA8888);
            h.SubmitScreenshot(img.constBits(), img.width(), img.height(), int(img.bytesPerLine()), false);
        }
        if (ph == Phase::Unthrottled)
            QTimer::singleShot(0, step);
        else
        {
            const auto deadline = h.NextDeadline();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            QTimer::singleShot(int(std::max<long long>(0, ms)), Qt::PreciseTimer, step);
        }
    };
    QTimer::singleShot(0, step);
    return app.exec();
}
