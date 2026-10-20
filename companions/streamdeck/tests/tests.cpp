// Hardware-free unit tests: model table, golden report bytes, input parsing, image ops, LCD mapping, theme upload.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#include "sdb/device.h"
#include "sdb/draw.h"
#include "sdb/icons.h"
#include "sdb/protocol.h"
#include "sdb/scenes.h"
#include "sdb/theme.h"

using namespace sdb;

#include "check.h"

namespace
{
const Model& ModelOf(std::uint16_t pid)
{
    return *FindModel(kElgatoVid, pid);
}

struct Mock final : Transport
{
    std::vector<std::vector<std::uint8_t>> writes, features;
    std::vector<std::vector<std::uint8_t>> inputs;
    std::vector<std::uint8_t> featureReply;
    int Write(std::span<const std::uint8_t> r) override
    {
        writes.emplace_back(r.begin(), r.end());
        return static_cast<int>(r.size());
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        features.emplace_back(r.begin(), r.end());
        return static_cast<int>(r.size());
    }
    int GetFeature(std::span<std::uint8_t> b) override
    {
        std::copy_n(featureReply.begin(), std::min(featureReply.size(), b.size()), b.begin());
        return static_cast<int>(b.size());
    }
    int Read(std::span<std::uint8_t> b, int) override
    {
        if (inputs.empty())
            return 0;
        auto in = inputs.front();
        inputs.erase(inputs.begin());
        std::copy_n(in.begin(), std::min(in.size(), b.size()), b.begin());
        return static_cast<int>(std::min(in.size(), b.size()));
    }
};
std::pair<Device, Mock*> MockDevice(std::uint16_t pid)
{
    auto t = std::make_unique<Mock>();
    Mock* raw = t.get();
    return {Device(ModelOf(pid), std::move(t), "TEST"), raw};
}

void TestModels()
{
    std::puts("models");
    CHECK_EQ(AllModels().size(), 16u);
    for (auto& m : AllModels())
    {
        CHECK(FindModel(kElgatoVid, m.pid) == &m);
        CHECK_EQ(m.rows * m.cols, m.keys);
    }
    CHECK(FindModel(0x1234, 0x0080) == nullptr);
    CHECK(FindModel(kElgatoVid, 0xFFFF) == nullptr);
    const Model& xl = ModelOf(0x00C6);
    CHECK_EQ(xl.keys, 36);
    CHECK_EQ(xl.rows, 4);
    CHECK_EQ(xl.cols, 9);
    CHECK_EQ(xl.dials, 6);
    CHECK_EQ(xl.lcdLogicalW, 1200);
    CHECK_EQ(xl.lcd.w, 100);
    CHECK_EQ(xl.lcd.h, 1200);
    CHECK_EQ(ModelOf(0x009A).touchPoints, 2);
    CHECK(!ModelOf(0x0086).HasKeyScreens());
    CHECK_EQ(ModelOf(0x006C).key.w, 96);
    CHECK_EQ(xl.key.w, 112);  // docs + Get Unit Information (crate said 120)
    CHECK_EQ(xl.screenLogicalW, 1280);
    CHECK_EQ(xl.screen.w, 800);  // rotated 90 CCW on the wire
    const auto l = GetPanelLayout(xl);
    CHECK_EQ(l.colX.size(), 9u);
    CHECK_EQ(l.rowY.size(), 4u);
    CHECK_EQ(l.colX[8] + 112 + 9, 1280);
    CHECK_EQ(l.rowY[3] + 112 + 100 + 100 + 26, 800);
    CHECK_EQ(l.stripX + l.stripW + 40, 1280);
    for (auto& mm : AllModels())
    {  // every documented layout adds up to its panel
        const auto p = GetPanelLayout(mm);
        if (p.colX.empty())
            continue;
        CHECK_EQ(static_cast<int>(p.colX.size()), mm.cols);
        CHECK_EQ(static_cast<int>(p.rowY.size()), mm.rows);
        CHECK(p.colX.back() + p.key <= p.w);
        CHECK(p.rowY.back() + p.key <= p.h);
        if (mm.HasFullScreen())
        {
            CHECK_EQ(p.w, mm.screenLogicalW);
            CHECK_EQ(p.h, mm.screenLogicalH);
        }
    }
    // keyIndex: valid cells map row-major, anything outside the grid is rejected
    CHECK_EQ(*KeyIndex(xl, 3, 8), 35);
    CHECK_EQ(*KeyIndex(xl, 0, 0), 0);
    CHECK_EQ(*KeyIndex(xl, 1, 2), 11);
    CHECK(!KeyIndex(xl, 4, 0));
    CHECK(!KeyIndex(xl, 0, 9));
    CHECK(!KeyIndex(xl, -1, 0));
    CHECK(!KeyIndex(xl, 0, -1));
}

void TestProtocol()
{
    std::puts("protocol");
    using Rep = proto::Report;
    CHECK(proto::Brightness(ModelOf(0x00C6), 80) == [] {
        Rep r{0x03, 0x08, 80};
        r.resize(32);
        return r;
    }());
    CHECK_EQ(proto::Brightness(ModelOf(0x00C6), 250)[2], 100);  // clamped
    CHECK(proto::Brightness(ModelOf(0x0063), 40) == [] {
        Rep r{0x05, 0x55, 0xAA, 0xD1, 0x01, 40};
        r.resize(17);
        return r;
    }());
    CHECK(proto::Reset(ModelOf(0x006C)) == [] {
        Rep r{0x03, 0x02};
        r.resize(32);
        return r;
    }());
    CHECK(proto::Reset(ModelOf(0x0063)) == [] {
        Rep r{0x0B, 0x63};
        r.resize(17);
        return r;
    }());
    CHECK(proto::TouchLed(ModelOf(0x009A), 1, 9, 8, 7) == Rep({0x03, 0x06, 9, 9, 8, 7}));

    // V2 key image: 2500 bytes -> 3 reports of 1024, payload 1016
    std::vector<std::uint8_t> img(2500, 0xAB);
    auto r = proto::KeyImage(ModelOf(0x006C), 5, img);
    CHECK_EQ(r.size(), 3u);
    for (auto& x : r)
        CHECK_EQ(x.size(), 1024u);
    CHECK(std::equal(r[0].begin(), r[0].begin() + 8,
                     Rep({0x02, 0x07, 5, 0, 0xF8, 0x03, 0, 0}).begin()));  // 1016 = 0x03F8
    CHECK(std::equal(r[2].begin(), r[2].begin() + 8,
                     Rep({0x02, 0x07, 5, 1, static_cast<std::uint8_t>(2500 - 2032), 0x01, 2, 0}).begin()));
    CHECK_EQ(r[1][8], 0xAB);
    CHECK_EQ(r[2][8 + (2500 - 2032)], 0);  // zero padded

    // V1 Mini: header 16, page 0-based, key+1
    auto mini = proto::KeyImage(ModelOf(0x0063), 2, std::vector<std::uint8_t>(1500, 1));
    CHECK_EQ(mini.size(), 2u);
    CHECK(std::equal(mini[0].begin(), mini[0].begin() + 6, Rep({0x02, 0x01, 0, 0, 0, 3}).begin()));
    CHECK(std::equal(mini[1].begin(), mini[1].begin() + 6, Rep({0x02, 0x01, 1, 0, 1, 3}).begin()));

    // Original v1: exactly two 8191 reports, page+1, mirrored key (0 -> 4 -> +1 = 5)
    auto orig = proto::KeyImage(ModelOf(0x0060), 0, std::vector<std::uint8_t>(15606, 2));
    CHECK_EQ(orig.size(), 2u);
    CHECK_EQ(orig[0].size(), 8191u);
    CHECK(std::equal(orig[0].begin(), orig[0].begin() + 6, Rep({0x02, 0x01, 1, 0, 0, 5}).begin()));
    CHECK(std::equal(orig[1].begin(), orig[1].begin() + 6, Rep({0x02, 0x01, 2, 0, 1, 5}).begin()));
    CHECK_EQ(proto::FlipKeyOriginal(ModelOf(0x0060), 7), 7);  // middle column stays
    CHECK_EQ(proto::FlipKeyOriginal(ModelOf(0x0060), 5), 9);

    // Neo LCD fill header
    auto neo = proto::LcdFill(ModelOf(0x009A), std::vector<std::uint8_t>(10, 9));
    CHECK(std::equal(neo[0].begin(), neo[0].begin() + 8, Rep({0x02, 0x0B, 0, 1, 10, 0, 0, 0}).begin()));

    // Plus region header (x=200,y=0,w=200,h=100) and XL full fill = region(0,0,100,1200)
    auto reg = proto::LcdRegion(ModelOf(0x0084), 200, 0, 200, 100, std::vector<std::uint8_t>(20, 1));
    CHECK(std::equal(reg[0].begin(), reg[0].begin() + 16,
                     Rep({0x02, 0x0C, 200, 0, 0, 0, 200, 0, 100, 0, 1, 0, 0, 20, 0, 0}).begin()));
    auto fill = proto::LcdFill(ModelOf(0x00C6), std::vector<std::uint8_t>(5, 1));
    CHECK(std::equal(fill[0].begin(), fill[0].begin() + 10,
                     Rep({0x02, 0x0C, 0, 0, 0, 0, 100, 0, 0xB0, 0x04}).begin()));  // 1200 = 0x04B0

    CHECK(proto::FillKeyColor(ModelOf(0x00C6), 5, 1, 2, 3) == [] {
        Rep r{0x03, 0x06, 5, 1, 2, 3};
        r.resize(32);
        return r;
    }());
    CHECK(proto::FillScreenColor(ModelOf(0x006C), 9, 8, 7) == [] {
        Rep r{0x03, 0x05, 9, 8, 7};
        r.resize(32);
        return r;
    }());
    CHECK(proto::SleepAfter(ModelOf(0x00C6), 300) == [] {
        Rep r{0x03, 0x0D, 0x2C, 0x01, 0, 0};
        r.resize(32);
        return r;
    }());
    CHECK(proto::SleepAfter(ModelOf(0x0063), 60) == [] {
        Rep r{0x0B, 0xA2, 60, 0, 0, 0};
        r.resize(32);
        return r;
    }());
    auto fs = proto::FullScreen(ModelOf(0x00C6), std::vector<std::uint8_t>(2000, 7));
    CHECK(fs.size() == 2 && fs[0][1] == 0x08 && fs[1][3] == 1);
    CHECK_EQ(proto::WindowImage(ModelOf(0x0084), std::vector<std::uint8_t>(10, 1))[0][1], 0x0B);
    CHECK_EQ(proto::FirmwareRead(ModelOf(0x0090)).id, 0xA1);  // Mini family: docs AP2 = 0xA1
    CHECK_EQ(proto::SleepRead(ModelOf(0x0063)).id, 0xA3);
    CHECK_EQ(proto::SerialRead(ModelOf(0x00C6)).id, 0x06);
    CHECK_EQ(proto::SerialRead(ModelOf(0x0063)).length, 17u);
    CHECK_EQ(proto::FirmwareRead(ModelOf(0x00B8)).id, 0xA1);
    CHECK_EQ(proto::FirmwareRead(ModelOf(0x00C6)).skip, 6u);
    CHECK_EQ(proto::InputReportLength(ModelOf(0x00C6)), 42u);  // max(6+36, 5+6)
    CHECK_EQ(proto::InputReportLength(ModelOf(0x0084)), 14u);  // max(6+8, 5+4)
    CHECK_EQ(proto::InputReportLength(ModelOf(0x006C)), 36u);  // 4+32
    CHECK_EQ(proto::InputReportLength(ModelOf(0x0063)), 7u);   // 1+6
    bool threw = false;
    try
    {
        (void)proto::KeyImage(ModelOf(0x006C), 32, img);
    }
    catch (const std::out_of_range&)
    {
        threw = true;
    }
    CHECK(threw);
}

void TestInput()
{
    std::puts("input");
    using B = std::vector<std::uint8_t>;
    {  // Plus XL
        InputParser p(ModelOf(0x00C6));
        B keys(42, 0);
        keys[0] = 1;
        keys[1] = 0x00;
        keys[4 + 3] = 1;
        auto e = p.Feed(keys);
        CHECK_EQ(e.size(), 1u);
        CHECK(e[0].type == EventType::KeyDown);
        CHECK_EQ(e[0].index, 3);
        CHECK(p.Feed(keys).empty());  // no change -> no edge
        keys[4 + 3] = 0;
        keys[4 + 35] = 1;
        e = p.Feed(keys);
        CHECK_EQ(e.size(), 2u);
        CHECK(e[0].type == EventType::KeyUp);
        CHECK(e[1].type == EventType::KeyDown);
        CHECK_EQ(e[1].index, 35);

        B turn(42, 0);
        turn[0] = 1;
        turn[1] = 3;
        turn[4] = 1;
        turn[5 + 2] = 0xFE;
        turn[5 + 5] = 3;
        e = p.Feed(turn);
        CHECK_EQ(e.size(), 2u);
        CHECK(e[0].type == EventType::DialTurn);
        CHECK_EQ(e[0].index, 2);
        CHECK_EQ(e[0].delta, -2);
        CHECK_EQ(e[1].delta, 3);

        B press(42, 0);
        press[0] = 1;
        press[1] = 3;
        press[4] = 0;
        press[5 + 1] = 1;
        e = p.Feed(press);
        CHECK_EQ(e.size(), 1u);
        CHECK(e[0].type == EventType::DialDown);
        CHECK_EQ(e[0].index, 1);
        press[5 + 1] = 0;
        e = p.Feed(press);
        CHECK(e.size() == 1 && e[0].type == EventType::DialUp);

        B tap(42, 0);
        tap[0] = 1;
        tap[1] = 2;
        tap[4] = 1;
        tap[6] = 0xB0;
        tap[7] = 0x04;
        tap[8] = 50;
        e = p.Feed(tap);
        CHECK(e.size() == 1 && e[0].type == EventType::LcdTap);
        CHECK_EQ(e[0].x, 1200);
        CHECK_EQ(e[0].y, 50);
        B sw(42, 0);
        sw[0] = 1;
        sw[1] = 2;
        sw[4] = 3;
        sw[6] = 10;
        sw[8] = 20;
        sw[10] = 30;
        sw[12] = 40;
        e = p.Feed(sw);
        CHECK(e.size() == 1 && e[0].type == EventType::LcdSwipe);
        CHECK_EQ(e[0].x2, 30);
        CHECK_EQ(e[0].y2, 40);
        CHECK(p.Feed(B(42, 0)).empty());  // report id 0 = no data
    }
    {  // XL v1/v2 style
        InputParser p(ModelOf(0x006C));
        B k(36, 0);
        k[0] = 1;
        k[4 + 31] = 1;
        auto e = p.Feed(k);
        CHECK(e.size() == 1 && e[0].type == EventType::KeyDown && e[0].index == 31);
    }
    {  // Neo touch points are pseudo-keys after the 8 keys
        InputParser p(ModelOf(0x009A));
        B k(14, 0);
        k[0] = 1;
        k[4 + 9] = 1;
        auto e = p.Feed(k);
        CHECK(e.size() == 1 && e[0].type == EventType::TouchPointDown && e[0].index == 1);
    }
    {  // Original v1 mirrored columns: physical report byte for key 0 is flip(0)+1 = 5
        InputParser p(ModelOf(0x0060));
        B k(16, 0);
        k[0] = 1;
        k[1 + 4] = 1;
        auto e = p.Feed(k);
        CHECK(e.size() == 1 && e[0].index == 0);
    }
    {  // Mini
        InputParser p(ModelOf(0x0063));
        B k(7, 0);
        k[0] = 1;
        k[1 + 2] = 1;
        auto e = p.Feed(k);
        CHECK(e.size() == 1 && e[0].index == 2);
    }
}

void TestImage()
{
    std::puts("image");
    Rgb im(3, 2);
    for (int i = 0; i < 6; ++i)
    {
        im.px[i * 3] = static_cast<std::uint8_t>(i);
    }  // red channel = index
    Rgb r90 = Rotate(im, Rotation::R90);  // clockwise: 2x3
    CHECK_EQ(r90.w, 2);
    CHECK_EQ(r90.h, 3);
    CHECK_EQ(r90.At(1, 0)[0], 0);  // top-left of src goes to top-right
    CHECK_EQ(r90.At(0, 0)[0], 3);
    Rgb r270 = Rotate(im, Rotation::R270);  // counter-clockwise
    CHECK_EQ(r270.At(0, 0)[0], 2);          // top-right of src goes to top-left
    CHECK_EQ(r270.At(1, 2)[0], 3);          // bottom-left goes to bottom-right
    CHECK_EQ(Rotate(Rotate(im, Rotation::R90), Rotation::R270).px == im.px, true);
    Rgb mx = MirrorImage(im, Mirror::X);
    CHECK_EQ(mx.At(0, 0)[0], 2);
    CHECK_EQ(MirrorImage(im, Mirror::Both).At(0, 0)[0], 5);

    Rgb big(144, 144, 200, 100, 50);
    Rgb small = Resize(big, 96, 96);
    CHECK_EQ(small.w, 96);
    CHECK_EQ(small.At(10, 10)[0], 200);
    CHECK_EQ(small.At(95, 95)[2], 50);  // flat colour preserved
    Rgb up = Resize(Rgb(2, 2, 10, 20, 30), 8, 8);
    CHECK_EQ(up.At(7, 7)[1], 20);

    for (auto& m : AllModels())
    {
        if (!m.HasKeyScreens())
            continue;
        auto bytes = EncodeForFormat(Rgb(144, 144, 90, 90, 90), m.key);
        CHECK(!bytes.empty());
        if (m.key.mode == ImageMode::Jpeg)
        {
            CHECK(bytes[0] == 0xFF && bytes[1] == 0xD8);
            auto d = DecodeJpeg(bytes);
            CHECK(d && d->w == m.key.w && d->h == m.key.h);
        }
        else
        {
            CHECK(bytes[0] == 'B' && bytes[1] == 'M');
            CHECK_EQ(bytes.size(), 54u + static_cast<size_t>(m.key.w) * m.key.h * 3);
        }
    }
    // BMP of 72x72 must match the crate's blank header size field (0x3CF6)
    auto b = EncodeBmp(Rgb(72, 72));
    CHECK(b[2] == 0xF6 && b[3] == 0x3C);

    // Plus XL strip: logical 1200x100 -> device 100x1200 JPEG
    auto strip = EncodeForFormat(Rgb(1200, 100, 5, 5, 5), ModelOf(0x00C6).lcd);
    auto d = DecodeJpeg(strip);
    CHECK(d && d->w == 100 && d->h == 1200);
}

void TestLcdMapping()
{
    std::puts("lcd mapping");
    {  // Plus XL tile 2: partial window 0x0C in LOGICAL coordinates (Elgato docs, verified on hardware)
        auto [dev, mock] = MockDevice(0x00C6);
        dev.SetLcdTile(2, Rgb(200, 100, 1, 2, 3));
        const auto& r = mock->writes.at(0);
        CHECK_EQ(r[1], 0x0C);
        CHECK_EQ(r[2] | (r[3] << 8), 400);     // x
        CHECK_EQ(r[4] | (r[5] << 8), 0);       // y
        CHECK_EQ(r[6] | (r[7] << 8), 200);     // w
        CHECK_EQ(r[8] | (r[9] << 8), 100);     // h
        CHECK_EQ(mock->writes.back()[10], 1);  // last flag on final report
        auto jpg =
            DecodeJpeg(std::vector<std::uint8_t>(r.begin() + 16, r.end()));  // payload is rotated: 100 wide x 200 tall
        (void)jpg;
    }
    {  // legacy device-space mapping still available
        auto [dev, mock] = MockDevice(0x00C6);
        dev.SetLcdCoords(Device::LcdCoords::Device);
        dev.SetLcdTile(2, Rgb(200, 100));
        const auto& r = mock->writes.at(0);
        CHECK_EQ(r[2] | (r[3] << 8), 0);
        CHECK_EQ(r[4] | (r[5] << 8), 600);
    }
    {  // Plus (rot0): tile 1 -> x=200
        auto [dev, mock] = MockDevice(0x0084);
        dev.SetLcdTile(1, Rgb(200, 100));
        const auto& r = mock->writes.at(0);
        CHECK_EQ(r[2] | (r[3] << 8), 200);
        CHECK_EQ(r[4] | (r[5] << 8), 0);
        CHECK_EQ(r[6] | (r[7] << 8), 200);
        CHECK_EQ(r[8] | (r[9] << 8), 100);
    }
    {  // full strip uses "Update Window Image" 0x0B
        auto [dev, mock] = MockDevice(0x00C6);
        dev.SetLcdFull(Rgb(1200, 100));
        CHECK_EQ(mock->writes.at(0)[1], 0x0B);
        dev.SetUseWindowCmd(false);
        dev.SetLcdFull(Rgb(1200, 100));
        CHECK_EQ(mock->writes.back()[1], 0x0C);
    }
    {  // full panel 0x08 and panel composition
        auto [dev, mock] = MockDevice(0x00C6);
        auto st = dev.SetPanel({{0, Rgb(10, 10, 255, 0, 0)}, {35, Rgb(112, 112)}});
        CHECK(st.reports > 0);
        CHECK_EQ(mock->writes.at(0)[1], 0x08);
        CHECK_EQ(mock->writes.back()[3], 1);
    }
}

void TestDevice()
{
    std::puts("device");
    auto [dev, mock] = MockDevice(0x00C6);
    CHECK(dev.SetBrightness(33));
    CHECK_EQ(mock->features.at(0)[2], 33);
    mock->featureReply = {0x05, 0, 0, 0, 0, 0, '1', '.', '3', '0'};  // id, 5 header bytes, then text
    auto fw = dev.Firmware();
    CHECK(fw && *fw == "1.30");
    auto st = dev.ClearAllKeys();
    CHECK(st.reports >= 36);
    mock->inputs.push_back([] {
        std::vector<std::uint8_t> b(42, 0);
        b[0] = 1;
        b[4] = 1;
        return b;
    }());
    auto ev = dev.PollEvents(1);
    CHECK(ev.size() == 1 && ev[0].tUs > 0);
    CHECK(dev.PollEvents(1).empty());
}

void TestTheme()
{
    std::puts("theme");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "sdb_theme_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "k");
    auto writeBmp = [&](const fs::path& p, const Rgb& im) {
        auto b = EncodeBmp(im);
        std::ofstream(p, std::ios::binary)
            .write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    };
    writeBmp(dir / "k/a.bmp", Rgb(64, 64, 255, 0, 0));
    writeBmp(dir / "k/strip.bmp", Rgb(1200, 100, 0, 0, 255));
    std::ofstream(dir / "theme.json") << R"({"name":"t","brightness":55,"quality":80,
        "keys":[{"key":0,"image":"k/a.bmp"},{"key":99,"image":"k/a.bmp"},{"key":1,"image":"missing.png"}],
        "lcd":{"image":"k/strip.bmp"}})";
    auto loaded = LoadTheme(dir);
    CHECK(loaded.theme.has_value());
    CHECK_EQ(loaded.warnings.size(), 1u);  // missing.png
    auto [dev, mock] = MockDevice(0x00C6);
    auto rep = ApplyTheme(dev, *loaded.theme);
    CHECK_EQ(rep.warnings.size(), 1u);  // key 99 out of range
    CHECK_EQ(rep.keys, 1u);
    CHECK_EQ(mock->features.front()[2], 0);  // hidden while loading
    CHECK_EQ(mock->features.back()[2], 55);  // revealed at target brightness
    CHECK(rep.reports >= 37);                // 36 keys (blank for unlisted) + LCD
    auto bad = LoadTheme(dir / "nope");
    CHECK(!bad.theme && !bad.error.empty());
    fs::remove_all(dir);
}
void TestScenes()
{
    std::puts("scenes");
    using namespace sdb::scenes;
    const Size sz{1200, 100};
    auto differs = [](const Rgb& a, const Rgb& b) { return a.px != b.px; };
    auto nonBlack = [](const Rgb& im) {
        for (auto v : im.px)
            if (v)
                return true;
        return false;
    };

    CHECK(draw::Renderable("TAPE ERROR R 12%"));
    CHECK(draw::Renderable("lower case ok"));
    CHECK(!draw::Renderable(""));
    CHECK(!draw::Renderable("under_score"));
    CHECK(!draw::Renderable("tilde~"));
    CHECK_EQ(draw::TextWidth("AB", 3), 2 * 6 * 3 - 3);

    // every scene has the requested size and is deterministic
    CHECK_EQ(Balance(sz, 10).w, 1200);
    CHECK_EQ(Balance(sz, 10).h, 100);
    CHECK(!differs(Balance(sz, 10, 3), Balance(sz, 10, 3)));
    CHECK(differs(Balance(sz, -50), Balance(sz, 50)));
    CHECK(differs(Balance(sz, 0), Balance(sz, 1)));
    CHECK_EQ(Balance(sz, 500).w, 1200);  // out-of-range value is clamped, not UB
    Loading l;
    l.progress = 10;
    Loading l2 = l;
    l2.progress = 90;
    CHECK(differs(LoadingScene(sz, l), LoadingScene(sz, l2)));
    CHECK(differs(LoadingScene(sz, l, 0), LoadingScene(sz, l, 1)));  // border stripes animate
    l2 = l;
    l2.pilot = true;
    CHECK(differs(LoadingScene(sz, l), LoadingScene(sz, l2)));
    for (auto st : {Status::Ok, Status::Error, Status::Warn, Status::Paused, Status::Resumed, Status::Muted,
                    Status::Unmuted, Status::Info, Status::Saved, Status::Reset})
    {
        auto im = StatusScene(sz, st, {}, {}, 10);
        CHECK(im.w == 1200 && im.h == 100 && nonBlack(im));
    }
    Status s{};
    CHECK(StatusByName("paused", s) && s == Status::Paused);
    CHECK(!StatusByName("nope", s));
    CHECK(differs(StatusScene(sz, Status::Ok, "A"), StatusScene(sz, Status::Ok, "B")));
    auto tile = VolumeTile(200, 100, {"SPEAKERS", 50, false}, false, 0);
    CHECK_EQ(tile.w, 200);
    CHECK_EQ(tile.h, 100);
    CHECK(differs(tile, VolumeTile(200, 100, {"SPEAKERS", 50, true}, false, 0)));  // mute changes the look
    CHECK(differs(tile, VolumeTile(200, 100, {"SPEAKERS", 80, false}, false, 0)));
    CHECK(differs(tile, VolumeTile(200, 100, {"SPEAKERS", 50, false}, true, 0)));  // selection frame
    auto vol = Volume(sz, {{"A", 10, false}, {"B", 20, false}}, 1);
    CHECK_EQ(vol.w, 1200);
    CHECK(vol.px ==
          [&] {
              auto v2 = vol;
              return v2;
          }()
              .px);
    Ttd t;
    t.total = 1000;
    t.pos = 100;
    auto a = TtdScene(sz, t), b = (t.pos = 900, TtdScene(sz, t));
    CHECK(differs(a, b));
    CHECK_EQ(a.w, 1200);
    t.pos = 5000;  // beyond total is clamped
    CHECK_EQ(TtdScene(sz, t).h, 100);
    t.pos = 500;
    t.breakpoints = {200, 800};
    t.checkpoints = {300};
    t.hits = {400};
    CHECK(differs(TtdScene(sz, t), b));
    CHECK(differs(Configs(sz, {"48K", "128K", "PENTAGON"}, 0), Configs(sz, {"48K", "128K", "PENTAGON"}, 1)));
    CHECK(Configs(sz, {}, 0).w == 1200);  // empty list is safe
    CHECK(differs(Marquee(sz, "HELLO", 1), Marquee(sz, "HELLO", 9)));
    CHECK(nonBlack(Splash(sz)));
    // narrower strips (Plus 800x100) still render
    CHECK_EQ(StatusScene(Size{800, 100}, Status::Ok).w, 800);
    CHECK_EQ(TtdScene(Size{800, 100}, t).w, 800);

    // icons
    CHECK_EQ(IconNames().size(), 28u);
    for (const auto& n : IconNames())
    {
        auto ic = IconByName(n);
        CHECK(ic.has_value());
        Rgb im(64, 64);
        DrawIcon(im, *ic, 0, 0, 64, draw::kBWhite, draw::kBYellow, 1);
        CHECK(nonBlack(im));
    }
    CHECK(!IconByName("unicorn").has_value());
    Rgb t1(64, 64), t2(64, 64);
    DrawIcon(t1, Icon::Tape, 0, 0, 64, draw::kBWhite, draw::kBYellow, 0);
    DrawIcon(t2, Icon::Tape, 0, 0, 64, draw::kBWhite, draw::kBYellow, 3);
    CHECK(differs(t1, t2));  // reels rotate with the frame
    // drawing outside the canvas is clipped, never crashes
    Rgb small(10, 10);
    DrawIcon(small, Icon::Disk, -20, -20, 80, draw::kBWhite, draw::kBYellow);
    draw::Text(small, 5, 5, "CLIPPED TEXT", 4, draw::kBWhite);
    draw::Disc(small, -5, -5, 50, draw::kRed);
}
void TestFitAndGrid()
{
    std::puts("fit + grid");
    // wide 12:1 source into a square: contain keeps the aspect ratio (bars top/bottom), stretch fills, cover crops
    Rgb wide(1200, 100, 200, 0, 0);
    Rgb c = FitImage(wide, 144, 144, Fit::Contain, 0, 0, 255);
    CHECK_EQ(c.w, 144);
    CHECK_EQ(c.h, 144);
    CHECK_EQ(c.At(72, 72)[0], 200);  // picture in the middle
    CHECK_EQ(c.At(72, 2)[2], 255);   // letterbox bar uses the background colour
    CHECK_EQ(c.At(72, 141)[2], 255);
    CHECK_EQ(c.At(0, 72)[0], 200);  // full width used
    // height of the picture inside the box = 144 / 12 = 12 px
    int rows = 0;
    for (int y = 0; y < 144; ++y)
        if (c.At(72, y)[0] == 200)
            ++rows;
    CHECK(rows >= 11 && rows <= 13);
    Rgb st = FitImage(wide, 144, 144, Fit::Stretch, 0, 0, 255);
    CHECK_EQ(st.At(72, 2)[0], 200);  // stretch distorts: no bars
    Rgb cv = FitImage(wide, 144, 144, Fit::Cover, 0, 0, 255);
    CHECK_EQ(cv.At(0, 0)[0], 200);
    CHECK_EQ(cv.At(143, 143)[0], 200);  // cover leaves no bars
    CHECK(FitByName("contain") && FitByName("cover") && FitByName("stretch") && !FitByName("zoom"));
    // tall source: bars left/right
    Rgb tall = FitImage(Rgb(100, 1000, 9, 9, 9), 144, 144, Fit::Contain, 1, 2, 3);
    CHECK_EQ(tall.At(1, 72)[0], 1);
    CHECK_EQ(tall.At(72, 72)[0], 9);

    // grid slicing on a 4x9 deck
    const Model& xl = ModelOf(0x00C6);
    GridSpec g;
    g.r0 = 3;
    g.r1 = 3;
    g.c0 = 0;
    g.c1 = 8;
    g.fit = Fit::Contain;
    CHECK(GridInRange(xl, g));
    Rgb canvas;
    auto tiles = SliceGrid(xl, wide, g, &canvas);
    CHECK_EQ(tiles.size(), 9u);
    // physical layout: the block spans x 11..1271 (keys + gaps), 112 px tall; tiles are the native 112 px
    CHECK_EQ(canvas.w, 1260);
    CHECK_EQ(canvas.h, 112);
    CHECK_EQ(tiles.front().first, 27);
    CHECK_EQ(tiles.back().first, 35);  // row 3 = keys 27..35
    for (auto& t : tiles)
    {
        CHECK_EQ(t.second.w, 112);
        CHECK_EQ(t.second.h, 112);
    }
    // 12:1 into 11.25:1 -> 1260 x 105, 3 px bars top and bottom
    CHECK_EQ(canvas.At(600, 56)[0], 200);
    CHECK_EQ(canvas.At(600, 1)[0], 0);
    // tile 1 starts where the second key physically is (x 154 - 11 = 143 in the canvas), not at 112
    Rgb ramp(1260, 112);
    for (int x = 0; x < 1260; ++x)
        for (int y = 0; y < 112; ++y)
            ramp.At(x, y)[0] = static_cast<std::uint8_t>(x / 5);
    GridSpec gs = g;
    gs.fit = Fit::Stretch;
    auto rt = SliceGrid(xl, ramp, gs);
    CHECK_EQ(rt[1].second.At(0, 50)[0], 143 / 5);
    GridSpec bad = g;
    bad.r1 = 4;
    CHECK(!GridInRange(xl, bad));
    bad = g;
    bad.c1 = 9;
    CHECK(!GridInRange(xl, bad));
    bad = g;
    bad.r0 = 3;
    bad.r1 = 2;
    CHECK(!GridInRange(xl, bad));
    // full grid: 4 rows x 9 cols = 36 tiles
    GridSpec all;
    all.r1 = 3;
    all.c1 = 8;
    CHECK_EQ(SliceGrid(xl, Rgb(900, 400, 1, 2, 3), all).size(), 36u);
    // gap draws a dark border on every tile
    GridSpec gp = all;
    gp.gap = 3;
    auto gt = SliceGrid(xl, Rgb(900, 400, 255, 255, 255), gp);
    CHECK_EQ(gt[0].second.At(0, 0)[0], 0);
    CHECK_EQ(gt[0].second.At(56, 56)[0], 255);
}

void TestDebuggerScenes()
{
    std::puts("debugger scenes");
    using namespace sdb::scenes;
    const Size sz{1200, 100};
    auto differs = [](const Rgb& a, const Rgb& b) { return a.px != b.px; };
    std::vector<Reg> regs{{"AF", 0x1F44, -1}, {"BC", 0x0120, -1}};
    auto calm = Registers(sz, regs, 0x45);
    regs[0].prev = 0x1F00;
    CHECK(differs(calm, Registers(sz, regs, 0x45)));  // changed register glows
    CHECK_EQ(calm.w, 1200);
    std::vector<AsmLine> l{{0x8000, "LD A,5"}, {0x8002, "LD B,A"}, {0x8003, "ADD A,B"}};
    CHECK(differs(Disasm(sz, l, 0, {}), Disasm(sz, l, 1, {})));        // PC row moves
    CHECK(differs(Disasm(sz, l, 2, {}), Disasm(sz, l, 2, {0x8003})));  // breakpoint dot + banner
    CHECK_EQ(Disasm(sz, {}, 0, {}).w, 1200);                           // empty list is safe
    CHECK(differs(Memory(sz, 0x8000, {1, 2, 3}), Memory(sz, 0x8000, {1, 2, 3}, {1})));
    CHECK_EQ(Memory(sz, 0x8000, {}).h, 100);
    std::vector<Frame> f{{0x8123, "PLOT"}, {0x8040, "DRAW"}};
    CHECK(differs(CallStack(sz, f, 0), CallStack(sz, f, 1)));
    CHECK_EQ(CallStack(sz, {}, 0).w, 1200);

    // theme with a grid + fit
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "sdb_theme_grid";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        auto b = EncodeBmp(Rgb(1200, 100, 0, 200, 0));
        std::ofstream(dir / "wide.bmp", std::ios::binary)
            .write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    }
    std::ofstream(dir / "theme.json")
        << R"({"name":"g","fit":"contain","grids":[{"image":"wide.bmp","rows":"3","cols":"0-8"},{"image":"wide.bmp","rows":"9","cols":"0"},{"image":"wide.bmp","rows":"x","cols":"0"}]})";
    auto grid = LoadTheme(dir);
    CHECK(grid.theme.has_value());
    CHECK_EQ(grid.theme->grids.size(), 2u);  // malformed range dropped with a warning
    CHECK_EQ(grid.warnings.size(), 1u);
    auto [dev, mock] = MockDevice(0x00C6);
    auto rep = ApplyTheme(dev, *grid.theme);
    CHECK_EQ(rep.warnings.size(), 1u);  // row 9 is outside the deck
    CHECK(rep.reports >= 36);
    fs::remove_all(dir);
}
void TestEmulation()
{
    std::puts("emulation");
    // every model's wire format decodes back to an upright picture (orientation preserved)
    for (auto& m : AllModels())
    {
        if (!m.HasKeyScreens())
            continue;
        Rgb img(m.key.w, m.key.h, 0, 0, 0);
        draw::FillRect(img, 0, 0, 12, 12, {255, 0, 0});                        // red top-left
        draw::FillRect(img, m.key.w - 12, m.key.h - 12, 12, 12, {0, 0, 255});  // blue bottom-right
        auto back = DecodeForFormat(EncodeForFormat(img, m.key, 95), m.key);
        CHECK(back.has_value());
        if (!back)
            continue;
        CHECK(back->At(4, 4)[0] > 180 && back->At(4, 4)[2] < 80);
        CHECK(back->At(m.key.w - 5, m.key.h - 5)[2] > 180 && back->At(m.key.w - 5, m.key.h - 5)[0] < 80);
    }
    CHECK(FindModelByName("plus") == FindModel(kElgatoVid, 0x0084));
    CHECK(FindModelByName("0x00C6") == FindModel(kElgatoVid, 0x00C6));
    CHECK(FindModelByName("Neo") == FindModel(kElgatoVid, 0x009A));
    CHECK(FindModelByName("nope") == nullptr);
    {  // Plus on a Plus XL: virtual key 5 (row 1, col 1) -> physical key 10; input translated back
        auto [dev, mock] = MockDevice(0x00C6);
        CHECK(dev.Emulate(ModelOf(0x0084)));
        CHECK_EQ(dev.GetModel().pid, 0x0084);
        mock->writes.clear();
        mock->features.clear();
        dev.SetKeyImage(5, Rgb(120, 120, 9, 9, 9));
        CHECK_EQ(mock->writes.at(0)[2], 10);
        dev.FillKeyColor(7, 1, 2, 3);
        CHECK_EQ(mock->features.back()[2], 1 * 9 + 3);
        std::vector<std::uint8_t> keys(42, 0);
        keys[0] = 1;
        keys[4 + 10] = 1;
        mock->inputs.push_back(keys);
        auto ev = dev.PollEvents(1);
        CHECK(ev.size() == 1 && ev[0].index == 5 && ev[0].type == EventType::KeyDown);
        std::vector<std::uint8_t> outside(42, 0);
        outside[0] = 1;
        outside[4 + 8] = 1;  // physical key 8 is outside the 4x2 block
        mock->inputs.push_back(outside);
        CHECK(dev.PollEvents(1).empty() || dev.PollEvents(1).empty());
        std::vector<std::uint8_t> dial(42, 0);
        dial[0] = 1;
        dial[1] = 3;
        dial[4] = 1;
        dial[5 + 5] = 2;  // dial 5: Plus has 4
        mock->inputs.push_back(dial);
        CHECK(dev.PollEvents(1).empty());
        std::vector<std::uint8_t> tap(42, 0);
        tap[0] = 1;
        tap[1] = 2;
        tap[4] = 1;
        tap[6] = 0x2C;
        tap[7] = 0x01;
        tap[8] = 50;  // x = 300
        mock->inputs.push_back(tap);
        ev = dev.PollEvents(1);
        CHECK(ev.size() == 1 && ev[0].x == 300 - 200);  // Plus window 800 wide, centred: starts at 200
        mock->writes.clear();
        dev.SetLcdTile(0, Rgb(200, 100));
        const auto& r = mock->writes.at(0);
        CHECK_EQ(r[2] | (r[3] << 8), 200);  // physical strip x = 200 + 0
    }
    {  // Neo: sensors are the keys under the first and last column; window 248 x 58 centred
        auto [dev, mock] = MockDevice(0x00C6);
        CHECK(dev.Emulate(ModelOf(0x009A)));
        mock->features.clear();
        dev.SetTouchLed(1, 9, 9, 9);
        CHECK_EQ(mock->features.back()[2], 2 * 9 + 3);
        std::vector<std::uint8_t> k(42, 0);
        k[0] = 1;
        k[4 + 18] = 1;  // physical key 18 = sensor 0
        mock->inputs.push_back(k);
        auto ev = dev.PollEvents(1);
        CHECK(ev.size() == 1 && ev[0].type == EventType::TouchPointDown && ev[0].index == 0);
    }
    {  // too big to emulate on a smaller device
        auto [dev, mock] = MockDevice(0x0084);
        std::string err;
        CHECK(!dev.Emulate(ModelOf(0x006C), &err));
        CHECK(!err.empty());
    }
}
}  // namespace

int main()
{
    TestModels();
    TestProtocol();
    TestInput();
    TestImage();
    TestLcdMapping();
    TestDevice();
    TestTheme();
    TestScenes();
    TestFitAndGrid();
    TestDebuggerScenes();
    TestEmulation();
    TestAutomation();
    TestScenario();
    TestOps();
    TestScenarioRender();
    TestActions();
    TestRuntime();
    TestIo();
    TestTagsAndPages();
    TestSubtrees();
    TestWebSocket();
    TestDeckHub();
    std::printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
