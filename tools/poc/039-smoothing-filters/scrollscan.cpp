// POC 039: does a capture scroll smoothly? Per consecutive pair the vector within +-8 px that explains most of the
// changed pixels (every 2nd pixel sampled), then: frames that changed, frames a non-zero vector explains (at least
// half of the change), and the most common such vectors.
//
//   scrollscan <seq.rgba> <width> <height> <count>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 5)
        return 2;
    const int w = std::atoi(argv[2]), h = std::atoi(argv[3]), n = std::atoi(argv[4]);
    std::vector<uint32_t> f(static_cast<size_t>(w) * h * n);
    FILE* in = std::fopen(argv[1], "rb");
    if (!in || std::fread(f.data(), 4, f.size(), in) != f.size())
        return 1;
    std::fclose(in);
    int changedFrames = 0, scrolled = 0;
    double changedShare = 0;
    std::map<std::pair<int, int>, int> votes;
    for (int k = 1; k < n; k++)
    {
        const uint32_t* a = &f[static_cast<size_t>(k - 1) * w * h];
        const uint32_t* b = &f[static_cast<size_t>(k) * w * h];
        int changed = 0;
        for (int y = 8; y < h - 8; y += 2)
            for (int x = 8; x < w - 8; x += 2)
                changed += a[y * w + x] != b[y * w + x];
        if (changed == 0)
            continue;
        changedFrames++;
        changedShare += changed * 4.0 / ((w - 16) * (h - 16));
        int best = 0, bx = 0, by = 0;
        for (int dy = -8; dy <= 8; dy++)
            for (int dx = -8; dx <= 8; dx++)
            {
                if (!dx && !dy)
                    continue;
                int ok = 0;
                for (int y = 8; y < h - 8; y += 2)
                    for (int x = 8; x < w - 8; x += 2)
                        ok += a[y * w + x] != b[y * w + x] && a[(y - dy) * w + x - dx] == b[y * w + x];
                if (ok > best)
                    best = ok, bx = dx, by = dy;
            }
        if (best * 2 >= changed && changed > 200)
        {
            scrolled++;
            votes[{bx, by}]++;
        }
    }
    std::printf("changed %d/%d frames (%.1f%% px), scrolled %d", changedFrames, n - 1,
                changedFrames ? changedShare * 100 / changedFrames : 0.0, scrolled);
    std::vector<std::pair<int, std::pair<int, int>>> top;
    for (auto& [v, c] : votes)
        top.push_back({c, v});
    std::sort(top.rbegin(), top.rend());
    for (size_t i = 0; i < top.size() && i < 4; i++)
        std::printf("  (%d,%d)x%d", top[i].second.first, top[i].second.second, top[i].first);
    std::printf("\n");
    return 0;
}
