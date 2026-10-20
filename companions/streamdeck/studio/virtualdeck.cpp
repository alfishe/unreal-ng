#include "virtualdeck.h"

#include <QFont>
#include <QPainter>
#include <QTransform>
#include <QtEndian>
#include <algorithm>

using namespace sdb;

namespace
{
constexpr int kFeatureLen = 32;
quint16 U16(const QByteArray& b, int o)
{
    return static_cast<quint16>(static_cast<quint8>(b[o]) | (static_cast<quint8>(b[o + 1]) << 8));
}
quint8 U8(const QByteArray& b, int o)
{
    return static_cast<quint8>(b[o]);
}
bool IsOriginal(const Model& m)
{
    return m.pid == 0x0060;
}
int FlipOriginal(int k)
{
    const int c = k % 5;
    return k - c + (4 - c);
}
QString NameOf(const Model& m)
{
    return QString::fromUtf8(m.name.data(), static_cast<qsizetype>(m.name.size()));
}
}  // namespace

VirtualDeck::VirtualDeck(QObject* parent) : QObject(parent)
{
    _idle.start();
    _clock.start();
    SetModel(*FindModel(kElgatoVid, 0x00C6));
}

void VirtualDeck::SetModel(const Model& m)
{
    _model = &m;
    _layout = GetPanelLayout(m);
    if (_layout.colX.empty() && m.HasKeyScreens())
        _layout = GetPanelLayout(*FindModel(kElgatoVid, 0x0080));  // Original: classic geometry
    _fb = QImage(std::max(1, _layout.w), std::max(1, _layout.h), QImage::Format_RGB32);
    _fb.fill(Qt::black);
    _keys = QVector<bool>(m.TotalButtons(), false);
    _dialDown = QVector<bool>(m.dials, false);
    _remoteKeys = QVector<bool>(m.TotalButtons(), false);
    _remoteDial = QVector<bool>(m.dials, false);
    _turns = QVector<DialTrace>(m.dials);
    _presses = QVector<DialTrace>(m.dials);
    _dialTicks = QVector<int>(m.dials, 0);
    _sensors = QVector<QColor>(m.touchPoints, QColor(Qt::black));
    _keyImg.clear();
    _full = _window = _partial = {};
    _backgrounds.clear();
    _bgUpload.clear();
    _serial = QStringLiteral("EMU%1").arg(m.pid, 4, 16, QLatin1Char('0')).toUpper();
    ShowLogo();
    emit Log(QStringLiteral("model: %1 (PID 0x%2)").arg(NameOf(m)).arg(m.pid, 4, 16, QLatin1Char('0')));
    emit ModelChanged();
    emit Changed();
}

bool VirtualDeck::Asleep() const
{
    return _sleepAfter > 0 && _idle.elapsed() > static_cast<qint64>(_sleepAfter) * 1000;
}

void VirtualDeck::ResetDial(int i, bool remote)
{
    if (i < 0 || i >= _dialTicks.size())
        return;
    _dialTicks[i] = 0;
    _presses[i] = {0, _clock.elapsed(), remote};
    emit Changed();
}

QImage VirtualDeck::KeyImage(int key) const
{
    if (key < 0 || key >= _model->keys || _layout.colX.empty())
        return {};
    const auto r = _layout.KeyRect(key, _model->cols);
    return _fb.copy(r.x, r.y, r.w, r.h);
}

void VirtualDeck::ClearRemote()
{
    _remoteKeys.fill(false);
    _remoteDial.fill(false);
    emit Changed();
}

void VirtualDeck::Note(const QString& line)
{
    if (!_quiet)
        emit Log(line);
}

QImage VirtualDeck::Inverse(const QImage& wire, const ImageFormat& f)
{
    // The host rotated, then mirrored. Undo: mirror back, then rotate back.
    const bool fx = f.mirror == Mirror::X || f.mirror == Mirror::Both;
    const bool fy = f.mirror == Mirror::Y || f.mirror == Mirror::Both;
    QImage img = wire;
    if (fx || fy)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        img = img.flipped((fx ? Qt::Horizontal : Qt::Orientations()) | (fy ? Qt::Vertical : Qt::Orientations()));
#else
        img = img.mirrored(fx, fy);
#endif
    }
    int deg = 0;
    switch (f.rotation)
    {
        case Rotation::R90:
            deg = -90;  // the host turned it 90 clockwise
            break;
        case Rotation::R270:
            deg = 90;  // the host turned it 90 counter-clockwise
            break;
        case Rotation::R180:
            deg = 180;
            break;
        case Rotation::R0:
            break;
    }
    if (deg)
        img = img.transformed(QTransform().rotate(deg));
    return img;
}

void VirtualDeck::DecodeAndPaint(const QByteArray& bytes, const ImageFormat& fmt, const QRect& target,
                                 const QString& what)
{
    _logo = false;
    QImage wire;
    if (!wire.loadFromData(bytes))
    {
        emit Log(QStringLiteral("%1: undecodable image (%2 bytes)").arg(what).arg(bytes.size()));
        return;
    }
    QImage up = Inverse(wire.convertToFormat(QImage::Format_RGB32), fmt);
    QPainter p(&_fb);
    // a smaller target: smooth; the same size or larger: nearest (crisp)
    p.setRenderHint(QPainter::SmoothPixmapTransform, up.width() > target.width());
    p.drawImage(target, up);
    ++_stats.images;
    Note(QStringLiteral("%1: %2x%3 %4, %5 bytes")
             .arg(what)
             .arg(wire.width())
             .arg(wire.height())
             .arg(fmt.mode == ImageMode::Bmp ? QStringLiteral("BMP") : QStringLiteral("JPEG"))
             .arg(bytes.size()));
    emit Changed();
}

void VirtualDeck::FillRect(const QRect& r, const QColor& c)
{
    _logo = false;
    QPainter p(&_fb);
    p.fillRect(r, c);
    emit Changed();
}

void VirtualDeck::DrawKey(int key, const QImage& img)
{
    _logo = false;
    if (key < 0 || key >= _model->keys || _layout.colX.empty())
        return;
    const auto r = _layout.KeyRect(key, _model->cols);
    QPainter p(&_fb);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRect(r.x, r.y, r.w, r.h), img);
    emit Changed();
}

void VirtualDeck::DrawStrip(const QImage& strip)
{
    _logo = false;
    if (_layout.stripW <= 0)
        return;
    QPainter p(&_fb);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRect(_layout.stripX, _layout.stripY, _layout.stripW, _layout.stripH), strip);
    emit Changed();
}

void VirtualDeck::ClearScreen(const QColor& c)
{
    _logo = false;
    _fb.fill(c);
    emit Changed();
}

void VirtualDeck::ShowLogo()
{
    _logo = true;  // the power-on picture: the first host clears it
    _fb.fill(QColor(8, 8, 12));
    QPainter p(&_fb);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f;
    f.setBold(true);
    f.setPixelSize(std::max(10, _fb.height() / 9));
    p.setFont(f);
    p.setPen(QColor(230, 230, 240));
    p.drawText(_fb.rect(), Qt::AlignCenter, QStringLiteral("STREAM DECK\nstudio"));
    emit Changed();
}

void VirtualDeck::Output(const QByteArray& r)
{
    ++_stats.reports;
    _stats.bytes += static_cast<quint64>(r.size());
    TouchActivity();
    if (r.size() < 8 || U8(r, 0) != 0x02)
        return;
    const Model& m = *_model;
    const int cmd = U8(r, 1);

    if (m.family == Family::V1)
    {  // Mini family / Original: 16-byte header, BMP, no length field
        if (cmd != 0x01 || r.size() < 17)
            return;
        const int key = IsOriginal(m) ? FlipOriginal(U8(r, 5) - 1) : U8(r, 5) - 1;
        const bool last = U8(r, 4) != 0;
        auto& a = _keyImg[key];
        if (IsOriginal(m))
        {  // exactly two reports of 8191 bytes, each carrying half the BMP (then zero padding)
            if (U8(r, 2) == 1)
            {
                a.data.clear();
                // the BMP file size
                a.expected = r.size() >= 22 ? static_cast<int>(qFromLittleEndian<quint32>(r.constData() + 18)) : 0;
                a.data.append(r.mid(16, a.expected / 2));
            }
            else
            {
                a.data.append(r.mid(16, a.expected - a.expected / 2));
            }
        }
        else
        {
            if (U8(r, 2) == 0)
                a.data.clear();
            a.data.append(r.mid(16));
        }
        if (last)
        {
            // a BMP knows its own size: drop the zero padding of the last report
            if (a.data.size() >= 6)
            {
                const int size = static_cast<int>(qFromLittleEndian<quint32>(a.data.constData() + 2));
                if (size > 0 && size <= a.data.size())
                    a.data.truncate(size);
            }
            if (key >= 0 && key < m.keys)
            {
                const auto kr = _layout.KeyRect(key, m.cols);
                DecodeAndPaint(a.data, m.key, QRect(kr.x, kr.y, _layout.key, _layout.key),
                               QStringLiteral("key %1").arg(key));
            }
            _keyImg.erase(key);
        }
        return;
    }

    switch (cmd)
    {
        case 0x07: {  // key image: [02 07 key last len16 page16] data
            const int key = U8(r, 2), last = U8(r, 3), len = U16(r, 4), page = U16(r, 6);
            auto& a = _keyImg[key];
            if (page == 0)
                a.data.clear();
            a.data.append(r.mid(8, len));
            if (last)
            {
                if (key < m.keys && m.HasKeyScreens())
                {
                    const auto kr = _layout.KeyRect(key, m.cols);
                    DecodeAndPaint(a.data, m.key, QRect(kr.x, kr.y, kr.w, kr.h), QStringLiteral("key %1").arg(key));
                }
                else
                {
                    emit Log(QStringLiteral("key image for invalid key %1 ignored").arg(key));
                }
                _keyImg.erase(key);
            }
            break;
        }
        case 0x08:    // full panel
        case 0x0B: {  // window
            auto& a = cmd == 0x08 ? _full : _window;
            const int last = U8(r, 3), len = U16(r, 4), page = U16(r, 6);
            if (page == 0)
                a.data.clear();
            a.data.append(r.mid(8, len));
            if (last)
            {
                if (cmd == 0x08)
                {
                    if (m.HasFullScreen())
                        DecodeAndPaint(a.data, m.screen, _fb.rect(), QStringLiteral("full panel"));
                    else
                        emit Log(QStringLiteral("full panel: not supported by this model"));
                }
                else if (m.windowFillCmd || m.lcdKind == LcdKind::Fill)
                {
                    DecodeAndPaint(a.data, m.lcd, QRect(_layout.stripX, _layout.stripY, _layout.stripW, _layout.stripH),
                                   QStringLiteral("window"));
                }
                else
                {
                    emit Log(QStringLiteral("window image: this model has no window"));
                }
                a.data.clear();
            }
            break;
        }
        case 0x0C: {  // partial window: [02 0C x y w h last page16 len16 0] data, logical coordinates
            if (r.size() < 16)
                return;
            const int x = U16(r, 2), y = U16(r, 4), w = U16(r, 6), h = U16(r, 8), last = U8(r, 10), page = U16(r, 11),
                      len = U16(r, 13);
            if (page == 0)
                _partial.data.clear();
            _partial.data.append(r.mid(16, len));
            if (last)
            {
                const QString where = QStringLiteral("partial window %1,%2 %3x%4").arg(x).arg(y).arg(w).arg(h);
                if (!m.partialWindow)
                    emit Log(QStringLiteral("partial window: not supported by this model"));
                else if (quirks && m.pid == 0x00C6 && (y != 0 || h != _layout.stripH))
                    emit Log(where + QStringLiteral(" IGNORED: + XL firmware draws only full-height rectangles"));
                else if (x + w > _layout.stripW || y + h > _layout.stripH)
                    emit Log(where + QStringLiteral(" outside the window, ignored"));
                else
                    DecodeAndPaint(_partial.data, m.lcd, QRect(_layout.stripX + x, _layout.stripY + y, w, h), where);
                _partial.data.clear();
            }
            break;
        }
        case 0x0D: {  // background: [02 0D index last page16 len16] (index before size)
            const int idx = U8(r, 2), last = U8(r, 3), page = U16(r, 4), len = U16(r, 6);
            auto& a = _bgUpload[idx];
            if (page == 0)
                a.data.clear();
            a.data.append(r.mid(8, len));
            if (last)
            {
                QImage wire;
                if (wire.loadFromData(a.data))
                {
                    _backgrounds[idx] = Inverse(wire.convertToFormat(QImage::Format_RGB32), m.screen);
                    Note(QStringLiteral("background %1 stored").arg(idx));
                }
                _bgUpload.erase(idx);
            }
            break;
        }
        default:
            emit Log(QStringLiteral("output command 0x%1 not emulated").arg(cmd, 2, 16, QLatin1Char('0')));
    }
}

void VirtualDeck::SetFeature(const QByteArray& r)
{
    ++_stats.reports;
    TouchActivity();
    if (r.size() < 2)
        return;
    const Model& m = *_model;
    const int id = U8(r, 0), cmd = U8(r, 1);
    if (m.family == Family::V1)
    {
        if (id == 0x05 && cmd == 0x55 && r.size() >= 6)
        {
            _brightness = std::clamp<int>(U8(r, 5), 0, 100);
            Note(QStringLiteral("brightness %1").arg(_brightness));
        }
        else if (id == 0x0B && cmd == 0x63)
        {
            ShowLogo();
            Note(QStringLiteral("show logo"));
        }
        else if (id == 0x0B && cmd == 0xA2 && r.size() >= 6)
        {
            _sleepAfter = static_cast<qint32>(qFromLittleEndian<quint32>(r.constData() + 2));
        }
        emit Changed();
        return;
    }
    if (id != 0x03)
        return;
    switch (cmd)
    {
        case 0x02:
            ShowLogo();
            Note(QStringLiteral("show logo"));
            break;
        case 0x05:
            if (r.size() >= 5)
            {
                _logo = false;
                _fb.fill(QColor(U8(r, 2), U8(r, 3), U8(r, 4)));
                Note(QStringLiteral("fill panel"));
            }
            break;
        case 0x06:
            if (r.size() >= 6)
            {
                const int k = U8(r, 2);
                const QColor c(U8(r, 3), U8(r, 4), U8(r, 5));
                if (k < m.keys && m.HasKeyScreens())
                {
                    const auto kr = _layout.KeyRect(k, m.cols);
                    FillRect(QRect(kr.x, kr.y, kr.w, kr.h), c);
                }
                else if (k >= m.keys && k - m.keys < _sensors.size())
                {
                    _sensors[k - m.keys] = c;
                }
            }
            break;
        case 0x08:
            if (r.size() >= 3)
            {
                _brightness = std::clamp<int>(U8(r, 2), 0, 100);
                Note(QStringLiteral("brightness %1").arg(_brightness));
            }
            break;
        case 0x0D:
            if (r.size() >= 6)
            {
                _sleepAfter = static_cast<qint32>(qFromLittleEndian<quint32>(r.constData() + 2));
                Note(QStringLiteral("sleep after %1 s").arg(_sleepAfter));
            }
            break;
        case 0x13:
            if (auto it = _backgrounds.find(U8(r, 2)); it != _backgrounds.end())
            {
                QPainter p(&_fb);
                p.drawImage(_fb.rect(), it->second);
            }
            break;
        default:
            emit Log(QStringLiteral("feature 03 %1 not emulated").arg(cmd, 2, 16, QLatin1Char('0')));
    }
    emit Changed();
}

QByteArray VirtualDeck::GetFeature(const QByteArray& req)
{
    QByteArray out(std::max<qsizetype>(req.size(), kFeatureLen), '\0');
    if (req.isEmpty())
        return out;
    const int id = U8(req, 0);
    out[0] = static_cast<char>(id);
    const Model& m = *_model;
    auto put = [&](qsizetype at, const QByteArray& s) {
        for (qsizetype i = 0; i < s.size() && at + i < out.size(); ++i)
            out[at + i] = s[i];
    };
    const QByteArray version = "1.0.0.E";
    const QByteArray serial = _serial.toLatin1();
    if (m.family == Family::V1)
    {
        if (id == 0xA0 || id == 0xA1 || id == 0xA2 || id == 0x04)
            put(5, version);  // text from byte 5
        else if (id == 0x03)
            put(5, serial);
        else if (id == 0xA3)
        {
            out[1] = 4;
            qToLittleEndian<quint32>(static_cast<quint32>(_sleepAfter), out.data() + 2);
        }
        return out;
    }
    switch (id)
    {
        case 0x04:
        case 0x05:
        case 0x07:  // len, UINT32 checksum, char[8]
            out[1] = 0x0C;
            put(6, version);
            break;
        case 0x06:
            out[1] = static_cast<char>(serial.size());
            put(2, serial);
            break;
        case 0x0A:
            out[1] = 4;
            qToLittleEndian<quint32>(static_cast<quint32>(_sleepAfter), out.data() + 2);
            break;
        case 0x08: {
            out[1] = static_cast<char>(m.rows);
            out[2] = static_cast<char>(m.cols);
            qToLittleEndian<quint16>(static_cast<quint16>(m.key.w), out.data() + 3);
            qToLittleEndian<quint16>(static_cast<quint16>(m.key.h), out.data() + 5);
            qToLittleEndian<quint16>(static_cast<quint16>(m.screenLogicalW), out.data() + 7);
            qToLittleEndian<quint16>(static_cast<quint16>(m.screenLogicalH), out.data() + 9);
            out[11] = 2;
            out[12] = 5;
            break;
        }
        default:
            break;
    }
    return out;
}

// ---------------------------------------------------------------- input
void VirtualDeck::SendKeys()
{
    const Model& m = *_model;
    QByteArray r;
    if (m.family == Family::V1)
    {  // Mini: [01][state per key]; Original: columns mirrored
        r = QByteArray(65, '\0');
        r[0] = 1;
        for (int i = 0; i < m.keys; ++i)
            r[1 + (IsOriginal(m) ? FlipOriginal(i) : i)] = _keys[i] ? 1 : 0;
    }
    else
    {
        r = QByteArray(512, '\0');
        r[0] = 1;
        r[1] = 0;
        qToLittleEndian<quint16>(static_cast<quint16>(_keys.size()), r.data() + 2);
        for (int i = 0; i < _keys.size(); ++i)
            r[4 + i] = _keys[i] ? 1 : 0;
    }
    emit InputReport(r);
}

void VirtualDeck::PressButton(int index, bool down)
{
    if (index < 0 || index >= _keys.size() || _keys[index] == down)
        return;
    TouchActivity();
    _keys[index] = down;
    SendKeys();
    emit Changed();
}

void VirtualDeck::PressDial(int index, bool down)
{
    if (index < 0 || index >= _dialDown.size())
        return;
    TouchActivity();
    _dialDown[index] = down;
    if (down)
        _presses[index] = {0, _clock.elapsed(), false};
    QByteArray r(512, '\0');
    r[0] = 1;
    r[1] = 3;
    qToLittleEndian<quint16>(static_cast<quint16>(_model->dials + 1), r.data() + 2);
    r[4] = 0;
    for (int i = 0; i < _dialDown.size(); ++i)
        r[5 + i] = _dialDown[i] ? 1 : 0;
    emit InputReport(r);
    emit Changed();
}

void VirtualDeck::TurnDial(int index, int ticks)
{
    if (index < 0 || index >= _dialTicks.size() || ticks == 0)
        return;
    TouchActivity();
    _dialTicks[index] += ticks;
    _turns[index] = {ticks > 0 ? 1 : -1, _clock.elapsed(), false};
    QByteArray r(512, '\0');
    r[0] = 1;
    r[1] = 3;
    qToLittleEndian<quint16>(static_cast<quint16>(_model->dials + 1), r.data() + 2);
    r[4] = 1;
    r[5 + index] = static_cast<char>(std::clamp(ticks, -127, 127));
    emit InputReport(r);
    emit Changed();
}

namespace
{
QByteArray TouchReport(int type, int x0, int y0, int x1 = 0, int y1 = 0)
{
    QByteArray r(512, '\0');
    r[0] = 1;
    r[1] = 2;
    qToLittleEndian<quint16>(0x0E, r.data() + 2);
    r[4] = static_cast<char>(type);
    qToLittleEndian<quint16>(static_cast<quint16>(x0), r.data() + 6);
    qToLittleEndian<quint16>(static_cast<quint16>(y0), r.data() + 8);
    qToLittleEndian<quint16>(static_cast<quint16>(x1), r.data() + 10);
    qToLittleEndian<quint16>(static_cast<quint16>(y1), r.data() + 12);
    return r;
}
}  // namespace

void VirtualDeck::TouchTap(int x, int y)
{
    TouchActivity();
    _touch = {{x, y}, {x, y}, 1, _clock.elapsed(), false};
    emit InputReport(TouchReport(1, x, y));
    emit Changed();
}

void VirtualDeck::TouchPress(int x, int y)
{
    TouchActivity();
    _touch = {{x, y}, {x, y}, 2, _clock.elapsed(), false};
    emit InputReport(TouchReport(2, x, y));
    emit Changed();
}

void VirtualDeck::TouchFlick(int x0, int y0, int x1, int y1)
{
    TouchActivity();
    _touch = {{x0, y0}, {x1, y1}, 3, _clock.elapsed(), false};
    emit InputReport(TouchReport(3, x0, y0, x1, y1));
    emit Changed();
}

void VirtualDeck::ShowRemote(const sdb::Event& e)
{
    const qint64 now = _clock.elapsed();
    switch (e.type)
    {
        case EventType::KeyDown:
        case EventType::KeyUp:
            if (e.index >= 0 && e.index < _remoteKeys.size())
                _remoteKeys[e.index] = e.type == EventType::KeyDown;
            break;
        case EventType::TouchPointDown:
        case EventType::TouchPointUp:
            if (_model->keys + e.index < _remoteKeys.size())
                _remoteKeys[_model->keys + e.index] = e.type == EventType::TouchPointDown;
            break;
        case EventType::DialDown:
        case EventType::DialUp:
            if (e.index >= 0 && e.index < _remoteDial.size())
            {
                if (_remoteDownTicks.size() != _remoteDial.size())
                    _remoteDownTicks.fill(0, _remoteDial.size());
                const bool wasDown = _remoteDial[e.index];
                _remoteDial[e.index] = e.type == EventType::DialDown;
                if (e.type == EventType::DialDown)
                {
                    _presses[e.index] = {0, now, true};
                    _remoteDownTicks[e.index] = _dialTicks[e.index];
                }
                // a short press (no hold, no turn) resets the indicator, as a double-click does
                else if (wasDown && now - _presses[e.index].at < 350 &&
                         _dialTicks[e.index] == _remoteDownTicks[e.index])
                {
                    ResetDial(e.index, true);
                    return;
                }
            }
            break;
        case EventType::DialTurn:
            if (e.index >= 0 && e.index < _dialTicks.size())
            {
                _dialTicks[e.index] += e.delta;
                _turns[e.index] = {e.delta > 0 ? 1 : -1, now, true};
            }
            break;
        case EventType::LcdTap:
            _touch = {{e.x, e.y}, {e.x, e.y}, 1, now, true};
            break;
        case EventType::LcdLongPress:
            _touch = {{e.x, e.y}, {e.x, e.y}, 2, now, true};
            break;
        case EventType::LcdSwipe:
            _touch = {{e.x, e.y}, {e.x2, e.y2}, 3, now, true};
            break;
    }
    emit Changed();
}
