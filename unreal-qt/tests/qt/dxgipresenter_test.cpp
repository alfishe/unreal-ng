/// @file dxgipresenter_test.cpp
/// @brief The DXGI presenter (platform/windows/dxgipresenter.cpp) made anew for the same window, as the screen window
/// does when its presenter stops: the first one lets the window go, the next one takes it at once. After a real GPU
/// reset the old device's swap chain was refused (E_ACCESSDENIED, seen on .101); a healthy device does not show that,
/// so this guards the release itself, not the reset (the window then takes a new native window,
/// DeviceScreenGLWindow::renderNative). Needs a real window and WGL_NV_DX_interop2 (UNREAL_QT_TEST_PLATFORM=windows on
/// an NVIDIA or AMD GPU); skips without them.

#include <gtest/gtest.h>

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QWindow>
#include <memory>

#include "platform/nativepresenter.h"

namespace
{
/// A shown GL window and a 3.3 core context current on an offscreen surface, as the screen window draws
class PresenterWindow
{
public:
    bool Create()
    {
        if (QGuiApplication::platformName() != QLatin1String("windows"))
            return false;
        QSurfaceFormat fmt;
        fmt.setRenderableType(QSurfaceFormat::OpenGL);
        fmt.setVersion(3, 3);
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        _window.setSurfaceType(QSurface::OpenGLSurface);
        _window.setFormat(fmt);
        _window.resize(320, 240);
        _window.show();
        _context.setFormat(fmt);
        if (!_context.create())
            return false;
        _surface.setFormat(_context.format());
        _surface.create();
        return _context.makeCurrent(&_surface);
    }

    void* Handle() { return reinterpret_cast<void*>(_window.winId()); }

    /// One picture drawn and handed over
    static bool Present(NativePresenter& presenter)
    {
        if (presenter.BeginFrame(320, 240) == 0)
            return false;
        QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
        f->glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
        f->glClear(GL_COLOR_BUFFER_BIT);
        presenter.PresentFrame(0, {});
        return true;
    }

private:
    QWindow _window;
    QOpenGLContext _context;
    QOffscreenSurface _surface;
};
}  // namespace

TEST(DxgiPresenter_Test, MadeAnewForTheSameWindow)
{
    PresenterWindow window;
    if (!window.Create())
        GTEST_SKIP() << "no real window with a GL context (UNREAL_QT_TEST_PLATFORM=windows)";
    std::unique_ptr<NativePresenter> first = NativePresenter::Create(window.Handle());
    if (!first)
        GTEST_SKIP() << "no DXGI presenter here (no WGL_NV_DX_interop2)";
    ASSERT_TRUE(PresenterWindow::Present(*first));
    ASSERT_TRUE(PresenterWindow::Present(*first));
    first.reset();

    // At once, as the screen window does when its presenter stops: the window let go
    std::unique_ptr<NativePresenter> second = NativePresenter::Create(window.Handle());
    ASSERT_TRUE(second) << "the swap chain for the window refused: the first presenter still holds the window";
    EXPECT_TRUE(PresenterWindow::Present(*second));
}
