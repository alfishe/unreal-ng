// Qt 6 Quick, own scene-graph nodes. QSG_RENDER_LOOP=basic so a frame begins and ends on one thread.
// Backend from the environment: the RHI (platform default, see qtcommon/quickhelpers.h), the software adaptation with QT_QUICK_BACKEND=software.

#include "../common/harness.h"
#include "../qtcommon/quickhelpers.h"
#include "../common/layout.h"
#include "items.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QTimer>

#include <chrono>

using namespace bench;

int main(int argc, char** argv)
{
    qputenv("QSG_RENDER_LOOP", "basic");
    qputenv("QT_QPA_UPDATE_IDLE_TIME", "0");
    const bool software = qgetenv("QT_QUICK_BACKEND") == "software";
    const HarnessOptions pre = ParseOptions(argc, argv, software ? "qt-quick-sg-software" : "qt-quick-sg");
    if (pre.scale != 1.0f)
        qputenv("QT_SCALE_FACTOR", QByteArray::number(pre.scale));
    QGuiApplication app(argc, argv);
    quick::ConfigureGraphicsApi();
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    Harness h(ParseOptions(argc, argv, software ? "qt-quick-sg-software" : "qt-quick-sg"));
    const HarnessOptions& opt = h.Options();
    QFontDatabase::addApplicationFont(bench::layout::kFontPath);

    qmlRegisterType<sg::HexItem>("BenchSG", 1, 0, "HexItem");
    qmlRegisterType<sg::DisasmItem>("BenchSG", 1, 0, "DisasmItem");
    qmlRegisterType<sg::TimelineItem>("BenchSG", 1, 0, "TimelineItem");
    qmlRegisterType<sg::ImageItem>("BenchSG", 1, 0, "ImageItem");
    sg::SetWorkload(&h.Data());

    QQuickView view;
    view.rootContext()->setContextProperty("showDisasm", opt.View(kViewDisasm));
    view.rootContext()->setContextProperty("showHex", opt.View(kViewHex));
    view.rootContext()->setContextProperty("showImages", opt.View(kViewImages));
    view.rootContext()->setContextProperty("showTimeline", opt.View(kViewTimeline));
    view.setSource(QUrl("qrc:/main.qml"));
    view.resize(kWindowW, kWindowH);
    view.show();
    view.raise();
    view.requestActivate();  // in front and unobscured: macOS throttles occluded windows

    QList<QQuickItem*> hot;
    for (QQuickItem* it : view.rootObject()->findChildren<QQuickItem*>())
        if (it->objectName() == "hot" && it->isVisible() && it->parentItem() && it->parentItem()->isVisible())
            hot.append(it);

    bool inFrame = false, screenshotPending = false;
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
            h.Finish(std::string("own scene-graph nodes, ") + quick::BackendName(view));
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
        h.PendingSwitch(newFont, newScale);  // font: the items see L::T() change and rebuild their rows
        for (QQuickItem* it : hot)
            it->update();
        view.update();
    };
    QTimer::singleShot(200, step);
    return app.exec();
}
