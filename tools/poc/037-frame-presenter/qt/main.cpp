// POC 037, the Qt 6 shell.
//
//   --backend gl      QOpenGLWindow, swap interval 1, as unreal-qt's DeviceScreenGLWindow
//                     a-signal: update() on the producer's frame-ready signal (queued to the GUI thread), as today
//                     b-vsync:  paintGL ends with requestUpdate() (Qt drives it from the display link), newest frame
//   --backend native  a QWindow (MetalSurface on macOS, a plain HWND on Windows) handed to the native presenter
//
//   poc037-qt --backend gl --variant a --rate zx48 --seconds 60 --out <dir>

#include "../common/lab.h"
#include "../common/presenter.h"

#include <QGuiApplication>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QOpenGLWindow>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWindow>

#include <memory>

using namespace poc037;

namespace
{

const char* kVs = R"(#version 330 core
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
const char* kFs = R"(#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D tex;
void main() { color = texture(tex, uv); })";

class GlWindow final : public QOpenGLWindow, protected QOpenGLFunctions
{
public:
    GlWindow(FrameRing& ring, Producer& producer, PresentLog& log, const Options& o)
        : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate), _ring(ring), _producer(producer), _log(log), _o(o)
    {
        QSurfaceFormat fmt;
        fmt.setVersion(3, 3);
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
        fmt.setSwapInterval(1);
        setFormat(fmt);
        _frame.pixels.assign(static_cast<size_t>(kWidth) * kHeight, 0);
        connect(this, &QOpenGLWindow::frameSwapped, this, [this] {
            if (_pending != SIZE_MAX)
                _log.SetTiming(_pending, NowNs(), ThreadCpuNs() - _cpu0);
            _pending = SIZE_MAX;
        });
        if (_o.variant == "a")
            _producer.OnLatch([this](uint64_t) { QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection); });
    }

protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        auto compile = [this](GLenum type, const char* src) {
            const GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            return s;
        };
        _program = glCreateProgram();
        glAttachShader(_program, compile(GL_VERTEX_SHADER, kVs));
        glAttachShader(_program, compile(GL_FRAGMENT_SHADER, kFs));
        glLinkProgram(_program);
        auto* f = context()->extraFunctions();
        f->glGenVertexArrays(1, &_vao);
        glGenTextures(1, &_tex);
        glBindTexture(GL_TEXTURE_2D, _tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        _producer.StartClocked();
    }

    void paintGL() override
    {
        const int64_t tick = NowNs();
        _cpu0 = ThreadCpuNs();
        if (_ring.CopyAt(0, _frame) && _frame.number != _uploaded)
        {
            glBindTexture(GL_TEXTURE_2D, _tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kWidth, kHeight, GL_BGRA, GL_UNSIGNED_BYTE, _frame.pixels.data());
            _uploaded = _frame.number;
        }
        const qreal dpr = devicePixelRatio();
        const int w = static_cast<int>(width() * dpr), h = static_cast<int>(height() * dpr);
        glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        int s = std::max(1, std::min(w / kWidth, h / kHeight));
        glViewport((w - kWidth * s) / 2, (h - kHeight * s) / 2, kWidth * s, kHeight * s);
        glUseProgram(_program);
        context()->extraFunctions()->glBindVertexArray(_vao);
        glBindTexture(GL_TEXTURE_2D, _tex);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        if (_uploaded != UINT64_MAX)
        {
            PresentRecord r;
            r.frame = _frame.number;
            r.latchNs = _frame.latchNs;
            r.tickNs = tick;
            _pending = _log.Add(r);
        }
        if (_o.variant == "b")
            requestUpdate();
    }

private:
    FrameRing& _ring;
    Producer& _producer;
    PresentLog& _log;
    Options _o;
    Frame _frame;
    uint64_t _uploaded = UINT64_MAX;
    size_t _pending = SIZE_MAX;
    int64_t _cpu0 = 0;
    GLuint _program = 0, _vao = 0, _tex = 0;
};

}  // namespace

int main(int argc, char** argv)
{
    const Options o = ParseOptions(argc, argv);
    QGuiApplication app(argc, argv);
    FrameRing ring;
    Producer producer(ring, o.rate, o.workMs);
    PresentLog log;
    log.Meta("shell", std::string("Qt ") + qVersion());
    log.Meta("backend", o.backend);
    log.Meta("variant", o.variant);
    log.Meta("rate_hz", std::to_string(o.rate));
    log.Meta("delay_ms", std::to_string(o.delayMs));
    log.Meta("fullscreen", o.fullscreen ? "1" : "0");

    std::unique_ptr<QWindow> window;
    std::unique_ptr<NativePresenter> presenter;
    if (o.backend == "gl")
    {
        window = std::make_unique<GlWindow>(ring, producer, log, o);
    }
    else
    {
        window = std::make_unique<QWindow>();
#ifdef __APPLE__
        window->setSurfaceType(QSurface::MetalSurface);
#endif
    }
    window->resize(kWidth * o.scale, kHeight * o.scale);
    window->setTitle("POC 037 Qt");
    if (o.fullscreen)
        window->showFullScreen();
    else
        window->show();
    window->raise();
    window->requestActivate();

    void* handle = nullptr;
    if (o.backend != "gl")
    {
#ifdef __APPLE__
        handle = MetalLayerOfView(reinterpret_cast<void*>(window->winId()));
#else
        handle = reinterpret_cast<void*>(window->winId());
#endif
        presenter = CreateNativePresenter(handle, ring, producer, log, o);
        if (!presenter || !presenter->Start())
        {
            std::fprintf(stderr, "the native presenter did not start\n");
            return 1;
        }
        log.Meta("presenter", presenter->Describe());
        const DisplayFacts d = QueryDisplay(handle);
        log.Meta("display", d.name);
        log.Meta("display_hz", std::to_string(d.refreshHz));
        log.Meta("display_min_hz", std::to_string(d.minHz));
        log.Meta("display_max_hz", std::to_string(d.maxHz));
    }
    else
    {
        log.Meta("presenter", o.variant == "a" ? "QOpenGLWindow update() on frame ready" : "QOpenGLWindow requestUpdate() loop");
        log.Meta("display", window->screen()->name().toStdString());
        log.Meta("display_hz", std::to_string(window->screen()->refreshRate()));
    }

    QTimer::singleShot(static_cast<int>(o.seconds * 1000), &app, [&] {
        if (presenter)
            log.Meta("visible_at_end", QueryDisplay(handle).visible ? "1" : "0");
        if (presenter)
            presenter->Stop();
        producer.Stop();
        log.Meta("frames_produced", std::to_string(producer.Frames()));
        log.Meta("producer_jitter_p99_ms", std::to_string(producer.LatchJitterP99Ms()));
        log.Write(RunPath(o, "qt"));
        app.quit();
    });
    return app.exec();
}
