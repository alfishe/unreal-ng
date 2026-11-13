#pragma once

/// @file handheldpad.h
/// @brief The pads as the handheld UI reads them (the controller menu, the on-screen keyboard): their controls as UI
/// verbs, with no profile, layer or context of the emulator's router in between. MainWindow puts the host input
/// service's events here (HostInputService::SetUiTap); while a screen of the UI is up it takes every pad event, and
/// otherwise only the system buttons (Menu, View), as SteamOS does with its own.

#include <QObject>
#include <QString>

#include <atomic>
#include <map>
#include <string>

class QTimer;

class HandheldPad : public QObject
{
    Q_OBJECT

public:
    enum class Verb
    {
        Up,
        Down,
        Left,
        Right,
        Accept,   ///< A (south), the left trackpad's click
        Back,     ///< B (east)
        OptionX,  ///< X (west)
        OptionY,  ///< Y (north)
        Prev,     ///< LB
        Next,     ///< RB
        Menu,     ///< Menu (☰)
        View,     ///< View (⧉)
        Paddle1,  ///< R4
        Paddle2,  ///< R5
        Paddle3,  ///< L4
        Paddle4   ///< L5
    };
    Q_ENUM(Verb)

    explicit HandheldPad(QObject* parent = nullptr);

    /// A UI screen is up: every pad event is the UI's (any thread reads it). Down: the directions stop repeating
    void SetGrabbing(bool grabbing);
    bool Grabbing() const { return _grabbing.load(); }

    /// The tap's question, on the input backend's thread: does the UI take this event of @p device (a keyboard's
    /// never)
    bool Takes(const std::string& device, const std::string& control) const;

    /// One event the UI took, on the GUI thread
    void Feed(const QString& device, const QString& control, float value);

signals:
    void verb(HandheldPad::Verb verb);

private:
    enum class Direction
    {
        None,
        Up,
        Down,
        Left,
        Right
    };

    /// A direction source (the D-pad, the left stick) points somewhere else now
    void steer(const QString& source, Direction direction);
    void stopRepeat();
    static Verb VerbOf(Direction direction);
    /// The left trackpad as a cursor: one step per stretch of the finger's travel on the axis the touch picked first
    void track(const QString& source, bool touching, float x, float y);

    struct PadTrack
    {
        bool touching = false;
        float x = 0;
        float y = 0;
        float travelX = 0;  ///< the travel not yet turned into steps
        float travelY = 0;
        int axis = 0;  ///< 0 not picked yet, 1 x, 2 y
    };

    std::atomic<bool> _grabbing{false};
    std::map<QString, float> _values;         ///< device|control -> the last value
    std::map<QString, Direction> _directions;  ///< device|source -> where it points
    std::map<QString, PadTrack> _pads;         ///< device|ltouchpad -> the touch
    QString _repeating;                        ///< the source whose direction repeats ("" none)
    Direction _repeatDirection = Direction::None;
    QTimer* _repeat = nullptr;
};
