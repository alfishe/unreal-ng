#include "registry.h"

#include "palette.h"

namespace zxdlss
{

namespace
{
void registerRaw(std::map<std::string, Factory>& registry);
}

std::map<std::string, Factory>& registry()
{
    static std::map<std::string, Factory> algorithms = [] {
        std::map<std::string, Factory> builtIn;
        registerRaw(builtIn);
        registerModTpgw(builtIn);
        return builtIn;
    }();
    return algorithms;
}

std::unique_ptr<Algorithm> createAlgorithm(const std::string& name)
{
    auto it = registry().find(name);
    return it == registry().end() ? nullptr : it->second();
}

std::vector<std::string> algorithmNames()
{
    std::vector<std::string> names;
    for (const auto& [name, factory] : registry())
        names.push_back(name);
    return names;
}

void decodePlaneB(const uint16_t* planeB, size_t pixels, std::vector<uint8_t>& plane, std::vector<uint8_t>& attr,
                  std::vector<uint8_t>& ink)
{
    plane.resize(pixels);
    attr.resize(pixels);
    ink.resize(pixels);
    for (size_t i = 0; i < pixels; ++i)
    {
        const uint16_t v = planeB[i];
        attr[i] = static_cast<uint8_t>(v & 0xFF);
        plane[i] = static_cast<uint8_t>((v >> 8) & 0x0F);
        ink[i] = static_cast<uint8_t>((v >> 12) & 1);
    }
}

namespace
{

/// No processing: the raw frame (for checking the tools and the video path).
class Raw final : public Algorithm
{
public:
    int delay() const override { return 0; }
    std::string name() const override { return "raw"; }
    void process(const FrameInput& in, RGBImage& out) override
    {
        if (!_pal.sameAs(in.palette))
            _pal = Palette::fromRGBA(in.palette);
        const size_t px = static_cast<size_t>(in.width) * in.height;
        out.resize(px * 3);
        for (size_t p = 0; p < px; ++p)
            for (int k = 0; k < 3; ++k)
                out[p * 3 + k] = Palette::linearToSrgb(0.0 + _pal.linear[in.plane[p]][k]);
    }

private:
    Palette _pal;
};

void registerRaw(std::map<std::string, Factory>& registry)
{
    registry["raw"] = [] { return std::make_unique<Raw>(); };
}

}  // namespace
}  // namespace zxdlss
