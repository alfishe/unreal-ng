// POC 032: small embedded JPEG encoders against libjpeg-turbo for the Stream Deck companion.
//
//   poc032-jpegbench bench   --out DIR [--theme DIR] [--quick] [--cpu]   timing + size + PSNR at q90, q sweep, frame
//   timing poc032-jpegbench inspect [--theme DIR]                       JPEG stream structure of every encoder
//   poc032-jpegbench device  [--theme DIR] [--hold S]            show every encoder's bytes on a Stream Deck + XL
//   poc032-jpegbench keys-compare [--rows enc:q,enc:q,...]       one variant per key row, the same icons per column
//
// Images are rendered by libsdb (theme icons, scenes) and moved to device space (ToDeviceSpace) outside the timed
// region. Encoders are timed interleaved (one call of each per round), so the machine's load hits them alike.
#include <turbojpeg.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "jpge.h"
#include "sdb/device.h"
#include "sdb/draw.h"
#include "sdb/image.h"
#include "sdb/jpeg.h"
#include "sdb/models.h"
#include "sdb/protocol.h"
#include "sdb/scenes.h"
#include "stb_image.h"  // the implementation is in libsdb (image.cpp)
#include "toojpeg/toojpeg.h"
#include "toojpegstep1.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

namespace fs = std::filesystem;
using sdb::Rgb;
using Bytes = std::vector<std::uint8_t>;

namespace
{

// ---------------------------------------------------------------- encoders

Bytes EncodeTurbo(const Rgb& img, int q)  // what image.cpp did before POC 032: a handle per call, 4:4:4
{
    tjhandle tj = tj3Init(TJINIT_COMPRESS);
    if (!tj)
        throw std::runtime_error("tj3Init");
    tj3Set(tj, TJPARAM_QUALITY, q);
    tj3Set(tj, TJPARAM_SUBSAMP, TJSAMP_444);
    unsigned char* out = nullptr;
    size_t size = 0;
    const int rc = tj3Compress8(tj, img.px.data(), img.w, 0, img.h, TJPF_RGB, &out, &size);
    Bytes v;
    if (rc == 0)
        v.assign(out, out + size);
    tj3Free(out);
    tj3Destroy(tj);
    if (rc)
        throw std::runtime_error("tj3Compress8");
    return v;
}

class VectorStream : public jpge::output_stream
{
public:
    explicit VectorStream(Bytes& out) : _out(out) {}
    bool put_buf(const void* buf, int len) override
    {
        const auto* b = static_cast<const std::uint8_t*>(buf);
        _out.insert(_out.end(), b, b + len);
        return true;
    }

private:
    Bytes& _out;
};

Bytes EncodeJpgeWith(const Rgb& img, int q, bool twoPass, bool stdTables)
{
    jpge::params p;
    p.m_quality = q;
    p.m_subsampling = jpge::H1V1;
    p.m_two_pass_flag = twoPass;
    p.m_use_std_tables = stdTables;
    Bytes out;
    out.reserve(img.px.size() / 4);
    VectorStream s(out);
    jpge::jpeg_encoder e;
    if (!e.init(&s, img.w, img.h, 3, p))
        throw std::runtime_error("jpge init");
    for (unsigned pass = 0; pass < e.get_total_passes(); ++pass)
    {
        for (int y = 0; y < img.h; ++y)
            if (!e.process_scanline(img.At(0, y)))
                throw std::runtime_error("jpge scanline");
        if (!e.process_scanline(nullptr))
            throw std::runtime_error("jpge flush");
    }
    e.deinit();
    return out;
}

Bytes EncodeJpge(const Rgb& img, int q)
{
    return EncodeJpgeWith(img, q, false, false);
}
Bytes EncodeJpge2(const Rgb& img, int q)
{
    return EncodeJpgeWith(img, q, true, false);
}
Bytes EncodeJpgeStd(const Rgb& img, int q)
{
    return EncodeJpgeWith(img, q, false, true);
}

void StbSink(void* ctx, void* data, int size)
{
    auto* out = static_cast<Bytes*>(ctx);
    const auto* b = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), b, b + size);
}

Bytes EncodeStb(const Rgb& img, int q)  // the POC copy is patched to 4:4:4 at every quality
{
    Bytes out;
    out.reserve(img.px.size() / 4);
    if (!stbi_write_jpg_to_func(StbSink, &out, img.w, img.h, 3, img.px.data(), q))
        throw std::runtime_error("stbi_write_jpg");
    return out;
}

thread_local Bytes* tooOut = nullptr;  // TooJpeg's callback has no context pointer
void TooSink(unsigned char b)
{
    tooOut->push_back(b);
}

Bytes EncodeToo(const Rgb& img, int q)
{
    Bytes out;
    out.reserve(img.px.size() / 4);
    tooOut = &out;
    const bool ok = TooJpeg::writeJpeg(TooSink, img.px.data(), static_cast<unsigned short>(img.w),
                                       static_cast<unsigned short>(img.h), true, static_cast<unsigned char>(q), false);
    tooOut = nullptr;
    if (!ok)
        throw std::runtime_error("TooJpeg");
    return out;
}

Bytes EncodeStep1(const Rgb& img, int q)
{
    return poc032::step1::Encode(img.px.data(), img.w, img.h, q);
}

Bytes EncodeSdb(const Rgb& img, int q)
{
    return sdb::jpeg::Encode(img.px.data(), img.w, img.h, q);
}

struct Encoder
{
    const char* name;
    Bytes (*fn)(const Rgb&, int);
};
const Encoder kEncoders[] = {
    {"a-turbojpeg", EncodeTurbo},  {"b-jpge", EncodeJpge},   {"b-jpge-2pass", EncodeJpge2},
    {"b-jpge-std", EncodeJpgeStd}, {"c-stb", EncodeStb},     {"d-toojpeg", EncodeToo},
    {"e-step1", EncodeStep1},      {"f-sdbjpeg", EncodeSdb},
};
constexpr int kAllEncoders = static_cast<int>(sizeof(kEncoders) / sizeof(kEncoders[0]));

// bench --only a,b,...: the encoders a run times (default: all)
std::vector<Encoder> active(std::begin(kEncoders), std::end(kEncoders));
int kEncoderCount = kAllEncoders;
void SelectEncoders(const std::string& list)
{
    if (list.empty())
        return;
    active.clear();
    for (size_t pos = 0; pos < list.size();)
    {
        const size_t comma = std::min(list.find(',', pos), list.size());
        const std::string name = list.substr(pos, comma - pos);
        bool found = false;
        for (const auto& e : kEncoders)
            if (name == e.name)
            {
                active.push_back(e);
                found = true;
            }
        if (!found)
            throw std::runtime_error("unknown encoder " + name);
        pos = comma + 1;
    }
    kEncoderCount = static_cast<int>(active.size());
}

// ---------------------------------------------------------------- decoding, quality

std::optional<Rgb> DecodeStb(const Bytes& b)
{
    int w = 0, h = 0, n = 0;
    unsigned char* d = stbi_load_from_memory(b.data(), static_cast<int>(b.size()), &w, &h, &n, 3);
    if (!d)
        return std::nullopt;
    Rgb o(w, h);
    std::memcpy(o.px.data(), d, o.px.size());
    stbi_image_free(d);
    return o;
}

std::optional<Rgb> DecodeTurbo(const Bytes& b)
{
    tjhandle tj = tj3Init(TJINIT_DECOMPRESS);
    std::optional<Rgb> out;
    if (tj && tj3DecompressHeader(tj, b.data(), b.size()) == 0)
    {
        Rgb img(tj3Get(tj, TJPARAM_JPEGWIDTH), tj3Get(tj, TJPARAM_JPEGHEIGHT));
        if (tj3Decompress8(tj, b.data(), b.size(), img.px.data(), 0, TJPF_RGB) == 0)
            out = std::move(img);
    }
    tj3Destroy(tj);
    return out;
}

double Psnr(const Rgb& a, const Rgb& b)
{
    if (a.w != b.w || a.h != b.h)
        return -1;
    double se = 0;
    for (size_t i = 0; i < a.px.size(); ++i)
    {
        const double d = static_cast<double>(a.px[i]) - b.px[i];
        se += d * d;
    }
    const double mse = se / static_cast<double>(a.px.size());
    return mse <= 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

// ---------------------------------------------------------------- stream structure

struct Structure
{
    std::string markers;   // e.g. "SOI APP0 DQT DQT SOF0 DHT DHT DHT DHT SOS"
    int sof = -1;          // 0xC0 baseline, 0xC1 extended, 0xC2 progressive
    std::string sampling;  // "11 11 11" = 4:4:4
    bool stdHuffman = true;
    int dqtPrecision = 0;
};

Structure Inspect(const Bytes& b)
{
    // Annex K.3 code-length counts (BITS) of the four standard tables, keyed by (class << 4 | id).
    static const std::uint8_t kDcLum[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
    static const std::uint8_t kDcChr[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
    static const std::uint8_t kAcLum[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
    static const std::uint8_t kAcChr[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
    Structure s;
    size_t i = 2;
    s.markers = "SOI";
    while (i + 4 <= b.size() && b[i] == 0xFF)
    {
        const int m = b[i + 1];
        const size_t len = static_cast<size_t>(b[i + 2]) << 8 | b[i + 3];
        char name[16];
        if (m >= 0xE0 && m <= 0xEF)
            std::snprintf(name, sizeof name, "APP%d", m - 0xE0);
        else if (m == 0xDB)
            std::snprintf(name, sizeof name, "DQT");
        else if (m == 0xC4)
            std::snprintf(name, sizeof name, "DHT");
        else if (m >= 0xC0 && m <= 0xCF)
            std::snprintf(name, sizeof name, "SOF%d", m - 0xC0);
        else if (m == 0xDA)
            std::snprintf(name, sizeof name, "SOS");
        else if (m == 0xDD)
            std::snprintf(name, sizeof name, "DRI");
        else if (m == 0xFE)
            std::snprintf(name, sizeof name, "COM");
        else
            std::snprintf(name, sizeof name, "%02X", m);
        s.markers += std::string(" ") + name;
        const size_t seg = i + 4;
        if (m == 0xC0 || m == 0xC1 || m == 0xC2)
        {
            s.sof = m;
            const int nc = b[seg + 5];
            for (int c = 0; c < nc; ++c)
            {
                char f[8];
                std::snprintf(f, sizeof f, "%s%X%X", c ? " " : "", b[seg + 6 + c * 3 + 1] >> 4,
                              b[seg + 6 + c * 3 + 1] & 15);
                s.sampling += f;
            }
        }
        if (m == 0xDB)
            for (size_t p = seg; p < i + 2 + len;)
            {
                s.dqtPrecision = std::max(s.dqtPrecision, b[p] >> 4 ? 16 : 8);
                p += 1 + ((b[p] >> 4) ? 128 : 64);
            }
        if (m == 0xC4)
            for (size_t p = seg; p < i + 2 + len;)
            {
                const int tc = b[p] >> 4, th = b[p] & 15;
                const std::uint8_t* ref = tc == 0 ? (th ? kDcChr : kDcLum) : (th ? kAcChr : kAcLum);
                int n = 0;
                for (int k = 0; k < 16; ++k)
                {
                    n += b[p + 1 + k];
                    if (b[p + 1 + k] != ref[k])
                        s.stdHuffman = false;
                }
                p += 17 + n;
            }
        if (m == 0xDA)
            break;
        i += 2 + len;
    }
    return s;
}

// ---------------------------------------------------------------- content

struct Item
{
    std::string category;  // key-real, key-noise, strip-real, strip-noise, screen-real, screen-noise
    std::string model, name;
    int reportPayload = 1016;  // image bytes per HID report: keys 1016, strip regions 1008
    Rgb dev;                   // device space
};

const sdb::Model& ModelOf(std::uint16_t pid)
{
    const sdb::Model* m = sdb::FindModel(sdb::kElgatoVid, pid);
    if (!m)
        throw std::runtime_error("model");
    return *m;
}

Rgb Noise(int w, int h, std::mt19937& rng)
{
    Rgb r(w, h);
    for (auto& p : r.px)
        p = static_cast<std::uint8_t>(rng() & 0xFF);
    return r;
}

std::vector<std::pair<std::string, Rgb>> LoadIcons(const fs::path& theme)
{
    std::vector<std::pair<std::string, Rgb>> v;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(theme / "keys"))
        if (e.path().extension() == ".png")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files)
        if (auto img = sdb::LoadImage(f))
            v.emplace_back(f.stem().string(), std::move(*img));
    if (v.empty())
        throw std::runtime_error("no icons in " + (theme / "keys").string());
    return v;
}

std::vector<std::pair<std::string, Rgb>> StripScenes(int w, int h, const fs::path& theme)
{
    namespace sc = sdb::scenes;
    const sc::Size sz{w, h};
    std::vector<std::pair<std::string, Rgb>> v;
    v.emplace_back("status-ok", sc::StatusScene(sz, sc::Status::Ok, "RUNNING", "PENTAGON 128K", 3));
    v.emplace_back("status-error", sc::StatusScene(sz, sc::Status::Error, "ERROR", "TAPE LOAD FAILED", 3));
    v.emplace_back("status-paused", sc::StatusScene(sz, sc::Status::Paused, "PAUSED", "", 3));
    v.emplace_back("status-saved", sc::StatusScene(sz, sc::Status::Saved, "SAVED", "SNAPSHOT 3", 3));
    sc::Loading data;
    data.progress = 40;
    v.emplace_back("loading-data", sc::LoadingScene(sz, data, 7));
    sc::Loading pilot;
    pilot.pilot = true;
    pilot.progress = 5;
    v.emplace_back("loading-pilot", sc::LoadingScene(sz, pilot, 7));
    sc::Ttd t;
    t.pos = 420;
    t.total = 1000;
    t.checkpoints = {100, 300, 600};
    t.breakpoints = {250, 800};
    t.fps = 48.83;
    v.emplace_back("ttd", sc::TtdScene(sz, t, 5));
    v.emplace_back("marquee", sc::Marquee(sz, "UNREAL-NG ZX SPECTRUM EMULATOR", 11));
    v.emplace_back("volume", sc::Volume(sz, {{"AY", 70, false}, {"BEEPER", 40, false}, {"GS", 0, true}}, 0));
    if (auto strip = sdb::LoadImage(theme / "strip.png"))
        v.emplace_back("theme-strip", sdb::FitImage(*strip, w, h, sdb::Fit::Contain));
    return v;
}

Rgb PanelComposite(const sdb::Model& m, const std::vector<std::pair<std::string, Rgb>>& icons, const fs::path& theme)
{
    const sdb::PanelLayout l = sdb::GetPanelLayout(m);
    Rgb p(l.w, l.h, 0, 0, 0);
    size_t k = 0;
    for (int y : l.rowY)
        for (int x : l.colX)
            sdb::Blit(p, sdb::FitImage(icons[k++ % icons.size()].second, l.key, l.key, sdb::Fit::Contain), x, y);
    if (l.stripW > 0)
    {
        auto scenes = StripScenes(l.stripW, l.stripH, theme);
        sdb::Blit(p, scenes.front().second, l.stripX, l.stripY);
    }
    return p;
}

std::vector<Item> BuildItems(const fs::path& theme, bool quick)
{
    std::vector<Item> items;
    std::mt19937 rng(32);
    const auto icons = LoadIcons(theme);
    struct KeyModel
    {
        std::uint16_t pid;
        const char* name;
    };
    const KeyModel keyModels[] = {{0x0080, "mk2-72"}, {0x006C, "xl-96"}, {0x00C6, "plusxl-112"}, {0x0084, "plus-120"}};
    for (const auto& km : keyModels)
    {
        const sdb::Model& m = ModelOf(km.pid);
        size_t n = 0;
        for (const auto& [name, img] : icons)
        {
            if (quick && ++n > 6)
                break;
            items.push_back({"key-real", km.name, name, 1016,
                             sdb::ToDeviceSpace(sdb::FitImage(img, m.key.w, m.key.h, sdb::Fit::Contain), m.key)});
        }
        items.push_back({"key-noise", km.name, "noise", 1016, Noise(m.key.w, m.key.h, rng)});
    }
    const KeyModel stripModels[] = {{0x00C6, "plusxl-1200x100"}, {0x0084, "plus-800x100"}, {0x009A, "neo-248x58"}};
    for (const auto& sm : stripModels)
    {
        const sdb::Model& m = ModelOf(sm.pid);
        for (auto& [name, img] : StripScenes(m.lcdLogicalW, m.lcdLogicalH, theme))
            items.push_back({"strip-real", sm.name, name, 1008, sdb::ToDeviceSpace(img, m.lcd)});
        items.push_back({"strip-noise", sm.name, "noise", 1008, Noise(m.lcd.w, m.lcd.h, rng)});
    }
    const KeyModel screenModels[] = {{0x006C, "xl-1024x600"}, {0x0084, "plus-800x480"}, {0x00C6, "plusxl-1280x800"}};
    for (const auto& sm : screenModels)
    {
        const sdb::Model& m = ModelOf(sm.pid);
        items.push_back(
            {"screen-real", sm.name, "panel", 1016, sdb::ToDeviceSpace(PanelComposite(m, icons, theme), m.screen)});
        items.push_back({"screen-noise", sm.name, "noise", 1016, Noise(m.screen.w, m.screen.h, rng)});
    }
    return items;
}

// ---------------------------------------------------------------- timing

// Wall time (steady_clock) or, with --cpu, the thread's CPU time: on a heavily loaded machine the wall time also
// counts the time the thread waited for a core, the CPU time does not.
bool useCpuTime = false;
double NowUs()
{
#ifndef _WIN32
    if (useCpuTime)
    {
        timespec ts{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        return static_cast<double>(ts.tv_sec) * 1e6 + static_cast<double>(ts.tv_nsec) / 1e3;
    }
#endif
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Stat
{
    double median = 0, p99 = 0;
};
Stat Stats(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    Stat s;
    s.median = v[v.size() / 2];
    s.p99 = v[std::min(v.size() - 1, static_cast<size_t>(std::ceil(v.size() * 0.99)) - 1)];
    return s;
}

int Iterations(const Rgb& img, bool quick)
{
    const size_t px = static_cast<size_t>(img.w) * img.h;
    const int n = px <= 120 * 120 ? 400 : px <= 1200 * 100 ? 150 : 30;
    return quick ? std::max(10, n / 10) : n;
}

int Reports(size_t bytes, int payload)
{
    return static_cast<int>((bytes + payload - 1) / payload);
}

std::string Arg(int argc, char** argv, const std::string& key, const std::string& def)
{
    for (int i = 2; i + 1 < argc; ++i)
        if (argv[i] == "--" + key)
            return argv[i + 1];
    return def;
}
bool Flag(int argc, char** argv, const std::string& key)
{
    for (int i = 2; i < argc; ++i)
        if (argv[i] == "--" + key)
            return true;
    return false;
}

int Bench(const fs::path& theme, const fs::path& out, bool quick)
{
    fs::create_directories(out);
    const auto items = BuildItems(theme, quick);
    std::fprintf(stderr, "%zu images, %d encoders\n", items.size(), kEncoderCount);

    // 1. q = 90: time (interleaved), bytes, reports, PSNR, both decoders.
    std::ofstream q90(out / "q90.csv");
    q90 << "encoder,category,model,image,w,h,bytes,reports,psnr,psnr_tj,median_us,p99_us\n";
    for (const auto& it : items)
    {
        const int n = Iterations(it.dev, quick);
        std::vector<std::vector<double>> t(kEncoderCount);
        for (int e = 0; e < kEncoderCount; ++e)
            for (int w = 0; w < 3; ++w)
                (void)active[e].fn(it.dev, 90);
        for (int r = 0; r < n; ++r)
            for (int e = 0; e < kEncoderCount; ++e)
            {
                const double t0 = NowUs();
                Bytes b = active[e].fn(it.dev, 90);
                const double t1 = NowUs();
                t[e].push_back(t1 - t0);
                if (b.empty())
                    return 2;
            }
        for (int e = 0; e < kEncoderCount; ++e)
        {
            const Bytes b = active[e].fn(it.dev, 90);
            const auto ds = DecodeStb(b);
            const auto dt = DecodeTurbo(b);
            if (!ds || !dt)
                std::fprintf(stderr, "DECODE FAILED: %s %s %s (stb %d, tj %d)\n", active[e].name, it.model.c_str(),
                             it.name.c_str(), ds.has_value(), dt.has_value());
            const Stat s = Stats(t[e]);
            char line[512];
            std::snprintf(line, sizeof line, "%s,%s,%s,%s,%d,%d,%zu,%d,%.3f,%.3f,%.2f,%.2f\n", active[e].name,
                          it.category.c_str(), it.model.c_str(), it.name.c_str(), it.dev.w, it.dev.h, b.size(),
                          Reports(b.size(), it.reportPayload), ds ? Psnr(it.dev, *ds) : -1.0,
                          dt ? Psnr(it.dev, *dt) : -1.0, s.median, s.p99);
            q90 << line;
        }
    }

    // 2. Quality sweep: bytes and PSNR (no timing).
    std::ofstream sweep(out / "sweep.csv");
    sweep << "encoder,category,model,image,q,bytes,psnr\n";
    const int qs[] = {50, 60, 70, 75, 80, 85, 88, 90, 92, 94, 95, 96, 98};
    for (const auto& it : items)
        for (int e = 0; e < kEncoderCount; ++e)
            for (int q : qs)
            {
                const Bytes b = active[e].fn(it.dev, q);
                const auto d = DecodeStb(b);
                char line[512];
                std::snprintf(line, sizeof line, "%s,%s,%s,%s,%d,%zu,%.3f\n", active[e].name, it.category.c_str(),
                              it.model.c_str(), it.name.c_str(), q, b.size(), d ? Psnr(it.dev, *d) : -1.0);
                sweep << line;
            }

    // 3. A whole + XL frame: 36 keys at 112 px + the 1200x100 strip, single thread, interleaved.
    std::ofstream frame(out / "frame.csv");
    frame << "encoder,content,median_us,p99_us,bytes,reports\n";
    const sdb::Model& pxl = ModelOf(0x00C6);
    const auto icons = LoadIcons(theme);
    std::mt19937 rng(7);
    for (const char* content : {"real", "noise"})
    {
        const bool noise = std::string(content) == "noise";
        std::vector<Rgb> keys;
        for (int k = 0; k < pxl.keys; ++k)
            keys.push_back(noise ? Noise(pxl.key.w, pxl.key.h, rng)
                                 : sdb::ToDeviceSpace(sdb::FitImage(icons[k % icons.size()].second, pxl.key.w,
                                                                    pxl.key.h, sdb::Fit::Contain),
                                                      pxl.key));
        const Rgb strip =
            noise ? Noise(pxl.lcd.w, pxl.lcd.h, rng)
                  : sdb::ToDeviceSpace(StripScenes(pxl.lcdLogicalW, pxl.lcdLogicalH, theme)[0].second, pxl.lcd);
        std::vector<std::vector<double>> t(kEncoderCount);
        std::vector<size_t> bytes(kEncoderCount, 0);
        std::vector<int> reports(kEncoderCount, 0);
        const int frames = quick ? 5 : 60;
        for (int f = 0; f < frames + 2; ++f)
            for (int e = 0; e < kEncoderCount; ++e)
            {
                size_t sum = 0;
                int rep = 0;
                const double t0 = NowUs();
                for (const auto& k : keys)
                {
                    const Bytes b = active[e].fn(k, 90);
                    sum += b.size();
                    rep += Reports(b.size(), 1016);
                }
                const Bytes sb = active[e].fn(strip, 90);
                const double t1 = NowUs();
                sum += sb.size();
                rep += Reports(sb.size(), 1008);
                if (f >= 2)  // two warm-up frames
                    t[e].push_back(t1 - t0);
                bytes[e] = sum;
                reports[e] = rep;
            }
        for (int e = 0; e < kEncoderCount; ++e)
        {
            const Stat s = Stats(t[e]);
            char line[256];
            std::snprintf(line, sizeof line, "%s,%s,%.1f,%.1f,%zu,%d\n", active[e].name, content, s.median, s.p99,
                          bytes[e], reports[e]);
            frame << line;
        }
    }
    std::fprintf(stderr, "wrote %s\n", out.string().c_str());
    return 0;
}

// The entropy-coded scan: everything after the (last) SOS segment.
Bytes ScanData(const Bytes& b)
{
    size_t i = 2;
    while (i + 4 <= b.size() && b[i] == 0xFF)
    {
        const size_t len = static_cast<size_t>(b[i + 2]) << 8 | b[i + 3];
        if (b[i + 1] == 0xDA)
            return Bytes(b.begin() + static_cast<std::ptrdiff_t>(i + 2 + len), b.end());
        i += 2 + len;
    }
    return {};
}

// B1: e-step1 byte-identical to d-toojpeg; and how often f-sdbjpeg's scan equals turbojpeg's, per quality.
int Identity(const fs::path& theme)
{
    const auto items = BuildItems(theme, false);
    const int qs[] = {50, 60, 70, 75, 80, 85, 88, 90, 92, 94, 95, 96, 98, 100};
    int step1Same = 0, step1All = 0;
    for (int q : qs)
    {
        int scanSame = 0, sizeDiff = 0;
        for (const auto& it : items)
        {
            const Bytes too = EncodeToo(it.dev, q), s1 = EncodeStep1(it.dev, q);
            ++step1All;
            if (too == s1)
                ++step1Same;
            else if (step1All - step1Same <= 5)
                std::printf("step1 differs: %s %s q%d (%zu vs %zu bytes)\n", it.model.c_str(), it.name.c_str(), q,
                            too.size(), s1.size());
            const Bytes tj = EncodeTurbo(it.dev, q), sj = EncodeSdb(it.dev, q);
            if (ScanData(tj) == ScanData(sj))
                ++scanSame;
            sizeDiff += static_cast<int>(sj.size()) - static_cast<int>(tj.size());
        }
        std::printf("q%-3d f-sdbjpeg scan == turbojpeg scan: %d / %zu images; total size f - tj = %+d bytes\n", q,
                    scanSame, items.size(), sizeDiff);
    }
    std::printf("e-step1 == d-toojpeg byte for byte: %d / %d streams\n", step1Same, step1All);
    return step1Same == step1All ? 0 : 1;
}

int InspectAll(const fs::path& theme)
{
    const auto icons = LoadIcons(theme);
    const sdb::Model& pxl = ModelOf(0x00C6);
    const Rgb key = sdb::ToDeviceSpace(sdb::FitImage(icons[0].second, 112, 112, sdb::Fit::Contain), pxl.key);
    for (const auto& e : kEncoders)
        for (int q : {75, 90, 95})
        {
            const Structure s = Inspect(e.fn(key, q));
            std::printf("%-13s q%-3d SOF=%02X sampling=[%s] std-huffman=%s dqt=%d-bit  %s\n", e.name, q, s.sof,
                        s.sampling.c_str(), s.stdHuffman ? "yes" : "no", s.dqtPrecision, s.markers.c_str());
        }
    return 0;
}

// ---------------------------------------------------------------- hardware

std::string Label(const Encoder& e)  // "b-jpge-2pass" -> "JPGE-2PASS" (the 5x7 font has upper case only)
{
    std::string s(e.name + 2);
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// First the full-screen image (output 0x08) of every encoder in turn, then one + XL screen per round: every key row
// from one encoder (label key in column 0), the six strip tiles from alternating encoders (each tile labelled).
int DeviceTest(const fs::path& theme, int holdSeconds)
{
    std::string err;
    auto dev = sdb::OpenFirst("", &err);
    if (!dev)
    {
        std::fprintf(stderr, "no device: %s\n", err.c_str());
        return 5;
    }
    const sdb::Model& m = dev->PhysicalModel();
    if (m.pid != 0x00C6)
    {
        std::fprintf(stderr, "needs a Stream Deck + XL, found %s\n", std::string(m.name).c_str());
        return 5;
    }
    auto send = [&](const std::vector<sdb::proto::Report>& reports) {
        int failed = 0;
        for (const auto& r : reports)
            if (dev->GetTransport().Write(r) < 0)
                ++failed;
        return failed;
    };
    const auto icons = LoadIcons(theme);
    int failures = 0;
    // full screen (output 0x08), one encoder at a time
    const Rgb panel = PanelComposite(m, icons, theme);
    for (const auto& enc : kEncoders)
    {
        Rgb p = panel;
        sdb::draw::FillRect(p, 0, 0, 520, 30, sdb::draw::kDark);
        sdb::draw::Text(p, 8, 6, "FULL SCREEN " + Label(enc), 3, sdb::draw::kBWhite);
        const Bytes b = enc.fn(sdb::ToDeviceSpace(p, m.screen), 90);
        std::printf("full screen: %s, %zu bytes\n", enc.name, b.size());
        failures += send(sdb::proto::FullScreen(m, b));
        std::this_thread::sleep_for(std::chrono::seconds(holdSeconds));
    }
    const int rounds[][4] = {{0, 2, 3, 1}, {0, 1, 4, 5}};  // the last one stays on the deck
    for (const auto& round : rounds)
    {
        std::printf("round: rows =");
        for (int row = 0; row < 4; ++row)
            std::printf(" %s", kEncoders[round[row]].name);
        std::printf("\n");
        for (int row = 0; row < m.rows; ++row)
        {
            const Encoder& enc = kEncoders[round[row]];
            for (int col = 0; col < m.cols; ++col)
            {
                Rgb img;
                if (col == 0)
                {
                    img = Rgb(m.key.w, m.key.h, 20, 20, 40);
                    sdb::draw::TextCentered(img, m.key.w / 2, 48, Label(enc).substr(0, 8), 2, sdb::draw::kBWhite);
                }
                else
                    img = sdb::FitImage(icons[(row * 9 + col) % icons.size()].second, m.key.w, m.key.h,
                                        sdb::Fit::Contain);
                failures +=
                    send(sdb::proto::KeyImage(m, row * m.cols + col, enc.fn(sdb::ToDeviceSpace(img, m.key), 90)));
            }
        }
        // strip: tile n (200x100 logical) at x = 200 n; the JPEG inside is the rotated 100x200 tile.
        auto scenes = StripScenes(m.lcdLogicalW, m.lcdLogicalH, theme);
        const Rgb& strip = scenes[4].second;  // loading-data: the heaviest real scene
        for (int tile = 0; tile < 6; ++tile)
        {
            const Encoder& enc = kEncoders[round[tile % 4]];
            Rgb t = sdb::Crop(strip, tile * 200, 0, 200, 100);
            sdb::draw::FillRect(t, 0, 0, 200, 18, sdb::draw::kDark);
            sdb::draw::Text(t, 4, 3, Label(enc), 2, sdb::draw::kBWhite);
            sdb::ImageFormat f = m.lcd;
            f.w = 100;
            f.h = 200;
            failures += send(sdb::proto::LcdRegion(m, tile * 200, 0, 200, 100, enc.fn(sdb::ToDeviceSpace(t, f), 90)));
        }
        std::this_thread::sleep_for(std::chrono::seconds(holdSeconds));
    }
    std::printf("HID write failures: %d\n", failures);
    return failures ? 1 : 0;
}

// Side by side on the + XL keys: one row per "encoder:quality" variant (e.g. "a-turbojpeg:90,d-toojpeg:90,..."),
// column 0 a label key, columns 1-8 the same eight theme icons in every row, so a column compares the variants on
// one picture. Everything 4:4:4, the product's key path (FitImage 144 -> 112, rotate 270). The image stays on the deck.
int KeysCompare(const fs::path& theme, const std::string& spec)
{
    struct Variant
    {
        const Encoder* enc;
        int q;
    };
    std::vector<Variant> rows;
    for (size_t pos = 0; pos < spec.size();)
    {
        const size_t comma = std::min(spec.find(',', pos), spec.size());
        const std::string item = spec.substr(pos, comma - pos);
        const size_t colon = item.find(':');
        const std::string name = item.substr(0, colon);
        const int q = colon == std::string::npos ? 90 : std::stoi(item.substr(colon + 1));
        const Encoder* found = nullptr;
        for (const auto& e : kEncoders)
            if (name == e.name)
                found = &e;
        if (!found)
            throw std::runtime_error("unknown encoder " + name);
        rows.push_back({found, q});
        pos = comma + 1;
    }
    std::string err;
    auto dev = sdb::OpenFirst("", &err);
    if (!dev || dev->PhysicalModel().pid != 0x00C6)
    {
        std::fprintf(stderr, "needs a Stream Deck + XL: %s\n", err.c_str());
        return 5;
    }
    const sdb::Model& m = dev->PhysicalModel();
    const auto icons = LoadIcons(theme);
    int failures = 0;
    for (int row = 0; row < m.rows && row < static_cast<int>(rows.size()); ++row)
    {
        const Variant& v = rows[row];
        size_t rowBytes = 0;
        for (int col = 0; col < m.cols; ++col)
        {
            Rgb img;
            if (col == 0)
            {
                img = Rgb(m.key.w, m.key.h, 20, 20, 40);
                sdb::draw::TextCentered(img, m.key.w / 2, 30, Label(*v.enc).substr(0, 8), 2, sdb::draw::kBWhite);
                sdb::draw::TextCentered(img, m.key.w / 2, 60, "Q" + std::to_string(v.q), 3, sdb::draw::kBYellow);
            }
            else
                img = sdb::FitImage(icons[(col - 1) % icons.size()].second, m.key.w, m.key.h, sdb::Fit::Contain);
            const Bytes b = v.enc->fn(sdb::ToDeviceSpace(img, m.key), v.q);
            if (col > 0)
                rowBytes += b.size();
            for (const auto& r : sdb::proto::KeyImage(m, row * m.cols + col, b))
                if (dev->GetTransport().Write(r) < 0)
                    ++failures;
        }
        std::printf("row %d (keys %d-%d): %s q%d 4:4:4, icons %s..%s, %zu bytes for the 8 icon keys (%.0f per key)\n",
                    row, row * m.cols, row * m.cols + m.cols - 1, v.enc->name, v.q, icons[0].first.c_str(),
                    icons[7 % icons.size()].first.c_str(), rowBytes, rowBytes / 8.0);
    }
    std::printf("HID write failures: %d\n", failures);
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr,
                     "usage: poc032-jpegbench bench --out DIR [--theme DIR] [--quick] | inspect | device [--hold S]\n");
        return 1;
    }
    const std::string cmd = argv[1];
    const fs::path theme = Arg(argc, argv, "theme", "data/streamdeck/themes/unrealng");
    try
    {
        useCpuTime = Flag(argc, argv, "cpu");
        SelectEncoders(Arg(argc, argv, "only", ""));
        if (cmd == "identity")
            return Identity(theme);
        if (cmd == "bench")
            return Bench(theme, Arg(argc, argv, "out", "scratch/2026-10-20-poc-032/run"), Flag(argc, argv, "quick"));
        if (cmd == "inspect")
            return InspectAll(theme);
        if (cmd == "keys-compare")
            return KeysCompare(theme,
                               Arg(argc, argv, "rows", "a-turbojpeg:90,d-toojpeg:90,a-turbojpeg:95,d-toojpeg:95"));
        if (cmd == "device")
            return DeviceTest(theme, std::stoi(Arg(argc, argv, "hold", "8")));
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
    return 1;
}
