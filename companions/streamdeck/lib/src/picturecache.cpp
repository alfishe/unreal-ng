#include "sdb/picturecache.h"

#include <algorithm>

#include "sdb/devicewire.h"

namespace sdb
{
namespace
{

constexpr std::size_t kMaxPictures = 1024;  // overlapping strip regions that never cover each other, bounded

int U16(std::span<const std::uint8_t> r, std::size_t at)
{
    return at + 1 < r.size() ? r[at] | (r[at + 1] << 8) : 0;
}

std::string Bytes(std::span<const std::uint8_t> r)
{
    return std::string(reinterpret_cast<const char*>(r.data()), r.size());
}

}  // namespace

bool PictureCache::Covers(const Picture& by, const Picture& old)
{
    switch (by.kind)
    {
        case Kind::FullScreen:
        case Kind::ScreenColor:
            return true;
        case Kind::Window:
            return old.kind == Kind::Window || old.kind == Kind::Region;
        case Kind::Region:
            return (old.kind == Kind::Region && old.x >= by.x && old.y >= by.y && old.x + old.w <= by.x + by.w &&
                    old.y + old.h <= by.y + by.h);
        case Kind::Key:
        case Kind::KeyColor:
            return (old.kind == Kind::Key || old.kind == Kind::KeyColor) && old.key == by.key;
    }
    return false;
}

void PictureCache::Commit(Picture picture)
{
    std::erase_if(_pictures, [&](const Picture& old) { return Covers(picture, old); });
    _pictures.push_back(std::move(picture));
    if (_pictures.size() > kMaxPictures)
        _pictures.erase(_pictures.begin());
}

void PictureCache::Output(std::span<const std::uint8_t> r)
{
    if (r.size() < 8 || r[0] != 0x02)
        return;
    Picture p;
    std::string slot;
    int page = 0;
    bool last = false;
    int firstPage = 0;
    if (_model->family == Family::V1)
    {
        // 02 01 page 00 last key+1 (the Original v1 counts pages from 1)
        if (r[1] != 0x01)
            return;
        p.kind = Kind::Key;
        p.key = r[5] - 1;
        page = r[2];
        last = r[4] != 0;
        firstPage = _model->pid == 0x0060 ? 1 : 0;
        slot = "k" + std::to_string(p.key);
    }
    else if (r[1] == 0x07)
    {
        // 02 07 key last len(2) page(2)
        p.kind = Kind::Key;
        p.key = r[2];
        last = r[3] != 0;
        page = U16(r, 6);
        slot = "k" + std::to_string(p.key);
    }
    else if (r[1] == 0x08 || r[1] == 0x0B)
    {
        // 02 cmd 00 last len(2) page(2)
        p.kind = r[1] == 0x08 ? Kind::FullScreen : Kind::Window;
        last = r[3] != 0;
        page = U16(r, 6);
        slot = r[1] == 0x08 ? "full" : "window";
    }
    else if (r[1] == 0x0C && r.size() >= 16)
    {
        // 02 0C x(2) y(2) w(2) h(2) last page(2) len(2) 00
        p.kind = Kind::Region;
        p.x = U16(r, 2);
        p.y = U16(r, 4);
        p.w = U16(r, 6);
        p.h = U16(r, 8);
        last = r[10] != 0;
        page = U16(r, 11);
        slot = "r" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.w) + "," +
               std::to_string(p.h);
        if (p.kind == Kind::Region && p.x == 0 && p.y == 0 && p.w >= _model->lcd.w && p.h >= _model->lcd.h)
            p.kind = Kind::Window;  // a region over the whole strip replaces every region
    }
    else
    {
        return;
    }

    Pending& pending = _pending[slot];
    if (page == firstPage)
        pending = Pending{std::move(p), firstPage};
    else if (page != pending.nextPage || pending.picture.reports.empty())
    {
        _pending.erase(slot);  // a chain we did not see begin: wait for the next one
        return;
    }
    pending.picture.reports.push_back(Entry{wire::Output, Bytes(r)});
    pending.nextPage = page + 1;
    if (last)
    {
        Commit(std::move(pending.picture));
        _pending.erase(slot);
    }
}

void PictureCache::Feature(std::span<const std::uint8_t> r)
{
    if (r.size() < 2)
        return;
    const bool v2 = _model->family != Family::V1;
    if ((v2 && r[0] == 0x03 && r[1] == 0x02) || (!v2 && r[0] == 0x0B && r[1] == 0x63))
    {
        _pictures.clear();  // reset: the device shows its logo
        _pending.clear();
        return;
    }
    if (v2 && r[0] == 0x03 && r[1] == 0x06 && r.size() >= 3)
    {
        if (r[2] >= _model->keys)
        {
            _state["03 06 " + std::to_string(r[2])] = Bytes(r);  // a Neo touch point LED
            return;
        }
        Picture p;
        p.kind = Kind::KeyColor;
        p.key = r[2];
        p.reports.push_back(Entry{wire::SetFeature, Bytes(r)});
        Commit(std::move(p));
        return;
    }
    if (v2 && r[0] == 0x03 && r[1] == 0x05)
    {
        Picture p;
        p.kind = Kind::ScreenColor;
        p.reports.push_back(Entry{wire::SetFeature, Bytes(r)});
        Commit(std::move(p));
        return;
    }
    // Brightness, sleep, background: the last value of each.
    _state[std::to_string(r[0]) + " " + std::to_string(r[1])] = Bytes(r);
}

void PictureCache::Clear()
{
    _pictures.clear();
    _pending.clear();
    _state.clear();
}

std::vector<PictureCache::Entry> PictureCache::Replay() const
{
    std::vector<Entry> out;
    for (const auto& [key, report] : _state)
        out.push_back(Entry{wire::SetFeature, report});
    for (const Picture& p : _pictures)
        out.insert(out.end(), p.reports.begin(), p.reports.end());
    return out;
}

}  // namespace sdb
