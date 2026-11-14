#include "handheldpad.h"

#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
constexpr int kRepeatDelayMs = 300;  ///< a held direction moves again after this
constexpr int kRepeatRateMs = 70;    ///< and then this often
constexpr float kPressed = 0.5f;     ///< a button
constexpr float kStickOn = 0.6f;     ///< a stick points once past this
constexpr float kStickOff = 0.4f;    ///< and lets go under this
// The left trackpad moves the focus as a cursor: by the finger's travel, not by where it rests (the pad spans -1..1).
// TUNING: the feel is these four numbers. Too twitchy: raise kPadStepY / kPadStepX (more travel per row); too slow:
// lower them. A vertical swipe drifting sideways: raise kPadAxisLock. A touch-down read as a move: lower kPadJump.
// The D-pad and stick repeat (kRepeatDelayMs / kRepeatRateMs above) do not apply to the trackpad.
constexpr float kPadAxisLock = 0.08f;  ///< the travel that picks the touch's axis (kept until the finger lifts)
constexpr float kPadStepY = 0.24f;     ///< one row per this much travel up or down (about 8 rows a full swipe)
constexpr float kPadStepX = 0.40f;     ///< one step sideways (leaves the page, changes a value): a longer travel
constexpr float kPadJump = 0.5f;       ///< a jump this large between two reports is a new touch, not a move

bool IsKeyboard(const std::string& device)
{
    return device.rfind("kbd:", 0) == 0;
}
}  // namespace

HandheldPad::HandheldPad(QObject* parent) : QObject(parent), _repeat(new QTimer(this))
{
    connect(_repeat, &QTimer::timeout, this, [this] {
        _repeat->setInterval(kRepeatRateMs);
        if (_repeatDirection != Direction::None)
            emit verb(VerbOf(_repeatDirection));
    });
}

void HandheldPad::SetGrabbing(bool grabbing)
{
    if (_grabbing.exchange(grabbing) == grabbing)
        return;
    // A screen that opens or closes starts from rest: a direction held across it does not repeat into the next one, and
    // a button starts released. A that opened a file or changed the model closed the menu while down: its release went
    // to the router, and a press kept here made the next A in the menu no press at all (it took two)
    stopRepeat();
    _directions.clear();
    _pads.clear();
    _values.clear();
}

bool HandheldPad::Takes(const std::string& device, const std::string& control) const
{
    if (IsKeyboard(device))
        return false;
    if (_grabbing.load())
        return true;
    if (_typing.load() && IsTypingControl(control))
        return true;
    return control == "menu" || control == "view";
}

void HandheldPad::SetTyping(bool typing)
{
    if (_typing.exchange(typing) == typing || typing)
        return;
    // The typing controls go back to the router: their releases no longer come here, so the next press starts from
    // released
    for (auto it = _values.begin(); it != _values.end();)
    {
        const auto bar = it->first.lastIndexOf('|');
        it = IsTypingControl(it->first.mid(bar + 1).toStdString()) ? _values.erase(it) : std::next(it);
    }
}

bool HandheldPad::IsTypingControl(const std::string& control)
{
    return control == "paddle.p1" || control == "paddle.p2" || control == "paddle.p3" || control == "east";
}

HandheldPad::Verb HandheldPad::VerbOf(Direction direction)
{
    switch (direction)
    {
        case Direction::Up: return Verb::Up;
        case Direction::Down: return Verb::Down;
        case Direction::Left: return Verb::Left;
        case Direction::Right: return Verb::Right;
        case Direction::None: break;
    }
    return Verb::Up;
}

void HandheldPad::stopRepeat()
{
    _repeat->stop();
    _repeating.clear();
    _repeatDirection = Direction::None;
}

void HandheldPad::steer(const QString& source, Direction direction)
{
    Direction& now = _directions[source];
    if (now == direction)
        return;
    now = direction;
    if (direction == Direction::None)
    {
        if (_repeating == source)
            stopRepeat();
        return;
    }
    emit verb(VerbOf(direction));
    _repeating = source;
    _repeatDirection = direction;
    _repeat->setInterval(kRepeatDelayMs);
    _repeat->start();
}

void HandheldPad::Feed(const QString& device, const QString& control, float value)
{
    const QString key = device + '|' + control;
    const float before = _values.count(key) != 0 ? _values[key] : 0.0f;
    _values[key] = value;
    const auto valueOf = [this, &device](const QString& name) {
        const auto it = _values.find(device + '|' + name);
        return it == _values.end() ? 0.0f : it->second;
    };

    // A button: its press is the verb
    static const std::map<QString, Verb> buttons = {
        {"south", Verb::Accept},     {"east", Verb::Back},        {"west", Verb::OptionX},
        {"north", Verb::OptionY},    {"lb", Verb::Prev},          {"rb", Verb::Next},
        {"menu", Verb::Menu},        {"view", Verb::View},        {"ltouchpad.click", Verb::Accept},
        {"paddle.p1", Verb::Paddle1}, {"paddle.p2", Verb::Paddle2}, {"paddle.p3", Verb::Paddle3},
        {"paddle.p4", Verb::Paddle4},
    };
    if (const auto it = buttons.find(control); it != buttons.end())
    {
        if (before < kPressed && value >= kPressed)
            emit verb(it->second);
        return;
    }

    // The D-pad: the last of its buttons held points
    if (control.startsWith(QLatin1String("dpad.")))
    {
        Direction direction = Direction::None;
        if (valueOf("dpad.up") >= kPressed)
            direction = Direction::Up;
        else if (valueOf("dpad.down") >= kPressed)
            direction = Direction::Down;
        else if (valueOf("dpad.left") >= kPressed)
            direction = Direction::Left;
        else if (valueOf("dpad.right") >= kPressed)
            direction = Direction::Right;
        steer(device + "|dpad", direction);
        return;
    }

    // The left trackpad: a cursor moved by the finger's travel
    if (control == QLatin1String("ltouchpad.x") || control == QLatin1String("ltouchpad.y") ||
        control == QLatin1String("ltouchpad.touch"))
    {
        track(device + QStringLiteral("|ltouchpad"), valueOf("ltouchpad.touch") >= kPressed, valueOf("ltouchpad.x"),
              valueOf("ltouchpad.y"));
        return;
    }

    // The left stick: the larger axis past its threshold points, with a hysteresis so a stick resting near the edge
    // does not chatter
    if (control != QLatin1String("lstick.x") && control != QLatin1String("lstick.y"))
        return;
    const QString source = device + QStringLiteral("|lstick");
    const float x = valueOf("lstick.x");
    const float y = valueOf("lstick.y");  // -1 up
    const bool held = _directions.count(source) != 0 && _directions[source] != Direction::None;
    const float threshold = held ? kStickOff : kStickOn;
    Direction direction = Direction::None;
    if (std::max(std::fabs(x), std::fabs(y)) >= threshold)
    {
        if (std::fabs(x) > std::fabs(y))
            direction = x < 0 ? Direction::Left : Direction::Right;
        else
            direction = y < 0 ? Direction::Up : Direction::Down;
    }
    steer(source, direction);
}

void HandheldPad::track(const QString& source, bool touching, float x, float y)
{
    PadTrack& pad = _pads[source];
    if (!touching)
    {
        pad = PadTrack{};
        return;
    }
    if (!pad.touching || std::fabs(x - pad.x) > kPadJump || std::fabs(y - pad.y) > kPadJump)
    {
        // A finger down (or a jump): where it is now is the start, nothing moves yet
        pad = PadTrack{};
        pad.touching = true;
        pad.x = x;
        pad.y = y;
        return;
    }
    pad.travelX += x - pad.x;
    pad.travelY += y - pad.y;
    pad.x = x;
    pad.y = y;
    if (pad.axis == 0)
    {
        if (std::max(std::fabs(pad.travelX), std::fabs(pad.travelY)) < kPadAxisLock)
            return;
        pad.axis = std::fabs(pad.travelX) > std::fabs(pad.travelY) ? 1 : 2;
    }
    // One verb per step of travel along the touch's axis; the rest carries over (a slow finger still moves)
    float& travel = pad.axis == 1 ? pad.travelX : pad.travelY;
    const float step = pad.axis == 1 ? kPadStepX : kPadStepY;
    while (std::fabs(travel) >= step)
    {
        const bool negative = travel < 0;
        travel += negative ? step : -step;
        if (pad.axis == 1)
            emit verb(negative ? Verb::Left : Verb::Right);
        else
            emit verb(negative ? Verb::Up : Verb::Down);  // y -1 is up: the focus follows the finger
    }
}
