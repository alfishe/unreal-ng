// The per-layer record of the Next picture (core/src/emulator/video/next/nextlayerframe.h, design:
// docs/inprogress/2026-10-07-zx-next/design-layer-capture.md section 4). The renderer-level tests use random but fixed scenes, so that
// every layer wins somewhere, and check the record against the composed row it was made with: the capture and the compositor cannot
// disagree. The machine-level tests run ScreenNext (segments of a copper line, the two sets, the swap at the frame end).

#include "stdafx.h"
#include "_helpers/testwaithelper.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/video/next/nextlayerframe.h"
#include "emulator/video/next/nextvideorenderer.h"
#include "emulator/video/next/screennext.h"
#include "_helpers/nextscene.h"

namespace
{
using namespace NextLayer;

uint16_t Nine(uint8_t rrrgggbb)
{
    return static_cast<uint16_t>((rrrgggbb << 1) | ((rrrgggbb & 3) ? 1 : 0));
}
unsigned R3(uint16_t c) { return (c >> 6) & 7; }
unsigned G3(uint16_t c) { return (c >> 3) & 7; }
unsigned B2(uint16_t c) { return (c >> 1) & 3; }
uint16_t Pack(unsigned r, unsigned g, unsigned b)
{
    return static_cast<uint16_t>((r << 6) | (g << 3) | (b << 1) | (b ? 1 : 0));
}
}  // namespace

// region <the renderer with a sink>

class NextLayerCapture_Test : public ::testing::Test
{
protected:
    NextRandomScene _scene;
    NextVideoInputs& _in = _scene.in;
    Frame _frame;
    std::vector<uint32_t> _line = std::vector<uint32_t>(NextVideoRenderer::kWidth);

    void SetUp() override { _frame.Allocate(); }

    /// Render line y into _line, recording into the frame's row when `record`
    void Render(unsigned y, bool record = true)
    {
        const LineSink sink = _frame.Line(y);
        _in.layerOut = record ? &sink : nullptr;
        NextVideoRenderer::RenderLine(_in, y, _line.data());
        _in.layerOut = nullptr;
    }

    Pixel Plane(unsigned plane, unsigned y, unsigned x) const { return _frame.plane[plane][y * kWidth + x]; }
    uint8_t Id(unsigned y, unsigned x) const { return _frame.ids[y * kWidth + x]; }
    static uint32_t Rgba(Pixel p) { return NextVideoRenderer::Rgba(p.colour & 0x1FF); }

    /// Count of each id over the checked lines; fails the test on the first pixel the record does not explain
    void CheckLines(const char* name, unsigned (&seen)[kIdCount])
    {
        const unsigned order = (_in.nr[0x15] >> 2) & 7;
        const unsigned blend = (_in.nr[0x68] >> 5) & 3;
        const bool stencilMode = (_in.nr[0x68] & 0x01) && (_in.nr[0x6B] & 0x80) && !(_in.nr[0x68] & 0x80);
        const uint32_t fallback = NextVideoRenderer::Rgba(Nine(_in.nr[0x4A]));
        for (unsigned y : NextRandomScene::Lines())
        {
            Render(y);
            for (unsigned x = 0; x < kWidth; x++)
            {
                const Pixel ula = Plane(kUla, y, x), l2 = Plane(kLayer2, y, x), tile = Plane(kTilemap, y, x), sprite = Plane(kSprites, y, x);
                const uint8_t id = Id(y, x);
                ASSERT_LT(id, kIdCount) << name;
                seen[id]++;
                const uint32_t composed = _line[x];
                uint32_t expected = 0;
                switch (id)
                {
                    case kFallback: expected = fallback; break;
                    case kIdUla:
                        ASSERT_TRUE(ula.opaque) << name << " y=" << y << " x=" << x;
                        ASSERT_FALSE(ula.border) << name << " y=" << y << " x=" << x;
                        expected = Rgba(ula);
                        break;
                    case kIdBorder:
                        ASSERT_TRUE(ula.opaque && ula.border) << name << " y=" << y << " x=" << x;
                        expected = Rgba(ula);
                        break;
                    case kIdLayer2:
                        ASSERT_TRUE(l2.opaque) << name << " y=" << y << " x=" << x;
                        expected = Rgba(l2);
                        break;
                    case kIdTilemap:
                        ASSERT_TRUE(tile.opaque) << name << " y=" << y << " x=" << x;
                        expected = Rgba(tile);
                        break;
                    case kIdSprite:
                        ASSERT_TRUE(sprite.opaque) << name << " y=" << y << " x=" << x;
                        expected = Rgba(sprite);
                        break;
                    case kIdStencil:
                        ASSERT_TRUE(stencilMode) << name;
                        expected = NextVideoRenderer::Rgba(Pack(R3(ula.colour) & R3(tile.colour), G3(ula.colour) & G3(tile.colour), B2(ula.colour) & B2(tile.colour)));
                        break;
                    case kIdBlend:
                    {
                        ASSERT_GE(order, 6u) << name;
                        const bool lv = l2.opaque, ulaV = ula.opaque, tv = tile.opaque, below = tile.below;
                        bool mixOpaque = false;
                        uint16_t mix = 0;
                        switch (blend)
                        {
                            case 0: mixOpaque = ulaV; mix = ula.colour; break;
                            case 1: break;
                            case 2:
                                if (stencilMode)
                                {
                                    mixOpaque = ulaV && tv;
                                    mix = Pack(R3(ula.colour) & R3(tile.colour), G3(ula.colour) & G3(tile.colour), B2(ula.colour) & B2(tile.colour));
                                }
                                else if (tv && (!below || !ulaV))
                                {
                                    mixOpaque = true;
                                    mix = tile.colour;
                                }
                                else
                                {
                                    mixOpaque = ulaV;
                                    mix = ula.colour;
                                }
                                break;
                            default: mixOpaque = tv; mix = tile.colour; break;
                        }
                        unsigned r = (lv ? R3(l2.colour) : 0) + (mixOpaque ? R3(mix) : 0);
                        unsigned g = (lv ? G3(l2.colour) : 0) + (mixOpaque ? G3(mix) : 0);
                        unsigned b = (lv ? B2(l2.colour) : 0) + (mixOpaque ? B2(mix) : 0);
                        if (order == 6)
                            expected = NextVideoRenderer::Rgba(Pack(std::min(r, 7u), std::min(g, 7u), std::min(b, 3u)));
                        else
                        {
                            auto sub = [](unsigned v) -> unsigned { return v <= 4 ? 0 : (((v >> 2) & 3) == 3 ? 7 : ((v + 0x0B) & 0x0F)); };
                            if (mixOpaque)
                            {
                                r = sub(r);
                                g = sub(g);
                                b = sub(b);
                            }
                            expected = NextVideoRenderer::Rgba(Pack(r & 7, g & 7, b & 3));
                        }
                        break;
                    }
                }
                ASSERT_EQ(composed, expected) << name << " y=" << y << " x=" << x << " id=" << unsigned(id);
                if (id == kIdSprite)
                {
                    ASSERT_LT(_frame.info[y * kWidth + x], NextSprites::kSprites);
                }
            }
        }
    }
};

TEST_F(NextLayerCapture_Test, IdsExplainTheComposedFrame)
{
    // every layer order, the ULA clip window, tiles above the ULA, text-mode tiles, LoRes, the stencil, the blend modes 110 / 111 with
    // their four mix sources and the ULA off (NextRandomScene::Configs): for every pixel the composed colour is the colour of the layer
    // the id names, or the result of the blend / stencil formula of its inputs
    unsigned seen[kIdCount] = {};
    for (const NextSceneConfig& config : NextRandomScene::Configs())
    {
        _scene.Apply(config);
        CheckLines(config.name, seen);
        ASSERT_FALSE(HasFatalFailure()) << config.name << " nr15=" << unsigned(config.nr15) << " nr68=" << unsigned(config.nr68);
    }
    for (unsigned id = 0; id < kIdCount; id++)
        EXPECT_GT(seen[id], 0u) << "the sweep never draws id " << id;
}

TEST_F(NextLayerCapture_Test, OffCostsNothing)
{
    // the sink changes nothing the compositor draws: the same line with and without it gives the same row, whatever the configuration
    for (const NextSceneConfig& config : NextRandomScene::Configs())
    {
        _scene.Apply(config);
        for (unsigned y : NextRandomScene::Lines())
        {
            Render(y, false);
            const std::vector<uint32_t> without = _line;
            Render(y, true);
            ASSERT_EQ(without, _line) << config.name << " y=" << y;
        }
    }
}

// The renderer's frames are what they were before the sink existed: the digest of every configuration and line of the fixed scene,
// taken with the renderer as of commit deb0ec112 (without the layer record). Re-taken 2026-10-12 after the VHDL re-read of the ULA
// (Timex mode fetch, hi-res bright border, fine scroll, the border never clipped), LoRes (bank 5, Y scroll) and the sprite engine
// (collision / budget before the clip window); the sink-versus-no-sink identity above is the check that the record changes nothing
TEST_F(NextLayerCapture_Test, FramesAreByteIdenticalToTheRendererBeforeTheSink)
{
    EXPECT_EQ(_scene.Digest(), 0x839B1F5D8CF2B7FBull);
}

TEST_F(NextLayerCapture_Test, TransparencyIsAlpha)
{
    // Layer 2 and ULA pixels in the global transparency colour, tile pixels of the transparent index and sprite pixels of the
    // transparent pattern value are not opaque in their plane; everything else painted is
    _scene.Apply(NextRandomScene::Configs()[0]);
    bool tileTransparent = false, tileOpaque = false, l2Transparent = false, l2Opaque = false, spriteOpaque = false, ulaTransparent = false;
    for (unsigned y : NextRandomScene::Lines())
    {
        Render(y);
        for (unsigned x = 0; x < kWidth; x++)
        {
            const Pixel l2 = Plane(kLayer2, y, x), tile = Plane(kTilemap, y, x), ula = Plane(kUla, y, x), sp = Plane(kSprites, y, x);
            if (l2.opaque)
            {
                EXPECT_NE((l2.colour >> 1) & 0xFF, 0xE3u) << "an opaque Layer 2 pixel is never the global transparency colour";
                l2Opaque = true;
            }
            else if (l2.colour)
            {
                EXPECT_EQ((l2.colour >> 1) & 0xFF, 0xE3u) << "a coloured but not opaque Layer 2 pixel is the global transparency colour";
                l2Transparent = true;
            }
            if (tile.opaque)
                tileOpaque = true;
            else
                tileTransparent = true;
            if (!ula.opaque && ula.colour)
            {
                EXPECT_EQ((ula.colour >> 1) & 0xFF, 0xE3u);
                ulaTransparent = true;
            }
            if (sp.opaque)
                spriteOpaque = true;
        }
    }
    EXPECT_TRUE(tileTransparent && tileOpaque && l2Opaque && spriteOpaque) << "the scene exercises every case";
    EXPECT_TRUE(l2Transparent && ulaTransparent) << "the global transparency colour is hit in Layer 2 and in the ULA";
}

// endregion

// region <ScreenNext>

class NextLayerCaptureMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;
    ScreenNext* _screen = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        _screen = dynamic_cast<ScreenNext*>(_context->pScreen);
        ASSERT_NE(_ports, nullptr);
        ASSERT_NE(_screen, nullptr);
        _context->pFeatureManager->setFeature(Features::kScreenHQ, true);
        _context->pMemory->UpdateFeatureCache();
        // the CPU parks in RAM (DI; JR $) so that the ROM does not write registers behind the test
        Memory* memory = _context->pMemory;
        memory->DirectWriteToZ80Memory(0xC000, 0xF3);
        memory->DirectWriteToZ80Memory(0xC001, 0x18);
        memory->DirectWriteToZ80Memory(0xC002, 0xFE);
        _context->pCore->GetZ80()->pc = 0xC000;
        Nr(0x15, 0x00);
        Nr(0x68, 0x00);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    void Nr(uint8_t reg, uint8_t value)
    {
        Out(0x243B, reg);
        Out(0x253B, value);
    }
    void Frames(unsigned n)
    {
        for (unsigned i = 0; i < n; i++)
            _emulator->RunFrame(true);
    }
    uint8_t* Page(unsigned p) { return _context->pMemory->RAMPageAddress(p); }
    const uint32_t* Framebuffer() const { return reinterpret_cast<const uint32_t*>(_context->pScreen->GetFramebufferDescriptor().memoryBuffer); }
};

TEST_F(NextLayerCaptureMachine_Test, OnByDefaultFromTheConfig)
{
    // [NEXT] LayerCapture defaults to on: the first frames of a machine already leave a complete one (the measured cost is under 5 %)
    EXPECT_TRUE(_screen->LayerCaptureEnabled());
    EXPECT_EQ(_context->config.next_layer_capture, 1);
    Frames(3);
    ASSERT_NE(_screen->LastLayerFrame(), nullptr);
    EXPECT_EQ(_screen->LayerCaptureBytes(), 2 * Frame::Bytes());
}

TEST_F(NextLayerCaptureMachine_Test, OffAllocatesNothingAndKeepsNoFrame)
{
    // switched off the screen holds no set, keeps no frame and counts none: the renderer is called as before the record existed
    _screen->SetLayerCapture(false);
    EXPECT_EQ(_context->config.next_layer_capture, 0) << "a screen made later (a model switch) starts the same way";
    Frames(2);
    const uint64_t counted = _screen->LayerFramesCaptured();
    Frames(2);
    EXPECT_FALSE(_screen->LayerCaptureEnabled());
    EXPECT_EQ(_screen->LastLayerFrame(), nullptr);
    EXPECT_EQ(_screen->LayerCaptureBytes(), 0u);
    EXPECT_EQ(_screen->LayerFramesCaptured(), counted);
}

TEST_F(NextLayerCaptureMachine_Test, PausedMachineAnswersFromTheLastFrame)
{
    _screen->SetLayerCapture(true);
    Frames(3);  // the switch applies at a frame start
    const auto last = _screen->LastLayerFrame();
    ASSERT_NE(last, nullptr);
    EXPECT_GT(_screen->LayerFramesCaptured(), 0u);
    EXPECT_EQ(_screen->LayerCaptureBytes(), 2 * Frame::Bytes());
    // no frame runs: the request still gets the same complete frame
    const auto again = _screen->LastLayerFrame();
    EXPECT_EQ(again, last);
    EXPECT_EQ(again->sequence, _screen->LayerFramesCaptured());
    // switched off, the record goes at the next frame start
    _screen->SetLayerCapture(false);
    Frames(1);
    EXPECT_EQ(_screen->LastLayerFrame(), nullptr);
    EXPECT_EQ(_screen->LayerCaptureBytes(), 0u);
    EXPECT_TRUE(last->Allocated()) << "a reader keeps the set it holds";
}

TEST_F(NextLayerCaptureMachine_Test, TwoSetsSwapAtTheFrameEnd)
{
    // Frame content = the attribute of every cell and the border: one colour per parity. A reader holding frame N must see none
    // of frame N+1 even after the renderer has drawn it
    _screen->SetLayerCapture(true);
    auto paint = [&](uint8_t attr, uint8_t border) {
        std::memset(Page(5) + 0x1800, attr, 768);
        std::memset(Page(5), 0xFF, 0x1800);  // all ink
        _context->emulatorState.pFE = border;
    };
    paint(0x02, 2);
    Frames(3);
    const auto first = _screen->LastLayerFrame();
    ASSERT_NE(first, nullptr);
    const uint16_t redPaper = first->plane[kUla][100 * kWidth + 200].colour;
    const uint16_t redBorder = first->plane[kUla][5 * kWidth + 5].colour;
    paint(0x01, 1);
    Frames(3);
    const auto second = _screen->LastLayerFrame();
    ASSERT_NE(second, nullptr);
    EXPECT_GT(second->sequence, first->sequence);
    EXPECT_NE(second->plane[kUla][100 * kWidth + 200].colour, redPaper);
    EXPECT_NE(second->plane[kUla][5 * kWidth + 5].colour, redBorder);
    // the held frame is still all of frame N
    for (unsigned y = 0; y < kHeight; y += 8)
    {
        EXPECT_EQ(first->plane[kUla][y * kWidth + 200].colour, y >= 32 && y < 224 ? redPaper : redBorder) << y;
        EXPECT_EQ(first->plane[kUla][y * kWidth + 3].colour, redBorder) << y;
    }
}

// 20 frames of the bare renderer (no CPU) while a reader thread polls: bounded by the frame count, not by time; the reader
// is waited for before the frames and after each of them (a reader that never ran proves nothing), and it only stops when
// the last frame is drawn.
TEST_F(NextLayerCaptureMachine_Test, ReaderThreadNeverSeesAHalfDrawnFrame)
{
    _screen->SetLayerCapture(true);
    const uint64_t before = _screen->LayerFramesCaptured();
    _screen->InitFrame();  // the switch applies at a frame start
    _screen->RenderFrameBatch();
    std::atomic<bool> stop{false};
    std::atomic<unsigned> torn{0}, reads{0}, held{0};
    std::thread reader([&]() {
        std::shared_ptr<const Frame> keep;  // every 7th frame is kept for a while, like a slow reader
        while (!stop.load())
        {
            auto frame = _screen->LastLayerFrame();
            if (!frame)
                continue;
            reads++;
            // every border pixel of a frame has the colour of that frame's parity: a torn frame mixes two
            const uint16_t ref = frame->plane[kUla][0].colour;
            for (unsigned y = 0; y < kHeight; y += 3)
                for (unsigned x : {0u, 30u, 300u, 639u})
                    if ((y < 32 || y >= 224 || x < 64) && frame->plane[kUla][y * kWidth + x].colour != ref)
                        torn++;
            if (reads % 7 == 0)
            {
                keep = frame;
                held++;
            }
        }
    });
    // The reader must be running before the frames count (under load it may not get a core for a while, and reads
    // beside no writer prove nothing), and it must read between the frames: the loop below waits for one more read
    // after each frame, bounded by the wait's timeout
    ASSERT_TRUE(TestWait::For([&] { return reads.load() > 0; }));
    for (unsigned i = 0; i < 20; i++)
    {
        const unsigned readsBefore = reads.load();
        _context->emulatorState.pFE = static_cast<uint8_t>(1 + (i & 1));
        _screen->InitFrame();
        _screen->RenderFrameBatch();
        EXPECT_TRUE(TestWait::For([&] { return reads.load() > readsBefore; })) << "the reader did not read after frame " << i;
    }
    stop = true;
    reader.join();
    EXPECT_EQ(torn.load(), 0u) << "a frame mixed the borders of two frames";
    EXPECT_GE(reads.load(), 20u) << "the reader read between the frames";
    EXPECT_EQ(_screen->LayerFramesCaptured(), before + 21u);
}

// The copper changes registers in the middle of a line: the record must change at the same pixel the composed frame does, and a
// re-render from the registers of the frame's end cannot show it (the case that rules out "render again on request")
TEST_F(NextLayerCaptureMachine_Test, CopperChangesAreCapturedPerSegment)
{
    _screen->SetLayerCapture(true);
    std::memset(Page(5) + 0x1800, 0x07, 768);
    std::memset(Page(5), 0xFF, 0x1800);
    // WAIT line 60 hpos 20; MOVE NR #68 <- #80 (the ULA off); WAIT line 120 hpos 30; MOVE NR #68 <- #00 (on again); HALT. The ULA is on
    // at the frame's start and end, so every frame draws the same picture: on, off from (60, 20) to (120, 30), on
    Nr(0x61, 0);
    Nr(0x62, 0);
    for (uint16_t w : {uint16_t(0x8000 | (20 << 9) | 60), uint16_t(0x6880), uint16_t(0x8000 | (30 << 9) | 120), uint16_t(0x6800), uint16_t(0xFFFF)})
    {
        Nr(0x60, static_cast<uint8_t>(w >> 8));
        Nr(0x60, static_cast<uint8_t>(w & 0xFF));
    }
    Nr(0x62, 0xC0);  // run, restarting every frame
    Frames(4);
    const auto frame = _screen->LastLayerFrame();
    ASSERT_NE(frame, nullptr);
    // one run of fallback pixels in raster order, inside lines, bounded by the two writes
    int start = -1, end = -1, runs = 0;
    bool inRun = false;
    for (unsigned i = 0; i < kWidth * kHeight; i++)
    {
        const bool off = frame->ids[i] == kFallback;
        if (off && !inRun)
        {
            runs++;
            start = static_cast<int>(i);
        }
        if (!off && inRun)
            end = static_cast<int>(i);
        inRun = off;
    }
    ASSERT_EQ(runs, 1) << "the ULA is off for one stretch of the frame";
    ASSERT_GT(end, start);
    EXPECT_NE(start % kWidth, 0) << "the first change is inside a line";
    EXPECT_NE(end % kWidth, 0) << "the second change is inside a line";
    EXPECT_LT(start / kWidth, end / kWidth);
    // the line table: the two lines with a copper write are cut into two segments at the write, with the register state of each;
    // every other line is one segment
    const unsigned y1 = static_cast<unsigned>(start) / kWidth, x1 = static_cast<unsigned>(start) % kWidth;
    const unsigned y2 = static_cast<unsigned>(end) / kWidth, x2 = static_cast<unsigned>(end) % kWidth;
    ASSERT_EQ(frame->lineSegments[y1], 2u);
    const Segment& before = frame->segments[frame->lineFirst[y1]];
    const Segment& after = frame->segments[frame->lineFirst[y1] + 1];
    EXPECT_EQ(before.fromX, 0u);
    EXPECT_EQ(before.toX, x1);
    EXPECT_EQ(after.fromX, x1);
    EXPECT_EQ(after.toX, kWidth);
    EXPECT_EQ(before.Reg(0x68), 0x00);
    EXPECT_EQ(after.Reg(0x68), 0x80);
    ASSERT_EQ(frame->lineSegments[y2], 2u);
    EXPECT_EQ(frame->segments[frame->lineFirst[y2]].Reg(0x68), 0x80);
    EXPECT_EQ(frame->segments[frame->lineFirst[y2] + 1].Reg(0x68), 0x00);
    EXPECT_EQ(frame->segments[frame->lineFirst[y2] + 1].fromX, x2);
    EXPECT_EQ(frame->lineSegments[10], 1u);
    EXPECT_EQ(frame->SegmentAt(y1, x1 - 1)->Reg(0x68), 0x00);
    EXPECT_EQ(frame->SegmentAt(y1, x1)->Reg(0x68), 0x80);
    EXPECT_EQ(frame->SegmentAt(y1 + 1, 0)->Reg(0x68), 0x80);
    EXPECT_EQ(frame->SegmentAt(kHeight, 0), nullptr);
    const uint32_t* fb = Framebuffer();
    for (int i = 0; i < static_cast<int>(kWidth * kHeight); i++)
    {
        const unsigned y = static_cast<unsigned>(i) / kWidth, x = static_cast<unsigned>(i) % kWidth;
        const Pixel ula = frame->plane[kUla][i];
        if (i >= start && i < end)
        {
            ASSERT_EQ(frame->ids[i], kFallback) << "y=" << y << " x=" << x;
            ASSERT_FALSE(ula.opaque) << "y=" << y << " x=" << x;
        }
        else
        {
            ASSERT_TRUE(frame->ids[i] == kIdUla || frame->ids[i] == kIdBorder) << "y=" << y << " x=" << x;
            ASSERT_TRUE(ula.opaque);
            ASSERT_EQ(fb[(y * 2) * kWidth + x], NextVideoRenderer::Rgba(ula.colour & 0x1FF)) << "y=" << y << " x=" << x;
        }
    }
    // the composed frame changes at the same pixels
    EXPECT_NE(fb[(start / kWidth) * 2 * kWidth + start % kWidth], fb[(start / kWidth) * 2 * kWidth + start % kWidth - 1])
        << "the composed row changes where the record does";
    // a re-render from the registers of the end of the frame (the ULA on) draws the whole stretch wrong
    NextVideoInputs in;
    in.ram = Page(0);
    in.ramPages = MAX_RAM_PAGES;
    in.regs = &_ports->Board().Video();
    in.sprites = &_ports->Board().Sprites();
    for (unsigned r = 0; r < 256; r++)
        in.nr[r] = _ports->Board().Stored(static_cast<uint8_t>(r));
    in.border = _context->emulatorState.pFE & 7;
    in.portFf = _ports->Board().Video().PortFf();
    const unsigned middle = static_cast<unsigned>((start / kWidth + end / kWidth) / 2);
    ASSERT_GT(middle, static_cast<unsigned>(start / kWidth));
    std::vector<uint32_t> again(kWidth);
    NextVideoRenderer::RenderLine(in, middle, again.data());
    EXPECT_NE(std::memcmp(again.data(), fb + middle * 2 * kWidth, kWidth * sizeof(uint32_t)), 0)
        << "the registers of the frame's end do not explain a line in the middle of the stretch; only the capture does";
}

// endregion

// Where a copper MOVE of the fallback colour first shows on the output, counted against the RTL (zxnext.vhd: fallback_rgb_0, _1a, _1, _2,
// rgb_out_3..6, rgb_out_o: nine CLK_14 registers behind the write; scratch/cosim-copper/LATENCY.md). The WAIT is true at the pixel clock
// thr = hpos * 8 + 12; the ULA pixel k of the grid leaves at its own hc; the changed fallback appears at the second 14 MHz half of grid pixel
// thr + 26, i.e. image pixel 2 * thr + 53
TEST_F(NextLayerCaptureMachine_Test, AFallbackColourMoveShowsAtTheOutputAfterTheRtlPipeline)
{
    constexpr unsigned kHpos = 20, kLine = 60;
    constexpr unsigned kWaitBase = 12, kWaitStep = 8;                     // copper.vhd: thr = (hpos << 3) + 12
    constexpr unsigned kThr = kHpos * kWaitStep + kWaitBase;
    constexpr unsigned kFirstChangedImagePixel = 2 * kThr + 53;           // LATENCY.md section 4
    constexpr uint8_t kUlaOff = 0x80, kFallbackBefore = 0x00, kFallbackAfter = 0xFC;
    Nr(0x68, kUlaOff);       // the ULA is off: every pixel is the fallback colour
    Nr(0x4A, kFallbackBefore);
    Nr(0x61, 0);
    Nr(0x62, 0);
    // every frame starts with the colour before (the register keeps the last frame's value), then the WAIT and the change
    for (uint16_t w : {uint16_t((0x4A << 8) | kFallbackBefore), uint16_t(0x8000 | (kHpos << 9) | kLine), uint16_t((0x4A << 8) | kFallbackAfter),
                       uint16_t(0xFFFF)})
    {
        Nr(0x60, static_cast<uint8_t>(w >> 8));
        Nr(0x60, static_cast<uint8_t>(w & 0xFF));
    }
    Nr(0x62, 0xC0);  // run, restarting every frame
    Frames(4);
    const uint32_t* fb = Framebuffer();
    constexpr unsigned kQuietRow = 40;  // after the first MOVE (copper line 0 is grid row 32) and before the WAIT line 60 (grid row 92)
    const uint32_t before = fb[(kQuietRow * 2) * kWidth];
    int row = -1, x = -1;
    for (unsigned y = kQuietRow; y < kHeight && row < 0; y++)
        for (unsigned i = 1; i < kWidth; i++)
            if (fb[(y * 2) * kWidth + i] != before)
            {
                row = static_cast<int>(y);
                x = static_cast<int>(i);
                break;
            }
    ASSERT_GE(row, 0) << "the fallback colour changed inside a line";
    EXPECT_EQ(static_cast<unsigned>(x), kFirstChangedImagePixel) << "row " << row;
    EXPECT_NE(fb[(static_cast<unsigned>(row + 1) * 2) * kWidth], before) << "the next row is the changed colour from its start";
}

// The ULA enable (NR #68 bit 7) travels the same registers as the fallback colour (ula_en_0, _1a, _1, _2, then the output chain): a MOVE
// that switches the ULA off shows at the same image pixel as a fallback colour change
TEST_F(NextLayerCaptureMachine_Test, AUlaEnableMoveShowsAtTheOutputAfterTheRtlPipeline)
{
    constexpr unsigned kHpos = 20, kLine = 60;
    constexpr unsigned kThr = kHpos * 8 + 12;
    constexpr unsigned kFirstChangedImagePixel = 2 * kThr + 53;
    Nr(0x68, 0x00);
    Nr(0x4A, 0xFC);  // the colour the ULA's absence shows
    Nr(0x61, 0);
    Nr(0x62, 0);
    for (uint16_t w : {uint16_t((0x68 << 8) | 0x00), uint16_t(0x8000 | (kHpos << 9) | kLine), uint16_t((0x68 << 8) | 0x80), uint16_t(0xFFFF)})
    {
        Nr(0x60, static_cast<uint8_t>(w >> 8));
        Nr(0x60, static_cast<uint8_t>(w & 0xFF));
    }
    Nr(0x62, 0xC0);
    Frames(4);
    const uint32_t* fb = Framebuffer();
    constexpr unsigned kQuietRow = 40;
    int row = -1, x = -1;
    for (unsigned y = kQuietRow + 1; y < kHeight && row < 0; y++)
        for (unsigned i = 0; i < kWidth; i++)
            if (fb[(y * 2) * kWidth + i] != fb[(kQuietRow * 2) * kWidth + i])
            {
                row = static_cast<int>(y);
                x = static_cast<int>(i);
                break;
            }
    ASSERT_GE(row, 0) << "the ULA's absence shows inside a line";
    EXPECT_EQ(static_cast<unsigned>(x), kFirstChangedImagePixel) << "row " << row;
}

// The latency table, as measured on the simulated RTL (POC 028): the chains, the pixel groups and the cells; unmeasured registers give 0
TEST(ScreenNextLatency_Test, TheLatencyFollowsTheChangedBitsOfTheRegister)
{
    auto of = [](uint8_t reg, uint8_t prev, uint8_t value, uint8_t select = 0) { return ScreenNext::PictureLatencyOf(reg, prev, value, select); };
    EXPECT_EQ(of(0x4A, 0x00, 0xFF).clocks, 22u);
    EXPECT_EQ(of(0x14, 0x00, 0xFF).clocks, 22u);
    EXPECT_EQ(of(0x41, 0x00, 0xFF, 0).clocks, 18u);  // palette values: the lookup of the palette the write select names (POC 028)
    EXPECT_EQ(of(0x44, 0x00, 0xFF, 4).clocks, 18u);  // the ULA's second palette
    EXPECT_EQ(of(0x41, 0x00, 0xFF, 1).clocks, 18u);  // Layer 2
    EXPECT_EQ(of(0x41, 0x00, 0xFF, 2).clocks, 16u);  // sprites
    EXPECT_EQ(of(0x41, 0x00, 0xFF, 3).clocks, 17u);  // tilemap
    EXPECT_EQ(of(0x19, 0x00, 0x80).clocks, 22u);     // the sprite clip: per pixel
    EXPECT_EQ(of(0x19, 0x00, 0x80).quantum, 4u);
    EXPECT_EQ(of(0x6B, 0x00, 0x02).clocks, 53u);     // the tilemap's 512-tile mode: latched with the cell
    EXPECT_EQ(of(0x6B, 0x00, 0x40).clocks, 24u);     // 80 columns
    EXPECT_EQ(of(0x4C, 0x0F, 0x00).clocks, 20u);     // the tilemap's transparent index
    EXPECT_EQ(of(0x68, 0x00, 0x80).clocks, 22u);    // the ULA enable
    EXPECT_EQ(of(0x68, 0x00, 0x04).clocks, 70u);    // the fine scroll: a cell
    EXPECT_EQ(of(0x68, 0x00, 0x04).quantum, 32u);
    EXPECT_EQ(of(0x15, 0x00, 0x01).clocks, 22u);    // sprites on
    EXPECT_EQ(of(0x15, 0x00, 0x08).clocks, 22u);    // the layer order (measured: the same registers as the enables)
    EXPECT_EQ(of(0x15, 0x00, 0x02).clocks, 0u);     // sprites over the border: a sprite-stage input
    EXPECT_EQ(of(0x43, 0x00, 0x02).clocks, 22u);    // the ULA's second palette (bit 1)
    EXPECT_EQ(of(0x43, 0x00, 0x10).clocks, 0u);     // the palette write select (bits 6:4) is no picture input
    EXPECT_EQ(of(0x43, 0x00, 0x01).clocks, 20u);    // ULANext
    EXPECT_EQ(of(0x6B, 0x00, 0x80).clocks, 22u);    // the tilemap enable
    EXPECT_EQ(of(0x1A, 0x00, 0x80).clocks, 22u);    // the ULA clip: per pixel
    EXPECT_EQ(of(0x1A, 0x00, 0x80).quantum, 4u);
    EXPECT_EQ(of(0x16, 0x00, 0x10).clocks, 26u);    // Layer 2 scroll: the SRAM read adds a pixel
    EXPECT_EQ(of(0x16, 0x00, 0x10).quantum, 4u);
    EXPECT_EQ(of(0x18, 0x00, 0x80).clocks, 26u);
    EXPECT_EQ(of(0x69, 0x00, 0x80).clocks, 26u);    // Layer 2 enable
    EXPECT_EQ(of(0x26, 0x00, 0x08).clocks, 56u);    // the ULA scroll: per 8-pixel cell
    EXPECT_EQ(of(0x26, 0x00, 0x08).quantum, 32u);
    EXPECT_EQ(of(0x30, 0x00, 0x01).clocks, 53u);    // the tilemap scroll
    EXPECT_EQ(of(0x32, 0x00, 0x01).clocks, 22u);    // the LoRes scroll: per pixel
    EXPECT_EQ(of(0x32, 0x00, 0x01).quantum, 4u);
    EXPECT_EQ(of(0x6A, 0x00, 0x20).clocks, 23u);    // Radastan
    EXPECT_EQ(of(0x6A, 0x00, 0x05).clocks, 23u);    // the LoRes palette offset
    EXPECT_EQ(of(0x70, 0x00, 0x10).clocks, 27u);    // the Layer 2 resolution
    EXPECT_EQ(of(0x70, 0x00, 0x01).clocks, 22u);    // its palette offset
    EXPECT_EQ(of(0x4B, 0x00, 0x21).clocks, 0u);     // the sprite transparent index: a fill-stage input, not a pixel one
}

// The ULA reads its scroll where an 8-pixel cell is fetched: a MOVE of NR #26 lands 56 clocks after the WAIT and takes effect at the next
// cell boundary (32-clock cells from the grid's x = 0; POC 028, measured on the simulated RTL)
TEST_F(NextLayerCaptureMachine_Test, AUlaScrollMoveTakesEffectAtTheNextCell)
{
    constexpr unsigned kHpos = 20, kLine = 60;
    constexpr unsigned kThr = kHpos * 8 + 12;
    constexpr unsigned kDelta = 4 * kThr + 84;          // the write's clock from the grid's x = 0 (the fallback test's 2 * thr + 53 is (delta + 22) / 2)
    constexpr unsigned kCell = 32, kLatency = 56;
    constexpr unsigned kExpectedImagePixel = ((kDelta + kLatency + kCell - 1) / kCell * kCell) / 2;
    uint8_t* screen = Page(5);
    std::memset(screen, 0xFF, 0x1800);                  // every pixel ink
    for (unsigned row = 0; row < 24; row++)
        for (unsigned col = 0; col < 32; col++)
            screen[0x1800 + row * 32 + col] = static_cast<uint8_t>(col & 7);  // the ink changes from cell to cell
    Nr(0x26, 0x00);
    Nr(0x61, 0);
    Nr(0x62, 0);
    for (uint16_t w : {uint16_t((0x26 << 8) | 0x00), uint16_t(0x8000 | (kHpos << 9) | kLine), uint16_t((0x26 << 8) | 0x08), uint16_t(0xFFFF)})
    {
        Nr(0x60, static_cast<uint8_t>(w >> 8));
        Nr(0x60, static_cast<uint8_t>(w & 0xFF));
    }
    Nr(0x62, 0xC0);
    Frames(4);
    const uint32_t* fb = Framebuffer();
    constexpr unsigned kQuietRow = 40;
    int row = -1, x = -1;
    for (unsigned y = kQuietRow + 1; y < kHeight && row < 0; y++)
        for (unsigned i = 0; i < kWidth; i++)
            if (fb[(y * 2) * kWidth + i] != fb[(kQuietRow * 2) * kWidth + i])
            {
                row = static_cast<int>(y);
                x = static_cast<int>(i);
                break;
            }
    ASSERT_GE(row, 0) << "the scroll changed the picture inside a line";
    EXPECT_EQ(static_cast<unsigned>(x), kExpectedImagePixel) << "row " << row;
}

// The sprite engine fills paper row R during row R - 1 (POC 028): a copper MOVE of NR #4B (the transparent index, read where a sprite's
// pixel is drawn) in the first part of row N reaches row N + 1, a MOVE later in row N reaches row N + 2
class NextSpriteFill_Test : public NextLayerCaptureMachine_Test
{
protected:
    static constexpr unsigned kSpriteTop = 40, kSpriteX = 200, kLine = 45;  // paper rows 40-55; the copper's line; a framebuffer column
    static constexpr unsigned kLastX = 330;  // the framebuffer column of the last sprite when it stands apart (x = 160)
    uint32_t RowPixel(unsigned paperRow, unsigned column = kSpriteX) const { return Framebuffer()[((32 + paperRow) * 2) * kWidth + column]; }
    void Run(unsigned hpos, unsigned sprites, bool lastApart = false)
    {
        // one pattern of solid #21, `sprites` sprites on top of each other above the ULA at paper row 40, x = 64: the engine's fill of a
        // row takes a clock a sprite and 17 more for each drawn one, so many sprites make it end late in the line
        Out(0x303B, 0);
        for (unsigned i = 0; i < 256; i++)
            Out(0x5B, 0x21);
        Out(0x303B, 0);
        for (unsigned s = 0; s < sprites; s++)
            for (uint8_t b : {uint8_t(lastApart && s + 1 == sprites ? 160 : 96), uint8_t(32 + kSpriteTop), uint8_t(0), uint8_t(0x80)})
                Out(0x57, b);
        Nr(0x15, 0x01);
        Nr(0x4B, 0xE3);
        std::memset(Page(5), 0, 0x1800);                  // the ULA paper everywhere
        Nr(0x61, 0);
        Nr(0x62, 0);
        for (uint16_t w : {uint16_t((0x4B << 8) | 0xE3), uint16_t(0x8000 | (hpos << 9) | kLine), uint16_t((0x4B << 8) | 0x21), uint16_t(0xFFFF)})
        {
            Nr(0x60, static_cast<uint8_t>(w >> 8));
            Nr(0x60, static_cast<uint8_t>(w & 0xFF));
        }
        Nr(0x62, 0xC0);
        Frames(4);
    }
};

// The MOVE (the sprites' transparent index becomes their colour) lands inside the fill of row N + 1: sprites.vhd compares each pixel with
// the index as it draws it, so the sprites the engine drew before the MOVE stay on the row, the last one, drawn after it, is not there
TEST_F(NextSpriteFill_Test, AMoveInsideAFillReachesTheSpritesNotYetDrawn)
{
    Run(1, 90, true);  // 90 drawn sprites: the fill of row N + 1 ends at about hc 384 of row N
    const uint32_t sprite = RowPixel(kSpriteTop + 2);
    ASSERT_NE(sprite, RowPixel(kSpriteTop - 4)) << "the sprite is on the picture";
    ASSERT_EQ(RowPixel(kSpriteTop + 2, kLastX), sprite) << "so is the last one";
    EXPECT_EQ(RowPixel(kLine), sprite) << "row N: its fill was before the MOVE";
    EXPECT_EQ(RowPixel(kLine, kLastX), sprite);
    EXPECT_EQ(RowPixel(kLine + 1), sprite) << "row N + 1: the first sprites were drawn before the MOVE";
    EXPECT_NE(RowPixel(kLine + 1, kLastX), sprite) << "row N + 1: the last one after it";
    EXPECT_NE(RowPixel(kLine + 2), sprite) << "row N + 2: filled after it";
}

TEST_F(NextSpriteFill_Test, AMoveLateInTheLineReachesTheRowAfter)
{
    Run(24, 1);  // one sprite: the fill of a row is done before the line starts
    const uint32_t sprite = RowPixel(kSpriteTop + 2);
    ASSERT_NE(sprite, RowPixel(kSpriteTop - 4));
    EXPECT_EQ(RowPixel(kLine + 1), sprite) << "row N + 1 was filled before the MOVE";
    EXPECT_NE(RowPixel(kLine + 2), sprite) << "row N + 2: filled after it";
}
