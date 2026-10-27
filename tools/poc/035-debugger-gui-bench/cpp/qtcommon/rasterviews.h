#pragma once

// Own CPU raster for the dense debugger views, shared by the Qt Widgets and Qt Quick front ends.
//   - every view keeps a QImage (RGB32, device pixels) and changes only the pixels whose data changed;
//   - text is not laid out per frame: glyphs are rendered once per (ink, background) pair into an atlas and copied;
//   - Frame() returns the changed region in LOGICAL coordinates, for the front end to repaint or re-upload.
// Nothing here needs a GPU; with one, the image is a texture.

#include "../common/layout.h"
#include "../common/workload.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QRegion>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace raster
{
using namespace bench;
namespace L = bench::layout;

inline QColor C(uint32_t rgb) { return QColor::fromRgb((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
inline uint32_t Px(uint32_t rgb) { return 0xFF000000u | rgb; }

inline uint32_t Blend(uint32_t bg, uint32_t fg, uint32_t a)
{
    if (a == 0)
        return bg;
    if (a == 255)
        return 0xFF000000u | fg;
    const uint32_t ia = 255 - a;
    const uint32_t r = (((fg >> 16) & 255) * a + ((bg >> 16) & 255) * ia + 127) / 255;
    const uint32_t g = (((fg >> 8) & 255) * a + ((bg >> 8) & 255) * ia + 127) / 255;
    const uint32_t b = ((fg & 255) * a + (bg & 255) * ia + 127) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/// Printable ASCII rendered once as coverage (8-bit alpha), plus opaque cells per (ink, background) pair built on demand
class GlyphAtlas
{
public:
    void Build(float fontPx, float k)
    {
        QFont f(L::kFontFamily);
        f.setPixelSize(int(std::lround(fontPx * k)));
        f.setStyleHint(QFont::Monospace);
        const QFontMetricsF fm(f);
        _adv = fm.horizontalAdvance(QLatin1Char('0'));
        _cw = int(std::ceil(_adv));
        _ch = int(std::lround(L::T().lineH * k));
        _ascent = fm.ascent();
        _cov.assign(size_t(95) * _cw * _ch, 0);
        QImage tmp(_cw, _ch, QImage::Format_ARGB32_Premultiplied);
        for (int c = 32; c < 127; c++)
        {
            tmp.fill(Qt::transparent);
            QPainter p(&tmp);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(QPointF(0, _ascent + (_ch - fm.height()) * 0.5), QString(QChar(c)));
            p.end();
            uint8_t* dst = &_cov[size_t(c - 32) * _cw * _ch];
            for (int y = 0; y < _ch; y++)
            {
                const uint32_t* row = reinterpret_cast<const uint32_t*>(tmp.constScanLine(y));
                for (int x = 0; x < _cw; x++)
                    dst[y * _cw + x] = uint8_t(row[x] >> 24);
            }
        }
        _pairs.clear();
    }

    float Advance() const { return _adv; }
    int CellW() const { return _cw; }
    int CellH() const { return _ch; }

    /// Opaque copy of a string (ink on an opaque background); x positions follow the monospace advance
    void DrawOpaque(QImage& img, float x, int y, const char* s, size_t n, uint32_t ink, uint32_t bg)
    {
        const std::vector<uint32_t>& cells = Pair(ink, bg);
        for (size_t i = 0; i < n; i++)
        {
            const int c = (unsigned char)s[i];
            const int gi = (c >= 32 && c < 127) ? c - 32 : '.' - 32;
            const int gx = int(std::lround(x + i * _adv));
            Copy(img, gx, y, &cells[size_t(gi) * _cw * _ch]);
        }
    }

    /// Blended text over whatever is in the image (labels on top of bars)
    void DrawBlend(QImage& img, float x, int y, const char* s, size_t n, uint32_t ink)
    {
        for (size_t i = 0; i < n; i++)
        {
            const int c = (unsigned char)s[i];
            if (c <= 32 || c >= 127)
                continue;
            const uint8_t* cov = &_cov[size_t(c - 32) * _cw * _ch];
            const int gx = int(std::lround(x + i * _adv));
            for (int yy = 0; yy < _ch; yy++)
            {
                if (y + yy < 0 || y + yy >= img.height())
                    continue;
                uint32_t* row = reinterpret_cast<uint32_t*>(img.scanLine(y + yy));
                for (int xx = 0; xx < _cw; xx++)
                {
                    const int px = gx + xx;
                    if (px >= 0 && px < img.width())
                        row[px] = Blend(row[px], ink, cov[yy * _cw + xx]);
                }
            }
        }
    }

private:
    const std::vector<uint32_t>& Pair(uint32_t ink, uint32_t bg)
    {
        const uint64_t key = (uint64_t(ink) << 32) | bg;
        auto it = _pairs.find(key);
        if (it != _pairs.end())
            return it->second;
        std::vector<uint32_t> cells(_cov.size());
        for (size_t i = 0; i < _cov.size(); i++)
            cells[i] = Blend(Px(bg), ink, _cov[i]);
        return _pairs.emplace(key, std::move(cells)).first->second;
    }

    void Copy(QImage& img, int x, int y, const uint32_t* cell)
    {
        const int x0 = std::max(0, x), x1 = std::min(img.width(), x + _cw);
        if (x1 <= x0)
            return;
        for (int yy = 0; yy < _ch; yy++)
        {
            if (y + yy < 0 || y + yy >= img.height())
                continue;
            uint32_t* row = reinterpret_cast<uint32_t*>(img.scanLine(y + yy));
            std::memcpy(row + x0, cell + yy * _cw + (x0 - x), size_t(x1 - x0) * 4);
        }
    }

    float _adv = 7, _ascent = 10;
    int _cw = 8, _ch = 16;
    std::vector<uint8_t> _cov;
    std::map<uint64_t, std::vector<uint32_t>> _pairs;
};

inline void FillRect(QImage& img, int x, int y, int w, int h, uint32_t rgb)
{
    const int x0 = std::max(0, x), x1 = std::min(img.width(), x + w);
    const int y0 = std::max(0, y), y1 = std::min(img.height(), y + h);
    const uint32_t v = Px(rgb);
    for (int yy = y0; yy < y1; yy++)
    {
        uint32_t* row = reinterpret_cast<uint32_t*>(img.scanLine(yy));
        std::fill(row + x0, row + x1, v);
    }
}

/// Base of the views: Frame(size, k) keeps the image at size * k device pixels and returns the changed logical region
class RasterView
{
public:
    explicit RasterView(Workload& w) : _w(w) {}
    virtual ~RasterView() = default;

    const QImage& Image() const { return _img; }
    float Scale() const { return _k; }

    QRegion Frame(QSize logical, float k)
    {
        bool full = false;
        if (_img.isNull() || k != _k || _fontPx != L::T().fontPx || logical != _logical)
        {
            _k = k;
            _logical = logical;
            _fontPx = L::T().fontPx;
            _img = QImage(int(std::lround(logical.width() * k)), int(std::lround(logical.height() * k)), QImage::Format_RGB32);
            _img.fill(C(L::kPanel));
            _atlas.Build(_fontPx, k);
            Reset();
            full = true;
        }
        QRegion dirty = Update(full);
        return full ? QRegion(QRect(QPoint(0, 0), logical)) : dirty;
    }

protected:
    virtual void Reset() {}
    /// Update the image; return the changed region in logical coordinates (Logical() converts device rectangles)
    virtual QRegion Update(bool full) = 0;

    QRect Logical(int x, int y, int w, int h) const
    {
        const int lx = int(std::floor(x / _k)), ly = int(std::floor(y / _k));
        return QRect(lx, ly, int(std::ceil((x + w) / _k)) - lx, int(std::ceil((y + h) / _k)) - ly);
    }

    Workload& _w;
    QImage _img;
    GlyphAtlas _atlas;
    float _k = 0;
    float _fontPx = 0;
    QSize _logical;
};

class HexRaster : public RasterView
{
public:
    explicit HexRaster(Workload& w) : RasterView(w) {}

protected:
    void Reset() override
    {
        for (auto& row : _val)
            row.fill(-1);
        for (auto& row : _chg)
            row.fill(0);
        for (auto& s : _ascii)
            s.clear();
    }

    QRegion Update(bool full) override
    {
        const L::TextMetrics& t = L::T();
        const float k = _k;
        const int lineH = int(std::lround(t.lineH * k));
        const uint8_t* mem = _w.Memory();
        QRegion dirty;
        for (int r = 0; r < t.hexRows; r++)
        {
            const int y = int(std::lround(r * t.lineH * k));
            if (full)
            {
                const std::string& a = _w.HexRowAddr(r);
                _atlas.DrawOpaque(_img, t.hexAddrX * k, y, a.data(), a.size(), L::kMuted, L::kPanel);
            }
            for (int c = 0; c < kHexCols; c++)
            {
                const int v = mem[(_w.HexBase() + r * kHexCols + c) & 0xFFFF];
                const uint8_t ch = _w.HexChanged(r, c) ? 1 : 0;
                if (v == _val[r][c] && ch == _chg[r][c])
                    continue;
                _val[r][c] = v;
                _chg[r][c] = ch;
                const float x = (t.hexBytesX + c * t.hexByteStep) * k;
                const int bx = int(std::lround(x - 2 * k)), bw = int(std::lround(t.hexChangedW * k));
                FillRect(_img, bx, y, bw, lineH - int(k), ch ? L::kChangedBg : L::kPanel);
                FillRect(_img, bx, y + lineH - int(k), bw, int(k), L::kPanel);
                _atlas.DrawOpaque(_img, x, y, Workload::Hex2(uint8_t(v)), 2, ch ? L::kAccent : L::kFg,
                                  ch ? L::kChangedBg : L::kPanel);
                dirty += Logical(bx, y, bw, lineH);
            }
            const std::string& ascii = _w.HexRowAscii(r);
            if (ascii != _ascii[r])
            {
                for (size_t i = 0; i < ascii.size(); i++)
                {
                    if (i < _ascii[r].size() && _ascii[r][i] == ascii[i])
                        continue;
                    const float x = t.hexAsciiX * k + i * _atlas.Advance();
                    _atlas.DrawOpaque(_img, x, y, &ascii[i], 1, L::kMuted, L::kPanel);
                    dirty += Logical(int(x), y, _atlas.CellW() + 1, lineH);
                }
                _ascii[r] = ascii;
            }
        }
        return dirty;
    }

private:
    std::array<std::array<int, kHexCols>, kHexRows> _val{};
    std::array<std::array<uint8_t, kHexCols>, kHexRows> _chg{};
    std::array<std::string, kHexRows> _ascii;
};

class DisasmRaster : public RasterView
{
public:
    explicit DisasmRaster(Workload& w) : RasterView(w)
    {
        // 8 px anti-aliased disc for breakpoints
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
            {
                const float dx = x + 0.5f - 4, dy = y + 0.5f - 4;
                _disc[y * 8 + x] = uint8_t(std::clamp((4.0f - std::sqrt(dx * dx + dy * dy)) * 255.f + 128.f, 0.f, 255.f));
            }
    }

protected:
    void Reset() override
    {
        for (auto& r : _rows)
            r = Row{};
    }

    QRegion Update(bool) override
    {
        const L::TextMetrics& t = L::T();
        const float k = _k;
        const int lineH = int(std::lround(t.lineH * k));
        QRegion dirty;
        for (int i = 0; i < t.disRows; i++)
        {
            const auto& l = _w.Disasm()[i];
            Row n{l.addr, l.bytes, l.text, l.comment, l.isPc, l.isBreakpoint, true};
            if (n == _rows[i])
                continue;
            _rows[i] = n;
            const int y = int(std::lround(i * t.lineH * k));
            const uint32_t bg = l.isPc ? L::kPcRowBg : L::kPanel;
            FillRect(_img, 0, y, _img.width(), lineH, bg);
            if (l.isBreakpoint)
            {
                const int cy = y + lineH / 2 - 4, cx = int(1 * k);
                for (int yy = 0; yy < 8; yy++)
                {
                    uint32_t* row = reinterpret_cast<uint32_t*>(_img.scanLine(cy + yy));
                    for (int xx = 0; xx < 8; xx++)
                        row[cx + xx] = Blend(row[cx + xx], L::kBreak, _disc[yy * 8 + xx]);
                }
            }
            _atlas.DrawOpaque(_img, t.disAddrX * k, y, l.addr.data(), l.addr.size(), L::kMuted, bg);
            _atlas.DrawOpaque(_img, t.disBytesX * k, y, l.bytes.data(), l.bytes.size(), L::kMuted, bg);
            _atlas.DrawOpaque(_img, t.disTextX * k, y, l.text.data(), l.text.size(), l.isPc ? L::kAccent : L::kFg, bg);
            if (!l.comment.empty())
                _atlas.DrawOpaque(_img, t.disCommentX * k, y, l.comment.data(), l.comment.size(), L::kMuted, bg);
            dirty += Logical(0, y, _img.width(), lineH);
        }
        return dirty;
    }

private:
    struct Row
    {
        std::string addr, bytes, text, comment;
        bool pc = false, bp = false, valid = false;
        bool operator==(const Row& o) const
        {
            return valid == o.valid && pc == o.pc && bp == o.bp && addr == o.addr && bytes == o.bytes && text == o.text &&
                   comment == o.comment;
        }
    };
    std::array<Row, kDisasmLines> _rows;
    uint8_t _disc[64]{};
};

class TimelineRaster : public RasterView
{
public:
    explicit TimelineRaster(Workload& w) : RasterView(w) {}

protected:
    void Reset() override
    {
        for (auto& lane : _h)
            lane.assign(size_t(_img.width()), -1);  // device columns (kTimelineWidth * scale)
        _playhead = -1000;
    }

    QRegion Update(bool) override
    {
        const L::TextMetrics& t = L::T();
        const float k = _k;
        const int W = int(std::lround(kTimelineWidth * k));
        const int ry = int(std::lround(kTimelineLanes * L::kLaneH * k));
        const float laneInner = (L::kLaneH - 4) * k;
        const int labelW = int(std::lround(4 * k + 12 * _atlas.Advance()));
        const int ph = int(std::lround(_w.TimelinePlayheadX() * k));
        // 1. which columns changed (bar height in any lane, the label strip, the old and new playhead)
        int minX = W, maxX = -1;
        for (int x = 0; x < W; x++)
        {
            const int sx = std::min(kTimelineWidth - 1, int(x / k));
            bool changed = x < labelW || std::abs(x - _playhead) <= int(k) + 1 || std::abs(x - ph) <= int(k) + 1;
            for (int lane = 0; lane < kTimelineLanes; lane++)
            {
                const int hh = int(_w.TimelineValue(lane, sx) * laneInner + 0.5f);
                if (hh != _h[lane][x])
                {
                    _h[lane][x] = hh;
                    changed = true;
                }
            }
            if (changed)
                minX = std::min(minX, x), maxX = std::max(maxX, x);
        }
        // 2. redraw the changed span row by row (sequential writes; the compiler vectorises the inner loop)
        if (maxX >= minX)
        {
            const uint32_t panel = Px(L::kPanel), laneBg = Px(L::kLaneBg);
            int y = 0;
            for (int lane = 0; lane < kTimelineLanes; lane++)
            {
                const int y0 = int(std::lround(lane * L::kLaneH * k)), y1 = int(std::lround((lane * L::kLaneH + L::kLaneH - 4) * k));
                for (; y < y0; y++)
                {
                    uint32_t* row = reinterpret_cast<uint32_t*>(_img.scanLine(y));
                    std::fill(row + minX, row + maxX + 1, panel);
                }
                const uint32_t fg = Px(Workload::LaneColour(lane));
                const int* hh = _h[lane].data();
                for (; y < y1; y++)
                {
                    uint32_t* row = reinterpret_cast<uint32_t*>(_img.scanLine(y));
                    const int need = y1 - y;  // a bar covers this row when its height >= need
                    for (int x = minX; x <= maxX; x++)
                        row[x] = hh[x] >= need ? fg : laneBg;
                }
            }
            for (; y < ry; y++)
            {
                uint32_t* row = reinterpret_cast<uint32_t*>(_img.scanLine(y));
                std::fill(row + minX, row + maxX + 1, panel);
            }
        }
        // lane names over the bars (their columns were redrawn above)
        for (int lane = 0; lane < kTimelineLanes; lane++)
        {
            const char* name = Workload::LaneName(lane);
            _atlas.DrawBlend(_img, 4 * k, int(std::lround((lane * L::kLaneH + 2) * k)), name, std::strlen(name), L::kFg);
        }
        // playhead
        for (int x = ph - int(k); x < ph + int(k); x++)
            if (x >= 0 && x < W)
                for (int y = 0; y < ry; y++)
                    reinterpret_cast<uint32_t*>(_img.scanLine(y))[x] = Px(L::kPlayhead);
        _playhead = ph;
        // ruler: redrawn whole
        const int rh = _img.height() - ry;
        FillRect(_img, 0, ry, _img.width(), rh, L::kPanel);
        for (const auto& tk : _w.TimelineTicks())
        {
            FillRect(_img, int(std::lround(tk.x * k)), ry, int(k), int(4 * k), L::kMuted);
            _atlas.DrawOpaque(_img, (tk.x + 2) * k, ry + int(std::lround(3 * k)), tk.label.data(), tk.label.size(), L::kMuted,
                              L::kPanel);
        }
        QRegion dirty = Logical(0, ry, _img.width(), rh);
        if (maxX >= minX)
            dirty += Logical(minX, 0, maxX - minX + 1, ry);
        (void)t;
        return dirty;
    }

private:
    std::array<std::vector<int>, kTimelineLanes> _h;
    int _playhead = -1000;
};

class ImagesRaster : public RasterView
{
public:
    explicit ImagesRaster(Workload& w) : RasterView(w) {}

protected:
    QRegion Update(bool) override
    {
        const float k = _k;
        const int dw = int(std::lround(L::kImgW * k)), dh = int(std::lround(L::kImgH * k));
        if (int(_xmap.size()) != dw)
        {
            _xmap.resize(dw);
            for (int x = 0; x < dw; x++)
                _xmap[x] = std::min(kTexW - 1, int((x + 0.5f) * kTexW / dw));
            _ymap.resize(dh);
            for (int y = 0; y < dh; y++)
                _ymap[y] = std::min(kTexH - 1, int((y + 0.5f) * kTexH / dh));
        }
        const uint32_t bg = Px(L::kPanel);
        for (int i = 0; i < kTexCount; i++)
        {
            const uint8_t* src = _w.Image(i);
            const int ox = int(std::lround((i % 2) * (L::kImgW + L::kPad) * k)), oy = int(std::lround((i / 2) * (L::kImgH + L::kPad) * k));
            for (int y = 0; y < dh; y++)
            {
                uint32_t* row = reinterpret_cast<uint32_t*>(_img.scanLine(oy + y)) + ox;
                const uint8_t* srow = src + size_t(_ymap[y]) * kTexW * 4;
                const uint32_t* s32 = reinterpret_cast<const uint32_t*>(srow);
                for (int x = 0; x < dw; x++)
                {
                    const uint32_t v = s32[_xmap[x]];  // bytes R G B A -> little-endian 0xAABBGGRR
                    const uint32_t a = v >> 24;
                    const uint32_t rgb = ((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF);
                    row[x] = a == 255 ? (0xFF000000u | rgb) : Blend(bg, rgb, a);
                }
            }
        }
        return QRegion(0, 0, _img.width(), _img.height());
    }

private:
    std::vector<int> _xmap, _ymap;
};


}  // namespace raster
