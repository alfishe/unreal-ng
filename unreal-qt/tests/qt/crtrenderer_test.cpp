/// @file crtrenderer_test.cpp
/// @brief The CRT model both screen backends share (CRTRenderer): every profile shows the picture (the old CPU filter
/// drew most of them black), a profile without effects is a plain nearest scale, the GPU shader's arithmetic gives the
/// integer composition's values, the tables are rebuilt only for a new size or profile, a GLSL 1.20 custom shader
/// becomes GLSL 330 for the core context.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "widgets/crtrenderer.h"

namespace
{
constexpr int kFrameWidth = 352;
constexpr int kFrameHeight = 288;

/// A Spectrum-like frame: white paper, a bright blue border, black ink stripes
std::vector<uint8_t> MakeFrame()
{
    std::vector<uint8_t> frame(static_cast<size_t>(kFrameWidth) * kFrameHeight * 4);
    for (int y = 0; y < kFrameHeight; ++y)
        for (int x = 0; x < kFrameWidth; ++x)
        {
            uint8_t* p = frame.data() + (static_cast<size_t>(y) * kFrameWidth + x) * 4;
            const bool border = x < 48 || x >= 304 || y < 48 || y >= 240;
            const bool ink = !border && (x / 8) % 2 == 0;
            p[0] = border ? 0 : (ink ? 0 : 0xCD);
            p[1] = border ? 0 : (ink ? 0 : 0xCD);
            p[2] = border ? 0xFF : (ink ? 0 : 0xCD);
            p[3] = 0xFF;
        }
    return frame;
}

CRTRenderer::Layout MakeLayout(int scale)
{
    CRTRenderer::Layout layout;
    layout.outWidth = kFrameWidth * scale;
    layout.outHeight = kFrameHeight * scale;
    layout.source = QRect(0, 0, kFrameWidth, kFrameHeight);
    layout.frameWidth = kFrameWidth;
    layout.frameHeight = kFrameHeight;
    return layout;
}

std::vector<uint8_t> Render(CRTRenderer& renderer, const CRTRenderer::Layout& layout, const CRTProfileParams& params,
                            const std::vector<uint8_t>& frame)
{
    renderer.setLayout(layout, params);
    renderer.grade(frame.data(), kFrameWidth * 4, params);
    std::vector<uint8_t> out(static_cast<size_t>(layout.outWidth) * layout.outHeight * 4);
    renderer.compose(out.data(), layout.outWidth * 4);
    return out;
}

/// Mean of R+G+B over the paper's middle (no border, no corners, no vignette edge)
double PaperMean(const std::vector<uint8_t>& out, const CRTRenderer::Layout& layout)
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
}  // namespace

TEST(CRTRenderer_Test, EveryProfileShowsThePicture)
{
    const std::vector<uint8_t> frame = MakeFrame();
    CRTRenderer plain;
    for (int scale : {1, 3})
    {
        const CRTRenderer::Layout layout = MakeLayout(scale);
        const double reference = PaperMean(Render(plain, layout, CRTProfileParams::None(), frame), layout);
        for (CRTProfile profile : CRTProfileParams::AllProfiles())
        {
            if (profile == CRTProfile::None)
                continue;
            CRTRenderer renderer;
            const double mean = PaperMean(Render(renderer, layout, CRTProfileParams::FromProfile(profile), frame), layout);
            // Scanlines and masks darken, never to black, and nothing brightens much beyond the plain picture
            EXPECT_GT(mean, reference * 0.4) << CRTProfileParams::ProfileKey(profile).toStdString() << " at " << scale
                                             << "x";
            EXPECT_LT(mean, reference * 1.3) << CRTProfileParams::ProfileKey(profile).toStdString() << " at " << scale
                                             << "x";
        }
    }
}

TEST(CRTRenderer_Test, WhiteStaysBrightWithContrast)
{
    // The old NEON filter multiplied by contrast * 256 in int16: full white overflowed to black
    std::vector<uint8_t> white(static_cast<size_t>(kFrameWidth) * kFrameHeight * 4, 0xFF);
    CRTProfileParams graded = CRTProfileParams::Basic();
    graded.contrast = 1.1f;
    graded.brightness = 1.05f;
    graded.saturation = 1.2f;
    CRTRenderer renderer;
    const CRTRenderer::Layout layout = MakeLayout(3);
    const std::vector<uint8_t> out = Render(renderer, layout, graded, white);
    EXPECT_GT(PaperMean(out, layout), 3 * 255 * 0.4);
}

TEST(CRTRenderer_Test, NoEffectsIsANearestScale)
{
    CRTProfileParams params = CRTProfileParams::None();
    params.profile = CRTProfile::Basic;
    params.gamma = 1.0f;
    const std::vector<uint8_t> frame = MakeFrame();
    CRTRenderer renderer;
    CRTRenderer::Layout layout = MakeLayout(3);
    layout.source = QRect(16, 8, 320, 272);  // a viewport crop
    layout.outWidth = 960;
    layout.outHeight = 816;
    const std::vector<uint8_t> out = Render(renderer, layout, params, frame);

    // The vignette stays even without effects: the expected value takes its factor from the table
    for (int y = 0; y < layout.outHeight; y += 7)
        for (int x = 0; x < layout.outWidth; x += 5)
        {
            const size_t i = (static_cast<size_t>(y) * layout.outWidth + x) * 4;
            const int sx = renderer.indexTable()[i] | (renderer.indexTable()[i + 1] << 8);
            const int sy = renderer.indexTable()[i + 2] | (renderer.indexTable()[i + 3] << 8);
            ASSERT_EQ(sx, 16 + x / 3) << x << "," << y;
            ASSERT_EQ(sy, 8 + y / 3) << x << "," << y;
            const uint8_t* source = frame.data() + (static_cast<size_t>(sy) * kFrameWidth + sx) * 4;
            for (int c = 0; c < 3; ++c)
            {
                const int expected = std::min(255, (source[c] * renderer.factorTable()[i + c]) >> 7);
                ASSERT_EQ(out[i + c], expected) << x << "," << y << " channel " << c;
            }
        }
}

TEST(CRTRenderer_Test, TheShaderArithmeticIsTheIntegerComposition)
{
    // kCompositionShader: floor(c * f / 128.0) on values recovered as floor(x * 255 + 0.5), in 32-bit floats
    for (int c = 0; c < 256; ++c)
        for (int f = 0; f < 256; ++f)
        {
            const float cf = std::floor(static_cast<float>(c) / 255.0f * 255.0f + 0.5f);
            const float ff = std::floor(static_cast<float>(f) / 255.0f * 255.0f + 0.5f);
            const float shader = std::fmin(std::floor(cf * ff / 128.0f), 255.0f);
            ASSERT_EQ(static_cast<int>(shader), std::min(255, (c * f) >> 7)) << c << " * " << f;
        }
}

TEST(CRTRenderer_Test, TablesOnlyForANewSizeOrProfile)
{
    CRTRenderer renderer;
    const CRTProfileParams basic = CRTProfileParams::Basic();
    EXPECT_TRUE(renderer.setLayout(MakeLayout(2), basic));
    EXPECT_FALSE(renderer.setLayout(MakeLayout(2), basic));
    EXPECT_TRUE(renderer.setLayout(MakeLayout(3), basic));
    EXPECT_TRUE(renderer.setLayout(MakeLayout(3), CRTProfileParams::None()));

    // Deterministic: the same input twice is the same picture
    const std::vector<uint8_t> frame = MakeFrame();
    CRTRenderer again;
    EXPECT_EQ(Render(renderer, MakeLayout(3), CRTProfileParams::Basic(), frame),
              Render(again, MakeLayout(3), CRTProfileParams::Basic(), frame));
}

TEST(CRTRenderer_Test, NoneIsThePlainPictureExactly)
{
    // Both backends draw the picture without CRT through the renderer: a nearest scale at pixel centres, every value
    // the source's (703 x 576: the window's odd fit, not a whole scale)
    const std::vector<uint8_t> frame = MakeFrame();
    CRTRenderer renderer;
    CRTRenderer::Layout layout = MakeLayout(2);
    layout.outWidth = 703;
    layout.outHeight = 576;
    const std::vector<uint8_t> out = Render(renderer, layout, CRTProfileParams::None(), frame);
    for (int y = 0; y < layout.outHeight; ++y)
        for (int x = 0; x < layout.outWidth; ++x)
        {
            const int sx = static_cast<int>((static_cast<float>(x) + 0.5f) / 703.0f * kFrameWidth);
            const int sy = static_cast<int>((static_cast<float>(y) + 0.5f) / 576.0f * kFrameHeight);
            const uint8_t* source = frame.data() + (static_cast<size_t>(sy) * kFrameWidth + sx) * 4;
            const uint8_t* shown = out.data() + (static_cast<size_t>(y) * layout.outWidth + x) * 4;
            ASSERT_EQ(0, std::memcmp(source, shown, 3)) << x << "," << y;
        }
}

TEST(CRTRenderer_Test, BgraIsTheSamePictureSwapped)
{
    // The software screen composes in its backing store's byte order: the same values, red and blue swapped
    const std::vector<uint8_t> frame = MakeFrame();
    for (CRTProfile profile : {CRTProfile::None, CRTProfile::Basic})
    {
        CRTRenderer renderer;
        const CRTRenderer::Layout layout = MakeLayout(2);
        const std::vector<uint8_t> rgba = Render(renderer, layout, CRTProfileParams::FromProfile(profile), frame);
        std::vector<uint8_t> bgra(rgba.size());
        renderer.compose(bgra.data(), layout.outWidth * 4, true);
        for (size_t i = 0; i < rgba.size(); i += 4)
        {
            ASSERT_EQ(bgra[i], rgba[i + 2]) << i;
            ASSERT_EQ(bgra[i + 1], rgba[i + 1]) << i;
            ASSERT_EQ(bgra[i + 2], rgba[i]) << i;
            ASSERT_EQ(bgra[i + 3], 255) << i;
        }
    }
}

TEST(CRTRenderer_Test, CustomShaderPreludeForTheCoreContext)
{
    // A GLSL 1.20 custom shader runs on the 3.3 core context: the legacy words become the core ones, the line numbers
    // keep their offset
    const QString legacy = QStringLiteral("#version 120\n"
                                          "uniform sampler2D tex;\n"
                                          "varying vec2 texCoord;\n"
                                          "void main() { vec4 texture2DColor = texture2D(tex, texCoord); "
                                          "gl_FragColor = texture2DColor; }\n");
    const QString core = CRTRenderer::coreFragmentSource(legacy);
    EXPECT_EQ(core, QStringLiteral("#version 330\n"
                                   "out vec4 unrealFragColor;\n"
                                   "\n"
                                   "uniform sampler2D tex;\n"
                                   "in vec2 texCoord;\n"
                                   "void main() { vec4 texture2DColor = texture(tex, texCoord); "
                                   "unrealFragColor = texture2DColor; }\n"));

    // Without a version line (GLSL 1.10) the same; a core shader stays as it is
    EXPECT_TRUE(CRTRenderer::coreFragmentSource(QStringLiteral("varying vec2 texCoord;\n"))
                    .startsWith(QStringLiteral("#version 330\nout vec4 unrealFragColor;\nin vec2 texCoord;")));
    const QString modern = QStringLiteral("#version 330 core\nin vec2 texCoord;\nout vec4 color;\nvoid main() {}\n");
    EXPECT_EQ(CRTRenderer::coreFragmentSource(modern), modern);
}
