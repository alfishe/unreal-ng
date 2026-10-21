/// @file crtbeammodel_test.cpp
/// @brief The integer models of the libretro CRT shaders (CRTBeamModel): every model shows the picture, the GPU
/// picture (CRTBeamModelGPU) is the software one bit for bit, the tables are rebuilt only for a new size or profile,
/// every compiler builds the same tables and picture.
/// The GPU test needs an OpenGL 3.3 core context: the offscreen platform has none on macOS
/// (UNREAL_QT_TEST_PLATFORM=cocoa), the test skips without one.
///
/// UNREAL_CRT_REFERENCE=<dir> compares with librashader, as slangpresetrunner_test does: <dir>/src/<frame>.png against
/// <dir>/out/<frame>/<key>.png; the mean difference goes to stdout.

#include <gtest/gtest.h>

#include <QDir>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "widgets/crtbeammodel.h"
#include "widgets/crtbeammodelgpu.h"

namespace
{
constexpr int kFrameWidth = 352;
constexpr int kFrameHeight = 288;

/// A Spectrum-like frame: a blue border, white paper with black ink stripes, a red block, a lone bright dot
std::vector<uint8_t> MakeFrame()
{
    std::vector<uint8_t> frame(static_cast<size_t>(kFrameWidth) * kFrameHeight * 4);
    for (int y = 0; y < kFrameHeight; ++y)
        for (int x = 0; x < kFrameWidth; ++x)
        {
            uint8_t* p = frame.data() + (static_cast<size_t>(y) * kFrameWidth + x) * 4;
            const bool border = x < 48 || x >= 304 || y < 48 || y >= 240;
            const bool ink = !border && (x / 8) % 2 == 0;
            const bool red = !border && x >= 200 && y >= 180;
            const bool dot = x == 20 && y == 20;
            p[0] = dot ? 0xFF : border ? 0 : red ? 0xCD : ink ? 0 : 0xCD;
            p[1] = dot ? 0xFF : border || red || ink ? 0 : 0xCD;
            p[2] = dot ? 0xFF : border ? 0xCD : red || ink ? 0 : 0xCD;
            p[3] = 0xFF;
        }
    return frame;
}

CRTBeamModel::Layout MakeLayout(int outWidth, int outHeight, const QRect& source = QRect(0, 0, kFrameWidth, kFrameHeight))
{
    CRTBeamModel::Layout layout;
    layout.outWidth = outWidth;
    layout.outHeight = outHeight;
    layout.source = source;
    layout.frameWidth = kFrameWidth;
    layout.frameHeight = kFrameHeight;
    return layout;
}

std::vector<CRTProfile> Models()
{
    std::vector<CRTProfile> models;
    for (CRTProfile profile : CRTProfileParams::AllProfiles())
        if (CRTBeamModel::isModel(profile))
            models.push_back(profile);
    return models;
}

std::string Key(CRTProfile profile)
{
    return CRTProfileParams::ProfileKey(profile).toStdString();
}

std::vector<uint8_t> Compose(CRTBeamModel& model, const CRTBeamModel::Layout& layout, CRTProfile profile,
                             const uint8_t* frame, int stride)
{
    model.setLayout(layout, profile);
    model.prepare(frame, stride);
    std::vector<uint8_t> out(static_cast<size_t>(layout.outWidth) * layout.outHeight * 4);
    model.compose(out.data(), layout.outWidth * 4);
    return out;
}

/// Mean of R+G+B over the middle third
double MiddleMean(const std::vector<uint8_t>& out, const CRTBeamModel::Layout& layout)
{
    double sum = 0.0;
    int count = 0;
    for (int y = layout.outHeight / 3; y < layout.outHeight * 2 / 3; ++y)
        for (int x = layout.outWidth / 3; x < layout.outWidth * 2 / 3; ++x)
        {
            const uint8_t* p = out.data() + (static_cast<size_t>(y) * layout.outWidth + x) * 4;
            sum += p[0] + p[1] + p[2];
            ++count;
        }
    return sum / count;
}

/// FNV-1a over a table's bytes
template <class T>
uint64_t Hash(uint64_t hash, const std::vector<T>& table)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(table.data());
    for (size_t i = 0; i < table.size() * sizeof(T); ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

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
}  // namespace

TEST(CRTBeamModel_Test, EveryModelShowsThePicture)
{
    // Seven models at 704 x 576 on the CPU, ~20 ms each: the scanlines and masks need a scaled picture
    const std::vector<uint8_t> frame = MakeFrame();
    const CRTBeamModel::Layout layout = MakeLayout(704, 576);
    std::vector<uint8_t> plain(static_cast<size_t>(layout.outWidth) * layout.outHeight * 4);
    for (int y = 0; y < layout.outHeight; ++y)
        for (int x = 0; x < layout.outWidth; ++x)
            std::memcpy(&plain[(static_cast<size_t>(y) * layout.outWidth + x) * 4],
                        &frame[(static_cast<size_t>(y / 2) * kFrameWidth + x / 2) * 4], 4);
    const double reference = MiddleMean(plain, layout);
    for (CRTProfile profile : Models())
    {
        CRTBeamModel model;
        const double mean = MiddleMean(Compose(model, layout, profile, frame.data(), kFrameWidth * 4), layout);
        // Scanlines and masks darken, never to black; a glow brightens a little
        EXPECT_GT(mean, reference * 0.4) << Key(profile);
        EXPECT_LT(mean, reference * 1.3) << Key(profile);
    }
}

TEST(CRTBeamModel_Test, NoModelForNoneAndBasic)
{
    EXPECT_FALSE(CRTBeamModel::isModel(CRTProfile::None));
    EXPECT_FALSE(CRTBeamModel::isModel(CRTProfile::Basic));
    EXPECT_EQ(CRTBeamModel::shapeOf(CRTProfile::Basic), nullptr);
    EXPECT_EQ(Models().size(), 7u);
}

TEST(CRTBeamModel_Test, TablesOnlyForANewSizeOrProfile)
{
    CRTBeamModel model;
    const CRTBeamModel::Layout layout = MakeLayout(400, 300);
    EXPECT_TRUE(model.setLayout(layout, CRTProfile::Lottes));
    EXPECT_FALSE(model.setLayout(layout, CRTProfile::Lottes));
    EXPECT_TRUE(model.setLayout(layout, CRTProfile::Consumer));
    EXPECT_TRUE(model.setLayout(MakeLayout(401, 300), CRTProfile::Consumer));
    EXPECT_EQ(model.geometry().size(), static_cast<size_t>(401) * 300 * 4);
}

TEST(CRTBeamModel_Test, BgraIsTheSamePictureSwapped)
{
    const std::vector<uint8_t> frame = MakeFrame();
    const CRTBeamModel::Layout layout = MakeLayout(500, 400);
    CRTBeamModel model;
    const std::vector<uint8_t> rgba = Compose(model, layout, CRTProfile::Consumer, frame.data(), kFrameWidth * 4);
    std::vector<uint8_t> bgra(rgba.size());
    model.compose(bgra.data(), layout.outWidth * 4, true);
    for (size_t i = 0; i < rgba.size(); i += 4)
    {
        ASSERT_EQ(bgra[i], rgba[i + 2]) << i / 4;
        ASSERT_EQ(bgra[i + 1], rgba[i + 1]) << i / 4;
        ASSERT_EQ(bgra[i + 2], rgba[i]) << i / 4;
        ASSERT_EQ(bgra[i + 3], 255) << i / 4;
    }
}

// The tables come from float math (crtmath.h, no contraction): every compiler, C library and CPU must build the same
// ones, and the CPU paths (NEON, SSE2, plain) must compose the same picture. A new hash after a change to a model is
// expected: take it from the failure message on one platform, the others must then agree
class CRTBeamModelBits_Test : public ::testing::TestWithParam<std::pair<CRTProfile, uint64_t>>
{
};

TEST_P(CRTBeamModelBits_Test, TheSameBitsOnEveryCompiler)
{
    const auto [profile, hash] = GetParam();
    const std::vector<uint8_t> frame = MakeFrame();
    const CRTBeamModel::Layout layout = MakeLayout(300, 240);
    CRTBeamModel model;
    const std::vector<uint8_t> out = Compose(model, layout, profile, frame.data(), kFrameWidth * 4);
    uint64_t h = 14695981039346656037ull;
    h = Hash(h, model.frame());
    h = Hash(h, model.glowFrame());
    h = Hash(h, model.geometry());
    h = Hash(h, model.factors());
    h = Hash(h, model.horizontalWeights());
    h = Hash(h, model.verticalTable());
    h = Hash(h, model.interpolationTable());
    h = Hash(h, model.transferTable());
    h = Hash(h, model.boostTable());
    h = Hash(h, model.glowPhase());
    h = Hash(h, model.glowWeightsX());
    h = Hash(h, model.glowWeightsY());
    h = Hash(h, model.glowWeightsB());
    h = Hash(h, model.outputTable());
    h = Hash(h, out);
    EXPECT_EQ(h, hash) << Key(profile) << std::hex << " 0x" << h;
}

INSTANTIATE_TEST_SUITE_P(CRTBeamModel, CRTBeamModelBits_Test,
                         ::testing::Values(std::pair{CRTProfile::Lottes, 0xd3ab40f1ec7799c5ull},
                                           std::pair{CRTProfile::Geom, 0x64b855aa5c490e24ull},
                                           std::pair{CRTProfile::Easymode, 0xce4b0640481085abull},
                                           std::pair{CRTProfile::GdvMini, 0x00c626f288a1dc2aull},
                                           std::pair{CRTProfile::Consumer, 0x6bb9adbf4394458bull},
                                           std::pair{CRTProfile::Pi, 0x2aaaeb52db431829ull},
                                           std::pair{CRTProfile::Zfast, 0x0f219ab6c5de8d9cull}),
                         [](const auto& testInfo) {
                             std::string name;
                             for (char c : Key(testInfo.param.first))
                                 if (c != '-')
                                     name += c;
                             return name;
                         });

TEST(CRTBeamModel_Test, TheGpuPictureIsTheSoftwarePicture)
{
    // ~0.6 s: the program's compilation and fourteen pictures on both backends
    GLContext context;
    if (!context.create())
        GTEST_SKIP() << "no OpenGL 3.3 core context on this platform";
    QOpenGLExtraFunctions* gl = context.gl();
    CRTBeamModelGPU gpu;
    ASSERT_TRUE(gpu.create()) << gpu.log().toStdString();

    const std::vector<uint8_t> frame = MakeFrame();
    // A whole frame at 2x, and a viewport crop at an odd size (phases off the grid)
    const CRTBeamModel::Layout layouts[] = {MakeLayout(704, 576), MakeLayout(1001, 787, QRect(16, 8, 320, 272))};
    for (const CRTBeamModel::Layout& layout : layouts)
    {
        QOpenGLFramebufferObject target(layout.outWidth, layout.outHeight, QOpenGLFramebufferObject::NoAttachment,
                                        GL_TEXTURE_2D, GL_RGBA8);
        ASSERT_TRUE(target.bind());
        gl->glViewport(0, 0, layout.outWidth, layout.outHeight);
        gl->glDisable(GL_DITHER);
        for (CRTProfile profile : Models())
        {
            CRTBeamModel model;
            const std::vector<uint8_t> cpu = Compose(model, layout, profile, frame.data(), kFrameWidth * 4);
            gpu.uploadTables(model);
            gpu.uploadFrame(model);
            gpu.draw(model);
            std::vector<uint8_t> picture(cpu.size());
            gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            gl->glReadPixels(0, 0, layout.outWidth, layout.outHeight, GL_RGBA, GL_UNSIGNED_BYTE, picture.data());
            ASSERT_EQ(gl->glGetError(), static_cast<GLenum>(GL_NO_ERROR)) << Key(profile);
            // GL rows bottom first
            int differ = 0;
            int firstX = -1;
            int firstY = -1;
            for (int y = 0; y < layout.outHeight; ++y)
            {
                const uint8_t* g = &picture[static_cast<size_t>(layout.outHeight - 1 - y) * layout.outWidth * 4];
                const uint8_t* c = &cpu[static_cast<size_t>(y) * layout.outWidth * 4];
                for (int x = 0; x < layout.outWidth; ++x)
                    if (std::memcmp(g + x * 4, c + x * 4, 3) != 0 && differ++ == 0)
                    {
                        firstX = x;
                        firstY = y;
                    }
            }
            EXPECT_EQ(differ, 0) << Key(profile) << " at " << layout.outWidth << "x" << layout.outHeight
                                 << ", first at " << firstX << "," << firstY;
        }
        target.release();
    }
    gpu.destroy();
}

TEST(CRTBeamModel_Test, AgainstLibrashader)
{
    const char* reference = std::getenv("UNREAL_CRT_REFERENCE");
    if (!reference)
        GTEST_SKIP() << "UNREAL_CRT_REFERENCE not set";
    const QDir dir(QString::fromUtf8(reference));
    const QStringList frames = QDir(dir.filePath("src")).entryList({"*.png"}, QDir::Files, QDir::Name);
    ASSERT_FALSE(frames.isEmpty());
    for (CRTProfile profile : Models())
    {
        double total = 0.0;
        for (const QString& file : frames)
        {
            const QString name = file.chopped(4);
            const QImage source = QImage(dir.filePath("src/" + file)).convertToFormat(QImage::Format_RGBA8888);
            const QImage expected = QImage(dir.filePath("out/" + name + "/" + CRTProfileParams::ProfileKey(profile) +
                                                        ".png")).convertToFormat(QImage::Format_RGBA8888);
            ASSERT_FALSE(expected.isNull()) << Key(profile) << " " << name.toStdString();
            CRTBeamModel::Layout layout = MakeLayout(expected.width(), expected.height(), source.rect());
            layout.frameWidth = source.width();
            layout.frameHeight = source.height();
            CRTBeamModel model;
            const std::vector<uint8_t> out =
                Compose(model, layout, profile, source.constBits(), static_cast<int>(source.bytesPerLine()));
            double sum = 0.0;
            for (int y = 0; y < layout.outHeight; ++y)
            {
                const uchar* e = expected.constScanLine(y);
                const uint8_t* o = &out[static_cast<size_t>(y) * layout.outWidth * 4];
                for (int x = 0; x < layout.outWidth * 4; ++x)
                    if (x % 4 != 3)
                        sum += std::abs(static_cast<int>(e[x]) - static_cast<int>(o[x]));
            }
            total += sum / (3.0 * layout.outWidth * layout.outHeight);
        }
        const double mean = total / frames.size();
        std::printf("%-14s mean |difference| %.2f over %lld frames\n", Key(profile).c_str(), mean,
                    static_cast<long long>(frames.size()));
        EXPECT_LT(mean, 0.5) << Key(profile);
    }
}
