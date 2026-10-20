// Touch-strip (LCD) orientation and region tests. Output is meant to be LOOKED AT on the device.
//   sdctl lcd-test orient [--rot 0|90|180|270] [--mirror x|y|both] [--hold SEC]   (no --rot: cycle all four)
//   sdctl lcd-test tiles  [--rot N] [--hold SEC]      one numbered colour tile per dial via region writes
//   sdctl lcd-test sweep  [--rot N] [--seconds S]     moving 100x100 block via region writes, prints FPS
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "cli.h"
#include "font5x7.h"

using namespace sdb;
using namespace std::chrono_literals;

namespace cli
{
namespace
{
std::optional<Rotation> ParseRot(const Args& a)
{
    if (!a.Has("rot"))
        return std::nullopt;
    switch (a.Num("rot", 0))
    {
        case 90:
            return Rotation::R90;
        case 180:
            return Rotation::R180;
        case 270:
            return Rotation::R270;
        default:
            return Rotation::R0;
    }
}
std::optional<Mirror> ParseMirror(const Args& a)
{
    const auto m = a.Str("mirror");
    if (m == "x")
        return Mirror::X;
    if (m == "y")
        return Mirror::Y;
    if (m == "both")
        return Mirror::Both;
    return std::nullopt;
}

Rgb OrientImage(int w, int h, const std::string& label)
{
    Rgb im(w, h, 12, 12, 24);
    for (int x = 0; x < w; x += 100)
    {  // 100 px ruler cells with alternating shade
        if ((x / 100) % 2)
            FillRect(im, x, 0, 100, h, 24, 24, 48);
        DrawText(im, x + 4, 12, std::to_string(x / 100), 2, 160, 160, 160);
        FillRect(im, x, 0, 1, h, 90, 90, 90);
    }
    FillRect(im, 0, 0, w, 3, 255, 255, 255);     // TOP edge: white
    FillRect(im, 0, h - 3, w, 3, 255, 220, 0);   // BOTTOM edge: yellow
    FillRect(im, 0, 3, 90, h - 6, 200, 30, 30);  // LEFT block: red + "L"
    DrawText(im, 22, 30, "L", 6, 255, 255, 255);
    FillRect(im, w - 90, 3, 90, h - 6, 30, 60, 220);  // RIGHT block: blue + "R"
    DrawText(im, w - 68, 30, "R", 6, 255, 255, 255);
    DrawText(im, w / 2 - 150, 30, label, 6, 255, 255, 255);  // readable only if orientation is right
    return im;
}

Rgb TileImage(int w, int h, int dial)
{
    static const std::uint8_t pal[6][3] = {{220, 40, 40}, {230, 150, 20}, {200, 200, 30},
                                           {40, 180, 60}, {40, 130, 220}, {160, 60, 200}};
    const auto* c = pal[dial % 6];
    Rgb im(w, h, c[0] / 3, c[1] / 3, c[2] / 3);
    FillRect(im, 2, 2, w - 4, h - 4, c[0], c[1], c[2]);
    FillRect(im, 8, 8, w - 16, h - 16, c[0] / 4, c[1] / 4, c[2] / 4);
    DrawText(im, w / 2 - 18, h / 2 - 21, std::to_string(dial), 6, 255, 255, 255);
    return im;
}
}  // namespace

int CmdLcdTest(const Args& a)
{
    if (a.pos.size() < 2)
    {
        std::fprintf(stderr, "usage: sdctl lcd-test <orient|tiles|sweep>\n");
        return 2;
    }
    auto dev = OpenOrExplain(a);
    if (!dev)
        return 1;
    const Model& m = dev->GetModel();
    if (!m.HasLcd())
    {
        std::fprintf(stderr, "this model has no LCD\n");
        return 1;
    }
    dev->SetBrightness(a.Num("brightness", 70));
    const int w = m.lcdLogicalW, h = m.lcdLogicalH;
    const double hold = a.Dbl("hold", 6);
    const std::string mode = a.pos[1];
    dev->SetLcdRotationOverride(ParseRot(a));
    dev->SetLcdMirrorOverride(ParseMirror(a));

    if (mode == "orient")
    {
        std::vector<Rotation> rots = a.Has("rot")
                                         ? std::vector{dev->LcdRotation()}
                                         : std::vector{Rotation::R0, Rotation::R90, Rotation::R180, Rotation::R270};
        for (Rotation r : rots)
        {
            dev->SetLcdRotationOverride(r);
            const std::string label = "ROT " + std::to_string(Degrees(r));
            const auto st = dev->SetLcdFull(OrientImage(w, h, label));
            std::printf(
                "showing %-8s  %zu reports, %.1f ms  -> expected look: red L at left, blue R at right, white edge on "
                "top, text readable\n",
                label.c_str(), st.reports, st.elapsed.count() / 1e6);
            std::fflush(stdout);
            std::this_thread::sleep_for(std::chrono::duration<double>(hold));
        }
        return 0;
    }
    if (mode == "tiles")
    {
        const int n = m.dials ? m.dials : w / m.dialTileW;
        for (int d = 0; d < n; ++d)
        {
            const auto st = dev->SetLcdTile(d, TileImage(m.dialTileW, h, d));
            std::printf("tile %d at logical x=%d: %zu reports, %.1f ms\n", d, d * m.dialTileW, st.reports,
                        st.elapsed.count() / 1e6);
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(hold));
        return 0;
    }
    if (mode == "sweep")
    {
        const double seconds = a.Dbl("seconds", 5);
        const int bs = std::min(100, h);
        const Rgb block = [&] {
            Rgb b(bs, bs, 255, 255, 255);
            FillRect(b, 4, 4, bs - 8, bs - 8, 230, 40, 40);
            return b;
        }();
        (void)dev->SetLcdFull(Rgb(w, h, 0, 0, 0));
        const auto t0 = std::chrono::steady_clock::now();
        int frames = 0;
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds)
        {
            const int x = (frames * 20) % (w - bs);
            (void)dev->SetLcdRegion(x, 0, block);
            ++frames;
        }
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("sweep: %d region updates of %dx%d in %.2f s = %.1f updates/s\n", frames, bs, bs, el, frames / el);
        return 0;
    }
    std::fprintf(stderr, "unknown lcd-test mode\n");
    return 2;
}

}  // namespace cli
