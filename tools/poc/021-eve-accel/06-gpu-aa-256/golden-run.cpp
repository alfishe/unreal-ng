// eve-accel 06: run BT8XX golden cases (eve-emu testdata/golden/<case>: script + the
// reference emulator's frame) on an eve-emu variant and measure how far its picture is
// from the reference. The antialiasing model is chosen by EVE_AA_MODEL (aa256 variant).
//
//   golden-run <case dir>...
//
// Per case: pixels that differ, the largest channel difference, the sum of absolute
// channel differences, and the time spent drawing (EveAdvance during the frames).
#include <eve/eve.h>

#include <zlib.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{

std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> Inflate(const std::vector<uint8_t>& packed)
{
    std::vector<uint8_t> out(64 * 1024 * 1024);
    uLongf size = out.size();
    if (uncompress(out.data(), &size, packed.data(), packed.size()) != Z_OK)
        return {};
    out.resize(size);
    return out;
}

struct Host
{
    EveChip* chip = nullptr;
    std::vector<uint32_t> frame;
    uint32_t width = 0, height = 0;
    double drawSeconds = 0;

    void Command(uint8_t c, uint8_t p)
    {
        EveSelect(chip, 1);
        EveExchange(chip, c);
        EveExchange(chip, p);
        EveExchange(chip, 0);
        EveSelect(chip, 0);
    }
    void Write(uint32_t address, const std::vector<uint8_t>& data)
    {
        EveSelect(chip, 1);
        EveExchange(chip, static_cast<uint8_t>(0x80 | ((address >> 16) & 0x3F)));
        EveExchange(chip, static_cast<uint8_t>(address >> 8));
        EveExchange(chip, static_cast<uint8_t>(address));
        for (uint8_t b : data)
            EveExchange(chip, b);
        EveSelect(chip, 0);
    }
    uint32_t Read16(uint32_t address)
    {
        EveSelect(chip, 1);
        EveExchange(chip, static_cast<uint8_t>((address >> 16) & 0x3F));
        EveExchange(chip, static_cast<uint8_t>(address >> 8));
        EveExchange(chip, static_cast<uint8_t>(address));
        EveExchange(chip, 0);
        const uint32_t lo = EveExchange(chip, 0), hi = EveExchange(chip, 0);
        EveSelect(chip, 0);
        return lo | (hi << 8);
    }
    void Advance(uint64_t clocks)
    {
        const auto t0 = std::chrono::steady_clock::now();
        EveAdvance(chip, clocks);
        drawSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    void NextFrame()
    {
        const uint64_t frames = EveCompletedFrames(chip);
        while (EveCompletedFrames(chip) == frames)
            Advance(EveClocksToNextEvent(chip));
    }
};

} // namespace

int main(int argc, char** argv)
{
    const char* romPath = std::getenv("EVE_ROM");
    const std::vector<uint8_t> rom = romPath ? ReadFile(romPath) : std::vector<uint8_t>();
    const char* model = std::getenv("EVE_AA_MODEL");
    std::printf("model %s\n", model ? model : "table");
    std::printf("%-16s %10s %10s %8s %12s %10s\n", "case", "pixels", "differ", "max", "sum |diff|", "draw ms");
    uint64_t allDiffer = 0, allPixels = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string dir = argv[i];
        EveConfig config{};
        config.structSize = sizeof(config);
        config.model = EVE_MODEL_FT812;
        config.externalClockHz = 12000000;
        config.romImage = rom.empty() ? nullptr : rom.data();
        config.romImageSize = rom.size();
        Host host;
        host.chip = EveCreate(&config);
        if (!host.chip)
            return 2;
        std::ifstream script(dir + "/script.txt");
        std::string line;
        while (std::getline(script, line))
        {
            std::istringstream words(line);
            std::string op, a, b;
            words >> op >> a >> b;
            if (op == "cmd")
                host.Command(static_cast<uint8_t>(std::stoul(a, nullptr, 16)), static_cast<uint8_t>(std::stoul(b, nullptr, 16)));
            else if (op == "write")
                host.Write(static_cast<uint32_t>(std::stoul(a, nullptr, 16)), ReadFile(dir + "/" + b));
            else if (op == "wait")
                host.Advance(static_cast<uint64_t>(EveSystemClockHz(host.chip)) * std::stoul(a) / 1000);
            else if (op == "idle" || op == "space")
            {
                const uint32_t need = op == "idle" ? 0xFFC : static_cast<uint32_t>(std::stoul(a, nullptr, 16));
                for (int k = 0; k < 100000; ++k)
                {
                    const uint32_t space = host.Read16(0x302574);
                    if (space >= need || (space & 3))
                        break;
                    host.Advance(1000);
                }
            }
            else if (op == "frame")
            {
                if (host.width == 0)
                {
                    EveTiming t{};
                    EveGetTiming(host.chip, &t);
                    host.width = t.hsize;
                    host.height = t.vsize;
                    host.frame.assign(static_cast<size_t>(host.width) * host.height, 0);
                    EveSetOutput(host.chip, host.frame.data(), host.width, host.width, host.height, 1);
                }
                host.drawSeconds = 0;
                for (int k = 0; k < 3; ++k)
                    host.NextFrame();
                const std::vector<uint8_t> theirs = Inflate(ReadFile(dir + "/" + a + ".z"));
                uint32_t w = 0, h = 0;
                if (theirs.size() >= 8)
                {
                    std::memcpy(&w, theirs.data(), 4);
                    std::memcpy(&h, theirs.data() + 4, 4);
                }
                if (w != host.width || h != host.height)
                {
                    std::printf("%-16s size differs\n", dir.substr(dir.find_last_of('/') + 1).c_str());
                    continue;
                }
                uint64_t differ = 0, sum = 0;
                uint32_t worst = 0;
                for (size_t p = 0; p < static_cast<size_t>(w) * h; ++p)
                {
                    uint32_t oracle = 0;
                    std::memcpy(&oracle, theirs.data() + 8 + 4 * p, 4);
                    const uint32_t mine = host.frame[p];
                    bool d = false;
                    for (int sh = 0; sh < 24; sh += 8)
                    {
                        const int x = static_cast<int>((oracle >> sh) & 255) - static_cast<int>((mine >> sh) & 255);
                        const uint32_t ad = static_cast<uint32_t>(x < 0 ? -x : x);
                        sum += ad;
                        worst = ad > worst ? ad : worst;
                        d = d || ad != 0;
                    }
                    differ += d;
                }
                allDiffer += differ;
                allPixels += static_cast<uint64_t>(w) * h;
                std::printf("%-16s %10u %10llu %8u %12llu %10.2f\n", dir.substr(dir.find_last_of('/') + 1).c_str(), w * h,
                            (unsigned long long)differ, worst, (unsigned long long)sum, host.drawSeconds * 1000.0 / 3);
            }
        }
        EveDestroy(host.chip);
    }
    std::printf("total: %llu of %llu pixels differ\n", (unsigned long long)allDiffer, (unsigned long long)allPixels);
    return 0;
}
