#include "sdb/device.h"

#include <algorithm>
#include <cctype>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include "sdb/draw.h"
#include "sdb/protocol.h"

namespace sdb
{

std::int64_t NowUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

Device::Device(const Model& m, std::unique_ptr<Transport> t, std::string serial)
    : _m(m), _t(std::move(t)), _serial(std::move(serial)), _parser(m), _emuParser(m)
{
}

namespace
{
std::optional<std::string> ReadString(Transport& t, const proto::FeatureRead& fr)
{
    std::vector<std::uint8_t> buf(fr.length + 1, 0);
    buf[0] = fr.id;
    const int n = t.GetFeature(buf);
    if (n <= 0)
        return std::nullopt;
    std::string s;
    for (size_t i = fr.skip;
         /* index into buffer that includes the report id, as in the crate */ i < static_cast<size_t>(n) &&
         i < buf.size();
         ++i)
        if (buf[i] >= 0x20 && buf[i] < 0x7f)
            s.push_back(static_cast<char>(buf[i]));
    return s;
}
}  // namespace

std::optional<std::string> Device::Firmware()
{
    if (auto v = ReadString(*_t, proto::FirmwareRead(_m)); v && !v->empty())
        return v;
    return ReadString(*_t, proto::FirmwareReadFallback(_m));
}
std::optional<std::string> Device::SerialFromDevice()
{
    return ReadString(*_t, proto::SerialRead(_m));
}

bool Device::SetBrightness(int p)
{
    auto r = proto::Brightness(_m, p);
    return _t->SendFeature(r) >= 0;
}
bool Device::Reset()
{
    auto r = proto::Reset(_m);
    return _t->SendFeature(r) >= 0;
}
bool Device::SetTouchLed(int pt, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    if (_emu)
        return pt >= 0 && pt < _emu->virt->touchPoints && FillKeyColor(_emu->virt->keys + pt, r, g, b);
    auto rep = proto::TouchLed(_m, pt, r, g, b);
    return _t->SendFeature(rep) >= 0;
}

namespace
{
// One image is many reports; the device reassembles them in order. Several processes may draw on one deck at once
// (sdctl, a scenario, Stream Deck Studio marking presses: HID is opened shared), so an image goes out whole, under a
// lock per deck that all of them take.
class DeckWriteLock
{
public:
    explicit DeckWriteLock(const std::string& serial)
    {
#ifndef _WIN32
        if (serial.empty())
            return;
        std::string name = "/tmp/streamdeck-";
        for (char c : serial)
            name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
        name += ".lock";
        _fd = ::open(name.c_str(), O_RDWR | O_CREAT, 0666);
        if (_fd >= 0 && ::flock(_fd, LOCK_EX) != 0)
        {
            ::close(_fd);
            _fd = -1;
        }
#else
        (void)serial;
#endif
    }
    ~DeckWriteLock()
    {
#ifndef _WIN32
        if (_fd >= 0)
        {
            ::flock(_fd, LOCK_UN);
            ::close(_fd);
        }
#endif
    }
    DeckWriteLock(const DeckWriteLock&) = delete;
    DeckWriteLock& operator=(const DeckWriteLock&) = delete;

private:
    int _fd = -1;
};
}  // namespace

WriteStats Device::Send(const std::vector<std::vector<std::uint8_t>>& reports)
{
    WriteStats s;
    const auto t0 = std::chrono::steady_clock::now();
    const DeckWriteLock lock(reports.size() > 1 ? _serial : std::string());
    for (const auto& r : reports)
    {
        if (_t->Write(r) < 0)
            break;
        ++s.reports;
        s.bytes += r.size();
    }
    s.elapsed = std::chrono::steady_clock::now() - t0;
    return s;
}

WriteStats Device::SetKeyRaw(int key, std::span<const std::uint8_t> enc)
{
    if (_emu)
        return EmuKeyRaw(key, enc);
    return Send(proto::KeyImage(_m, key, enc));
}
WriteStats Device::SetKeyImage(int key, const Rgb& img, int q)
{
    return SetKeyRaw(key, EncodeForFormat(img, GetModel().key, q));  // emulation: the virtual model's format
}
WriteStats Device::ClearKey(int key)
{
    return SetKeyImage(key, Rgb(GetModel().key.w, GetModel().key.h));
}
WriteStats Device::ClearAllKeys()
{
    WriteStats total;
    if (_emu)
    {
        for (int k = 0; k < _emu->virt->keys; ++k)
            if (FillKeyColor(k, 0, 0, 0))
            {
                ++total.reports;
                total.bytes += 32;
            }
        return total;
    }
    if (!_m.HasKeyScreens())
        return total;
    if (_m.family == Family::V2)
    {  // one 32-byte feature report per key instead of a JPEG upload (Elgato "Fill Key with Color")
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < _m.keys; ++k)
            if (FillKeyColor(k, 0, 0, 0))
            {
                ++total.reports;
                total.bytes += 32;
            }
        total.elapsed = std::chrono::steady_clock::now() - t0;
        return total;
    }
    const auto blank = EncodeForFormat(Rgb(_m.key.w, _m.key.h), _m.key);  // encode once, reuse for every key
    for (int k = 0; k < _m.keys; ++k)
    {
        auto s = SetKeyRaw(k, blank);
        total.reports += s.reports;
        total.bytes += s.bytes;
        total.elapsed += s.elapsed;
    }
    return total;
}

WriteStats Device::SetLcdFull(const Rgb& logical, int q)
{
    if (_emu)
    {
        const Model& v = *_emu->virt;
        if (!v.HasLcd())
            return {};
        return EmuLcd(0, 0,
                      logical.w == v.lcdLogicalW && logical.h == v.lcdLogicalH
                          ? logical
                          : Resize(logical, v.lcdLogicalW, v.lcdLogicalH),
                      q);
    }
    if (!_m.HasLcd())
        return {};
    ImageFormat f = _m.lcd;
    f.rotation = LcdRotation();
    f.mirror = _lcdMir.value_or(f.mirror);
    // Resize in logical space first so rotation never has to interpolate.
    const Rgb scaled = logical.w == _m.lcdLogicalW && logical.h == _m.lcdLogicalH
                           ? logical
                           : Resize(logical, _m.lcdLogicalW, _m.lcdLogicalH);
    _stripShadow = scaled;
    if (_useWindowCmd && _m.windowFillCmd)
        return Send(proto::WindowImage(_m, EncodeForFormat(scaled, f, q)));
    return Send(proto::LcdFill(_m, EncodeForFormat(scaled, f, q)));
}

WriteStats Device::SetLcdRegion(int lx, int ly, const Rgb& tile, int q)
{
    if (_emu)
    {
        const Model& v = *_emu->virt;
        if (!v.HasLcd() || lx < 0 || ly < 0 || lx + tile.w > v.lcdLogicalW || ly + tile.h > v.lcdLogicalH)
            return {};
        return EmuLcd(lx, ly, tile, q);
    }
    if (_m.lcdKind != LcdKind::Region)
        return SetLcdFull(tile, q);
    // The + XL firmware ignores a partial window write that does not span the strip's full height [hw]. Keep a shadow
    // of the strip and send the full-height column range around the change.
    if (_stripShadow.w != _m.lcdLogicalW || _stripShadow.h != _m.lcdLogicalH)
        _stripShadow = Rgb(_m.lcdLogicalW, _m.lcdLogicalH);
    Blit(_stripShadow, tile, lx, ly);
    if (ly != 0 || tile.h != _m.lcdLogicalH)
    {
        const int x0 = std::clamp(lx, 0, _m.lcdLogicalW - 1), x1 = std::clamp(lx + tile.w, x0 + 1, _m.lcdLogicalW);
        return SetLcdRegion(x0, 0, Crop(_stripShadow, x0, 0, x1 - x0, _m.lcdLogicalH), q);
    }
    ImageFormat f{ImageMode::Jpeg, 0, 0, LcdRotation(), Mirror::None};
    const Rotation r = LcdRotation();
    const bool swap = r == Rotation::R90 || r == Rotation::R270;
    f.w = swap ? tile.h : tile.w;
    f.h = swap ? tile.w : tile.h;
    // Map the logical rectangle into device space for the chosen rotation.
    const int w = _m.lcdLogicalW, h = _m.lcdLogicalH;
    int dx = lx, dy = ly;
    switch (r)
    {
        case Rotation::R0:
            break;
        case Rotation::R180:
            dx = w - lx - tile.w;
            dy = h - ly - tile.h;
            break;
        case Rotation::R90:
            dx = h - ly - tile.h;
            dy = lx;
            break;  // clockwise: (x,y)->(H-1-y, x)
        case Rotation::R270:
            dx = ly;
            dy = w - lx - tile.w;
            break;  // counter-clockwise: (x,y)->(y, W-1-x)
    }
    if (_lcdCoords == LcdCoords::Logical)
        return Send(proto::LcdRegion(_m, lx, ly, tile.w, tile.h, EncodeForFormat(tile, f, q)));
    return Send(proto::LcdRegion(_m, dx, dy, f.w, f.h, EncodeForFormat(tile, f, q)));
}

WriteStats Device::SetLcdTile(int dial, const Rgb& tile, int q)
{
    return SetLcdRegion(dial * GetModel().dialTileW, 0, tile, q);
}

WriteStats Device::SetFullScreen(const Rgb& logical, int q)
{
    if (_emu)
    {  // cut the virtual panel by the virtual layout and show every piece where it belongs
        const Model& v = *_emu->virt;
        const PanelLayout l = GetPanelLayout(v);
        if (!v.HasFullScreen() || l.colX.empty())
            return {};
        const Rgb panel = logical.w == l.w && logical.h == l.h ? logical : Resize(logical, l.w, l.h);
        WriteStats t;
        for (int k = 0; k < v.keys; ++k)
        {
            const auto r = l.KeyRect(k, v.cols);
            Add(t, SetKeyImage(k, Crop(panel, r.x, r.y, r.w, r.h), q));
        }
        if (l.stripW > 0 && v.HasLcd())
            Add(t, SetLcdFull(Crop(panel, l.stripX, l.stripY, l.stripW, l.stripH), q));
        for (size_t i = 0; i < l.sensors.size() && i < static_cast<size_t>(v.touchPoints); ++i)
        {  // sensor LEDs: mean colour
            const auto& sr = l.sensors[i];
            const Rgb part = Crop(panel, sr.x, sr.y, sr.w, sr.h);
            long acc[3] = {0, 0, 0};
            for (size_t j = 0; j < part.px.size(); j += 3)
                for (int c = 0; c < 3; ++c)
                    acc[c] += part.px[j + c];
            const long n = static_cast<long>(part.px.size() / 3);
            SetTouchLed(static_cast<int>(i), static_cast<std::uint8_t>(acc[0] / n),
                        static_cast<std::uint8_t>(acc[1] / n), static_cast<std::uint8_t>(acc[2] / n));
        }
        return t;
    }
    if (!_m.HasFullScreen())
        return {};
    const Rgb scaled = logical.w == _m.screenLogicalW && logical.h == _m.screenLogicalH
                           ? logical
                           : Resize(logical, _m.screenLogicalW, _m.screenLogicalH);
    return Send(proto::FullScreen(_m, EncodeForFormat(scaled, _m.screen, q)));
}
WriteStats Device::SetPanel(const std::vector<std::pair<int, Rgb>>& keys, const Rgb* strip, int q)
{
    if (_emu)
    {  // per key through the virtual pipeline
        WriteStats t;
        for (const auto& [k, img] : keys)
            Add(t,
                SetKeyImage(
                    k,
                    img.w == GetModel().key.w ? img : FitImage(img, GetModel().key.w, GetModel().key.h, Fit::Contain),
                    q));
        if (strip)
            Add(t, SetLcdFull(*strip, q));
        return t;
    }
    const PanelLayout l = GetPanelLayout(_m);
    if (!_m.HasFullScreen() || l.colX.empty())
        return {};
    Rgb panel(l.w, l.h);
    for (const auto& [k, img] : keys)
    {
        if (k < 0 || k >= _m.keys)
            continue;
        const auto r = l.KeyRect(k, _m.cols);
        Blit(panel, img.w == r.w && img.h == r.h ? img : FitImage(img, r.w, r.h, Fit::Contain), r.x, r.y);
    }
    if (strip && l.stripW > 0)
        Blit(panel, Resize(*strip, l.stripW, l.stripH), l.stripX, l.stripY);
    return SetFullScreen(panel, q);
}

WriteStats Device::SetWindowImage(const Rgb& logical, int q)
{
    if (_emu)
        return SetLcdFull(logical, q);
    if (!_m.windowFillCmd)
        return {};
    ImageFormat f = _m.lcd;
    f.rotation = LcdRotation();
    const Rgb scaled = logical.w == _m.lcdLogicalW && logical.h == _m.lcdLogicalH
                           ? logical
                           : Resize(logical, _m.lcdLogicalW, _m.lcdLogicalH);
    return Send(proto::WindowImage(_m, EncodeForFormat(scaled, f, q)));
}
bool Device::FillKeyColor(int key, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    if (_emu)
    {
        if (key < 0 || key >= static_cast<int>(_emu->keyMap.size()))
            return false;
        key = _emu->keyMap[static_cast<size_t>(key)];
    }
    auto rep = proto::FillKeyColor(_m, key, r, g, b);
    return _t->SendFeature(rep) >= 0;
}
bool Device::FillScreenColor(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    if (_emu)
    {  // only the virtual device's area
        bool ok = true;
        for (int k = 0; k < static_cast<int>(_emu->keyMap.size()); ++k)
            ok &= FillKeyColor(k, r, g, b);
        if (_emu->virt->HasLcd())
            (void)SetLcdFull(Rgb(_emu->virt->lcdLogicalW, _emu->virt->lcdLogicalH, r, g, b));
        return ok;
    }
    auto rep = proto::FillScreenColor(_m, r, g, b);
    return _t->SendFeature(rep) >= 0;
}
bool Device::SetSleepAfter(std::int32_t s)
{
    auto rep = proto::SleepAfter(_m, s);
    return _t->SendFeature(rep) >= 0;
}
std::optional<std::int32_t> Device::SleepAfter()
{
    const auto fr = proto::SleepRead(_m);
    std::vector<std::uint8_t> buf(fr.length + 1, 0);
    buf[0] = fr.id;
    if (_t->GetFeature(buf) < 6)
        return std::nullopt;
    return static_cast<std::int32_t>(buf[2] | (buf[3] << 8) | (buf[4] << 16) |
                                     (static_cast<std::uint32_t>(buf[5]) << 24));
}
std::optional<Device::UnitInfo> Device::ReadUnitInfo()
{
    if (_m.family == Family::V1)
        return std::nullopt;
    std::vector<std::uint8_t> buf(33, 0);
    buf[0] = proto::kUnitInfoReport;
    const int n = _t->GetFeature(buf);
    if (n < 17 || buf[0] != proto::kUnitInfoReport)
        return std::nullopt;
    auto u16 = [&](int o) { return buf[o] | (buf[o + 1] << 8); };
    UnitInfo u{buf[1],  buf[2],  u16(3),
               u16(5),  u16(7),  u16(9),
               buf[11], buf[12], std::vector<std::uint8_t>(buf.begin(), buf.begin() + n)};
    return u;
}

std::vector<Event> Device::PollEvents(int timeoutMs)
{
    std::vector<std::uint8_t> buf(proto::InputReportLength(_m), 0);
    const int n = _t->Read(buf, timeoutMs);
    if (n <= 0)
        return {};
    const InputSource src = _t->LastSource();
    auto ev = (src == InputSource::Emulator ? _emuParser : _parser).Feed(std::span(buf).first(static_cast<size_t>(n)));
    const auto t = NowUs();
    for (auto& e : ev)
    {
        e.tUs = t;
        e.source = src;
    }
    if (!_emu)
        return ev;
    std::vector<Event> out;  // emulation: physical -> virtual, anything outside the virtual device is dropped
    const Model& v = *_emu->virt;
    for (auto e : ev)
    {
        switch (e.type)
        {
            case EventType::KeyDown:
            case EventType::KeyUp: {
                const int vk =
                    e.index < static_cast<int>(_emu->reverse.size()) ? _emu->reverse[static_cast<size_t>(e.index)] : -1;
                if (vk < 0)
                    continue;
                if (vk >= v.keys)
                {
                    e.type = e.type == EventType::KeyDown ? EventType::TouchPointDown : EventType::TouchPointUp;
                    e.index = vk - v.keys;
                }
                else
                    e.index = vk;
                break;
            }
            case EventType::DialDown:
            case EventType::DialUp:
            case EventType::DialTurn:
                if (e.index >= v.dials)
                    continue;
                break;
            case EventType::LcdTap:
            case EventType::LcdLongPress:
            case EventType::LcdSwipe:
                if (v.lcdKind != LcdKind::Region)
                    continue;  // only touch-capable windows (Neo's window has no touch)
                e.x -= _emu->stripX;
                e.y -= _emu->stripY;
                e.x2 -= _emu->stripX;
                e.y2 -= _emu->stripY;
                if (e.x < 0 || e.y < 0 || e.x >= v.lcdLogicalW || e.y >= v.lcdLogicalH)
                    continue;
                e.x2 = std::clamp(e.x2, 0, v.lcdLogicalW - 1);
                e.y2 = std::clamp(e.y2, 0, v.lcdLogicalH - 1);
                break;
            default:
                break;
        }
        out.push_back(e);
    }
    return out;
}

bool Device::Emulate(const Model& v, std::string* error)
{
    auto fail = [&](const std::string& why) {
        if (error)
            *error = why;
        return false;
    };
    if (&v == &_m)
    {
        _emu.reset();
        return true;
    }
    if (!_m.HasKeyScreens() || _m.family != Family::V2)
        return fail("the physical device cannot emulate");
    const int sensorRows = v.touchPoints > 0 ? 1 : 0;
    if (v.rows + sensorRows > _m.rows || v.cols > _m.cols)
        return fail("virtual keys do not fit on " + std::string(_m.name));
    if (v.dials > _m.dials)
        return fail("virtual model has more dials than " + std::string(_m.name));
    if (v.HasLcd() && (!_m.HasLcd() || v.lcdLogicalW > _m.lcdLogicalW || v.lcdLogicalH > _m.lcdLogicalH))
        return fail("virtual window does not fit the strip");
    Emulation e;
    e.virt = &v;
    e.reverse.assign(static_cast<size_t>(_m.keys), -1);
    for (int k = 0; k < v.keys; ++k)
        e.keyMap.push_back((k / v.cols) * _m.cols + k % v.cols);
    if (v.touchPoints >= 1)
        e.keyMap.push_back(v.rows * _m.cols);  // sensor 0: under the first column
    if (v.touchPoints >= 2)
        e.keyMap.push_back(v.rows * _m.cols + v.cols - 1);  // sensor 1: under the last column
    for (size_t i = 0; i < e.keyMap.size(); ++i)
        e.reverse[static_cast<size_t>(e.keyMap[i])] = static_cast<int>(i);
    if (v.HasLcd())
    {
        e.stripX = (_m.lcdLogicalW - v.lcdLogicalW) / 2;
        e.stripY = (_m.lcdLogicalH - v.lcdLogicalH) / 2;
    }
    // Dark everything outside the virtual device so its outline is obvious.
    for (int k = 0; k < _m.keys; ++k)
        if (e.reverse[static_cast<size_t>(k)] < 0)
        {
            auto rep = proto::FillKeyColor(_m, k, 12, 12, 12);
            _t->SendFeature(rep);
        }
    if (_m.HasLcd())
    {
        Rgb strip(_m.lcdLogicalW, _m.lcdLogicalH, 12, 12, 12);
        if (v.HasLcd())
            draw::FillRect(strip, e.stripX, e.stripY, v.lcdLogicalW, v.lcdLogicalH, {0, 0, 0});
        _emu.reset();
        (void)SetLcdFull(strip);
    }
    _emu = std::move(e);
    return true;
}

WriteStats Device::EmuKeyRaw(int vkey, std::span<const std::uint8_t> encoded)
{
    const Model& v = *_emu->virt;
    if (vkey < 0 || vkey >= v.keys)
        throw std::out_of_range("key index");
    // Decode the virtual model's own wire format (JPEG or BMP, rotated / mirrored as that model needs) back to the
    // upright picture it would show, then display it, upscaled, on the physical key.
    auto img = DecodeForFormat(std::vector<std::uint8_t>(encoded.begin(), encoded.end()), v.key);
    if (!img)
        return {};
    return Send(proto::KeyImage(_m, _emu->keyMap[static_cast<size_t>(vkey)], EncodeForFormat(*img, _m.key, 90)));
}

WriteStats Device::EmuLcd(int vx, int vy, const Rgb& img, int q)
{
    const Model& v = *_emu->virt;
    // Round trip through the virtual window format, then into the physical strip at the virtual window's place.
    ImageFormat f = v.lcd;
    f.w = (f.rotation == Rotation::R90 || f.rotation == Rotation::R270) ? img.h : img.w;
    f.h = (f.rotation == Rotation::R90 || f.rotation == Rotation::R270) ? img.w : img.h;
    auto shown = DecodeForFormat(EncodeForFormat(img, f, q), f);
    if (!shown)
        return {};
    auto saved = std::move(_emu);
    _emu.reset();
    WriteStats st = SetLcdRegion(saved->stripX + vx, saved->stripY + vy, *shown, q);
    _emu = std::move(saved);
    return st;
}

std::vector<Found> ListDevices()
{
    std::vector<Found> out;
    for (auto& i : EnumerateElgato())
        if (const Model* m = FindModel(i.vid, i.pid))
            out.push_back({m, i});
    return out;
}

std::unique_ptr<Device> OpenDevice(const Found& f, std::string* error)
{
    if (f.info.path.rfind("emu:", 0) == 0)
    {  // a Stream Deck Studio found by findTarget
        auto dev = OpenEmulator(f.info.path.substr(4), error);
        if (dev && f.physical && !dev->Emulate(*f.model, error))
            return nullptr;
        return dev;
    }
    auto t = OpenHid(f.info.path, error);
    if (!t)
        return nullptr;
    auto dev = std::make_unique<Device>(f.physical ? *f.physical : *f.model, std::move(t), f.info.serial);
    if (f.mirror)
    {  // before emulate: the mirror shows the real model
        std::string merr;
        if (!dev->AttachMirror(*f.mirror, &merr) && !f.mirrorOptional)
        {
            if (error)
                *error = merr;
            return nullptr;
        }
    }
    if (f.physical && !dev->Emulate(*f.model, error))
        return nullptr;
    return dev;
}

std::unique_ptr<Device> OpenFirst(const std::string& sub, std::string* error)
{
    for (auto& f : ListDevices())
        if (sub.empty() || f.info.serial.find(sub) != std::string::npos)
            return OpenDevice(f, error);
    if (error)
        *error = "no matching device";
    return nullptr;
}

}  // namespace sdb
