// Qt 6 Widgets variant: one custom QWidget per view, painted with QPainter on the raster backing store
// (the default for Qt Widgets: CPU rasterization, flushed to the window). Hex bytes use a cache of 256 QStaticText,
// the timeline batches its bars per lane into one drawLines call, images are QImage views drawn scaled.

#include "../common/harness.h"
#include "../common/layout.h"

#include <QApplication>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QStaticText>
#include <QTimer>
#include <QRegion>
#include <QWidget>

#include <array>
#include <chrono>

using namespace bench;
namespace L = bench::layout;

static QColor C(uint32_t rgb) { return QColor::fromRgb((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

class Panel : public QWidget
{
public:
    Panel(QWidget* parent, const L::Rect& r, QString title) : QWidget(parent), _title(std::move(title))
    {
        setGeometry(int(r.x), int(r.y), int(r.w), int(r.h));
        setAttribute(Qt::WA_OpaquePaintEvent, false);
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

/// Content area of a panel; subclasses paint the view
class View : public QWidget
{
public:
    View(QWidget* parent, const L::Rect& panel, Workload& w) : QWidget(parent), _w(w)
    {
        setGeometry(int(L::kPad), int(L::kTitleH + L::kPad), int(panel.w - 2 * L::kPad),
                    int(panel.h - L::kTitleH - 2 * L::kPad));
        setAttribute(Qt::WA_OpaquePaintEvent, true);  // the panel under it is not repainted
    }

protected:
    void changeEvent(QEvent* e) override
    {
        if (e->type() == QEvent::FontChange)
            FontChanged();
        QWidget::changeEvent(e);
    }
    virtual void FontChanged() { _ascent = QFontMetricsF(font()).ascent(); }

    Workload& _w;
    float _ascent = 0;
};

class HexView : public View
{
public:
    HexView(QWidget* parent, Workload& w, const QFont& f) : View(parent, L::kHex, w)
    {
        for (int i = 0; i < 256; i++)
            _bytes[i].setText(QString::fromLatin1(Workload::Hex2(uint8_t(i))));
        setFont(f);
        FontChanged();
    }

protected:
    void FontChanged() override
    {
        View::FontChanged();
        for (auto& st : _bytes)
            st.prepare(QTransform(), font());
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), C(L::kPanel));
        const uint8_t* mem = _w.Memory();
        const L::TextMetrics& t = L::T();
        for (int r = 0; r < t.hexRows; r++)
        {
            const float y = r * t.lineH;
            p.setPen(C(L::kMuted));
            p.drawText(QPointF(t.hexAddrX, y + _ascent), QString::fromLatin1(_w.HexRowAddr(r)));
            for (int c = 0; c < kHexCols; c++)
            {
                const float x = t.hexBytesX + c * t.hexByteStep;
                const bool ch = _w.HexChanged(r, c);
                if (ch)
                    p.fillRect(QRectF(x - 2, y, t.hexChangedW, t.lineH - 1), C(L::kChangedBg));
                p.setPen(C(ch ? L::kAccent : L::kFg));
                p.drawStaticText(QPointF(x, y), _bytes[mem[(_w.HexBase() + r * kHexCols + c) & 0xFFFF]]);
            }
            p.setPen(C(L::kMuted));
            p.drawText(QPointF(t.hexAsciiX, y + _ascent), QString::fromLatin1(_w.HexRowAscii(r)));
        }
    }

private:
    std::array<QStaticText, 256> _bytes;
};

class DisasmView : public View
{
public:
    DisasmView(QWidget* parent, Workload& w, const QFont& f) : View(parent, L::kDisasm, w)
    {
        setFont(f);
        FontChanged();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), C(L::kPanel));
        const L::TextMetrics& t = L::T();
        for (int i = 0; i < t.disRows; i++)
        {
            const auto& l = _w.Disasm()[i];
            const float y = i * t.lineH;
            if (l.isPc)
                p.fillRect(QRectF(0, y, width(), t.lineH), C(L::kPcRowBg));
            if (l.isBreakpoint)
            {
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(Qt::NoPen);
                p.setBrush(C(L::kBreak));
                p.drawEllipse(QPointF(5, y + t.lineH * 0.5f), 4, 4);
                p.setRenderHint(QPainter::Antialiasing, false);
            }
            const float b = y + _ascent;
            p.setPen(C(L::kMuted));
            p.drawText(QPointF(t.disAddrX, b), QString::fromLatin1(l.addr));
            p.drawText(QPointF(t.disBytesX, b), QString::fromLatin1(l.bytes));
            p.setPen(C(l.isPc ? L::kAccent : L::kFg));
            p.drawText(QPointF(t.disTextX, b), QString::fromLatin1(l.text));
            if (!l.comment.empty())
            {
                p.setPen(C(L::kMuted));
                p.drawText(QPointF(t.disCommentX, b), QString::fromLatin1(l.comment));
            }
        }
    }
};

class TimelineView : public View
{
public:
    TimelineView(QWidget* parent, Workload& w, const QFont& f) : View(parent, L::kTimeline, w)
    {
        setFont(f);
        FontChanged();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), C(L::kPanel));
        QVector<QLineF> lines;
        lines.reserve(kTimelineWidth);
        for (int lane = 0; lane < kTimelineLanes; lane++)
        {
            const float y0 = lane * L::kLaneH, y1 = y0 + L::kLaneH - 4;
            p.fillRect(QRectF(0, y0, kTimelineWidth, y1 - y0), C(L::kLaneBg));
            lines.clear();
            for (int x = 0; x < kTimelineWidth; x++)
            {
                const float v = _w.TimelineValue(lane, x);
                if (v > 0.f)
                    lines.append(QLineF(x + 0.5, y1, x + 0.5, y1 - v * (y1 - y0)));
            }
            p.setPen(QPen(C(Workload::LaneColour(lane)), 1));
            p.drawLines(lines);
            p.setPen(C(L::kFg));
            p.drawText(QPointF(4, y0 + 2 + _ascent), QString::fromLatin1(Workload::LaneName(lane)));
        }
        const float ry = kTimelineLanes * L::kLaneH;
        p.setPen(C(L::kMuted));
        for (const auto& t : _w.TimelineTicks())
        {
            p.drawLine(QPointF(t.x, ry), QPointF(t.x, ry + 4));
            p.drawText(QPointF(t.x + 2, ry + 3 + _ascent), QString::fromLatin1(t.label));
        }
        p.fillRect(QRectF(_w.TimelinePlayheadX() - 1, 0, 2, ry), C(L::kPlayhead));
    }
};

class ImagesView : public View
{
public:
    ImagesView(QWidget* parent, Workload& w) : View(parent, L::kImages, w) {}

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), C(L::kPanel));
        for (int i = 0; i < kTexCount; i++)
        {
            const QImage img(_w.Image(i), kTexW, kTexH, QImage::Format_RGBA8888);
            const QRectF dst((i % 2) * (L::kImgW + L::kPad), (i / 2) * (L::kImgH + L::kPad), L::kImgW, L::kImgH);
            p.drawImage(dst, img);
        }
    }
};

class Root : public QWidget
{
public:
    Root()
    {
        setFixedSize(kWindowW, kWindowH);
        setAutoFillBackground(true);
        QPalette pal = palette();
        pal.setColor(QPalette::Window, C(L::kBg));
        setPalette(pal);
    }
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
    // UI scale through Qt's own high-DPI scaling; it can only be set before the application starts
    const HarnessOptions pre = ParseOptions(argc, argv, "qt-widgets");
    if (pre.scale != 1.0f)
        qputenv("QT_SCALE_FACTOR", QByteArray::number(pre.scale));
    QApplication app(argc, argv);
    Harness h(ParseOptions(argc, argv, "qt-widgets"));
    const HarnessOptions& opt = h.Options();

    QFontDatabase::addApplicationFont(L::kFontPath);
    QFont font(L::kFontFamily);
    font.setPixelSize(int(L::kFontPx));
    font.setStyleHint(QFont::Monospace);
    app.setFont(font);  // titles
    QFont content = font;
    content.setPixelSize(int(opt.fontPx));

    Root root;
    QWidget* views[4] = {};
    if (opt.View(kViewDisasm))
        views[0] = new DisasmView(new Panel(&root, L::kDisasm, "Disassembly"), h.Data(), content);
    if (opt.View(kViewHex))
        views[1] = new HexView(new Panel(&root, L::kHex, "Memory"), h.Data(), content);
    if (opt.View(kViewImages))
        views[2] = new ImagesView(new Panel(&root, L::kImages, "Screen / layers"), h.Data());
    if (opt.View(kViewTimeline))
        views[3] = new TimelineView(new Panel(&root, L::kTimeline, "Timeline"), h.Data(), content);
    root.show();
    root.raise();
    root.activateWindow();  // in front and unobscured: macOS throttles occluded windows
    h.SetDisplay(float(root.devicePixelRatioF()), "qt-raster");  // Widgets always paint on the CPU

    std::function<void()> step = [&]() {
        const Phase ph = h.Update();
        if (ph == Phase::Done)
        {
            h.Finish("raster paint engine (QPainter on the backing store)");
            app.quit();
            return;
        }
        if (ph == Phase::Idle)
        {
            // nothing changes: no repaint, the event loop sleeps until the phase ends
            QTimer::singleShot(h.MsUntilPhaseEnd(), Qt::PreciseTimer, step);
            return;
        }
        h.TickWorkload();
        h.FrameBegin();
        float newFont = 0, newScale = 0;
        if (h.PendingSwitch(newFont, newScale))
        {
            // runtime font change; a runtime scale change has no public API in Qt Widgets (QT_SCALE_FACTOR is read at
            // start-up, the device pixel ratio changes only when the window moves to another screen)
            QFont nf = content;
            nf.setPixelSize(int(newFont));
            for (QWidget* v : views)
                if (v && v != views[2])
                    v->setFont(nf);
        }
        // only the four views are repainted (the panels and the window background do not change); repaint() of
        // their region on the top level paints and flushes synchronously, so the frame is complete when it returns
        for (QWidget* v : views)
            if (v)
                v->update();
        FlushNow(root);
        h.FrameEnd();
        if (h.WantScreenshot())
        {
            const QImage img = root.grab().toImage().convertToFormat(QImage::Format_RGBA8888);  // physical pixels
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
