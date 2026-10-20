// Hardware benchmarks: how fast does the firmware actually accept images / commands, and does reading stay responsive?
//   sdctl bench keys    [--keys N|all] [--content flat|gradient|noise] [--quality Q] [--seconds S] [--fps F]
//   sdctl bench matrix  [--seconds S]                 sweep content x quality x key-count
//   sdctl bench lcd     [--seconds S]                 full strip / dial tile / 100x100 region
//   sdctl bench feature [--count N]                   brightness feature-report latency
//   sdctl bench mixed   [--seconds S]                 key writer thread + event reader: read-gap under write load
// All numbers are HOST-side timings of blocking hid_write() calls. hid_write returns when the OS/USB stack has
// handed the report to the device, so sustained throughput == what the firmware consumes (USB flow control NAKs when it
// is busy).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <map>
#include <numeric>
#include <random>
#include <string_view>
#include <thread>

#include "cli.h"
#include "font5x7.h"
#include "sdb/protocol.h"
#include "sdb/theme.h"

using namespace sdb;
using clk = std::chrono::steady_clock;

namespace cli
{
namespace
{

struct Stat
{
    std::vector<double> v;  // microseconds
    void Add(double us)
    {
        v.push_back(us);
    }
    double Pct(double p)
    {
        if (v.empty())
            return 0;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, static_cast<size_t>(p * (v.size() - 1) + 0.5))];
    }
    double Mean() const
    {
        return v.empty() ? 0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    }
    void Print(const char* name)
    {
        std::printf("  %-22s n=%-6zu mean %8.0f us  p50 %8.0f  p95 %8.0f  p99 %8.0f  max %8.0f\n", name, v.size(),
                    Mean(), Pct(.5), Pct(.95), Pct(.99), Pct(1.0));
    }
};

Rgb MakeFrame(const std::string& content, int w, int h, int i, std::mt19937& rng)
{
    Rgb im(w, h);
    if (content == "flat")
    {
        const std::uint8_t c = static_cast<std::uint8_t>((i * 37) & 0xff);
        return Rgb(w, h, c, static_cast<std::uint8_t>(255 - c), 128);
    }
    if (content == "noise")
    {
        for (auto& p : im.px)
            p = static_cast<std::uint8_t>(rng());
        return im;
    }
    // "gradient": smooth colour field + moving bar + frame number (typical UI-like content)
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            auto* p = im.At(x, y);
            p[0] = static_cast<std::uint8_t>((x * 255 / w + i * 7) & 0xff);
            p[1] = static_cast<std::uint8_t>(y * 255 / h);
            p[2] = static_cast<std::uint8_t>(128 + 100 * std::sin((x + i * 3) * 0.08));
        }
    FillRect(im, (i * 5) % std::max(1, w - 10), 0, 10, h, 255, 255, 255);
    DrawText(im, 4, h / 2 - 7, std::to_string(i % 1000), std::max(1, h / 40), 0, 0, 0);
    return im;
}

struct RunResult
{
    double imagesPerSec = 0, mbPerSec = 0, avgBytes = 0, reportsPerImage = 0;
    Stat writeUs, imageUs;
    std::size_t images = 0;
};

// Streams pre-encoded frames to a set of keys for `seconds`; optionally paced to `fps` per key.
RunResult RunKeys(Device& dev, const std::vector<int>& keys, const std::vector<std::vector<std::uint8_t>>& frames,
                  double seconds, double fps)
{
    RunResult r;
    const Model& m = dev.GetModel();
    std::size_t bytes = 0, reports = 0;
    const auto t0 = clk::now();
    auto next = t0;
    std::size_t i = 0;
    while (std::chrono::duration<double>(clk::now() - t0).count() < seconds)
    {
        if (fps > 0)
        {
            std::this_thread::sleep_until(next);
            next += std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(1.0 / (fps * keys.size())));
        }
        const int key = keys[i % keys.size()];
        const auto& enc = frames[i % frames.size()];
        const auto reps = proto::KeyImage(m, key, enc);
        const auto ti = clk::now();
        for (const auto& rep : reps)
        {
            const auto tw = clk::now();
            if (dev.GetTransport().Write(rep) < 0)
            {
                std::fprintf(stderr, "write failed: %s\n", dev.GetTransport().LastError().c_str());
                return r;
            }
            r.writeUs.Add(std::chrono::duration<double, std::micro>(clk::now() - tw).count());
            bytes += rep.size();
            ++reports;
        }
        r.imageUs.Add(std::chrono::duration<double, std::micro>(clk::now() - ti).count());
        ++r.images;
        ++i;
    }
    const double el = std::chrono::duration<double>(clk::now() - t0).count();
    r.imagesPerSec = r.images / el;
    r.mbPerSec = bytes / el / 1e6;
    r.avgBytes = r.images ? static_cast<double>(bytes) / r.images : 0;
    r.reportsPerImage = r.images ? static_cast<double>(reports) / r.images : 0;
    return r;
}

std::vector<std::vector<std::uint8_t>> EncodeFrames(const Model& m, const std::string& content, int quality, int n,
                                                    double* avgJpegBytes = nullptr, double* encodeUs = nullptr)
{
    std::mt19937 rng(1234);
    std::vector<std::vector<std::uint8_t>> out;
    double total = 0;
    const auto t0 = clk::now();
    for (int i = 0; i < n; ++i)
    {
        out.push_back(EncodeForFormat(MakeFrame(content, 144, 144, i, rng), m.key, quality));
        total += out.back().size();
    }
    if (avgJpegBytes)
        *avgJpegBytes = total / n;
    if (encodeUs)
        *encodeUs = std::chrono::duration<double, std::micro>(clk::now() - t0).count() / n;
    return out;
}

std::vector<int> KeyRange(const Model& m, const std::string& spec)
{
    int n = spec == "all" ? m.keys : std::max(1, std::min<int>(std::atoi(spec.c_str()), m.keys));
    std::vector<int> k(n);
    std::iota(k.begin(), k.end(), 0);
    return k;
}

int BenchKeys(const Args& a)
{
    auto dev = OpenOrExplain(a);
    if (!dev)
        return 1;
    const Model& m = dev->GetModel();
    if (!m.HasKeyScreens())
    {
        std::fprintf(stderr, "no key screens\n");
        return 1;
    }
    dev->SetBrightness(60);
    const auto keys = KeyRange(m, a.Str("keys", "1"));
    const std::string content = a.Str("content", "gradient");
    const int q = a.Num("quality", 90);
    double avgBytes = 0, encUs = 0;
    const auto frames = EncodeFrames(m, content, q, 16, &avgBytes, &encUs);
    std::printf(
        "%s | keys=%zu content=%s q=%d | frame %.0f B (%.1f reports), host JPEG encode %.0f us/frame | pace %s\n",
        std::string(m.name).c_str(), keys.size(), content.c_str(), q, avgBytes, std::ceil(avgBytes / 1016), encUs,
        a.Has("fps") ? (a.Str("fps") + " fps/key").c_str() : "unpaced");
    auto r = RunKeys(*dev, keys, frames, a.Dbl("seconds", 3), a.Dbl("fps", 0));
    std::printf("  images/s total %.1f  | per key %.1f fps | %.2f MB/s | %.1f reports/image\n", r.imagesPerSec,
                r.imagesPerSec / keys.size(), r.mbPerSec, r.reportsPerImage);
    r.writeUs.Print("hid_write per report");
    r.imageUs.Print("per image (all reports)");
    return 0;
}

int BenchMatrix(const Args& a)
{
    auto dev = OpenOrExplain(a);
    if (!dev)
        return 1;
    const Model& m = dev->GetModel();
    if (!m.HasKeyScreens())
        return 1;
    dev->SetBrightness(60);
    const double secs = a.Dbl("seconds", 1.5);
    std::printf("%s - sweep (%.1f s per cell). img/s = images the firmware accepted per second, summed over keys.\n",
                std::string(m.name).c_str(), secs);
    std::printf("%-9s %3s %6s %9s %5s | %9s %9s %8s %10s\n", "content", "q", "keys", "frameB", "rpts", "img/s",
                "fps/key", "MB/s", "p50 wr us");
    const std::vector<std::string> contents{"flat", "gradient", "noise"};
    const std::vector<int> qualities{30, 60, 90};
    std::vector<std::string> keySets{"1", "8", "all"};
    for (const auto& c : contents)
        for (int q : qualities)
        {
            double avg = 0;
            const auto frames = EncodeFrames(m, c, q, 16, &avg);
            for (const auto& ks : keySets)
            {
                const auto keys = KeyRange(m, ks);
                auto r = RunKeys(*dev, keys, frames, secs, 0);
                std::printf("%-9s %3d %6zu %9.0f %5.1f | %9.1f %9.2f %8.2f %10.0f\n", c.c_str(), q, keys.size(), avg,
                            r.reportsPerImage, r.imagesPerSec, r.imagesPerSec / keys.size(), r.mbPerSec,
                            r.writeUs.Pct(.5));
                std::fflush(stdout);
            }
        }
    return 0;
}

int BenchLcd(const Args& a)
{
    auto dev = OpenOrExplain(a);
    if (!dev)
        return 1;
    const Model& m = dev->GetModel();
    if (!m.HasLcd())
    {
        std::fprintf(stderr, "no LCD\n");
        return 1;
    }
    dev->SetBrightness(60);
    const double secs = a.Dbl("seconds", 3);
    std::mt19937 rng(7);
    struct Case
    {
        const char* name;
        int w, h;
        bool full;
    };
    const int w = m.lcdLogicalW, h = m.lcdLogicalH;
    std::vector<Case> cases{{"full strip", w, h, true}};
    if (m.lcdKind == LcdKind::Region)
    {
        cases.push_back({"dial tile", m.dialTileW, h, false});
        cases.push_back({"100x100 block", std::min(100, h), std::min(100, h), false});
        cases.push_back({"50x50 block", 50, 50, false});
    }
    std::printf("%s LCD logical %dx%d  device %dx%d rot%d\n", std::string(m.name).c_str(), w, h, m.lcd.w, m.lcd.h,
                Degrees(dev->LcdRotation()));
    for (const auto& c : cases)
        for (const std::string& content : {std::string("gradient"), std::string("noise")})
        {
            std::vector<Rgb> imgs;
            for (int i = 0; i < 8; ++i)
                imgs.push_back(MakeFrame(content, c.w, c.h, i, rng));
            Stat per;
            std::size_t n = 0, bytes = 0, reports = 0;
            const auto t0 = clk::now();
            while (std::chrono::duration<double>(clk::now() - t0).count() < secs)
            {
                const auto s =
                    c.full ? dev->SetLcdFull(imgs[n % imgs.size()])
                           : dev->SetLcdRegion((static_cast<int>(n) * 20) % (w - c.w + 1), 0, imgs[n % imgs.size()]);
                per.Add(std::chrono::duration<double, std::micro>(s.elapsed).count());
                bytes += s.bytes;
                reports += s.reports;
                ++n;
            }
            const double el = std::chrono::duration<double>(clk::now() - t0).count();
            std::printf("  %-14s %-8s %7.1f upd/s  %6.2f MB/s  %5.1f reports/upd  write mean %.0f us\n", c.name,
                        content.c_str(), n / el, bytes / el / 1e6, static_cast<double>(reports) / n, per.Mean());
        }
    return 0;
}

int BenchFeature(const Args& a)
{
    auto dev = OpenOrExplain(a);
    if (!dev)
        return 1;
    const int n = a.Num("count", 300);
    Stat s;
    for (int i = 0; i < n; ++i)
    {
        const auto t = clk::now();
        dev->SetBrightness(50 + (i & 1));
        s.Add(std::chrono::duration<double, std::micro>(clk::now() - t).count());
    }
    s.Print("brightness feature rpt");
    std::printf("  => %.0f commands/s\n", 1e6 / s.Mean());
    dev->SetBrightness(60);
    return 0;
}

int BenchMixed(const Args& a)
{
    auto dev = OpenOrExplain(a);
    if (!dev)
        return 1;
    const Model& m = dev->GetModel();
    if (!m.HasKeyScreens())
        return 1;
    const double secs = a.Dbl("seconds", 5);
    dev->SetBrightness(60);
    auto readGaps = [&](double s, int timeout, Stat& gaps, std::size_t& events) {
        const auto t0 = clk::now();
        auto last = t0;
        while (std::chrono::duration<double>(clk::now() - t0).count() < s)
        {
            events += dev->PollEvents(timeout).size();
            const auto now = clk::now();
            gaps.Add(std::chrono::duration<double, std::micro>(now - last).count());
            last = now;
        }
    };
    Stat idleGaps, loadGaps;
    std::size_t idleEv = 0, loadEv = 0;
    std::printf("phase 1: reader only (%.0f s, 10 ms timeout) - baseline. Press keys now if you like.\n", secs / 2);
    readGaps(secs / 2, 10, idleGaps, idleEv);

    const auto keys = KeyRange(m, "all");
    std::mt19937 rng(3);
    const auto frames = EncodeFrames(m, "gradient", 60, 16);
    std::atomic<bool> stop{false};
    RunResult wr;
    std::thread writer([&] {
        wr = RunKeys(*dev, keys, frames, secs, 0);
        stop = true;
    });
    std::printf("phase 2: reader + writer thread streaming all %zu keys (%.0f s)\n", keys.size(), secs);
    while (!stop)
        readGaps(0.25, 10, loadGaps, loadEv);
    writer.join();
    idleGaps.Print("read gap, idle");
    loadGaps.Print("read gap, under load");
    std::printf("  writer under concurrent read: %.1f img/s (%.2f fps/key)\n  events seen: idle=%zu load=%zu\n",
                wr.imagesPerSec, wr.imagesPerSec / keys.size(), idleEv, loadEv);
    std::printf("  verdict: %s\n", loadGaps.Pct(1.0) < 40000
                                       ? "reads stay responsive (<40 ms worst gap) while writing from another thread"
                                       : "reads STALL under write load - serialise access or use a separate handle");
    return 0;
}
// Emulator-style video on a key block: every frame is laid out on the block's physical geometry, cut per key, JPEG
// encoded, and only keys whose bytes changed are sent ("partial refresh"). Reports achieved fps against a target.
//   sdctl bench block [--image F] [--rows 0-2 --cols 0-3] [--mode scroll|sprite|flip] [--fps 50] [--seconds 5]
//   [--quality 85]
int BenchBlock(const Args& a)
{
    auto dev = OpenOrExplain(a);
    if (!dev)
        return 1;
    const Model& m = dev->GetModel();
    const std::string path = a.Str("image", "examples/.cache/zx/dizzyx.png");
    auto src0 = LoadImage(path);
    if (!src0)
    {
        std::fprintf(stderr, "error: cannot read %s\n", path.c_str());
        return 4;
    }
    const Rgb src = TrimBorder(*src0);
    GridSpec g;
    auto range = [](const std::string& t, int& lo, int& hi) {
        const auto d = t.find('-');
        lo = std::stoi(t.substr(0, d));
        hi = d == std::string::npos ? lo : std::stoi(t.substr(d + 1));
    };
    range(a.Str("rows", "0-2"), g.r0, g.r1);
    range(a.Str("cols", "0-3"), g.c0, g.c1);
    g.fit = Fit::Contain;
    const std::string mode = a.Str("mode", "scroll");
    const double fps = a.Dbl("fps", 50), seconds = a.Dbl("seconds", 5);
    const int q = a.Num("quality", 85);
    dev->SetBrightness(60);

    std::map<int, std::size_t> lastHash;
    std::size_t frames = 0, keysSent = 0, reports = 0, late = 0;
    Stat frameUs, encodeUs, sendUs;
    const auto period = std::chrono::duration<double>(1.0 / fps);
    using Encoded = std::vector<std::pair<int, std::vector<std::uint8_t>>>;
    // frame n: build -> cut per key -> encode every key in parallel. Runs on a worker while frame n-1 goes over USB.
    auto prepare = [&](std::size_t n) {
        Rgb frame = src;
        if (mode == "scroll")
        {  // whole picture moves 2 px per frame: every key changes
            const int sh = static_cast<int>(n * 2) % src.w;
            for (int y = 0; y < src.h; ++y)
                for (int x = 0; x < src.w; ++x)
                    std::memcpy(frame.At(x, y), src.At((x + sh) % src.w, y), 3);
        }
        else if (mode == "sprite")
        {  // a 16x16 sprite crosses a static screen: 1-2 keys change
            const int sx = static_cast<int>(n * 3) % (src.w - 16), sy = src.h / 2;
            draw::FillRect(frame, sx, sy, 16, 16, {255, 255, 0});
        }
        else if (mode == "flip")
        {  // screen alternates with its negative: all keys, full detail
            if (n & 1)
                for (auto& v : frame.px)
                    v = static_cast<std::uint8_t>(255 - v);
        }
        auto tiles = SliceGrid(m, frame, g);
        std::vector<std::future<std::vector<std::uint8_t>>> jobs;
        for (auto& t : tiles)
            jobs.push_back(std::async(std::launch::async, [&m, &t, q] { return EncodeForFormat(t.second, m.key, q); }));
        Encoded out;
        for (size_t i = 0; i < tiles.size(); ++i)
            out.emplace_back(tiles[i].first, jobs[i].get());
        return out;
    };
    const auto t0 = clk::now();
    auto next = t0;
    auto pending = std::async(std::launch::async, prepare, std::size_t{0});
    while (std::chrono::duration<double>(clk::now() - t0).count() < seconds)
    {
        const auto tf = clk::now();
        Encoded enc = pending.get();
        const auto ts = clk::now();
        pending = std::async(std::launch::async, prepare, frames + 1);  // overlap: next frame encodes during this send
        std::size_t sent = 0;
        for (auto& [key, bytes] : enc)
        {
            const std::size_t h = std::hash<std::string_view>{}(
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            if (lastHash[key] == h)
                continue;  // partial refresh: unchanged keys are skipped
            lastHash[key] = h;
            auto st = dev->SetKeyRaw(key, bytes);
            reports += st.reports;
            ++sent;
        }
        const auto td = clk::now();
        encodeUs.Add(std::chrono::duration<double, std::micro>(ts - tf).count());  // time waiting for the encoder
        sendUs.Add(std::chrono::duration<double, std::micro>(td - ts).count());
        frameUs.Add(std::chrono::duration<double, std::micro>(td - tf).count());
        keysSent += sent;
        ++frames;
        next += std::chrono::duration_cast<clk::duration>(period);
        if (clk::now() > next)
        {
            ++late;
            next = clk::now();
        }
        else
            std::this_thread::sleep_until(next);
    }
    pending.wait();
    const double el = std::chrono::duration<double>(clk::now() - t0).count();
    std::printf("%s block rows %d-%d cols %d-%d (%d keys), mode %s, JPEG q%d 4:4:4, target %.0f fps\n",
                std::string(m.name).c_str(), g.r0, g.r1, g.c0, g.c1, (g.r1 - g.r0 + 1) * (g.c1 - g.c0 + 1),
                mode.c_str(), q, fps);
    std::printf("  achieved %.1f fps (%zu frames in %.2f s, %zu late)  |  %.1f keys/frame sent, %.1f reports/frame\n",
                frames / el, frames, el, late, static_cast<double>(keysSent) / frames,
                static_cast<double>(reports) / frames);
    encodeUs.Print("wait for encoder");
    sendUs.Print("USB send per frame");
    frameUs.Print("frame total");
    return 0;
}
}  // namespace

int CmdBench(const Args& a)
{
    if (a.pos.size() < 2)
    {
        std::fprintf(stderr, "usage: sdctl bench <keys|matrix|lcd|feature|mixed>\n");
        return 2;
    }
    const auto& w = a.pos[1];
    if (w == "keys")
        return BenchKeys(a);
    if (w == "matrix")
        return BenchMatrix(a);
    if (w == "lcd")
        return BenchLcd(a);
    if (w == "feature")
        return BenchFeature(a);
    if (w == "mixed")
        return BenchMixed(a);
    if (w == "block")
        return BenchBlock(a);
    std::fprintf(stderr, "unknown bench\n");
    return 2;
}

}  // namespace cli
