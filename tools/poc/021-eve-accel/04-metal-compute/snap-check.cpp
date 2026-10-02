// eve-accel 04: flatten each snapshot's display list into the op list and evaluate it on
// the CPU, pixel by pixel - the exact arithmetic the GPU kernels use. Checks the result
// against the picture eve-emu drew and reports which frames the op list cannot express.
//
//   snap-check <snapshot>...
#include "eve-snap.h"

#include <chrono>
#include <cstring>
#include <map>

int main(int argc, char** argv)
{
    std::map<std::string, int> reasons;
    int supported = 0, exact = 0, total = 0;
    double cpuUs = 0, uberUs = 0;
    bool listOps = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--ops"))
        {
            listOps = true;
            continue;
        }
        EveSnap::Snapshot s;
        if (!EveSnap::Load(argv[i], s))
        {
            std::fprintf(stderr, "cannot read %s\n", argv[i]);
            continue;
        }
        ++total;
        const EveSnap::Flattened f = EveSnap::Flatten(s);
        if (!f.supported)
        {
            ++reasons[f.reason];
            std::printf("%-60s unsupported: %s\n", argv[i], f.reason.c_str());
            continue;
        }
        ++supported;
        if (listOps)
            for (const EveSnap::Op& op : f.ops)
                std::printf("  op kind %u rect %d..%d x %d..%d format %u filter %u wrap %u/%u A %d B %d D %d E %d blend %u/%u mask %X color %06X/%u\n",
                            op.kind, op.x0, op.x1, op.y0, op.y1, op.format, op.filterMode, op.wrapX, op.wrapY, op.a, op.b,
                            op.d, op.e, op.blendSrc, op.blendDst, op.colorMask, op.colorRgb, op.colorA);
        std::vector<uint32_t> out;
        const auto t0 = std::chrono::steady_clock::now();
        EveSnap::RenderCpu(s, f, out);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        size_t diff = 0, firstDiff = SIZE_MAX;
        for (size_t p = 0; p < out.size(); ++p)
            if (out[p] != s.picture[p])
            {
                if (firstDiff == SIZE_MAX)
                    firstDiff = p;
                ++diff;
            }
        if (diff == 0)
            ++exact;
        cpuUs += s.cpuUs;
        uberUs += us;
        std::printf("%-60s %ux%u ops %zu (bitmaps %u, clears %u), band refs %zu: %s", argv[i], s.width, s.height,
                    f.ops.size(), f.bitmapOps, f.clearOps, f.bandOps.size(), diff ? "DIFFERS" : "exact");
        if (diff)
            std::printf(" (%zu pixels, first at %zu,%zu: ours %08X eve-emu %08X)", diff, firstDiff % s.width,
                        firstDiff / s.width, out[firstDiff], s.picture[firstDiff]);
        std::printf("; eve-emu %.2f ms, op list on one CPU core %.2f ms\n", s.cpuUs / 1000.0, us / 1000.0);
    }
    std::printf("\nframes %d, expressible as an op list %d, bit-exact %d\n", total, supported, exact);
    for (const auto& r : reasons)
        std::printf("  unsupported (%s): %d\n", r.first.c_str(), r.second);
    if (supported)
        std::printf("mean per frame: eve-emu %.2f ms, op-list evaluation (scalar, 1 core) %.2f ms\n",
                    cpuUs / supported / 1000.0, uberUs / supported / 1000.0);
    return exact == supported ? 0 : 1;
}
