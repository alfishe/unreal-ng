#include "sdb/icons.h"

#include <array>
#include <cmath>

namespace sdb
{
using namespace draw;
namespace
{
constexpr float kPi = 3.14159265f;
struct Name
{
    const char* n;
    Icon i;
};
constexpr std::array<Name, 28> kNames{
    {{"record", Icon::Record},     {"tape", Icon::Tape},       {"disk", Icon::Disk},
     {"play", Icon::Play},         {"pause", Icon::Pause},     {"stop", Icon::Stop},
     {"reset", Icon::Reset},       {"speaker", Icon::Speaker}, {"mute", Icon::Mute},
     {"rewind", Icon::Rewind},     {"forward", Icon::Forward}, {"stepback", Icon::StepBack},
     {"flag", Icon::Flag},         {"bug", Icon::Bug},         {"gear", Icon::Gear},
     {"check", Icon::Check},       {"cross", Icon::Cross},     {"warn", Icon::Warn},
     {"camera", Icon::Camera},     {"chip", Icon::Chip},       {"stepinto", Icon::StepInto},
     {"stepover", Icon::StepOver}, {"stepout", Icon::StepOut}, {"eye", Icon::Eye},
     {"memory", Icon::Memory},     {"regs", Icon::Regs},       {"disasm", Icon::Disasm},
     {"stack", Icon::Stack}}};

void Reel(Rgb& im, float cx, float cy, float r, Color c, int frame)
{
    Ring(im, cx, cy, r, std::max(1.5f, r * 0.22f), c);
    for (int k = 0; k < 3; ++k)
    {
        const float a = frame * 0.45f + k * 2.0944f;
        Line(im, cx, cy, cx + std::cos(a) * r * 0.85f, cy + std::sin(a) * r * 0.85f, std::max(1.2f, r * 0.2f), c);
    }
}
}  // namespace

std::optional<Icon> IconByName(const std::string& name)
{
    for (auto& k : kNames)
        if (name == k.n)
            return k.i;
    return std::nullopt;
}
std::vector<std::string> IconNames()
{
    std::vector<std::string> v;
    for (auto& k : kNames)
        v.push_back(k.n);
    return v;
}

void DrawIcon(Rgb& im, Icon icon, int x0, int y0, int size, Color fg, Color accent, int frame)
{
    const float s = static_cast<float>(size);
    auto x = [&](float u) { return x0 + u * s; };
    auto y = [&](float v) { return y0 + v * s; };
    const float t = std::max(1.5f, s * 0.09f);  // base stroke

    switch (icon)
    {
        case Icon::Tape: {  // compact cassette with spinning reels
            RoundRect(im, x(.04f), y(.2f), s * .92f, s * .6f, s * .06f, Dim(fg, .55f));
            RoundRect(im, x(.1f), y(.26f), s * .8f, s * .22f, s * .03f, fg);   // label strip
            RoundRect(im, x(.2f), y(.5f), s * .6f, s * .22f, s * .1f, kDark);  // tape window
            Reel(im, x(.34f), y(.61f), s * .075f, accent, frame);
            Reel(im, x(.66f), y(.61f), s * .075f, accent, frame);
            Line(im, x(.3f), y(.78f), x(.7f), y(.78f), t * .8f, Dim(fg, .3f));
            Line(im, x(.15f), y(.37f), x(.55f), y(.37f), t * .6f, kDark);  // "handwriting"
            break;
        }
        case Icon::Disk: {  // 3.5" floppy
            RoundRect(im, x(.1f), y(.08f), s * .8f, s * .84f, s * .06f, Dim(fg, .85f));
            FillRect(im, static_cast<int>(x(.28f)), static_cast<int>(y(.08f)), static_cast<int>(s * .44f),
                     static_cast<int>(s * .28f), kWhite);  // shutter
            FillRect(im, static_cast<int>(x(.56f)), static_cast<int>(y(.12f)), static_cast<int>(s * .09f),
                     static_cast<int>(s * .2f), kGrey);
            FillRect(im, static_cast<int>(x(.2f)), static_cast<int>(y(.52f)), static_cast<int>(s * .6f),
                     static_cast<int>(s * .34f), kBWhite);  // label
            Line(im, x(.26f), y(.62f), x(.74f), y(.62f), t * .5f, kGrey);
            Line(im, x(.26f), y(.73f), x(.6f), y(.73f), t * .5f, kGrey);
            if (frame % 4 < 2)
                Disc(im, x(.82f), y(.86f), s * .035f, accent);  // activity LED
            break;
        }
        case Icon::Play:
            Tri(im, x(.25f), y(.15f), x(.25f), y(.85f), x(.85f), y(.5f), fg);
            break;
        case Icon::Pause:
            RoundRect(im, x(.22f), y(.16f), s * .21f, s * .68f, s * .04f, fg);
            RoundRect(im, x(.57f), y(.16f), s * .21f, s * .68f, s * .04f, fg);
            break;
        case Icon::Stop:
            RoundRect(im, x(.2f), y(.2f), s * .6f, s * .6f, s * .06f, fg);
            break;
        case Icon::Record:
            Disc(im, x(.5f), y(.5f), s * .32f, {230, 40, 50});
            break;  // red, whatever the face's colour
        case Icon::Reset: {
            Arc(im, x(.5f), y(.52f), s * .3f, -60, 240, t * 1.2f, fg);
            const float a = -60 * kPi / 180;
            const float ax = x(.5f) + std::cos(a) * s * .3f, ay = y(.52f) + std::sin(a) * s * .3f;
            Tri(im, ax - s * .02f, ay - s * .2f, ax + s * .2f, ay + s * .02f, ax - s * .16f, ay + s * .1f, fg);
            break;
        }
        case Icon::Speaker:
        case Icon::Mute: {
            FillRect(im, static_cast<int>(x(.12f)), static_cast<int>(y(.38f)), static_cast<int>(s * .18f),
                     static_cast<int>(s * .24f), fg);
            Tri(im, x(.3f), y(.38f), x(.3f), y(.62f), x(.56f), y(.8f), fg);
            Tri(im, x(.3f), y(.38f), x(.56f), y(.2f), x(.56f), y(.8f), fg);
            if (icon == Icon::Speaker)
            {
                Arc(im, x(.56f), y(.5f), s * .16f, -45, 45, t, fg);
                Arc(im, x(.56f), y(.5f), s * .28f, -50, 50, t, fg);
                Arc(im, x(.56f), y(.5f), s * .4f, -50, 50, t, Dim(fg, .6f));
            }
            else
            {
                Line(im, x(.64f), y(.34f), x(.92f), y(.66f), t * 1.3f, accent);
                Line(im, x(.92f), y(.34f), x(.64f), y(.66f), t * 1.3f, accent);
            }
            break;
        }
        case Icon::Rewind:
            Tri(im, x(.5f), y(.2f), x(.5f), y(.8f), x(.1f), y(.5f), fg);
            Tri(im, x(.9f), y(.2f), x(.9f), y(.8f), x(.5f), y(.5f), fg);
            break;
        case Icon::Forward:
            Tri(im, x(.1f), y(.2f), x(.1f), y(.8f), x(.5f), y(.5f), fg);
            Tri(im, x(.5f), y(.2f), x(.5f), y(.8f), x(.9f), y(.5f), fg);
            break;
        case Icon::StepBack:
            FillRect(im, static_cast<int>(x(.14f)), static_cast<int>(y(.2f)), std::max(2, static_cast<int>(s * .1f)),
                     static_cast<int>(s * .6f), fg);
            Tri(im, x(.8f), y(.2f), x(.8f), y(.8f), x(.3f), y(.5f), fg);
            break;
        case Icon::Flag:
            Line(im, x(.25f), y(.1f), x(.25f), y(.92f), t, fg);
            Tri(im, x(.25f), y(.12f), x(.25f), y(.55f), x(.85f), y(.335f), accent);
            break;
        case Icon::Bug:
            Disc(im, x(.5f), y(.5f), s * .32f, fg);
            Disc(im, x(.4f), y(.4f), s * .09f, Mix(fg, kBWhite, .6f));
            break;
        case Icon::Gear: {
            for (int k = 0; k < 8; ++k)
            {
                const float a = k * kPi / 4;
                Line(im, x(.5f) + std::cos(a) * s * .26f, y(.5f) + std::sin(a) * s * .26f,
                     x(.5f) + std::cos(a) * s * .4f, y(.5f) + std::sin(a) * s * .4f, s * .14f, fg);
            }
            Disc(im, x(.5f), y(.5f), s * .3f, fg);
            Disc(im, x(.5f), y(.5f), s * .12f, kDark);
            break;
        }
        case Icon::Check:
            Line(im, x(.18f), y(.55f), x(.4f), y(.76f), t * 1.6f, fg);
            Line(im, x(.4f), y(.76f), x(.84f), y(.26f), t * 1.6f, fg);
            break;
        case Icon::Cross:
            Line(im, x(.2f), y(.2f), x(.8f), y(.8f), t * 1.6f, fg);
            Line(im, x(.8f), y(.2f), x(.2f), y(.8f), t * 1.6f, fg);
            break;
        case Icon::Warn:
            Tri(im, x(.5f), y(.1f), x(.08f), y(.86f), x(.92f), y(.86f), fg);
            FillRect(im, static_cast<int>(x(.47f)), static_cast<int>(y(.36f)), std::max(2, static_cast<int>(s * .07f)),
                     static_cast<int>(s * .28f), kDark);
            Disc(im, x(.505f), y(.73f), s * .045f, kDark);
            break;
        case Icon::Camera:
            RoundRect(im, x(.08f), y(.28f), s * .84f, s * .56f, s * .08f, fg);
            FillRect(im, static_cast<int>(x(.3f)), static_cast<int>(y(.18f)), static_cast<int>(s * .3f),
                     static_cast<int>(s * .12f), fg);
            Disc(im, x(.5f), y(.56f), s * .17f, kDark);
            Ring(im, x(.5f), y(.56f), s * .17f, t * .7f, accent);
            break;
        case Icon::Chip:
            RoundRect(im, x(.22f), y(.22f), s * .56f, s * .56f, s * .04f, fg);
            for (int k = 0; k < 4; ++k)
            {
                const float p = .3f + k * .13f;
                Line(im, x(p), y(.1f), x(p), y(.22f), t * .6f, Dim(fg, .7f));
                Line(im, x(p), y(.78f), x(p), y(.9f), t * .6f, Dim(fg, .7f));
                Line(im, x(.1f), y(p), x(.22f), y(p), t * .6f, Dim(fg, .7f));
                Line(im, x(.78f), y(p), x(.9f), y(p), t * .6f, Dim(fg, .7f));
            }
            TextCentered(im, static_cast<int>(x(.5f)), static_cast<int>(y(.5f) - s * .09f), "Z80",
                         std::max(1, static_cast<int>(s / 28)), kDark);
            break;
        case Icon::StepInto:
            Line(im, x(.5f), y(.08f), x(.5f), y(.5f), t * 1.3f, fg);
            Tri(im, x(.27f), y(.46f), x(.73f), y(.46f), x(.5f), y(.74f), fg);
            for (int k = 0; k < 3; ++k)
                FillRect(im, static_cast<int>(x(.18f + k * .26f)), static_cast<int>(y(.84f)),
                         static_cast<int>(s * .18f), std::max(2, static_cast<int>(s * .07f)), Dim(fg, .7f));
            break;
        case Icon::StepOver: {
            Arc(im, x(.5f), y(.62f), s * .32f, 185, 350, t * 1.3f, fg);
            Tri(im, x(.84f), y(.52f), x(.5f), y(.5f), x(.82f), y(.78f), fg);
            Disc(im, x(.18f), y(.84f), s * .06f, Dim(fg, .7f));
            Disc(im, x(.82f), y(.88f), s * .06f, Dim(fg, .7f));
            Disc(im, x(.5f), y(.84f), s * .06f, accent);
            break;
        }
        case Icon::StepOut:
            Line(im, x(.5f), y(.92f), x(.5f), y(.5f), t * 1.3f, fg);
            Tri(im, x(.27f), y(.54f), x(.73f), y(.54f), x(.5f), y(.2f), fg);
            for (int k = 0; k < 3; ++k)
                FillRect(im, static_cast<int>(x(.18f + k * .26f)), static_cast<int>(y(.08f)),
                         static_cast<int>(s * .18f), std::max(2, static_cast<int>(s * .07f)), Dim(fg, .7f));
            break;
        case Icon::Eye:
            Arc(im, x(.5f), y(.8f), s * .5f, -145, -35, t * 1.2f, fg);
            Arc(im, x(.5f), y(.2f), s * .5f, 35, 145, t * 1.2f, fg);
            Disc(im, x(.5f), y(.5f), s * .17f, accent);
            Disc(im, x(.5f), y(.5f), s * .08f, kDark);
            Disc(im, x(.45f), y(.45f), s * .035f, kBWhite);
            break;
        case Icon::Memory:
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    RoundRect(im, x(.14f + c * .27f), y(.14f + r * .27f), s * .22f, s * .22f, s * .03f,
                              (r == 1 && c == 2) ? accent : fg);
            break;
        case Icon::Regs:
            for (int k = 0; k < 4; ++k)
            {
                RoundRect(im, x(.12f), y(.12f + k * .2f), s * .22f, s * .15f, s * .03f, accent);
                RoundRect(im, x(.4f), y(.12f + k * .2f), s * (.48f - (k % 2) * .14f), s * .15f, s * .03f, fg);
            }
            break;
        case Icon::Disasm:
            for (int k = 0; k < 4; ++k)
            {
                const bool cur = k == 1;
                if (cur)
                    Tri(im, x(.08f), y(.2f + k * .2f), x(.08f), y(.36f + k * .2f), x(.2f), y(.28f + k * .2f), accent);
                RoundRect(im, x(.26f), y(.2f + k * .2f), s * (.66f - (k % 3) * .14f), s * .13f, s * .02f,
                          cur ? accent : Dim(fg, .8f));
            }
            break;
        case Icon::Stack:
            for (int k = 0; k < 3; ++k)
                RoundRect(im, x(.16f + k * .04f), y(.12f + k * .26f), s * (.68f - k * .08f), s * .2f, s * .04f,
                          k == 0 ? accent : Dim(fg, 1.f - k * .2f));
            Tri(im, x(.5f), y(.95f), x(.38f), y(.86f), x(.62f), y(.86f), fg);
            break;
    }
}

}  // namespace sdb
