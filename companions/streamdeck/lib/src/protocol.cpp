#include "sdb/protocol.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace sdb::proto
{
namespace
{
constexpr bool IsOriginalV1(const Model& m)
{
    return m.pid == 0x0060;
}

struct Chunking
{
    std::size_t reportLen, headerLen, payloadLen;
};

template <class Header>
std::vector<Report> Chunked(std::span<const std::uint8_t> data, const Chunking& c, Header&& header)
{
    std::vector<Report> out;
    std::size_t sent = 0;
    std::size_t page = 0;
    while (sent < data.size())
    {
        const std::size_t n = std::min(data.size() - sent, c.payloadLen);
        // The report built at its final size, header and payload copied in: growing the header vector (insert,
        // resize) makes gcc 15 at -O3 report a false -Warray-bounds in the inlined growth path
        const Report h = header(page, n, sent + n == data.size());
        Report r(c.reportLen, 0);
        const std::size_t hn = std::min(h.size(), r.size());
        std::memcpy(r.data(), h.data(), hn);
        std::memcpy(r.data() + hn, data.data() + sent, std::min(n, r.size() - hn));
        out.push_back(std::move(r));
        sent += n;
        ++page;
    }
    return out;
}
constexpr std::uint8_t Lo(std::size_t v)
{
    return static_cast<std::uint8_t>(v & 0xff);
}
constexpr std::uint8_t Hi(std::size_t v)
{
    return static_cast<std::uint8_t>((v >> 8) & 0xff);
}
}  // namespace

Report Brightness(const Model& m, int percent)
{
    const auto p = static_cast<std::uint8_t>(std::clamp(percent, 0, 100));
    if (m.family == Family::V1)
    {
        Report r{0x05, 0x55, 0xAA, 0xD1, 0x01, p};
        r.resize(17, 0);
        return r;
    }
    Report r{0x03, 0x08, p};
    r.resize(32, 0);
    return r;
}

Report Reset(const Model& m)
{
    if (m.family == Family::V1)
    {
        Report r{0x0B, 0x63};
        r.resize(17, 0);
        return r;
    }
    Report r{0x03, 0x02};
    r.resize(32, 0);
    return r;
}

Report TouchLed(const Model& m, int point, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    if (point < 0 || point >= m.touchPoints)
        throw std::out_of_range("touch point");
    return {0x03, 0x06, static_cast<std::uint8_t>(m.keys + point), r, g, b};
}

int FlipKeyOriginal(const Model& m, int key)
{
    const int col = key % m.cols;
    return (key - col) + (m.cols - 1 - col);
}

std::vector<Report> KeyImage(const Model& m, int key, std::span<const std::uint8_t> data)
{
    if (key < 0 || key >= m.keys)
        throw std::out_of_range("key index");
    if (!m.HasKeyScreens())
        throw std::logic_error("model has no key screens");
    if (IsOriginalV1(m))
    {
        key = FlipKeyOriginal(m, key);
        const std::size_t payload = std::max<std::size_t>(1, data.size() / 2);  // exactly two reports
        return Chunked(data, {8191, 16, payload}, [&](std::size_t page, std::size_t, bool last) {
            Report h{0x02,
                     0x01,
                     static_cast<std::uint8_t>(page + 1),
                     0,
                     static_cast<std::uint8_t>(last),
                     static_cast<std::uint8_t>(key + 1)};
            h.resize(16, 0);
            return h;
        });
    }
    if (m.family == Family::V1)
    {
        return Chunked(data, {1024, 16, 1024 - 16}, [&](std::size_t page, std::size_t, bool last) {
            Report h{0x02,
                     0x01,
                     static_cast<std::uint8_t>(page),
                     0,
                     static_cast<std::uint8_t>(last),
                     static_cast<std::uint8_t>(key + 1)};
            h.resize(16, 0);
            return h;
        });
    }
    return Chunked(data, {1024, 8, 1024 - 8}, [&](std::size_t page, std::size_t n, bool last) {
        return Report{0x02,     0x07,    static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(last), Lo(n), Hi(n),
                      Lo(page), Hi(page)};
    });
}

std::vector<Report> LcdFill(const Model& m, std::span<const std::uint8_t> data)
{
    switch (m.lcdKind)
    {
        case LcdKind::Fill:
            return Chunked(data, {1024, 8, 1024 - 8}, [](std::size_t page, std::size_t n, bool last) {
                return Report{0x02, 0x0B, 0x00, static_cast<std::uint8_t>(last), Lo(n), Hi(n), Lo(page), Hi(page)};
            });
        case LcdKind::Region:
            return LcdRegion(m, 0, 0, m.lcd.w, m.lcd.h, data);
        case LcdKind::None:
            break;
    }
    throw std::logic_error("model has no LCD");
}

std::vector<Report> LcdRegion(const Model& m, int x, int y, int w, int h, std::span<const std::uint8_t> data)
{
    if (m.lcdKind != LcdKind::Region)
        throw std::logic_error("model has no LCD regions");
    return Chunked(data, {1024, 16, 1024 - 16}, [&](std::size_t page, std::size_t n, bool last) {
        return Report{
            0x02,     0x0C,     Lo(x), Hi(x), Lo(y), Hi(y), Lo(w), Hi(w), Lo(h), Hi(h), static_cast<std::uint8_t>(last),
            Lo(page), Hi(page), Lo(n), Hi(n), 0x00};
    });
}

namespace
{
std::vector<Report> V2Image(std::uint8_t cmd, std::span<const std::uint8_t> data)
{
    return Chunked(data, {1024, 8, 1024 - 8}, [cmd](std::size_t page, std::size_t n, bool last) {
        return Report{0x02, cmd, 0x00, static_cast<std::uint8_t>(last), Lo(n), Hi(n), Lo(page), Hi(page)};
    });
}
Report Feature32(std::initializer_list<std::uint8_t> head)
{
    Report r(head);
    r.resize(32, 0);
    return r;
}
}  // namespace

std::vector<Report> FullScreen(const Model& m, std::span<const std::uint8_t> data)
{
    if (!m.HasFullScreen())
        throw std::logic_error("model has no full-screen image command");
    return V2Image(0x08, data);
}
std::vector<Report> WindowImage(const Model& m, std::span<const std::uint8_t> data)
{
    if (!m.windowFillCmd)
        throw std::logic_error("model has no window image command");
    return V2Image(0x0B, data);
}
Report FillKeyColor(const Model& m, int key, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    if (m.family == Family::V1)
        throw std::logic_error("fill colour is not in the Mini/Original protocol");
    if (key < 0 || key >= m.TotalButtons())
        throw std::out_of_range("key index");
    return Feature32({0x03, 0x06, static_cast<std::uint8_t>(key), r, g, b});
}
Report FillScreenColor(const Model& m, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    if (m.family == Family::V1)
        throw std::logic_error("fill colour is not in the Mini/Original protocol");
    return Feature32({0x03, 0x05, r, g, b});
}
Report SleepAfter(const Model& m, std::int32_t s)
{
    const auto u = static_cast<std::uint32_t>(s);
    const std::uint8_t b0 = u & 0xff, b1 = (u >> 8) & 0xff, b2 = (u >> 16) & 0xff, b3 = u >> 24;
    if (m.family == Family::V1)
        return Feature32({0x0B, 0xA2, b0, b1, b2, b3});
    return Feature32({0x03, 0x0D, b0, b1, b2, b3});
}
Report ShowBackground(const Model&, int index)
{
    return Feature32({0x03, 0x13, static_cast<std::uint8_t>(index)});
}

FeatureRead SerialRead(const Model& m)
{
    if (m.pid == 0x0060 || m.pid == 0x0063)
        return {0x03, 17, 5};
    if (m.pid == 0x0090 || m.pid == 0x00B3 || m.pid == 0x00B8)
        return {0x03, 32, 5};
    return {0x06, 32, 2};
}

FeatureRead FirmwareRead(const Model& m)
{
    // Docs: V2 "AP2" = 0x05 (12 bytes: len, UINT32 checksum, 8 ASCII -> text from byte 6); Mini family AP2 = 0xA1 (text
    // from 5).
    if (m.pid == 0x0060)
        return {0x04, 17, 5};  // Original v1: crate
    if (m.family == Family::V1)
        return {0xA1, 17, 5};
    return {0x05, 32, 6};
}
FeatureRead FirmwareReadFallback(const Model& m)
{
    if (m.family == Family::V1)
        return {0x04, 17, 5};  // older Mini firmware (crate / python-elgato-streamdeck)
    return {0x05, 32, 6};
}
FeatureRead SleepRead(const Model& m)
{
    return m.family == Family::V1 ? FeatureRead{0xA3, 32, 2} : FeatureRead{0x0A, 32, 2};
}

std::size_t InputReportLength(const Model& m)
{
    if (m.dials > 0)
        return static_cast<std::size_t>(std::max(6 + m.keys, 5 + m.dials));  // Plus / Plus XL
    if (m.family == Family::V1)
        return 1u + m.keys;
    return 4u + m.keys + m.touchPoints;
}

}  // namespace sdb::proto
