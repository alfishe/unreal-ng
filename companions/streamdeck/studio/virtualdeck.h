// The device side of the Stream Deck HID protocol, in software: takes the same output / feature reports a real device
// takes, keeps one LCD frame buffer laid out like the real panel, and produces real input reports.
#pragma once
#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QVector>
#include <map>

#include "sdb/input.h"
#include "sdb/models.h"

class VirtualDeck : public QObject
{
    Q_OBJECT
public:
    explicit VirtualDeck(QObject* parent = nullptr);

    void SetModel(const sdb::Model& m);
    [[nodiscard]] const sdb::Model& GetModel() const
    {
        return *_model;
    }
    [[nodiscard]] const sdb::PanelLayout& GetLayout() const
    {
        return _layout;
    }
    [[nodiscard]] const QImage& Lcd() const
    {
        return _fb;
    }
    [[nodiscard]] QString Serial() const
    {
        return _serial;
    }
    // The serial this deck reports (feature reports); a mirror shows the real deck's.
    void SetSerial(const QString& serial)
    {
        _serial = serial;
    }
    [[nodiscard]] int Brightness() const
    {
        return _brightness;
    }
    [[nodiscard]] bool Asleep() const;
    [[nodiscard]] QColor SensorColor(int i) const
    {
        return i < _sensors.size() ? _sensors[i] : QColor(Qt::black);
    }
    [[nodiscard]] bool KeyDown(int i) const
    {
        return (i < _keys.size() && _keys[i]) || RemoteKeyDown(i);
    }
    [[nodiscard]] bool DialDown(int i) const
    {
        return (i < _dialDown.size() && _dialDown[i]) || RemoteDialDown(i);
    }
    [[nodiscard]] bool RemoteKeyDown(int i) const
    {
        return i < _remoteKeys.size() && _remoteKeys[i];
    }
    [[nodiscard]] bool RemoteDialDown(int i) const
    {
        return i < _remoteDial.size() && _remoteDial[i];
    }

    // Activity the view animates (fading traces): touches on the strip and dial turns, from either source.
    struct TouchTrace
    {
        QPoint a, b;
        int kind = 0;  // 1 tap, 2 press, 3 flick
        qint64 at = -100000;
        bool remote = false;
    };
    struct DialTrace
    {
        int dir = 0;
        qint64 at = -100000;
        bool remote = false;
    };
    [[nodiscard]] const TouchTrace& TouchMark() const
    {
        return _touch;
    }
    [[nodiscard]] DialTrace TurnMark(int dial) const
    {
        return dial < _turns.size() ? _turns[dial] : DialTrace{};
    }
    [[nodiscard]] DialTrace PressMark(int dial) const  // dir unused
    {
        return dial < _presses.size() ? _presses[dial] : DialTrace{};
    }
    [[nodiscard]] qint64 ClockMs() const
    {
        return _clock.elapsed();
    }
    // The real deck's input (relayed by the daemon): shown on the controls, never sent back.
    void ShowRemote(const sdb::Event& e);
    // The indicator back to 12 o'clock (a short press of the dial, or a double-click on it); the ring in the source's
    // colour.
    void ResetDial(int i, bool remote = false);
    // The host draws pressed keys itself (a scenario runtime): the view adds no glow of its own.
    void SetHostDrawsPresses(bool on)
    {
        if (_hostDrawsPresses != on)
        {
            _hostDrawsPresses = on;
            emit Changed();
        }
    }
    [[nodiscard]] bool HostDrawsPresses() const
    {
        return _hostDrawsPresses;
    }
    [[nodiscard]] QImage KeyImage(int key) const;  // what the key shows (its logical size)
    void ClearRemote();                            // the real deck went away
    [[nodiscard]] int DialAngle(int i) const
    {
        return i < _dialTicks.size() ? _dialTicks[i] * 15 : 0;
    }

    // Behave like the + XL firmware where we measured quirks (e.g. partial window writes must be full height).
    bool quirks = true;

    // Host -> device
    void Output(const QByteArray& report);
    void SetFeature(const QByteArray& report);
    QByteArray GetFeature(const QByteArray& request);
    // While quiet, reports are applied without a log line each (a replay of the whole picture).
    void SetQuiet(bool on)
    {
        _quiet = on;
    }

    // User -> host (generate input reports)
    void PressButton(int index, bool down);  // keys, then Neo sensors
    void PressDial(int index, bool down);
    void TurnDial(int index, int ticks);
    void TouchTap(int x, int y);
    void TouchPress(int x, int y);
    void TouchFlick(int x0, int y0, int x1, int y1);

    // Design time (the editor's canvas): draw without the protocol. `img` is the key's picture in landscape
    // orientation (any size), `strip` the window in logical coordinates.
    void DrawKey(int key, const QImage& img);
    void DrawStrip(const QImage& strip);
    void ClearScreen(const QColor& c = Qt::black);
    void ResetScreen()
    {
        ShowLogo();
    }
    [[nodiscard]] bool ShowingLogo() const  // nothing drawn since the logo
    {
        return _logo;
    }
    void SetBrightnessLevel(int percent)
    {
        _brightness = percent;
        emit Changed();
    }

    struct Stats
    {
        quint64 reports = 0, bytes = 0, images = 0;
    };
    [[nodiscard]] Stats GetStats() const
    {
        return _stats;
    }

signals:
    void Changed();
    void InputReport(const QByteArray& report);
    void Log(const QString& line);
    void ModelChanged();

private:
    struct Assembly
    {
        QByteArray data;
        int expected = 0;
    };
    void DecodeAndPaint(const QByteArray& bytes, const sdb::ImageFormat& fmt, const QRect& target, const QString& what);
    void FillRect(const QRect& r, const QColor& c);
    void ShowLogo();
    void TouchActivity()
    {
        _idle.restart();
    }
    void SendKeys();
    void Note(const QString& line);
    [[nodiscard]] static QImage Inverse(const QImage& wire, const sdb::ImageFormat& fmt);

    const sdb::Model* _model = nullptr;
    sdb::PanelLayout _layout;
    QImage _fb;
    bool _hostDrawsPresses = false;
    bool _logo = false;
    bool _quiet = false;
    QString _serial;
    int _brightness = 70;
    qint32 _sleepAfter = 0;
    QElapsedTimer _idle;
    QVector<bool> _keys, _dialDown, _remoteKeys, _remoteDial;
    QVector<DialTrace> _turns, _presses;
    QVector<int> _remoteDownTicks;  // a real dial's position when it went down (a turn while held is no short press)
    TouchTrace _touch;
    QElapsedTimer _clock;
    QVector<int> _dialTicks;
    QVector<QColor> _sensors;
    std::map<int, Assembly> _keyImg;
    Assembly _full, _window, _partial;
    std::map<int, QImage> _backgrounds;
    std::map<int, Assembly> _bgUpload;
    Stats _stats;
};
