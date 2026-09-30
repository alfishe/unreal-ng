// Video debug translation reports (PLAN #42 phase 2, devicestatevideo.cpp):
// the one StateNode tree per question that WebAPI, MCP, CLI, Lua and Python all
// render - the field names are the interface contract (design G6).

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/screen.h"

namespace
{
const StateNode* Member(const StateNode& node, const std::string& key)
{
    for (const auto& m : node.members)
        if (m.first == key)
            return &m.second;
    return nullptr;
}

class DeviceStateVideo_Test : public ::testing::Test
{
protected:
    std::unique_ptr<EmulatorContext> _context;
    std::unique_ptr<Core> _core;

    void Build(MEM_MODEL model, uint32_t ramKB, uint32_t frame, int ff77 = -1)
    {
        _context = std::make_unique<EmulatorContext>(LoggerLevel::LogError);
        _context->config.mem_model = model;
        _context->config.ramsize = ramKB;
        _context->config.frame = frame;
        _context->config.t_line = 224;
        _core = std::make_unique<Core>(_context.get());
        ASSERT_TRUE(_core->Init());
        if (ff77 >= 0)
        {
            _context->emulatorState.pFF77 = static_cast<uint8_t>((ff77 & 0x07) | 0x20);
            _context->emulatorState.aFF77 = 0x0100;
        }
        _context->pScreen->InitRaster();
    }
};
} // namespace

TEST_F(DeviceStateVideo_Test, BeamKeepsItsFieldsAndNamesTheLayerPixel)
{
    Build(MM_SPECTRUM48, 48, 69888);
    // Line 100, T 50: 48K paper pixel (52, 28) (design §3.1 worked example)
    _context->pCore->GetZ80()->t = 100 * 224 + 50;
    const StateNode beam = DeviceState::VideoBeam(_context.get());

    for (const char* key : {"model", "video_mode", "tstate", "tstate_in_frame", "line", "dot_in_line", "beam_x", "zone",
                            "vertical_zone", "horizontal_zone", "in_visible_area", "in_paper", "paper", "layers",
                            "frame_timing", "raster"})
        EXPECT_NE(Member(beam, key), nullptr) << key;
    EXPECT_EQ(Member(beam, "line")->i, 100);
    ASSERT_EQ(Member(beam, "layers")->items.size(), 1u);
    const StateNode& layer = Member(beam, "layers")->items[0];
    EXPECT_EQ(Member(layer, "id")->s, "zx");
    EXPECT_EQ(Member(layer, "x")->i, 52);
    EXPECT_EQ(Member(layer, "x_end")->i, 53);
    EXPECT_EQ(Member(layer, "y")->i, 28);

    _context->pCore->GetZ80()->t = 10;  // vsync: no layer
    EXPECT_TRUE(Member(DeviceState::VideoBeam(_context.get()), "layers")->items.empty());
}

TEST_F(DeviceStateVideo_Test, LayoutDescribesTheAtmWindow)
{
    Build(MM_ATM710, 1024, 69888, FF77_16);
    const StateNode layout = DeviceState::VideoLayout(_context.get());
    EXPECT_TRUE(Member(layout, "mapped")->b);
    EXPECT_EQ(Member(layout, "family")->s, "atm");
    ASSERT_EQ(Member(layout, "layers")->items.size(), 1u);
    const StateNode& layer = Member(layout, "layers")->items[0];
    EXPECT_EQ(Member(layer, "id")->s, "atm16");
    EXPECT_EQ(Member(*Member(layer, "surface"), "width")->i, 320);
    EXPECT_EQ(Member(*Member(layer, "window"), "first_t")->i, 8);
    EXPECT_EQ(Member(*Member(layer, "window"), "t_count")->i, 160);
    EXPECT_NE(Member(layout, "framebuffer"), nullptr);
}

TEST_F(DeviceStateVideo_Test, PixelListsSourcesWithZ80Addresses)
{
    Build(MM_SPECTRUM48, 48, 69888);
    const StateNode pixel = DeviceState::VideoPixel(_context.get(), 0, 0, 0);
    EXPECT_TRUE(Member(pixel, "available")->b);
    EXPECT_EQ(Member(pixel, "layer")->s, "zx");
    const StateNode& sources = *Member(pixel, "sources");
    ASSERT_EQ(sources.items.size(), 2u);
    EXPECT_EQ(Member(sources.items[0], "space")->s, "ram");
    EXPECT_EQ(Member(sources.items[0], "role")->s, "pixel_bits");
    EXPECT_EQ(Member(sources.items[0], "bit_mask")->s, "0x80");
    ASSERT_EQ(Member(sources.items[0], "z80")->items.size(), 1u);
    EXPECT_EQ(Member(sources.items[0], "z80")->items[0].s, "0x4000");
    EXPECT_EQ(Member(sources.items[1], "role")->s, "attribute");
    EXPECT_EQ(Member(sources.items[1], "z80")->items[0].s, "0x5800");
    EXPECT_EQ(Member(pixel, "rgb")->s.size(), 7u) << "#RRGGBB";
    EXPECT_EQ(Member(pixel, "values_at")->s, "current");

    EXPECT_FALSE(Member(DeviceState::VideoPixel(_context.get(), 0, 256, 0), "available")->b) << "outside the surface";
}

TEST_F(DeviceStateVideo_Test, PixelAtTheBeamReportsTheBorder)
{
    Build(MM_SPECTRUM48, 48, 69888);
    _context->emulatorState.pFE = 0x02;
    // Line 30 (top border, visible), T 50
    const StateNode border = DeviceState::VideoPixelAtBeam(_context.get(), 30 * 224 + 50);
    ASSERT_TRUE(Member(border, "available")->b);
    EXPECT_TRUE(Member(border, "border")->b);
    EXPECT_EQ(Member(Member(border, "sources")->items[0], "space")->s, "register");
}

TEST_F(DeviceStateVideo_Test, AddressReportsTheAreasAByteFeeds)
{
    Build(MM_SPECTRUM48, 48, 69888);
    const StateNode attr = DeviceState::VideoAddressZ80(_context.get(), 0x5800 + 33);
    EXPECT_TRUE(Member(attr, "feeds_picture")->b);
    const StateNode& area = Member(attr, "areas")->items.at(0);
    EXPECT_EQ(Member(area, "x")->i, 8);
    EXPECT_EQ(Member(area, "y")->i, 8);
    EXPECT_EQ(Member(area, "width")->i, 8);
    EXPECT_EQ(Member(area, "height")->i, 8);

    EXPECT_FALSE(Member(DeviceState::VideoAddressZ80(_context.get(), 0x0000), "feeds_picture")->b) << "ROM";
    EXPECT_TRUE(Member(DeviceState::VideoAddress(_context.get(), 5, 0x0000), "feeds_picture")->b);
    EXPECT_FALSE(Member(DeviceState::VideoAddress(_context.get(), 5, 0x4000), "available")->b) << "offset past the page";
}

TEST_F(DeviceStateVideo_Test, TextGridInTextModesOnly)
{
    Build(MM_ATM710, 1024, 69888, FF77_TX);
    // ATMTX cell (0, 0): char code at video page 5, offset 0x1C0
    _context->pMemory->RAMPageAddress(5)[0x1C0] = 'H';
    const StateNode text = DeviceState::VideoText(_context.get());
    ASSERT_TRUE(Member(text, "available")->b);
    EXPECT_EQ(Member(text, "columns")->i, 80);
    EXPECT_EQ(Member(text, "rows")->i, 25);
    const StateNode& first = Member(text, "lines")->items.at(0);
    EXPECT_EQ(Member(first, "text")->s.substr(0, 1), "H");
    EXPECT_EQ(Member(first, "codes")->s.substr(0, 2), "48");

    Build(MM_SPECTRUM48, 48, 69888);
    EXPECT_FALSE(Member(DeviceState::VideoText(_context.get()), "available")->b) << "bitmap mode";
}
