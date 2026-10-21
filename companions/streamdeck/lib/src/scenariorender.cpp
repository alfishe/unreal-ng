#include "sdb/scenariorender.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <tuple>

#include "sdb/datadir.h"
#include "sdb/format.h"
#include "sdb/icons.h"
#include "sdb/svg.h"

namespace sdb::scn
{

namespace
{
constexpr int kCrumbScale = 3;  // 15 x 21 px glyph cells on a 100 px strip
constexpr int kCrumbPad = 14, kCrumbGap = 26, kCrumbLeft = 10;
constexpr draw::Color kChip{40, 40, 58}, kChipCur{0, 110, 200}, kChipText{230, 230, 240}, kSep{120, 120, 150};

int FitScale(const std::string& s, int width, int maxScale)
{
    int sc = maxScale;
    while (sc > 1 && draw::TextWidth(s, sc) > width)
        --sc;
    return sc;
}
}  // namespace

std::string DisplayText(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if ((static_cast<unsigned char>(c) & 0xC0) == 0x80)
            continue;  // UTF-8 continuation: one blank per character
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        out += draw::Renderable(std::string(1, up)) ? up : ' ';
    }
    return out;
}

// Faces are laid out on a 144 px grid and drawn at `S` px (the device's own key size: nothing is resized after, so
// the pixel font and the lines stay sharp). The font scale is the largest whole number that fits.
namespace
{
struct Grid
{
    int s;
    double k;
    explicit Grid(int size) : s(size), k(size / static_cast<double>(kFaceSize)) {}
    [[nodiscard]] int operator()(double v) const
    {
        return static_cast<int>(std::lround(v * k));
    }
    [[nodiscard]] float F(double v) const
    {
        return static_cast<float>(v * k);
    }
    [[nodiscard]] int Font(int at144) const
    {
        return std::max(1, static_cast<int>(std::floor(at144 * k + 0.25)));
    }
};

void Place(Rgb& out, const Rgb& in, int x0, int y0);

// The pressed look (as Stream Deck Studio marks a press on the deck): a neon glow round the rounded rect (x, y, w, h)
// on black: a bright core at its edge, a halo fading outwards. Blue-violet: it stays clean on the LCD.
void Neon(Rgb& im, int x, int y, int w, int h, double radius, double unit)
{
    const double core = 1.4 * unit, halo = 3.2 * unit;
    const double cx = x + w / 2.0, cy = y + h / 2.0, hx = w / 2.0 - radius, hy = h / 2.0 - radius;
    for (int py = 0; py < im.h; ++py)
        for (int px = 0; px < im.w; ++px)
        {
            const double qx = std::max(std::abs(px + 0.5 - cx) - hx, 0.0),
                         qy = std::max(std::abs(py + 0.5 - cy) - hy, 0.0);
            const double d = std::hypot(qx, qy) - radius;  // > 0 outside the rounded rect
            if (d < -0.5)
                continue;
            const double c =
                std::clamp(1.0 - std::abs(d - core * 0.5) / core, 0.0, 1.0);  // the bright core at the edge
            const double g = d > 0 ? std::exp(-d / halo) * 0.75 : 0.0;        // the halo
            auto* o = im.At(px, py);
            const double r = 190 * c + 120 * g, gg = 170 * c + 90 * g, b = 255 * c + 255 * g;
            o[0] = static_cast<std::uint8_t>(std::max<double>(o[0], std::min(255.0, r)));
            o[1] = static_cast<std::uint8_t>(std::max<double>(o[1], std::min(255.0, gg)));
            o[2] = static_cast<std::uint8_t>(std::max<double>(o[2], std::min(255.0, b)));
        }
}

// A pressed key: what it shows, at 86 % of the visible area on black, a neon frame round it.
template <class Draw>
Rgb PressedKey(int size, int safePad, Draw&& content)
{
    constexpr double kPressed = 0.86;
    const int vis = size - 2 * std::max(0, safePad);
    const int inner = std::max(8, static_cast<int>(std::lround(vis * kPressed)));
    const int off = std::max(0, safePad) + (vis - inner) / 2;
    Rgb out(size, size, 0, 0, 0);
    Place(out, content(inner), off, off);
    const double unit = size / 112.0;
    Neon(out, off, off, inner, inner, 7 * unit, unit);
    return out;
}

// `in` copied into `out` at (x, y), pixel for pixel
void Place(Rgb& out, const Rgb& in, int x0, int y0)
{
    for (int y = 0; y < in.h; ++y)
        for (int x = 0; x < in.w; ++x)
        {
            const auto* s = in.At(x, y);
            auto* d = out.At(x0 + x, y0 + y);
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
}
}  // namespace

Rgb RenderFace(const Face& f, const std::string& fallbackTitle, bool pressed, bool isMenu, int safePad, int size,
               const std::string& scenarioDir)
{
    if (pressed)
        return PressedKey(size, safePad,
                          [&](int s) { return RenderFace(f, fallbackTitle, false, isMenu, 0, s, scenarioDir); });
    if (safePad > 0 && f.image.empty() && size > 2 * safePad)
    {
        // the bezel hides the edges: draw the content straight at the visible size and centre it (no shrinking after,
        // which would blur the pixel font); a picture keeps the whole key
        Rgb out(size, size, f.bg.r, f.bg.g, f.bg.b);
        Place(out, RenderFace(f, fallbackTitle, false, isMenu, 0, size - 2 * safePad, scenarioDir), safePad, safePad);
        return out;
    }
    const Grid g(size);
    const int s = g.s;
    Rgb im(s, s, f.bg.r, f.bg.g, f.bg.b);
    std::string label =
        DisplayText(f.label.empty() && f.icon.empty() && f.svg.empty() && f.image.empty() ? fallbackTitle : f.label);
    while (!label.empty() && label.back() == ' ')
        label.pop_back();
    const bool hasLabel = !label.empty();
    if (!f.image.empty())
    {
        if (auto src = LoadImageRef(f.image, scenarioDir))
        {
            const auto* p = src->At(0, 0);
            im = FitImage(*src, s, s, FitByName(f.fit).value_or(Fit::Contain), p[0], p[1], p[2]);
        }
    }
    else if (!f.svg.empty() || !f.icon.empty())
    {
        const int sz = g(hasLabel ? 92 : 112);
        bool drawn = !f.svg.empty() && DrawSvg(im, data::Resolve(f.svg, scenarioDir), (s - sz) / 2,
                                               g(hasLabel ? 6 : 16), sz, f.fg, 0.08f);
        if (!drawn)
            if (auto ic = IconByName(f.icon))
                DrawIcon(im, *ic, (s - sz) / 2, g(hasLabel ? 8 : 16), sz, f.fg, {255, 215, 0});
    }
    if (hasLabel && f.image.empty())
    {
        const bool alone = f.icon.empty() && f.svg.empty();
        const int maxSc = g.Font(alone ? 4 : 3);
        const int sc = std::min(maxSc, FitScale(label, s - g(12), maxSc));
        const int y = alone ? (s - 7 * sc) / 2 : s - 7 * sc - g(8);
        draw::Text(im, std::max(2, (s - draw::TextWidth(label, sc)) / 2), y, label, sc, f.fg);
    }
    if (isMenu)
        draw::Tri(im, s - g.F(22), g.F(8), s - g.F(8), g.F(8), s - g.F(8), g.F(22), f.fg);  // corner mark: opens a menu
    return im;
}

Rgb RenderBackKey(const std::string& parentTitle, int depth, bool pressed, int safePad, int size)
{
    if (pressed)
        return PressedKey(size, safePad, [&](int s) { return RenderBackKey(parentTitle, depth, false, 0, s); });
    if (safePad > 0 && size > 2 * safePad)
    {  // as renderFace: drawn at the visible size, centred
        Rgb out(size, size, 24, 24, 36);
        Place(out, RenderBackKey(parentTitle, depth, false, 0, size - 2 * safePad), safePad, safePad);
        return out;
    }
    const Grid g(size);
    const int s = g.s;
    const draw::Color bg{24, 24, 36}, fg{230, 230, 240};
    Rgb im(s, s, bg.r, bg.g, bg.b);
    const int w = std::max(2, g(9));
    draw::Line(im, g(58), g(44), g(34), g(66), w, fg);  // "<"
    draw::Line(im, g(34), g(66), g(58), g(88), w, fg);
    draw::Line(im, g(40), g(66), g(108), g(66), w, fg);
    const std::string t = DisplayText(parentTitle);
    const int sc = FitScale(t, s - g(16), g.Font(3));
    draw::Text(im, std::max(2, (s - draw::TextWidth(t, sc)) / 2), g(100), t, sc, fg);
    const int dots = std::min(depth, 6);
    for (int i = 0; i < dots; ++i)
        draw::Disc(im, s / 2.f - (dots - 1) * g.F(8) + i * g.F(16), g.F(18), std::max(1.5f, g.F(4)),
                   i == depth - 1 ? draw::Color{0, 150, 255} : fg);
    return im;
}

Rgb RenderEmptyKey(int size)
{
    return Rgb(size, size, 0, 0, 0);
}

std::vector<Segment> CrumbSegments(const std::vector<std::string>& path, int stripW)
{
    std::vector<Segment> segs;
    if (path.empty())
        return segs;
    auto width = [](const std::string& t) { return draw::TextWidth(t, kCrumbScale) + 2 * kCrumbPad; };
    const int budget = stripW * 3 / 5;  // the right part stays for the menu's own content
    // keep the root and the newest levels; collapse the middle into ".."
    std::vector<int> keep;
    for (int i = 0; i < static_cast<int>(path.size()); ++i)
        keep.push_back(i);
    auto total = [&](const std::vector<int>& k, bool collapsed) {
        int w = kCrumbLeft;
        for (int i : k)
            w += width(DisplayText(path[static_cast<size_t>(i)])) + kCrumbGap;
        if (collapsed)
            w += width("..") + kCrumbGap;
        return w;
    };
    bool collapsed = false;
    while (keep.size() > 2 && total(keep, collapsed) > budget)
    {
        keep.erase(keep.begin() + 1);
        collapsed = true;
    }
    int x = kCrumbLeft;
    for (size_t k = 0; k < keep.size(); ++k)
    {
        const int i = keep[k];
        const std::string t = DisplayText(path[static_cast<size_t>(i)]);
        segs.push_back({x, x + width(t), i, t});
        x += width(t) + kCrumbGap;
        if (k == 0 && collapsed)
        {
            segs.push_back({x, x + width(".."), -1, ".."});
            x += width("..") + kCrumbGap;
        }
    }
    return segs;
}

int CrumbHit(const std::vector<std::string>& path, int stripW, int x)
{
    const auto segs = CrumbSegments(path, stripW);
    for (size_t i = 0; i < segs.size(); ++i)
    {
        const int lo = i == 0 ? 0 : segs[i].x0 - kCrumbGap / 2,
                  hi = i + 1 == segs.size() ? segs[i].x1 + kCrumbGap / 2 : segs[i].x1 + kCrumbGap / 2;
        if (x >= lo && x < hi)
            return segs[i].depth;
    }
    return -1;
}

// ---------------------------------------------------------------- the surface client's strip (StripLive)
namespace
{
// The UnrealNG look on a dark strip: grey-blue words, the theme's blue for what has been played, cyan for counters,
// amber for marks, a muted red for recording only. Fine lines (1-2 px), no fills behind the timeline.
constexpr draw::Color kLiveText{200, 205, 220}, kLiveDim{120, 128, 158}, kLiveFaint{70, 76, 104},
    kLiveTrack{52, 58, 84}, kLivePlayed{0, 110, 200}, kLiveCounter{90, 200, 230}, kLiveMark{200, 160, 60},
    kLiveRec{205, 70, 58}, kLivePos{235, 240, 250}, kLiveAmber{220, 170, 60};

std::string TimeCode(long long frames, double fps)
{
    if (fps <= 0)
        fps = 50;
    const double sec = std::max(0LL, frames) / fps;
    const int whole = static_cast<int>(sec);
    return sdb::Format("{:02}:{:02}.{:02}", whole / 60, whole % 60,
                       std::min(static_cast<int>(fps) - 1, static_cast<int>(std::fmod(sec, 1.0) * fps)));
}

long long Num(const json& j, const char* key)
{
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<long long>() : 0;
}

// The TTD timeline over [x0, x1) of the strip's area (height areaH); the counters end before `rightEnd`
void DrawTimeline(Rgb& im, int x0, int x1, int areaH, int rightEnd, const json& t, const json* marks)
{
    const long long first = Num(t, "first"), end = std::max(first, Num(t, "end"));
    const long long frame = std::clamp(Num(t, "frame"), first, end);
    const std::string state = t.value("state", "");
    const bool history = t.value("history", end > first);
    const double fps = t.contains("fps") && t["fps"].is_number() ? t["fps"].get<double>() : 50.0;
    const int top = areaH >= 90 ? 12 : 6;
    const int track = areaH - 24;

    // the header: TTD, its state, then the frame counter and the time code at the right
    int x = x0 + draw::Text(im, x0, top, "TTD", 2, kLiveText) + 14;
    std::string word = "NO HISTORY";
    draw::Color wordColor = kLiveFaint;
    if (t.contains("available") && !t.value("available", true))
        word = "NOT AVAILABLE";
    else if (state == "recording")
        word = "REC", wordColor = kLiveRec;
    else if (state == "detached")
        word = "REPLAY", wordColor = kLiveCounter;
    else if (history)
        word = t.value("paused", false) ? "PAUSED" : "AT END", wordColor = kLiveDim;
    draw::Text(im, x, top, word, 2, wordColor);
    const long long at = frame - first, len = end - first;
    if (history)
    {
        const std::string frames = sdb::Format("{:06} / {:06}", at, len);
        const std::string code = TimeCode(at, fps) + " / " + TimeCode(len, fps);
        const int codeW = draw::TextWidth(code, 2), framesW = draw::TextWidth(frames, 2);
        const int right = std::min(rightEnd, x1);
        if (right - codeW - framesW - 28 > x + draw::TextWidth(word, 2) + 20)
        {
            draw::Text(im, right - codeW, top, code, 2, kLiveDim);
            draw::Text(im, right - codeW - 28 - framesW, top, frames, 2, kLiveCounter);
        }
    }

    // the track: the recorded span, a ruler of seconds under it, the played part, the marks, the position
    const int span = std::max(1, x1 - x0);
    draw::FillRect(im, x0, track, span, 1, kLiveTrack);
    if (!history || len <= 0)
        return;
    auto xOf = [&](long long f) {
        return x0 + static_cast<int>(static_cast<double>(span - 1) * static_cast<double>(f - first) / len);
    };
    const double seconds = len / fps;
    const double steps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 1800, 3600};
    double step = steps[0];
    for (double candidate : steps)
        if (step = candidate; seconds / candidate <= 40)
            break;
    for (double sec = step; sec < seconds; sec += step)
        draw::FillRect(im, xOf(first + static_cast<long long>(sec * fps)), track + 4, 1, 3, kLiveFaint);
    draw::Text(im, x0, track + 10, TimeCode(0, fps).substr(0, 5), 1, kLiveFaint);
    const std::string last = TimeCode(len, fps).substr(0, 5);
    draw::Text(im, x1 - draw::TextWidth(last, 1), track + 10, last, 1, kLiveFaint);
    const int px = std::clamp(xOf(frame), x0, x1 - 2);
    draw::FillRect(im, x0, track, px - x0, 2, kLivePlayed);
    if (marks && marks->is_array())
        for (const auto& m : *marks)
        {
            if (!m.is_object())
                continue;
            const long long f = Num(m, "frame");
            if (f < first || f > end)
                continue;
            const std::string kind = m.value("kind", "");
            const bool strong = kind == "bookmark" || kind == "barrier" || kind == "edge";
            draw::FillRect(im, xOf(f), track - (strong ? 7 : 5), 1, strong ? 6 : 4, strong ? kLiveMark : kLiveDim);
        }
    draw::FillRect(im, px, track - 9, 2, 13, state == "recording" ? kLiveRec : kLivePos);
}

// run.state as status words
std::pair<std::string, draw::Color> RunWords(const json* r)
{
    if (!r || !r->is_object())
        return {"", kLiveDim};
    const std::string state = r->value("state", "");
    if (state == "paused")
        return {"PAUSED", kLiveAmber};
    if (state == "stopped")
        return {"STOPPED", kLiveDim};
    if (r->value("turbo", false))
        return {"RUNNING  TURBO", kLiveCounter};
    const long long speed = Num(*r, "speed");
    return {speed > 1 ? sdb::Format("RUNNING  X{}", speed) : std::string("RUNNING"), kLiveCounter};
}

// media.tape as status words; progress 0..1 (-1 none)
std::pair<std::string, double> TapeWords(const json* t)
{
    if (!t || !t->is_object())
        return {"", -1};
    if (!t->value("loaded", false))
        return {"NO TAPE", -1};
    std::string name = t->value("name", "");
    if (const auto slash = name.find_last_of("/\\"); slash != std::string::npos)
        name = name.substr(slash + 1);
    std::string line = "TAPE  " + name;
    if (const long long blocks = Num(*t, "blocks"); blocks > 0)
        line += sdb::Format("  {}/{}", Num(*t, "block") + 1, blocks);
    line += t->value("playing", false) ? "  PLAYING" : "  STOPPED";
    const double progress = t->contains("progress") && (*t)["progress"].is_number() ? (*t)["progress"].get<double>()
                                                                                    : -1;
    return {line, progress};
}
}  // namespace

std::string TargetLine(const json& target)
{
    if (!target.is_object() || !target["instance"].is_string())
        return "NO MACHINE";
    std::string line = target["model"].is_string() ? target["model"].get<std::string>() : std::string("MACHINE");
    if (target["ram_kb"].is_number())
        line += sdb::Format(" {}K", target["ram_kb"].get<long long>());
    if (target["index"].is_number())
        line += sdb::Format(" ({})", target["index"].get<long long>());  // the WebAPI index (the font has no #)
    if (target.value("mode", "") == "instance")
        line += " PINNED";
    return line;
}

std::string StripLive::Key() const
{
    std::string k = std::to_string(static_cast<int>(link)) + (showTarget ? "t" : "-") + notice + "|" + activity;
    if (target)
        k += target->dump();
    if (readouts)
        for (const auto& [name, value] : *readouts)
            k += name + value.dump();
    return k;
}

Rgb RenderStrip(const Page& page, int w, int h, int dialTileW, const std::string& banner, double autoExitLeft,
                const std::string& status, const std::string& scenarioDir, const StripLive* live)
{
    Rgb im(w, h, 8, 8, 14);
    int contentX = 12;
    const bool dialLabels =
        std::any_of(page.dials.begin(), page.dials.end(), [](const Dial* d) { return d && !d->label.empty(); });
    const int bandH = dialLabels ? 22 : 0;
    const int areaH = h - bandH;
    if (page.crumbs == Crumbs::Strip)
    {
        const auto segs = CrumbSegments(page.path, w);
        const int ch = 7 * kCrumbScale + 16, cy = (areaH - ch) / 2;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            const auto& s = segs[i];
            const bool cur = s.depth == static_cast<int>(page.path.size()) - 1;
            draw::RoundRect(im, static_cast<float>(s.x0), static_cast<float>(cy), static_cast<float>(s.x1 - s.x0),
                            static_cast<float>(ch), 10.f, cur ? kChipCur : kChip);
            draw::Text(im, s.x0 + kCrumbPad, cy + 8, s.text, kCrumbScale, kChipText);
            if (i + 1 < segs.size())
                draw::Text(im, s.x1 + (kCrumbGap - 5 * 2) / 2, cy + 11, ">", 2, kSep);
        }
        if (!segs.empty())
            contentX = segs.back().x1 + kCrumbGap;
    }
    // the status line (time travel): it takes the content area, one line as large as its longest form fits, laid out
    // from the left at a fixed place (the font is monospaced): changing digits never move anything
    const bool scrubs = std::any_of(page.dials.begin(), page.dials.end(),
                                    [](const Dial* d) {
                                        return d && d->turn &&
                                               (d->turn->kind == "ttd_seek" ||
                                                (d->turn->kind == "action" &&
                                                 d->turn->spec.value("id", "") == "ttd.seek_by"));
                                    });
    const bool statusLine = !status.empty() || (page.strip && !page.strip->status.empty()) || scrubs;
    // the surface client's readouts (live): the timeline, the status words, the target line
    const bool liveOn = live && live->link == StripLive::Link::Live;
    auto readout = [&](const char* name) -> const json* {
        if (!liveOn || !live->readouts)
            return nullptr;
        auto it = live->readouts->find(name);
        return it != live->readouts->end() ? &it->second : nullptr;
    };
    const std::string statusKind = page.strip ? page.strip->status : std::string();
    const json* ttdPosition = readout("ttd.position");
    const bool timeline = ttdPosition && (statusKind == "ttd" || scrubs);
    std::string words;
    draw::Color wordsColor = kLiveText;
    double progress = -1;
    if (liveOn && !timeline)
    {
        if (statusKind == "run")
            std::tie(words, wordsColor) = RunWords(readout("run.state"));
        else if (statusKind == "tape")
            std::tie(words, progress) = TapeWords(readout("media.tape"));
        else if (statusKind == "target" && live->target)
            words = TargetLine(*live->target);
    }
    std::string targetLine;
    if (live && live->showTarget && live->link != StripLive::Link::None && statusKind != "target")
        targetLine = live->link == StripLive::Link::Lost ? std::string("RECONNECTING")
                                                         : (live->target ? TargetLine(*live->target) : std::string());
    const int targetW = targetLine.empty() ? 0 : draw::TextWidth(DisplayText(targetLine), 2) + 24;
    if (timeline && banner.empty())
    {
        auto marks = live->readouts->find("ttd.marks");
        DrawTimeline(im, contentX, w - 12, areaH, w - 12 - targetW, *ttdPosition,
                     marks != live->readouts->end() ? &marks->second : nullptr);
    }
    else if (!words.empty() && banner.empty())
    {
        const std::string t = DisplayText(words);
        const int avail = w - 12 - contentX - targetW;
        const int sc = FitScale(t, avail, 4);
        const int y = (areaH - 7 * sc) / 2;
        draw::Text(im, contentX, y, t, sc, wordsColor);
        if (progress >= 0)
        {
            const int tw = draw::TextWidth(t, sc), py = y + 7 * sc + 8;
            draw::FillRect(im, contentX, py, tw, 1, kLiveTrack);
            draw::FillRect(im, contentX, py, static_cast<int>(tw * std::clamp(progress, 0.0, 1.0)), 2, kLivePlayed);
        }
    }
    else if (statusLine && banner.empty())
    {
        constexpr size_t kStatusChars = 36;
        const int avail = w - 12 - contentX;
        const int sc = FitScale(std::string(std::max(kStatusChars, status.size()), '0'), avail, 5);
        // colour by part, each word at its own column: the state (REC red, else violet), frames amber, time codes cyan
        const std::string t = DisplayText(status);
        const int y = (areaH - 7 * sc) / 2;
        const bool rec = t.rfind("REC", 0) == 0;
        for (size_t a = 0; a < t.size();)
        {
            if (t[a] == ' ')
            {
                ++a;
                continue;
            }
            size_t b = t.find(' ', a);
            if (b == std::string::npos)
                b = t.size();
            const std::string word = t.substr(a, b - a);
            const bool digits = std::isdigit(static_cast<unsigned char>(word[0])) != 0;
            // REC red, the state light grey, frames violet, time codes amber (as an editing suite's time code); a dim
            // halo one pixel round every stroke
            const draw::Color c = !digits ? (rec ? kLiveRec : draw::Color{205, 205, 220})
                                  : word.find(':') != std::string::npos ? kLiveAmber
                                                                        : kLiveCounter;
            const draw::Color halo{static_cast<std::uint8_t>(c.r * 0.35), static_cast<std::uint8_t>(c.g * 0.35),
                                   static_cast<std::uint8_t>(c.b * 0.35)};
            const int x = contentX + (a ? draw::TextWidth(t.substr(0, a) + "0", sc) - draw::TextWidth("0", sc) : 0);
            for (const auto& [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}})
                draw::Text(im, x + dx, y + dy, word, sc, halo);
            draw::Text(im, x, y, word, sc, c);
            a = b;
        }
    }
    const int right = statusLine ? contentX : w - 12;  // the line owns the area: no static text beside it
    // the menu's own content in the remaining area
    const int cw = right - contentX;
    if (!banner.empty())
    {
        const std::string t = DisplayText(banner);
        const int bw = w - contentX - 12;  // the whole area after the crumbs, whatever else the strip shows
        const int sc = FitScale(t, bw, 5);
        draw::FillRect(im, contentX - 6, 0, w - contentX + 6, areaH, {120, 10, 20});
        draw::Text(im, contentX + std::max(0, (bw - draw::TextWidth(t, sc)) / 2), (areaH - 7 * sc) / 2, t, sc,
                   {255, 255, 255});
    }
    else if (page.strip && cw > 40)
    {
        const std::string& c = page.strip->content;
        std::string text;
        if (c == "title")
            text = page.path.empty() ? std::string() : page.path.back();
        else if (c == "text")
            text = page.strip->text;
        else if (c == "breadcrumbs" && page.crumbs != Crumbs::Strip)
        {
            for (size_t i = 0; i < page.path.size(); ++i)
                text += (i ? " > " : "") + page.path[i];
        }
        if (c == "title" && page.crumbs == Crumbs::Strip)
            text.clear();  // the highlighted chip already names the menu
        if (c == "title" && !page.strip->text.empty())
            text = page.strip->text;
        // live placeholders: $left = seconds until auto exit, $title = the menu
        auto sub = [&](const std::string& key, const std::string& val) {
            for (size_t p = text.find(key); p != std::string::npos; p = text.find(key, p + val.size()))
                text.replace(p, key.size(), val);
        };
        const int secs = autoExitLeft >= 0 ? static_cast<int>(std::ceil(autoExitLeft)) : page.autoExit;
        sub("$left", std::to_string(secs));
        sub("$title", page.path.empty() ? std::string() : page.path.back());
        if (!text.empty())
        {
            const std::string t = DisplayText(text);
            const int sc = FitScale(t, cw, 6);
            draw::Text(im, contentX + std::max(0, (cw - draw::TextWidth(t, sc)) / 2), (areaH - 7 * sc) / 2, t, sc,
                       {230, 230, 240});
        }
        else if (c == "image" && !page.strip->image.empty())
        {
            // a picture takes its whole area, edge to edge: from the crumbs (or the left edge) to the right edge
            const int x0 = page.crumbs == Crumbs::Strip ? contentX : 0, iw = w - x0;
            if (auto src = LoadImageRef(page.strip->image, scenarioDir); src && iw > 0)
                Blit(im, FitImage(*src, iw, areaH, Fit::Contain), x0, 0);
        }
    }
    if (autoExitLeft >= 0 && page.autoExit > 0)
    {  // auto exit countdown: a bar along the top that runs out, and the seconds
        const double f = std::clamp(autoExitLeft / page.autoExit, 0.0, 1.0);
        const draw::Color c = f > 0.4 ? draw::Color{240, 170, 0} : draw::Color{240, 60, 40};
        // sub-pixel end: the last column is blended, so the bar glides instead of stepping
        const double end = w * f;
        const int whole = static_cast<int>(end);
        const draw::Color track{40, 30, 10};
        draw::FillRect(im, 0, 0, w, 6, track);
        draw::FillRect(im, 0, 0, whole, 6, c);
        if (whole < w)
            draw::FillRect(im, whole, 0, 1, 6, draw::Mix(track, c, static_cast<float>(end - whole)));
        const std::string t = "BACK IN " + std::to_string(static_cast<int>(std::ceil(autoExitLeft)));
        draw::Text(im, w - draw::TextWidth(t, 2) - 10, 10, t, 2, c);
    }
    if (!targetLine.empty() && banner.empty())
    {
        const std::string t = DisplayText(targetLine);
        draw::Text(im, w - 12 - draw::TextWidth(t, 2), areaH >= 90 ? 12 : 6, t, 2,
                   live->link == StripLive::Link::Lost ? kLiveAmber : kLiveDim);
    }
    if (live && (!live->notice.empty() || !live->activity.empty()) && banner.empty())
    {
        // a notice stays (revoked) and wins; an activity line is quiet
        const bool notice = !live->notice.empty();
        const std::string t = DisplayText(notice ? live->notice : live->activity);
        const int bw = w - contentX - 12;
        const int sc = FitScale(t, bw, notice ? 4 : 3);
        draw::FillRect(im, contentX - 6, 0, w - contentX + 6, areaH, notice ? draw::Color{44, 20, 22}
                                                                            : draw::Color{14, 18, 30});
        if (notice)
            draw::FillRect(im, contentX - 6, areaH - 2, w - contentX + 6, 1, kLiveRec);
        draw::Text(im, contentX + std::max(0, (bw - draw::TextWidth(t, sc)) / 2), (areaH - 7 * sc) / 2, t, sc,
                   notice ? draw::Color{235, 215, 205} : kLiveText);
    }
    if (dialLabels)
    {
        for (size_t i = 0; i < page.dials.size(); ++i)
        {
            const Dial* d = page.dials[i];
            if (!d || d->label.empty())
                continue;
            const int x0 = static_cast<int>(i) * dialTileW;
            const std::string t = DisplayText(d->label);
            draw::FillRect(im, x0 + 2, h - bandH, dialTileW - 4, bandH, {20, 20, 34});
            draw::Text(im, x0 + std::max(2, (dialTileW - draw::TextWidth(t, 2)) / 2), h - bandH + 4, t, 2,
                       {150, 200, 255});
        }
    }
    return im;
}

}  // namespace sdb::scn
