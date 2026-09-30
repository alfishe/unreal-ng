/// @file bench.cpp
/// @brief zxdlss-bench: per-frame cost of an algorithm on a clip range, input
/// decoded up front so only the algorithm is timed.
///
///   zxdlss-bench --clip data/clip_v2 --from 12100 --to 12300 [--alg mod-tpgw] [--repeat 3]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "frames.h"
#include "emulator/video/zxdlss/algorithm.h"

using namespace zxdlss;

int main(int argc, char** argv)
{
    std::string clip, algName = "mod-tpgw";
    long long from = -1, to = -1;
    int repeat = 3;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string a = argv[i], v = argv[i + 1];
        if (a == "--clip") clip = v;
        else if (a == "--from") from = std::stoll(v);
        else if (a == "--to") to = std::stoll(v);
        else if (a == "--alg") algName = v;
        else if (a == "--repeat") repeat = std::stoi(v);
    }
    if (clip.empty() || from < 0 || to < from)
    {
        std::cerr << "usage: zxdlss-bench --clip DIR --from N --to N [--alg NAME] [--repeat N]\n";
        return 2;
    }
    struct Decoded
    {
        int w, h;
        std::vector<uint8_t> plane, attr, ink;
        std::array<uint32_t, 16> palette;
    };
    std::vector<Decoded> frames;
    const std::string err = readClip(clip, from, to, [&](const SourceFrame& f) {
        Decoded d{f.width, f.height, {}, {}, {}, f.palette};
        decodePlaneB(f.planeB.data(), f.planeB.size(), d.plane, d.attr, d.ink);
        frames.push_back(std::move(d));
        return true;
    });
    if (!err.empty())
    {
        std::cerr << err << "\n";
        return 1;
    }
    std::vector<double> perRun;
    std::string stats;
    for (int r = 0; r < repeat; ++r)
    {
        auto alg = createAlgorithm(algName);
        if (!alg)
        {
            std::cerr << "unknown algorithm " << algName << "\n";
            return 2;
        }
        RGBImage out;
        const auto t0 = std::chrono::steady_clock::now();
        for (const Decoded& d : frames)
            alg->process(FrameInput{d.w, d.h, d.plane.data(), d.attr.data(), d.ink.data(), 48, 48, d.palette.data()}, out);
        perRun.push_back(1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() /
                         static_cast<double>(frames.size()));
        stats = alg->stats();
    }
    std::sort(perRun.begin(), perRun.end());
    std::printf("%s: %zu frames, best %.3f ms/frame, median %.3f ms/frame (%d runs)\n", algName.c_str(), frames.size(),
                perRun.front(), perRun[perRun.size() / 2], repeat);
    if (!stats.empty())
        std::printf("%s", stats.c_str());
    return 0;
}
