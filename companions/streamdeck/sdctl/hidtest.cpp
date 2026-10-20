// sdctl hid-test [--hold SEC] [--only N]: walks through the parts of the Elgato HID docs that differ from what the
// crate does, one visual check per step. Watch the deck; each step prints what correct looks like.
#include <chrono>
#include <cstdio>
#include <thread>

#include "cli.h"
#include "sdb/draw.h"

using namespace sdb;
using namespace sdb::draw;

namespace cli
{
namespace
{
Rgb KeyPattern(int w, int idx)
{
    Rgb im(w, w, 20, 20, 40);
    Outline(im, 0, 0, w, w, 3, kBYellow);          // the frame must be fully visible on every side
    FillRect(im, 0, 0, 12, 12, kBRed);             // red = top-left corner
    FillRect(im, w - 12, w - 12, 12, 12, kBCyan);  // cyan = bottom-right corner
    TextCentered(im, w / 2, w / 2 - 14, std::to_string(idx), 4, kBWhite);
    return im;
}
Rgb TilePattern(int w, int h, int n, Color c)
{
    Rgb im(w, h, c.r / 3, c.g / 3, c.b / 3);
    Outline(im, 0, 0, w, h, 3, c);
    TextCentered(im, w / 2, h / 2 - 21, std::to_string(n), 6, kBWhite);
    return im;
}
}  // namespace

int CmdHidTest(const Args& a)
{
    auto dev = OpenOrExplain(a);
    if (!dev)
        return kDeviceError;
    const Model& m = dev->GetModel();
    const double hold = a.Dbl("hold", 6);
    const int only = a.Num("only", 0);
    auto wait = [&] {
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::duration<double>(hold));
    };
    auto step = [&](int n) { return only == 0 || only == n; };
    dev->SetBrightness(70);

    if (step(1) && m.HasKeyScreens())
    {
        std::printf(
            "1) KEY SIZE %dx%d (docs; crate used 120 on the + XL). Every key: yellow frame on all 4 sides, red square "
            "top-left,\n"
            "   cyan square bottom-right, number upright = key index (0 top-left, row by row).\n",
            m.key.w, m.key.h);
        for (int k = 0; k < m.keys; ++k)
            (void)dev->SetKeyImage(k, KeyPattern(m.key.w, k));
        wait();
    }
    if (step(2) && m.HasFullScreen())
    {
        std::printf(
            "2) FULL SCREEN %dx%d in ONE image (output 0x08). Same pattern as step 1 on every key, placed by the "
            "Elgato pixel layout; the strip shows a cyan frame.\n",
            m.screenLogicalW, m.screenLogicalH);
        // Keys and the strip at their real places (Elgato "Unit Pixel Layout"); the gaps between keys are dark grey.
        const PanelLayout l = GetPanelLayout(m);
        Rgb im(l.w, l.h, 40, 40, 40);
        for (int k = 0; k < m.keys; ++k)
        {
            const auto r = l.KeyRect(k, m.cols);
            Rgb key = KeyPattern(r.w, k);
            Blit(im, key, r.x, r.y);
        }
        if (l.stripW > 0)
        {
            FillRect(im, l.stripX, l.stripY, l.stripW, l.stripH, {0, 60, 120});
            Outline(im, l.stripX, l.stripY, l.stripW, l.stripH, 3, kBCyan);
            TextCentered(im, l.stripX + l.stripW / 2, l.stripY + l.stripH / 2 - 14, "STRIP IN THE FULL SCREEN", 4,
                         kBWhite);
        }
        const auto st = dev->SetFullScreen(im);
        std::printf("   %zu reports, %.1f ms\n", st.reports, st.elapsed.count() / 1e6);
        wait();
    }
    if (step(3) && m.windowFillCmd)
    {
        std::printf("3) WINDOW IMAGE (output 0x0B, full strip): tiles 0..5 left to right, upright.\n");
        Rgb s(m.lcdLogicalW, m.lcdLogicalH);
        for (int i = 0; i < m.lcdLogicalW / m.dialTileW; ++i)
            Blit(s, TilePattern(m.dialTileW, m.lcdLogicalH, i, Hsv(i / 6.f, 1, 1)), i * m.dialTileW, 0);
        const auto st = dev->SetWindowImage(s);
        std::printf("   %zu reports, %.1f ms\n", st.reports, st.elapsed.count() / 1e6);
        wait();
    }
    if ((step(4) || step(5)) && m.partialWindow && m.lcdKind == LcdKind::Region)
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            if (!step(4 + mode))
                continue;
            dev->SetLcdCoords(mode == 0 ? Device::LcdCoords::Device : Device::LcdCoords::Logical);
            (void)dev->SetLcdFull(Rgb(m.lcdLogicalW, m.lcdLogicalH));
            std::printf("%d) PARTIAL WINDOW 0x0C, %s coordinates: tiles 0..5 should appear one by one LEFT to RIGHT.\n",
                        4 + mode, mode == 0 ? "DEVICE (rotated, what we use now)" : "LOGICAL (what the docs say)");
            for (int i = 0; i < m.lcdLogicalW / m.dialTileW; ++i)
            {
                (void)dev->SetLcdTile(i, TilePattern(m.dialTileW, m.lcdLogicalH, i, mode == 0 ? kBGreen : kBMagenta));
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
            }
            wait();
        }
        dev->SetLcdCoords(Device::LcdCoords::Device);
    }
    if (step(6) && m.family == Family::V2 && m.HasKeyScreens())
    {
        std::printf("6) FILL KEY WITH COLOR (feature 03 06, no image): a rainbow over the keys.\n");
        for (int k = 0; k < m.keys; ++k)
        {
            const Color c = Hsv(k / static_cast<float>(m.keys), 1, 1);
            dev->FillKeyColor(k, c.r, c.g, c.b);
        }
        wait();
    }
    if (step(7) && m.family == Family::V2)
    {
        std::printf("7) FILL LCD WITH COLOR (feature 03 05): the whole panel turns blue.\n");
        dev->FillScreenColor(0, 0, 200);
        wait();
    }
    if (step(8))
    {
        std::printf("8) SHOW LOGO (feature 03 02): the Elgato boot logo.\n");
        dev->Reset();
        wait();
    }
    std::printf("done\n");
    return kOk;
}

}  // namespace cli
