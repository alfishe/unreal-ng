#include "sdb/image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "sdb/jpeg.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "stb_image.h"

namespace sdb
{

Rgb::Rgb(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) : w(w), h(h), px(static_cast<size_t>(w) * h * 3)
{
    for (size_t i = 0; i < px.size(); i += 3)
    {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
    }
}

Rgb Rotate(const Rgb& s, Rotation r)
{
    switch (r)
    {
        case Rotation::R0:
            return s;
        case Rotation::R180: {
            Rgb o(s.w, s.h);
            for (int y = 0; y < s.h; ++y)
                for (int x = 0; x < s.w; ++x)
                    std::memcpy(o.At(s.w - 1 - x, s.h - 1 - y), s.At(x, y), 3);
            return o;
        }
        case Rotation::R90: {  // clockwise: (x,y) -> (H-1-y, x)
            Rgb o(s.h, s.w);
            for (int y = 0; y < s.h; ++y)
                for (int x = 0; x < s.w; ++x)
                    std::memcpy(o.At(s.h - 1 - y, x), s.At(x, y), 3);
            return o;
        }
        case Rotation::R270: {  // counter-clockwise: (x,y) -> (y, W-1-x)
            Rgb o(s.h, s.w);
            for (int y = 0; y < s.h; ++y)
                for (int x = 0; x < s.w; ++x)
                    std::memcpy(o.At(y, s.w - 1 - x), s.At(x, y), 3);
            return o;
        }
    }
    return s;
}

Rgb MirrorImage(const Rgb& s, Mirror m)
{
    if (m == Mirror::None)
        return s;
    const bool fx = m == Mirror::X || m == Mirror::Both;
    const bool fy = m == Mirror::Y || m == Mirror::Both;
    Rgb o(s.w, s.h);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x)
            std::memcpy(o.At(fx ? s.w - 1 - x : x, fy ? s.h - 1 - y : y), s.At(x, y), 3);
    return o;
}

namespace
{
// Separable triangle-filter resampling (same family as image::imageops::FilterType::Triangle).
struct Weights
{
    std::vector<int> first, count;
    std::vector<float> w;
    int stride = 0;
};

Weights MakeWeights(int src, int dst)
{
    const float scale = static_cast<float>(src) / dst;
    const float support = std::max(1.0f, scale);  // triangle support 1.0, widened when shrinking
    Weights k;
    k.stride = static_cast<int>(std::ceil(support * 2)) + 1;
    k.first.resize(dst);
    k.count.resize(dst);
    k.w.assign(static_cast<size_t>(dst) * k.stride, 0.f);
    for (int i = 0; i < dst; ++i)
    {
        const float centre = (i + 0.5f) * scale;
        const int lo = std::max(0, static_cast<int>(std::floor(centre - support)));
        const int hi = std::min(src - 1, static_cast<int>(std::ceil(centre + support)));
        float sum = 0;
        int n = 0;
        for (int j = lo; j <= hi && n < k.stride; ++j, ++n)
        {
            const float d = std::fabs((j + 0.5f - centre) / support);
            const float wgt = d < 1.f ? 1.f - d : 0.f;
            k.w[static_cast<size_t>(i) * k.stride + n] = wgt;
            sum += wgt;
        }
        if (sum <= 0)
        {
            k.w[static_cast<size_t>(i) * k.stride] = 1;
            sum = 1;
            n = 1;
        }
        for (int q = 0; q < n; ++q)
            k.w[static_cast<size_t>(i) * k.stride + q] /= sum;
        k.first[i] = lo;
        k.count[i] = n;
    }
    return k;
}
}  // namespace

Rgb Resize(const Rgb& s, int w, int h)
{
    if (s.w == w && s.h == h)
        return s;
    if (s.Empty() || w <= 0 || h <= 0)
        throw std::invalid_argument("resize: bad dimensions");
    const Weights hx = MakeWeights(s.w, w), vy = MakeWeights(s.h, h);
    std::vector<float> tmp(static_cast<size_t>(w) * s.h * 3);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
            {
                float acc = 0;
                for (int q = 0; q < hx.count[x]; ++q)
                    acc += hx.w[static_cast<size_t>(x) * hx.stride + q] * s.At(hx.first[x] + q, y)[c];
                tmp[(static_cast<size_t>(y) * w + x) * 3 + c] = acc;
            }
    Rgb o(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
            {
                float acc = 0;
                for (int q = 0; q < vy.count[y]; ++q)
                    acc += vy.w[static_cast<size_t>(y) * vy.stride + q] *
                           tmp[(static_cast<size_t>(vy.first[y] + q) * w + x) * 3 + c];
                o.At(x, y)[c] = static_cast<std::uint8_t>(std::clamp(acc + 0.5f, 0.f, 255.f));
            }
    return o;
}

Rgb Crop(const Rgb& s, int x0, int y0, int w, int h)
{
    Rgb o(w, h);
    for (int y = 0; y < h; ++y)
        std::memcpy(o.At(0, y), s.At(x0, y0 + y), static_cast<size_t>(w) * 3);
    return o;
}

void Blit(Rgb& d, const Rgb& s, int x0, int y0)
{
    for (int y = 0; y < s.h; ++y)
    {
        const int dy = y0 + y;
        if (dy < 0 || dy >= d.h)
            continue;
        const int xs = std::max(0, -x0), xe = std::min(s.w, d.w - x0);
        if (xe > xs)
            std::memcpy(d.At(x0 + xs, dy), s.At(xs, y), static_cast<size_t>(xe - xs) * 3);
    }
}

std::optional<Fit> FitByName(const std::string& n)
{
    if (n == "contain")
        return Fit::Contain;
    if (n == "cover")
        return Fit::Cover;
    if (n == "stretch")
        return Fit::Stretch;
    return std::nullopt;
}

Rgb FitImage(const Rgb& s, int w, int h, Fit fit, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    if (s.Empty() || w <= 0 || h <= 0)
        throw std::invalid_argument("fitImage: bad dimensions");
    if (fit == Fit::Stretch)
        return Resize(s, w, h);
    const double k = fit == Fit::Contain ? std::min(static_cast<double>(w) / s.w, static_cast<double>(h) / s.h)
                                         : std::max(static_cast<double>(w) / s.w, static_cast<double>(h) / s.h);
    const int nw = std::max(1, static_cast<int>(std::lround(s.w * k))),
              nh = std::max(1, static_cast<int>(std::lround(s.h * k)));
    const Rgb scaled = Resize(s, nw, nh);
    if (fit == Fit::Cover)
        return Crop(scaled, (nw - w) / 2, (nh - h) / 2, w, h);
    Rgb out(w, h, r, g, b);
    Blit(out, scaled, (w - nw) / 2, (h - nh) / 2);
    return out;
}

Rgb InsetContent(const Rgb& s, int pad, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    if (pad <= 0 || s.w <= 2 * pad || s.h <= 2 * pad)
        return s;
    Rgb out(s.w, s.h, r, g, b);
    Blit(out, Resize(s, s.w - 2 * pad, s.h - 2 * pad), pad, pad);
    return out;
}

Rgb TrimBorder(const Rgb& s, int tol)
{
    if (s.w < 4 || s.h < 4)
        return s;
    const std::uint8_t* c = s.At(0, 0);
    auto same = [&](const std::uint8_t* p) {
        return std::abs(p[0] - c[0]) <= tol && std::abs(p[1] - c[1]) <= tol && std::abs(p[2] - c[2]) <= tol;
    };
    auto rowSame = [&](int y) {
        for (int x = 0; x < s.w; ++x)
            if (!same(s.At(x, y)))
                return false;
        return true;
    };
    auto colSame = [&](int x, int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            if (!same(s.At(x, y)))
                return false;
        return true;
    };
    int top = 0, bottom = s.h, left = 0, right = s.w;
    while (top < bottom - 1 && rowSame(top))
        ++top;
    while (bottom - 1 > top && rowSame(bottom - 1))
        --bottom;
    while (left < right - 1 && colSame(left, top, bottom))
        ++left;
    while (right - 1 > left && colSame(right - 1, top, bottom))
        --right;
    if (right - left < 2 || bottom - top < 2)
        return s;
    return Crop(s, left, top, right - left, bottom - top);
}

std::optional<Rgb> LoadImage(const std::filesystem::path& file)
{
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(file.string().c_str(), &w, &h, &n, 4);
    if (!data)
        return std::nullopt;
    Rgb o(w, h);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i)
    {
        const unsigned a = data[i * 4 + 3];
        for (int c = 0; c < 3; ++c)
            o.px[i * 3 + c] = static_cast<std::uint8_t>(data[i * 4 + c] * a / 255);
    }
    stbi_image_free(data);
    return o;
}

std::vector<std::uint8_t> EncodeJpeg(const Rgb& img, int quality)
{
    // Baseline JPEG, 4:4:4 (no chroma subsampling): the Stream Deck + / + XL key decoder garbles 4:2:0 into stringy
    // noise (the crate's reference blank JPEG for those models is 4:4:4). Every model accepts 4:4:4. The encoder and
    // its measurement against libjpeg-turbo: sdb/jpeg.h, tools/poc/032-streamdeck-jpeg-encoder.
    if (img.Empty() || img.w > 65535 || img.h > 65535)
        throw std::invalid_argument("JPEG encode: bad dimensions");
    return jpeg::Encode(img.px.data(), img.w, img.h, quality);
}

std::optional<Rgb> DecodeJpeg(const std::vector<std::uint8_t>& data)
{
    if (data.empty() || data.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return std::nullopt;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &n, 3);
    if (!px)
        return std::nullopt;
    Rgb img(w, h);
    std::memcpy(img.px.data(), px, img.px.size());
    stbi_image_free(px);
    return img;
}

std::vector<std::uint8_t> EncodeBmp(const Rgb& img)
{
    const size_t rowBytes = (static_cast<size_t>(img.w) * 3 + 3) & ~size_t{3};
    const size_t dataSize = rowBytes * img.h;
    std::vector<std::uint8_t> b(54 + dataSize, 0);
    auto le32 = [&](size_t o, std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    b[0] = 'B';
    b[1] = 'M';
    le32(2, static_cast<std::uint32_t>(b.size()));
    le32(10, 54);
    le32(14, 40);
    le32(18, static_cast<std::uint32_t>(img.w));
    le32(22, static_cast<std::uint32_t>(img.h));
    b[26] = 1;
    b[28] = 24;
    le32(34, static_cast<std::uint32_t>(dataSize));
    le32(38, 3780);
    le32(42, 3780);  // same resolution fields as the crate's blank BMP blob
    for (int y = 0; y < img.h; ++y)
    {
        std::uint8_t* row = &b[54 + static_cast<size_t>(img.h - 1 - y) * rowBytes];  // bottom-up
        for (int x = 0; x < img.w; ++x)
        {
            const std::uint8_t* p = img.At(x, y);
            row[x * 3] = p[2];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = p[0];  // BGR
        }
    }
    return b;
}

Rgb ToDeviceSpace(const Rgb& src, const ImageFormat& f)
{
    Rgb r = Rotate(src, f.rotation);
    if (r.w != f.w || r.h != f.h)
        r = Resize(r, f.w, f.h);
    return MirrorImage(r, f.mirror);
}

std::optional<Rgb> DecodeBmp(const std::vector<std::uint8_t>& b)
{
    if (b.size() < 54 || b[0] != 'B' || b[1] != 'M')
        return std::nullopt;
    auto le32 = [&](size_t o) {
        return static_cast<std::int32_t>(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) |
                                         (static_cast<std::uint32_t>(b[o + 3]) << 24));
    };
    const std::int32_t off = le32(10), w = le32(18), rawH = le32(22);
    if (w <= 0 || rawH == 0 || (b[28] | (b[29] << 8)) != 24)
        return std::nullopt;
    const int h = std::abs(rawH);
    const size_t row = (static_cast<size_t>(w) * 3 + 3) & ~size_t{3};
    if (static_cast<size_t>(off) + row * h > b.size())
        return std::nullopt;
    Rgb o(w, h);
    for (int y = 0; y < h; ++y)
    {
        const std::uint8_t* src = &b[static_cast<size_t>(off) + static_cast<size_t>(h > 0 ? h - 1 - y : y) * row];
        for (int x = 0; x < w; ++x)
        {
            auto* p = o.At(x, y);
            p[0] = src[x * 3 + 2];
            p[1] = src[x * 3 + 1];
            p[2] = src[x * 3];
        }
    }
    return o;
}

Rgb FromDeviceSpace(const Rgb& d, const ImageFormat& f)
{
    const Rgb u = MirrorImage(d, f.mirror);  // mirroring is its own inverse
    switch (f.rotation)
    {
        case Rotation::R90:
            return Rotate(u, Rotation::R270);
        case Rotation::R270:
            return Rotate(u, Rotation::R90);
        case Rotation::R180:
            return Rotate(u, Rotation::R180);
        case Rotation::R0:
            break;
    }
    return u;
}

std::optional<Rgb> DecodeForFormat(const std::vector<std::uint8_t>& data, const ImageFormat& f)
{
    std::optional<Rgb> img = f.mode == ImageMode::Bmp ? DecodeBmp(data) : DecodeJpeg(data);
    if (!img)
        return std::nullopt;
    return FromDeviceSpace(*img, f);
}

std::vector<std::uint8_t> EncodeForFormat(const Rgb& src, const ImageFormat& f, int q)
{
    const Rgb d = ToDeviceSpace(src, f);
    switch (f.mode)
    {
        case ImageMode::Jpeg:
            return EncodeJpeg(d, q);
        case ImageMode::Bmp:
            return EncodeBmp(d);
        case ImageMode::None:
            return {};
    }
    return {};
}

}  // namespace sdb
