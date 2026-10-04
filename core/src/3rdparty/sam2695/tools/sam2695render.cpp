// sam2695render - render a Standard MIDI File through libsam2695, or check a bank.
//
//   sam2695render --bank B.sf2 --midi M.mid --out O.wav [--rate 37500] [--interp sinc|cubic|linear]
//                 [--gain 0.25] [--tail 2] [--line] [--polyphony N] [--no-reset-delay]
//   sam2695render --bank B.sf2 --info                  presets and loader warnings
//   sam2695render --bank B.sf2 --midi M.mid --check    one JSON line: load result, NaN / denormal /
//                                                      peak of the render (bank corpus check)
//
// --line sends every byte through the serial line model (31 250 baud 8N1 frames on WriteLine), so
// bytes queue behind each other as on a real MIDI cable; without it bytes arrive at their file time.
// The output is a 32-bit float stereo WAV.
#include "sam2695/sam2695.h"
#include "smfreader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

using namespace sam2695;
using namespace sam2695tools;

namespace
{

// The chip's 9.6 MHz crystal: an exact multiple of the internal rate (256 ticks per sample) and of
// common SMF tick lengths (0.5 ms = 4800 ticks)
constexpr uint64_t kHostRate = 9600000;

void Put16(std::ofstream& f, uint16_t v) { f.put(static_cast<char>(v & 0xFF)).put(static_cast<char>(v >> 8)); }
void Put32(std::ofstream& f, uint32_t v)
{
    Put16(f, static_cast<uint16_t>(v));
    Put16(f, static_cast<uint16_t>(v >> 16));
}

bool WriteWav(const std::string& path, const std::vector<float>& stereo, uint32_t rate)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    const uint32_t dataBytes = static_cast<uint32_t>(stereo.size() * 4);
    f.write("RIFF", 4);
    Put32(f, 36 + dataBytes);
    f.write("WAVEfmt ", 8);
    Put32(f, 16);
    Put16(f, 3); // IEEE float
    Put16(f, 2);
    Put32(f, rate);
    Put32(f, rate * 8);
    Put16(f, 8);
    Put16(f, 32);
    f.write("data", 4);
    Put32(f, dataBytes);
    f.write(reinterpret_cast<const char*>(stereo.data()), static_cast<std::streamsize>(dataBytes));
    return static_cast<bool>(f);
}

std::string JsonEscape(const std::string& s)
{
    std::string o;
    for (char c : s)
    {
        if (c == '"' || c == '\\')
            o.push_back('\\');
        if (static_cast<unsigned char>(c) < 0x20)
            continue;
        o.push_back(c);
    }
    return o;
}

struct Options
{
    std::string bank, midi, out;
    uint32_t rate = kInternalRate;
    Interpolation interp = Interpolation::Sinc;
    float gain = 0.25f;
    double tail = 2.0;
    bool line = false, info = false, check = false, resetDelay = true;
    uint32_t polyphony = 0;
};

bool Parse(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--bank")
            o.bank = next();
        else if (a == "--midi")
            o.midi = next();
        else if (a == "--out")
            o.out = next();
        else if (a == "--rate")
            o.rate = static_cast<uint32_t>(std::atoi(next()));
        else if (a == "--gain")
            o.gain = static_cast<float>(std::atof(next()));
        else if (a == "--tail")
            o.tail = std::atof(next());
        else if (a == "--polyphony")
            o.polyphony = static_cast<uint32_t>(std::atoi(next()));
        else if (a == "--interp")
        {
            const std::string m = next();
            o.interp = m == "linear" ? Interpolation::Linear : m == "cubic" ? Interpolation::Cubic : Interpolation::Sinc;
        }
        else if (a == "--line")
            o.line = true;
        else if (a == "--info")
            o.info = true;
        else if (a == "--check")
            o.check = true;
        else if (a == "--no-reset-delay")
            o.resetDelay = false;
        else
        {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return false;
        }
    }
    return !o.bank.empty() && (o.info || !o.midi.empty());
}

// Serial line driver: byte frames back to back, each starting no earlier than its file time.
struct LineDriver
{
    double busyUntil = 0.0; // seconds
    void Send(Synth& s, double at, uint8_t byte)
    {
        const double bit = 1.0 / kMidiBaud;
        const double t0 = std::max(at, busyUntil);
        auto tick = [](double sec) { return static_cast<uint64_t>(std::llround(sec * kHostRate)); };
        s.WriteLine(tick(t0), false);
        for (int b = 0; b < 8; b++)
            s.WriteLine(tick(t0 + (b + 1) * bit), ((byte >> b) & 1) != 0);
        s.WriteLine(tick(t0 + 9 * bit), true);
        busyUntil = t0 + 10 * bit;
    }
};

} // namespace

int main(int argc, char** argv)
{
    Options o;
    if (!Parse(argc, argv, o))
    {
        std::fprintf(stderr, "usage: sam2695render --bank B.sf2 (--info | --midi M.mid (--out O.wav | --check)) "
                             "[--rate R] [--interp sinc|cubic|linear] [--gain G] [--tail S] [--line] [--polyphony N] "
                             "[--no-reset-delay]\n");
        return 2;
    }
    const auto t0 = std::chrono::steady_clock::now();
    Sf2Bank::LoadResult loaded = Sf2Bank::LoadFile(o.bank);
    const double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!loaded.bank)
    {
        if (o.check)
            std::printf("{\"bank\":\"%s\",\"ok\":false,\"error\":\"%s\",\"reason\":\"%s\"}\n", JsonEscape(o.bank).c_str(),
                        BankErrorName(loaded.error), JsonEscape(loaded.reason).c_str());
        else
            std::fprintf(stderr, "bank refused: %s: %s\n", BankErrorName(loaded.error), loaded.reason.c_str());
        return o.check ? 0 : 1;
    }
    const BankModel& m = loaded.bank->Model();
    if (o.info)
    {
        std::printf("bank '%s' version %u.%u sha256 %s\n", m.name.c_str(), m.versionMajor, m.versionMinor,
                    DigestHex(loaded.bank->Digest()).c_str());
        std::printf("%zu presets, %zu instruments, %zu samples, %zu frames%s, %zu warnings\n", m.presets.size(),
                    m.instruments.size(), m.samples.size(), m.data16.size(), m.data24.empty() ? "" : " (24-bit)",
                    m.warnings.size());
        for (const Preset& p : m.presets)
            std::printf("  %3u:%3u %s\n", p.bank, p.program, p.name.c_str());
        for (const std::string& w : m.warnings)
            std::printf("  warning: %s\n", w.c_str());
        return 0;
    }

    SmfFile smf;
    std::string error;
    if (!ReadSmf(o.midi, smf, error))
    {
        std::fprintf(stderr, "%s: %s\n", o.midi.c_str(), error.c_str());
        return 1;
    }
    SynthConfig cfg;
    cfg.hostTickRate = kHostRate;
    cfg.outputRate = o.rate;
    cfg.interpolation = o.interp;
    cfg.outputGain = o.gain;
    cfg.polyphony = o.polyphony;
    cfg.resetDelay = o.resetDelay;
    cfg.eventCapacity = 65536;
    Synth synth;
    if (!synth.Configure(cfg))
    {
        std::fprintf(stderr, "bad configuration\n");
        return 1;
    }
    synth.LoadBank(loaded.bank);

    std::vector<float> out;
    std::vector<float> buf(8192 * 2);
    auto drain = [&]() {
        for (;;)
        {
            const size_t n = synth.Render(buf.data(), 8192);
            out.insert(out.end(), buf.begin(), buf.begin() + static_cast<long>(n * 2));
            if (n < 8192)
                break;
        }
    };
    // Run() in steps of at most 0.1 s, draining after each: the stream keeps 1 s of internal audio
    uint64_t now = 0;
    auto advance = [&](uint64_t t) {
        while (now < t)
        {
            now = std::min(t, now + kHostRate / 10);
            synth.Run(now);
            drain();
        }
    };
    LineDriver line;
    const auto r0 = std::chrono::steady_clock::now();
    for (const SmfEvent& e : smf.events)
    {
        const uint64_t t = static_cast<uint64_t>(std::llround(e.seconds * kHostRate));
        advance(t);
        for (uint8_t b : e.bytes)
        {
            if (o.line)
                line.Send(synth, e.seconds, b);
            else
                synth.WriteByte(t, b);
        }
    }
    const double endSeconds = std::max(smf.durationSeconds, line.busyUntil) + o.tail;
    const uint64_t end = static_cast<uint64_t>(std::llround(endSeconds * kHostRate));
    advance(end);
    const double renderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r0).count();

    SynthReport rep;
    synth.Describe(rep);
    if (o.check)
    {
        size_t nan = 0, denormal = 0;
        double peak = 0.0;
        for (float v : out)
        {
            if (!std::isfinite(v))
                nan++;
            else if (v != 0.0f && std::fabs(v) < 1.17549435e-38f)
                denormal++;
            else
                peak = std::fmax(peak, std::fabs(v));
        }
        std::printf("{\"bank\":\"%s\",\"ok\":true,\"presets\":%zu,\"samples\":%zu,\"warnings\":%zu,\"sm24\":%s,"
                    "\"frames\":%zu,\"nan\":%zu,\"denormal\":%zu,\"peak\":%.4f,\"stolen\":%llu,\"loadMs\":%.0f,"
                    "\"renderMs\":%.0f,\"sha256\":\"%s\"}\n",
                    JsonEscape(o.bank).c_str(), m.presets.size(), m.samples.size(), m.warnings.size(),
                    m.data24.empty() ? "false" : "true", out.size() / 2, nan, denormal, peak,
                    static_cast<unsigned long long>(rep.voicesStolen), loadMs, renderMs,
                    DigestHex(loaded.bank->Digest()).c_str());
        return 0;
    }
    if (!WriteWav(o.out, out, o.rate))
    {
        std::fprintf(stderr, "cannot write %s\n", o.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "%s: %.1f s audio in %.0f ms, %llu voices stolen, %llu framing errors\n", o.out.c_str(),
                 static_cast<double>(out.size() / 2) / o.rate, renderMs,
                 static_cast<unsigned long long>(rep.voicesStolen), static_cast<unsigned long long>(rep.framingErrors));
    return 0;
}
