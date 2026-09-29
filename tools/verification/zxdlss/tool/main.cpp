/// @file main.cpp
/// @brief zxdlss-render: render a TTD file or a clip through a de-flicker
/// algorithm into a video and/or an exact RGB dump.
///
///   zxdlss-render --ttd session.ttd [--model PENTAGON] --video out.mp4
///   zxdlss-render --clip data/clip_v2 --from 12100 --to 12300 --alg mod-tpgw --layout raw-out --video spiral.mp4
///   zxdlss-render --clip data/clip_v2 --from 12100 --to 12300 --dump out/dump   (compare_dump.py)
///   zxdlss-render --list

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "frames.h"
#include "zxdlss/algorithm.h"

using namespace zxdlss;

namespace
{

void usage()
{
    std::cout << "zxdlss-render - render a TTD file or a clip through a ZX DLSS de-flicker algorithm\n\n"
                 "input (one of):\n"
                 "  --ttd FILE          TTD session (replayed through the emulator core)\n"
                 "  --model NAME        machine of the TTD session (default PENTAGON)\n"
                 "  --overscan          TTD: Pentagon overscan, 352 x 304 with the paper centered\n"
                 "                      horizontally (the UI's Symmetric Horizontal viewport)\n"
                 "  --clip DIR          clip exported with POST /ttd/export-clip (plane B)\n"
                 "range:\n"
                 "  --from N --to N     emulated frames to output (default: all that have look-ahead)\n"
                 "algorithm:\n"
                 "  --alg NAME          default mod-tpgw; --list prints the registered ones\n"
                 "output (any):\n"
                 "  --video FILE.mp4    H.264 via ffmpeg (clip: 50 fps; TTD: the machine's rate + sound)\n"
                 "  --layout L          out (default) | raw-out (raw left, processed right)\n"
                 "  --scale N           integer upscale of the video (default 2)\n"
                 "  --dump DIR          exact RGB of every output frame (rgb_NNNN.zst + dump.json)\n"
                 "  --audio FILE.wav    also keep the machine's sound as a WAV (TTD input only)\n"
                 "  --no-audio          TTD input: a silent video (default: with the machine's sound)\n"
                 "TTD input renders the sound in a continuous pass first and writes the video at\n"
                 "the machine's frame rate (Pentagon 48.83 fps), so picture and sound stay in sync.\n"
                 "  --quiet             no progress line\n"
                 "  --stats             print the algorithm's statistics (stage cost, detector usage)\n";
}

}  // namespace

int main(int argc, char** argv)
{
    std::string ttd, clip, model = "PENTAGON", algName = "mod-tpgw", video, layout = "out", dump, audio;
    long long from = -1, to = -1;
    int scale = 2;
    bool quiet = false, noAudio = false, showStats = false, overscan = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc)
            {
                std::cerr << "missing value for " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--ttd") ttd = next();
        else if (a == "--model") model = next();
        else if (a == "--clip") clip = next();
        else if (a == "--from") from = std::stoll(next());
        else if (a == "--to") to = std::stoll(next());
        else if (a == "--alg") algName = next();
        else if (a == "--video") video = next();
        else if (a == "--layout") layout = next();
        else if (a == "--scale") scale = std::stoi(next());
        else if (a == "--dump") dump = next();
        else if (a == "--audio") audio = next();
        else if (a == "--quiet") quiet = true;
        else if (a == "--no-audio") noAudio = true;
        else if (a == "--stats") showStats = true;
        else if (a == "--overscan") overscan = true;
        else if (a == "--list")
        {
            for (const auto& n : algorithmNames())
                std::cout << n << "\n";
            return 0;
        }
        else
        {
            usage();
            return a == "--help" || a == "-h" ? 0 : 2;
        }
    }
    if (ttd.empty() == clip.empty() || (video.empty() && dump.empty() && audio.empty()) ||
        (layout != "out" && layout != "raw-out") || (!audio.empty() && ttd.empty()))
    {
        usage();
        return 2;
    }
    auto alg = createAlgorithm(algName);
    if (!alg)
    {
        std::cerr << "unknown algorithm " << algName << " (--list)\n";
        return 2;
    }
    const int delay = alg->delay();
    auto rawAlg = createAlgorithm("raw");

    // Output frames [outFrom, outTo]; the input runs delay frames further
    uint64_t first = 0, last = 0;
    std::string error;
    const bool ranged = clip.empty() ? ttdRange(ttd, model, first, last, error) : clipRange(clip, first, last, error);
    if (!ranged)
    {
        std::cerr << error << "\n";
        return 1;
    }
    const uint64_t outFrom = from < 0 ? first : static_cast<uint64_t>(from);
    const uint64_t outTo = to < 0 ? (last >= static_cast<uint64_t>(delay) ? last - delay : first) : static_cast<uint64_t>(to);
    if (outFrom < first || outTo + delay > last || outFrom > outTo)
    {
        std::cerr << "frames " << outFrom << ".." << outTo << " need input up to " << outTo + delay << "; the source has "
                  << first << ".." << last << "\n";
        return 1;
    }

    // TTD input: the sound first, in one continuous run (the per-frame walk below
    // restarts the sound path at every checkpoint - clicks at frame boundaries)
    std::vector<int16_t> sound;
    size_t minSamples = SIZE_MAX, maxSamples = 0;
    std::string wavPath = audio;
    double fps = 50.0;
    const bool withSound = !ttd.empty() && (!audio.empty() || (!video.empty() && !noAudio));
    if (withSound)
    {
        const std::string a = readTtdAudio(ttd, model, outFrom, outTo, sound, minSamples, maxSamples);
        if (!a.empty())
        {
            std::cerr << a << "\n";
            return 1;
        }
        const double frames = static_cast<double>(outTo - outFrom + 1);
        fps = 44100.0 * frames / (static_cast<double>(sound.size()) / 2.0);     // the machine's frame rate
        if (wavPath.empty())
            wavPath = video + ".wav";
        if (!writeWav(wavPath, sound, 44100, error))
        {
            std::cerr << error << "\n";
            return 1;
        }
        std::printf("audio: %.2f s, %zu..%zu samples per frame, %.4f fps\n", sound.size() / 2 / 44100.0, minSamples,
                    maxSamples, fps);
    }

    VideoWriter vw;
    DumpWriter dw;
    bool opened = false;
    RGBImage out, side;
    std::vector<uint8_t> plane, attr, ink;
    std::vector<RGBImage> rawQueue;             // raw pictures waiting for their output (raw-out layout)
    uint64_t written = 0;
    double algoSeconds = 0.0;
    const auto started = std::chrono::steady_clock::now();

    auto onFrame = [&](const SourceFrame& f) -> bool {
        const size_t px = static_cast<size_t>(f.width) * f.height;
        decodePlaneB(f.planeB.data(), px, plane, attr, ink);
        if (!opened)
        {
            const int vwidth = layout == "raw-out" ? f.width * 2 : f.width;
            if (!video.empty() && !vw.open(video, vwidth, f.height, scale, error, fps, withSound && !noAudio ? wavPath : ""))
                return false;
            if (!dump.empty() && !dw.open(dump, f.width, f.height, outFrom, error))
                return false;
            opened = true;
        }
        FrameInput in{f.width, f.height, plane.data(), attr.data(), ink.data(), f.paperX, f.paperY};
        const auto t0 = std::chrono::steady_clock::now();
        alg->process(in, out);
        algoSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        if (layout == "raw-out")
        {
            RGBImage raw(px * 3);
            rawAlg->process(in, raw);
            rawQueue.push_back(std::move(raw));
        }
        // the output belongs to frame f.frame - delay
        if (f.frame < outFrom + delay)
            return true;
        if (!dump.empty())
            dw.write(out.data());
        if (!video.empty())
        {
            if (layout == "out")
                vw.write(out.data());
            else
            {
                const RGBImage& raw = rawQueue.front();
                side.resize(px * 6);
                for (int y = 0; y < f.height; ++y)
                {
                    std::memcpy(&side[static_cast<size_t>(y) * f.width * 6], &raw[static_cast<size_t>(y) * f.width * 3], f.width * 3);
                    std::memcpy(&side[static_cast<size_t>(y) * f.width * 6 + f.width * 3], &out[static_cast<size_t>(y) * f.width * 3], f.width * 3);
                }
                vw.write(side.data());
            }
        }
        if (layout == "raw-out")
            rawQueue.erase(rawQueue.begin());
        ++written;
        if (!quiet && written % 50 == 0)
            std::fprintf(stderr, "\r%llu frames, %.1f ms/frame (algorithm)", static_cast<unsigned long long>(written),
                         1000.0 * algoSeconds / static_cast<double>(written + delay));
        return true;
    };

    // Feed from outFrom - 0: the algorithm's first outputs (during its look-ahead
    // fill) are for frames before outFrom and are not written
    const uint64_t inFrom = outFrom, inTo = outTo + delay;
    const std::string walk = clip.empty() ? readTtd(ttd, model, inFrom, inTo, onFrame, overscan) : readClip(clip, inFrom, inTo, onFrame);
    std::string closeError;
    dw.close(closeError);
    if (!quiet)
        std::fprintf(stderr, "\n");
    if (!error.empty() || !walk.empty() || !closeError.empty())
    {
        std::cerr << (error.empty() ? (walk.empty() ? closeError : walk) : error) << "\n";
        return 1;
    }
    if (withSound && audio.empty())
    {
        vw.close();                              // ffmpeg has read the WAV once it exits
        std::remove(wavPath.c_str());
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("%s: %llu frames (%llu..%llu), algorithm %.2f ms/frame, total %.1f s\n", algName.c_str(),
                static_cast<unsigned long long>(written), static_cast<unsigned long long>(outFrom),
                static_cast<unsigned long long>(outTo), 1000.0 * algoSeconds / static_cast<double>(written + delay), total);
    if (showStats)
        std::printf("%s", alg->stats().c_str());
    return 0;
}
