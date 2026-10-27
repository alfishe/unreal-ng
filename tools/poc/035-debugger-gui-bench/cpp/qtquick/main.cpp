// Qt 6 Quick variant: QQuickView, basic (single-threaded) render loop so frame begin/end are measurable on one
// thread; OpenGL through the RHI. Text views: Repeater + Text bound to list models; timeline and images: custom
// scene-graph items (items.cpp).

#include "../common/harness.h"
#include "../qtcommon/quickhelpers.h"
#include "../common/layout.h"
#include "items.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QTimer>

#include <chrono>
#include <cmath>

using namespace bench;

int main(int argc, char** argv)
{
    qputenv("QSG_RENDER_LOOP", "basic");
    qputenv("QT_QPA_UPDATE_IDLE_TIME", "0");  // QWindow::requestUpdate otherwise waits 5 ms before every frame
    const HarnessOptions pre = ParseOptions(argc, argv, "qt-quick");
    if (pre.scale != 1.0f)
        qputenv("QT_SCALE_FACTOR", QByteArray::number(pre.scale));  // start-up UI scale: Qt's high-DPI scaling
    QGuiApplication app(argc, argv);
    quick::ConfigureGraphicsApi();
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    Harness h(ParseOptions(argc, argv, "qt-quick"));
    const HarnessOptions& opt = h.Options();
    const bool empty = opt.empty;
    QFontDatabase::addApplicationFont(bench::layout::kFontPath);

    qmlRegisterType<TimelineItem>("Bench", 1, 0, "TimelineItem");
    qmlRegisterType<ImageItem>("Bench", 1, 0, "ImageItem");
    TimelineItem::SetWorkload(&h.Data());
    ImageItem::SetWorkload(&h.Data());

    HexModel hexModel(h.Data());
    DisasmModel disasmModel(h.Data());
    Bridge bridge(h.Data());
    bridge.Refresh();
    Metrics metrics;

    QQuickView view;
    view.rootContext()->setContextProperty("hexModel", &hexModel);
    view.rootContext()->setContextProperty("disasmModel", &disasmModel);
    view.rootContext()->setContextProperty("bridge", &bridge);
    view.rootContext()->setContextProperty("metrics", &metrics);
    view.rootContext()->setContextProperty("showDisasm", opt.View(kViewDisasm));
    view.rootContext()->setContextProperty("showHex", opt.View(kViewHex));
    view.rootContext()->setContextProperty("showImages", opt.View(kViewImages));
    view.rootContext()->setContextProperty("showTimeline", opt.View(kViewTimeline));
    view.setSource(QUrl(empty ? "qrc:/empty.qml" : "qrc:/main.qml"));
    view.resize(kWindowW, kWindowH);
    view.show();
    view.raise();
    view.requestActivate();  // in front and unobscured: macOS throttles occluded windows

    QList<QQuickItem*> sgItems;
    if (!empty)
        for (QQuickItem* it : view.rootObject()->findChildren<QQuickItem*>())
            if (qobject_cast<TimelineItem*>(it) || qobject_cast<ImageItem*>(it))
                sgItems.append(it);

    bool inFrame = false;
    bool screenshotPending = false;
    std::function<void()> step;

    QObject::connect(&view, &QQuickWindow::beforeSynchronizing, &view, [&] {
        if (inFrame)
            h.MarkRenderStart();
    }, Qt::DirectConnection);
    quick::Instrument(view, h);
    QObject::connect(&view, &QQuickWindow::frameSwapped, &view, [&] {
        if (!inFrame)
            return;
        inFrame = false;
        h.FrameEnd();
        screenshotPending = h.WantScreenshot();
        if (h.Current() == Phase::Unthrottled)
            QTimer::singleShot(0, step);
        else
        {
            const auto deadline = h.NextDeadline();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            QTimer::singleShot(int(std::max<long long>(0, ms)), Qt::PreciseTimer, step);
        }
    }, Qt::QueuedConnection);

    step = [&]() {
        if (screenshotPending)
        {
            screenshotPending = false;
            const QImage img = view.grabWindow().convertToFormat(QImage::Format_RGBA8888);
            h.SubmitScreenshot(img.constBits(), img.width(), img.height(), int(img.bytesPerLine()), false);
        }
        const Phase ph = h.Update();
        if (ph == Phase::Done)
        {
            h.Finish(std::string("basic render loop, ") + quick::BackendName(view));
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
        inFrame = true;
        float newFont = 0, newScale = 0;
        if (h.PendingSwitch(newFont, newScale))
        {
            if (std::abs(newScale - float(metrics.zoom())) > 1e-3f)
            {
                // runtime zoom: no public API changes the device pixel ratio at run time, so the root item is scaled
                // (Text uses distance-field glyphs, which stay sharp under a transform) and the window resized
                metrics.SetZoom(newScale);
                view.resize(int(kWindowW * newScale), int(kWindowH * newScale));
            }
            else
            {
                metrics.Notify();          // font-dependent bindings re-evaluate
                hexModel.ResetRows();      // the number of rows that fit changed
                disasmModel.ResetRows();
            }
        }
        if (!empty)
        {
            if (opt.View(kViewHex))
                hexModel.Refresh();
            if (opt.View(kViewDisasm))
                disasmModel.Refresh();
            if (opt.View(kViewTimeline))
                bridge.Refresh();
            for (QQuickItem* it : sgItems)
                it->update();
        }
        view.update();
    };
    QTimer::singleShot(200, step);
    return app.exec();
}
