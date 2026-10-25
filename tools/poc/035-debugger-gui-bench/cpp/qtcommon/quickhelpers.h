#pragma once

// Shared by the Qt Quick variants: which scene-graph backend runs, how the end of a frame is measured, what is reported.
//
//   QT_QUICK_BACKEND=software   the software adaptation (QPainter into the window's backing store, no GPU)
//   POC035_RHI=metal|opengl     force an RHI backend; default: the platform's native one (Metal on macOS, OpenGL elsewhere),
//                               the same choice an application gets without configuring anything
//
// End of frame: with an RHI backend, afterFrameEnd waits for the GPU (QRhi::finish), so the frame time includes the GPU
// work just like glFinish() does in the SDL variants; the software adaptation has nothing to wait for.

#include "../common/harness.h"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <rhi/qrhi.h>

namespace quick
{

inline bool Software() { return qgetenv("QT_QUICK_BACKEND") == "software"; }

/// Call before the first QQuickWindow exists
inline void ConfigureGraphicsApi()
{
    if (Software())
        return;
    const QByteArray rhi = qgetenv("POC035_RHI");
    if (rhi == "opengl")
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    else if (rhi == "metal")
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#if !defined(__APPLE__)
    else
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);  // the Linux / Windows measurements used OpenGL
#endif
}

inline const char* BackendName(QQuickWindow& w)
{
    switch (w.rendererInterface()->graphicsApi())
    {
    case QSGRendererInterface::Software: return "qt-quick-software";
    case QSGRendererInterface::OpenGL: return "opengl";
    case QSGRendererInterface::Metal: return "metal";
    case QSGRendererInterface::Vulkan: return "vulkan";
    case QSGRendererInterface::Direct3D11: return "d3d11";
    case QSGRendererInterface::Direct3D12: return "d3d12";
    default: return "other";
    }
}

/// Make every frame end only when the GPU is done with it, and report the backend and pixel ratio once known
inline void Instrument(QQuickWindow& w, bench::Harness& h)
{
    QObject::connect(&w, &QQuickWindow::afterFrameEnd, &w, [&w] {
        if (QRhi* rhi = w.rhi())
            rhi->finish();
    }, Qt::DirectConnection);
    QObject::connect(&w, &QQuickWindow::sceneGraphInitialized, &w, [&w, &h] {
        h.SetDisplay(float(w.effectiveDevicePixelRatio()), BackendName(w));
    }, Qt::DirectConnection);
    h.SetDisplay(float(w.devicePixelRatio()), Software() ? "qt-quick-software" : "rhi");
}

}  // namespace quick
