/// @file slangpresetrunner_test.cpp
/// @brief The libretro presets of data/shaders/crt on SlangPresetRunner: every one compiles, binds its semantics and
/// shows the picture; the window's composition shader gives the software picture. Needs an OpenGL 3.3 core context:
/// the offscreen platform has none on macOS (UNREAL_QT_TEST_PLATFORM=cocoa), the test skips without one.
///
/// UNREAL_CRT_REFERENCE=<dir> compares with librashader: <dir>/src/<frame>.png rendered by `librashader-cli render
/// -f 60 -d WxH` (UNREAL_CRT_FRAME=<n> for another -f) into <dir>/out/<frame>/<key>.png; the mean and the share of
/// pixels off by more than 8 go to stdout (UNREAL_CRT_OUTPUT=<dir> keeps our pictures as <dir>/<frame>/<key>.png).

#include <gtest/gtest.h>

#include <QDir>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QVector2D>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "_helpers/testpathhelper.h"
#include "widgets/crtrenderer.h"
#include "widgets/slangpresetrunner.h"

namespace
{
class GLContext
{
public:
    bool create()
    {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        _context.setFormat(format);
        if (!_context.create() || _context.isOpenGLES() || _context.format().version() < qMakePair(3, 3))
            return false;
        _surface.setFormat(_context.format());
        _surface.create();
        return _context.makeCurrent(&_surface);
    }
    QOpenGLExtraFunctions* gl() { return _context.extraFunctions(); }

private:
    QOpenGLContext _context;
    QOffscreenSurface _surface;
};

/// The picture as a texture, rows top first
GLuint Upload(QOpenGLExtraFunctions* gl, const QImage& picture)
{
    const QImage rgba = picture.convertToFormat(QImage::Format_RGBA8888);
    GLuint id = 0;
    gl->glGenTextures(1, &id);
    gl->glBindTexture(GL_TEXTURE_2D, id);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba.width(), rgba.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     rgba.constBits());
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

/// A Spectrum-like frame: a blue border, white paper with black ink stripes, a red block
QImage MakeFrame()
{
    QImage frame(352, 288, QImage::Format_RGBA8888);
    for (int y = 0; y < frame.height(); ++y)
        for (int x = 0; x < frame.width(); ++x)
        {
            const bool border = x < 48 || x >= 304 || y < 48 || y >= 240;
            const bool ink = !border && (x / 8) % 2 == 0;
            const bool red = !border && x >= 200 && y >= 180;
            frame.setPixelColor(x, y, border ? QColor(0, 0, 0xCD) : red ? QColor(0xCD, 0, 0) : ink ? Qt::black : QColor(0xCD, 0xCD, 0xCD));
        }
    return frame;
}

double Mean(const QImage& image)
{
    double sum = 0.0;
    for (int y = 0; y < image.height(); ++y)
    {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x)
            sum += row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2];
    }
    return sum / (3.0 * image.width() * image.height());
}

QStringList Presets()
{
    const QDir dir(QString::fromStdString((TestPathHelper::FindProjectRoot() / "data" / "shaders" / "crt").string()));
    return dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

QString PresetDir(const QString& key)
{
    return QString::fromStdString((TestPathHelper::FindProjectRoot() / "data" / "shaders" / "crt").string()) +
           QLatin1Char('/') + key;
}
}  // namespace

/// The parameters the automation lists (video window crt_params) before a context exists: the preset.json's, at their
/// initial values; no GL needed
TEST(SlangPresetRunner_Test, ReadParametersWithoutAContext)
{
    const std::vector<SlangPresetRunner::Parameter> list = SlangPresetRunner::readParameters(PresetDir("crt-geom"));
    ASSERT_EQ(list.size(), 22u);
    const auto gamma = std::find_if(list.begin(), list.end(),
                                    [](const SlangPresetRunner::Parameter& p) { return p.name == "CRTgamma"; });
    ASSERT_NE(gamma, list.end());
    EXPECT_FLOAT_EQ(gamma->initial, 2.4f);
    EXPECT_FLOAT_EQ(gamma->value, 2.4f);
    EXPECT_FLOAT_EQ(gamma->minimum, 0.1f);
    EXPECT_FLOAT_EQ(gamma->maximum, 5.0f);
    EXPECT_FLOAT_EQ(gamma->step, 0.1f);
    EXPECT_TRUE(SlangPresetRunner::readParameters(PresetDir("no-such-preset")).empty());
}

TEST(SlangPresetRunner_Test, EveryPresetShowsThePicture)
{
    GLContext context;
    if (!context.create())
        GTEST_SKIP() << "no OpenGL 3.3 core context on this platform";
    const QStringList presets = Presets();
    ASSERT_GE(presets.size(), 19);

    QOpenGLExtraFunctions* gl = context.gl();
    const QImage frame = MakeFrame();
    const GLuint source = Upload(gl, frame);
    const double plain = Mean(frame);
    for (const QString& key : presets)
    {
        SlangPresetRunner runner;
        ASSERT_TRUE(runner.load(PresetDir(key))) << key.toStdString() << ": " << runner.error().toStdString();
        for (int i = 0; i < 3; ++i)
            runner.render(source, frame.rect(), QSize(704, 576));
        const QImage out = runner.outputImage();
        EXPECT_EQ(gl->glGetError(), static_cast<GLenum>(GL_NO_ERROR)) << key.toStdString();
        ASSERT_EQ(out.size(), QSize(704, 576)) << key.toStdString();
        // Scanlines and masks darken (Megatron's SDR mask the most), never to black
        const double mean = Mean(out);
        EXPECT_GT(mean, plain * 0.15) << key.toStdString();
        EXPECT_LT(mean, plain * 1.6) << key.toStdString();
        runner.release();
    }
    gl->glDeleteTextures(1, &source);
}

TEST(SlangPresetRunner_Test, MatchesLibrashader)
{
    const char* reference = std::getenv("UNREAL_CRT_REFERENCE");
    if (!reference)
        GTEST_SKIP() << "UNREAL_CRT_REFERENCE not set";
    GLContext context;
    if (!context.create())
        GTEST_SKIP() << "no OpenGL 3.3 core context on this platform";
    const char* output = std::getenv("UNREAL_CRT_OUTPUT");
    // The frame librashader-cli rendered (-f, 60 unless UNREAL_CRT_FRAME says otherwise)
    const char* frameEnv = std::getenv("UNREAL_CRT_FRAME");
    const int frames = frameEnv ? std::atoi(frameEnv) : 60;
    QOpenGLExtraFunctions* gl = context.gl();
    const QDir root(QString::fromUtf8(reference));
    for (const QString& file : QDir(root.filePath(QStringLiteral("src"))).entryList({QStringLiteral("*.png")}))
    {
        const QString frameName = file.chopped(4);
        const QImage frame(root.filePath(QStringLiteral("src/") + file));
        const GLuint source = Upload(gl, frame);
        for (const QString& key : Presets())
        {
            const QImage expected = QImage(root.filePath(QStringLiteral("out/%1/%2.png").arg(frameName, key)))
                                        .convertToFormat(QImage::Format_RGBA8888);
            if (expected.isNull())
                continue;
            SlangPresetRunner runner;
            ASSERT_TRUE(runner.load(PresetDir(key))) << runner.error().toStdString();
            for (int i = 0; i <= frames; ++i)
                runner.render(source, frame.rect(), expected.size());
            const QImage out = runner.outputImage();
            runner.release();
            if (output)
            {
                QDir().mkpath(QStringLiteral("%1/%2").arg(QString::fromUtf8(output), frameName));
                out.save(QStringLiteral("%1/%2/%3.png").arg(QString::fromUtf8(output), frameName, key));
            }
            double sum = 0.0;
            long long off = 0;
            for (int y = 0; y < out.height(); ++y)
            {
                const uchar* a = out.constScanLine(y);
                const uchar* b = expected.constScanLine(y);
                for (int x = 0; x < out.width() * 4; ++x)
                {
                    if (x % 4 == 3)
                        continue;
                    const int d = std::abs(a[x] - b[x]);
                    sum += d;
                    off += d > 8;
                }
            }
            const double channels = 3.0 * out.width() * out.height();
            std::printf("%-12s %-22s mean diff %6.2f  off>8 %6.2f%%  mean %6.1f / %6.1f\n", frameName.toUtf8().constData(),
                        key.toUtf8().constData(), sum / channels, 100.0 * static_cast<double>(off) / channels, Mean(out),
                        Mean(expected));
        }
        gl->glDeleteTextures(1, &source);
    }
}

TEST(SlangPresetRunner_Test, CompositionShaderIsTheCpuPicture)
{
    // The window's GPU composition (CRTRenderer::kCompositionShader on the 3.3 core context) gives the software
    // screen's picture bit for bit: a Basic picture into a framebuffer, rows read back top first
    GLContext context;
    if (!context.create())
        GTEST_SKIP() << "no OpenGL 3.3 core context on this platform";
    QOpenGLExtraFunctions* gl = context.gl();
    const QImage frame = MakeFrame();
    CRTRenderer renderer;
    CRTRenderer::Layout layout;
    layout.outWidth = 703;
    layout.outHeight = 576;
    layout.source = frame.rect();
    layout.frameWidth = frame.width();
    layout.frameHeight = frame.height();
    const CRTProfileParams params = CRTProfileParams::Basic();
    renderer.setLayout(layout, params);
    renderer.grade(frame.constBits(), static_cast<int>(frame.bytesPerLine()), params);
    std::vector<uint8_t> expected(static_cast<size_t>(layout.outWidth) * layout.outHeight * 4);
    renderer.compose(expected.data(), layout.outWidth * 4);

    auto upload = [gl](int w, int h, const uint8_t* pixels) {
        GLuint id = 0;
        gl->glGenTextures(1, &id);
        gl->glBindTexture(GL_TEXTURE_2D, id);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return id;
    };
    const GLuint graded = upload(layout.frameWidth, layout.frameHeight, renderer.graded().data());
    const GLuint index = upload(layout.outWidth, layout.outHeight, renderer.indexTable().data());
    const GLuint factor = upload(layout.outWidth, layout.outHeight, renderer.factorTable().data());

    GLuint target = 0;
    GLuint framebuffer = 0;
    gl->glGenTextures(1, &target);
    gl->glBindTexture(GL_TEXTURE_2D, target);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, layout.outWidth, layout.outHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl->glGenFramebuffers(1, &framebuffer);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    gl->glViewport(0, 0, layout.outWidth, layout.outHeight);

    QOpenGLShaderProgram program;
    ASSERT_TRUE(program.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                                "#version 330\n"
                                                "out vec2 texCoord;\n"
                                                "void main() {\n"
                                                "    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
                                                "    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
                                                "    texCoord = vec2(corner.x, 1.0 - corner.y);\n"
                                                "}\n"));
    ASSERT_TRUE(program.addShaderFromSourceCode(QOpenGLShader::Fragment, CRTRenderer::kCompositionShader));
    ASSERT_TRUE(program.link()) << program.log().toStdString();
    program.bind();
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, graded);
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, index);
    gl->glActiveTexture(GL_TEXTURE2);
    gl->glBindTexture(GL_TEXTURE_2D, factor);
    program.setUniformValue("frame", 0);
    program.setUniformValue("indexTable", 1);
    program.setUniformValue("factorTable", 2);
    program.setUniformValue("frameSize", QVector2D(static_cast<float>(layout.frameWidth), static_cast<float>(layout.frameHeight)));
    GLuint vao = 0;
    gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    std::vector<uint8_t> shown(expected.size());
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl->glReadPixels(0, 0, layout.outWidth, layout.outHeight, GL_RGBA, GL_UNSIGNED_BYTE, shown.data());
    // GL rows from the bottom
    for (int y = 0; y < layout.outHeight; ++y)
        for (int x = 0; x < layout.outWidth; ++x)
        {
            const uint8_t* cpu = expected.data() + (static_cast<size_t>(y) * layout.outWidth + x) * 4;
            const uint8_t* gpu = shown.data() + (static_cast<size_t>(layout.outHeight - 1 - y) * layout.outWidth + x) * 4;
            ASSERT_EQ(0, std::memcmp(cpu, gpu, 3)) << x << "," << y << " cpu " << int(cpu[0]) << " gpu " << int(gpu[0]);
        }

    gl->glBindVertexArray(0);
    gl->glDeleteVertexArrays(1, &vao);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &framebuffer);
    for (GLuint id : {graded, index, factor, target})
        gl->glDeleteTextures(1, &id);
}
