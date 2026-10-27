// The images of a captured Next frame (core/src/emulator/video/next/nextlayerimage.h, design-layer-capture.md): one layer as RGBA / PNG with
// alpha 0 where it does not paint, the ids image in the colours of the legend, the words every surface parses, the refusals (not a Next, the
// capture off, no frame yet) and the report of the switch. The frames are built by hand, so every pixel is known.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <vector>

#include "3rdparty/lodepng/lodepng.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/nextreporthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/next/nextlayerimage.h"
#include "emulator/video/next/screennext.h"

using namespace NextLayer;

namespace
{
uint32_t Px(const std::vector<uint8_t>& rgba, unsigned x, unsigned row)
{
    uint32_t v;
    std::memcpy(&v, rgba.data() + (static_cast<size_t>(row) * kWidth + x) * 4, 4);
    return v;
}
}  // namespace

TEST(NextLayerImage_Test, TheWordsOfARequest)
{
    Kind kind = Kind::Composed;
    for (const char* word : {"composed", "ula", "layer2", "tilemap", "sprites", "ids", "ULA", "Layer2"})
        EXPECT_TRUE(ParseKind(word, kind)) << word;
    EXPECT_TRUE(ParseKind("tilemap", kind));
    EXPECT_EQ(kind, Kind::Tilemap);
    EXPECT_STREQ(KindName(kind), "tilemap");
    EXPECT_FALSE(ParseKind("layer3", kind));
    EXPECT_FALSE(ParseKind("", kind));
    bool rgba = true, alpha = false;
    std::string message;
    EXPECT_TRUE(ParseImageFormat("", rgba, message));
    EXPECT_FALSE(rgba) << "png is the default";
    EXPECT_TRUE(ParseImageFormat("RGBA", rgba, message));
    EXPECT_TRUE(rgba);
    EXPECT_FALSE(ParseImageFormat("gif", rgba, message));
    EXPECT_NE(message.find("png or rgba"), std::string::npos) << message;
    EXPECT_TRUE(ParseAlpha("", alpha, message));
    EXPECT_TRUE(alpha) << "alpha is the default";
    EXPECT_TRUE(ParseAlpha("false", alpha, message));
    EXPECT_FALSE(alpha);
    EXPECT_TRUE(ParseAlpha("1", alpha, message));
    EXPECT_TRUE(alpha);
    EXPECT_FALSE(ParseAlpha("maybe", alpha, message));
}

TEST(NextLayerImage_Test, ALayerImageIsTheGridWithEveryLineTwiceAndAlphaZeroWhereNothingIsPainted)
{
    Frame frame;
    frame.Allocate();
    frame.BeginFrame();
    // tilemap: line 10, x 100 is an opaque white pixel, x 101 a pixel in the colour the layer carries but not opaque (the global transparency)
    frame.plane[kTilemap][10 * kWidth + 100] = MakePixel(0x1FF, true);
    frame.plane[kTilemap][10 * kWidth + 101] = MakePixel(0x1C7, false);
    // the fallback colour of the line's segment: NR #4A = #E3 -> 9-bit 0x1C7
    Segment segment;
    segment.fromX = 0;
    segment.toX = kWidth;
    for (unsigned i = 0; i < kKeyRegCount; i++)
        if (kKeyRegs[i] == 0x4A)
            segment.nr[i] = 0xE3;
    for (unsigned y = 0; y < kHeight; y++)
    {
        frame.lineFirst[y] = static_cast<uint16_t>(frame.segments.size());
        frame.lineSegments[y] = 1;
        frame.segments.push_back(segment);
    }
    std::vector<uint8_t> rgba;
    Render(frame, Kind::Tilemap, true, rgba);
    ASSERT_EQ(rgba.size(), static_cast<size_t>(kWidth) * kHeight * 2 * 4);
    const uint32_t white = NextVideoRenderer::Rgba(0x1FF);
    EXPECT_EQ(Px(rgba, 100, 20), white) << "the grid line 10 is the image rows 20 and 21";
    EXPECT_EQ(Px(rgba, 100, 21), white);
    EXPECT_EQ(Px(rgba, 100, 22), 0u) << "the next grid line paints nothing";
    EXPECT_EQ(Px(rgba, 101, 20), 0u) << "alpha 0: not opaque (all four bytes zero)";
    EXPECT_EQ(Px(rgba, 100, 20) >> 24, 0xFFu) << "an opaque pixel has alpha 255";
    // flattened: what the layer does not paint becomes the fallback colour of its segment, and the image is opaque
    Render(frame, Kind::Tilemap, false, rgba);
    EXPECT_EQ(Px(rgba, 100, 20), white);
    EXPECT_EQ(Px(rgba, 101, 20), NextVideoRenderer::Rgba(0x1C7));
    EXPECT_EQ(Px(rgba, 5, 300) >> 24, 0xFFu);
}

TEST(NextLayerImage_Test, TheIdsImageUsesTheColoursOfTheLegendAndAPngKeepsItsAlpha)
{
    Frame frame;
    frame.Allocate();
    frame.BeginFrame();
    for (unsigned id = 0; id < kIdCount; id++)
        frame.ids[3 * kWidth + id] = static_cast<uint8_t>(id);
    std::vector<uint8_t> rgba;
    Render(frame, Kind::Ids, true, rgba);
    for (unsigned id = 0; id < kIdCount; id++)
    {
        EXPECT_EQ(Px(rgba, id, 6), IdColour(id)) << IdName(id);
        EXPECT_EQ(Px(rgba, id, 6) >> 24, 0xFFu) << "the ids image is opaque";
        for (unsigned other = 0; other < id; other++)
            EXPECT_NE(IdColour(id), IdColour(other)) << "the legend colours are distinct: " << IdName(id) << " / " << IdName(other);
    }
    EXPECT_STREQ(IdName(kIdBlend), "blend");
    EXPECT_STREQ(IdName(kIdStencil), "stencil");
    // a PNG of a layer image decodes to the same RGBA, alpha included
    frame.plane[kSprites][3 * kWidth + 40] = MakePixel(0x0C0, true);
    Render(frame, Kind::Sprites, true, rgba);
    const std::vector<uint8_t> png = EncodePng(rgba, kWidth, kHeight * 2);
    ASSERT_GT(png.size(), 8u);
    EXPECT_EQ(png[1], 'P');
    EXPECT_EQ(png[2], 'N');
    EXPECT_EQ(png[3], 'G');
    std::vector<uint8_t> decoded;
    unsigned w = 0, h = 0;
    ASSERT_EQ(lodepng::decode(decoded, w, h, png), 0u);
    EXPECT_EQ(w, kWidth);
    EXPECT_EQ(h, kHeight * 2);
    EXPECT_EQ(decoded, rgba);
    EXPECT_EQ(Px(decoded, 41, 6), 0u);
    EXPECT_EQ(Px(decoded, 40, 6) >> 24, 0xFFu);
}

// region <on a machine>

class NextLayerImageMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        _context->pFeatureManager->setFeature(Features::kScreenHQ, true);
        _context->pMemory->UpdateFeatureCache();
        // park the CPU in RAM (DI; JR $)
        _context->pMemory->DirectWriteToZ80Memory(0xC000, 0xF3);
        _context->pMemory->DirectWriteToZ80Memory(0xC001, 0x18);
        _context->pMemory->DirectWriteToZ80Memory(0xC002, 0xFE);
        _context->pCore->GetZ80()->pc = 0xC000;
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    ScreenNext* Screen() { return dynamic_cast<ScreenNext*>(_context->pScreen); }
    void Frames(unsigned n)
    {
        for (unsigned i = 0; i < n; i++)
            _emulator->RunFrame(true);
    }
};

TEST_F(NextLayerImageMachine_Test, TheRefusalsSayWhatToDo)
{
    ASSERT_NE(Screen(), nullptr);
    // on, but no frame has run
    Screen()->SetLayerCapture(true);
    Image image = Capture(_context, Kind::Ula, true);
    EXPECT_EQ(image.status, Status::NoFrame);
    EXPECT_NE(image.message.find("resume the machine or step a frame"), std::string::npos) << image.message;
    // off
    Screen()->SetLayerCapture(false);
    image = Capture(_context, Kind::Ula, true);
    EXPECT_EQ(image.status, Status::Off);
    EXPECT_NE(image.message.find("next/layer-capture"), std::string::npos) << image.message;
    // composed is not a layer image
    Screen()->SetLayerCapture(true);
    Frames(3);
    EXPECT_EQ(Capture(_context, Kind::Composed, true).status, Status::BadParameter);
    // bad words through Request
    EXPECT_EQ(Request(_context, "layer3", "", "", "").status, Status::BadParameter);
    EXPECT_EQ(Request(_context, "ula", "gif", "", "").status, Status::BadParameter);
    EXPECT_EQ(Request(_context, "ula", "", "perhaps", "").status, Status::BadParameter);
}

TEST_F(NextLayerImageMachine_Test, AnotherMachineHasNoLayers)
{
    Emulator* other = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(NextScreen(other->GetContext()), nullptr);
    const Image image = Capture(other->GetContext(), Kind::Ula, true);
    EXPECT_EQ(image.status, Status::NotNext);
    EXPECT_NE(image.message.find("not a ZX Spectrum Next"), std::string::npos) << image.message;
    const StateNode control = DeviceState::NextLayerCaptureControl(other->GetContext(), 1);
    EXPECT_EQ(NextReportHelper::Path(control, "available"), "off");
    EmulatorTestHelper::CleanupEmulator(other);
}

TEST_F(NextLayerImageMachine_Test, ALayerOfTheLastFrameAsPngAndRawBytesAndAFile)
{
    Screen()->SetLayerCapture(true);
    // the ULA screen: all ink, red on blue; the other layers are off
    std::memset(_context->pMemory->RAMPageAddress(5) + 0x1800, 0x0A, 768);
    std::memset(_context->pMemory->RAMPageAddress(5), 0xFF, 0x1800);
    Frames(3);
    const Result png = Request(_context, "ula", "", "", "");
    ASSERT_EQ(png.status, Status::Ok) << png.message;
    EXPECT_FALSE(png.rgba);
    EXPECT_EQ(png.width, 640u);
    EXPECT_EQ(png.height, 512u);
    EXPECT_GT(png.sequence, 0u);
    std::vector<uint8_t> decoded;
    unsigned w = 0, h = 0;
    ASSERT_EQ(lodepng::decode(decoded, w, h, png.bytes), 0u);
    EXPECT_EQ(w, 640u);
    EXPECT_EQ(Px(decoded, 200, 2 * 100), NextVideoRenderer::Rgba(_ports->Board().Video().PaletteEntry(0, 2) & 0x1FF)) << "ink 2 (red), the paper pixel";
    // the same frame as raw bytes
    const Result raw = Request(_context, "ULA", "rgba", "true", "");
    ASSERT_EQ(raw.status, Status::Ok) << raw.message;
    EXPECT_TRUE(raw.rgba);
    EXPECT_EQ(raw.bytes.size(), 640u * 512u * 4u);
    // the layers the machine does not draw are empty: Layer 2 and the sprites are alpha 0 everywhere
    const Result layer2 = Request(_context, "layer2", "rgba", "", "");
    ASSERT_EQ(layer2.status, Status::Ok);
    size_t painted = 0;
    for (size_t i = 3; i < layer2.bytes.size(); i += 4)
        painted += layer2.bytes[i] != 0;
    EXPECT_EQ(painted, 0u);
    // to a file
    const std::string file = TestPathHelper::GetUniqueTestScratchPath("layer.png");
    const Result saved = Request(_context, "ids", "png", "", file);
    ASSERT_EQ(saved.status, Status::Ok) << saved.message;
    EXPECT_EQ(saved.savedFile, file);
    EXPECT_TRUE(saved.bytes.empty());
    std::ifstream in(file, std::ios::binary);
    EXPECT_TRUE(in.good());
    EXPECT_EQ(static_cast<size_t>(in.seekg(0, std::ios::end).tellg()), saved.size);
    std::remove(file.c_str());
}

TEST_F(NextLayerImageMachine_Test, TheSwitchReportsAndTheVideoReportCarriesTheField)
{
    StateNode state = DeviceState::NextLayerCaptureControl(_context, -1);
    EXPECT_EQ(NextReportHelper::Path(state, "available"), "on");
    EXPECT_EQ(NextReportHelper::Path(state, "enabled"), "on") << "on by default";
    Frames(3);
    state = DeviceState::NextLayerCaptureControl(_context, -1);
    EXPECT_GT(std::stoi(NextReportHelper::Path(state, "frames")), 0);
    EXPECT_GT(std::stoi(NextReportHelper::Path(state, "last_frame")), 0);
    EXPECT_EQ(NextReportHelper::Path(state, "bytes"), std::to_string(2 * Frame::Bytes()));
    const StateNode video = DeviceState::NextVideo(_context);
    EXPECT_EQ(NextReportHelper::Path(video, "layer_capture.enabled"), "on");
    EXPECT_EQ(NextReportHelper::Path(video, "layer_capture.frames"), NextReportHelper::Path(state, "frames"));
    EXPECT_EQ(NextReportHelper::Path(video, "layer_capture.bytes"), NextReportHelper::Path(state, "bytes"));
    state = DeviceState::NextLayerCaptureControl(_context, 0);
    EXPECT_EQ(NextReportHelper::Path(state, "enabled"), "off");
    Frames(1);
    EXPECT_EQ(NextReportHelper::Path(DeviceState::NextLayerCaptureControl(_context, -1), "bytes"), "0");
    EXPECT_EQ(NextReportHelper::Path(DeviceState::NextVideo(_context), "layer_capture.enabled"), "off");
}

// endregion

// size=grid keeps the 640 sub-pixels with every line once; size=pixel keeps the first sub-pixel of each Next pixel: 320 x 256
TEST(NextLayerImage_Test, TheSmallerSizesAreTheGridAndThePixelsOfTheScreenImage)
{
    Scale scale;
    std::string message;
    ASSERT_TRUE(ParseScale("", scale, message));
    EXPECT_EQ(scale, Scale::Screen);
    ASSERT_TRUE(ParseScale("Grid", scale, message));
    EXPECT_EQ(scale, Scale::Grid);
    ASSERT_TRUE(ParseScale("pixel", scale, message));
    EXPECT_EQ(scale, Scale::Pixel);
    EXPECT_FALSE(ParseScale("raw", scale, message));
    EXPECT_NE(message.find("screen (640x512)"), std::string::npos);

    unsigned w, h;
    ScaleSize(Scale::Screen, w, h);
    EXPECT_EQ(w * h, 640u * 512u);
    ScaleSize(Scale::Grid, w, h);
    EXPECT_EQ(w * h, 640u * 256u);
    ScaleSize(Scale::Pixel, w, h);
    EXPECT_EQ(w * h, 320u * 256u);

    // a screen image whose pixel (x, row) carries x + 1000 * (row / 2)
    std::vector<uint8_t> screen(static_cast<size_t>(kWidth) * kHeight * 2 * 4);
    uint32_t* p = reinterpret_cast<uint32_t*>(screen.data());
    for (unsigned row = 0; row < kHeight * 2; row++)
        for (unsigned x = 0; x < kWidth; x++)
            p[static_cast<size_t>(row) * kWidth + x] = x + 1000u * (row / 2);
    std::vector<uint8_t> out;
    Reduce(screen, Scale::Grid, out);
    ASSERT_EQ(out.size(), static_cast<size_t>(640) * 256 * 4);
    const uint32_t* g = reinterpret_cast<const uint32_t*>(out.data());
    EXPECT_EQ(g[7 * 640 + 301], 301u + 7000u);
    Reduce(screen, Scale::Pixel, out);
    ASSERT_EQ(out.size(), static_cast<size_t>(320) * 256 * 4);
    const uint32_t* q = reinterpret_cast<const uint32_t*>(out.data());
    EXPECT_EQ(q[7 * 320 + 150], 300u + 7000u) << "pixel 150 is sub-pixel 300";
    Reduce(screen, Scale::Screen, out);
    EXPECT_EQ(out, screen);
}
