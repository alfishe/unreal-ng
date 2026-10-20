// Draws the selected model like the real thing (chassis, the LCD behind a bezel mask, dials, sensors) and turns mouse
// input into the device's input events.
#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRectF>
#include <QVector>
#include <QWidget>

class QTimer;
class VirtualDeck;

class DeckWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DeckWidget(VirtualDeck* deck, QWidget* parent = nullptr);
    bool bezelMask = true;  // false = show the raw LCD, gaps included
    bool showSafeArea = false;

    // Geometry for subclasses and tools: what is under a widget position, where a key is on the widget.
    struct Hit
    {
        enum Kind
        {
            None,
            Key,
            Sensor,
            Dial,
            Strip
        } kind = None;
        int index = -1;
        QPointF lcd;
    };
    [[nodiscard]] Hit HitTest(const QPointF& widgetPos) const;
    [[nodiscard]] QRectF KeyRectWidget(int key) const;
    [[nodiscard]] QRectF StripRectWidget() const;
    [[nodiscard]] QRectF DialRectWidget(int dial) const;
    [[nodiscard]] VirtualDeck* Deck() const
    {
        return _deck;
    }
    // A line over the deck (no connection, a deck unplugged); empty = none.
    void SetBanner(const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void leaveEvent(QEvent*) override;

    [[nodiscard]] QTransform ToWidget() const;
    [[nodiscard]] QRectF BodyRect() const;       // device units (LCD pixels), chassis included
    [[nodiscard]] QRectF DialRect(int i) const;  // device units

private:
    void Animate();
    [[nodiscard]] bool Animating() const;
    // Dial under the mouse: drag around it = turn (one detent per 15 degrees), click = press, hold still = long press.
    struct DialDrag
    {
        int index = -1;
        double lastAngle = 0, accum = 0, total = 0;
        bool rotated = false, holdSent = false;
    };
    [[nodiscard]] double AngleAround(int dial, const QPointF& widgetPos) const;
    // The knob, procedurally: black anodized aluminium, a straight cylinder with a milled top and a chamfer, lit from a
    // fixed light (the texture does not turn; the orange zero mark does). Rendered once per size in device pixels.
    void DrawKnob(QPainter& p, const QRectF& d, double angleDeg, bool down);
    void EnsureKnob(int px);
    void DrawBanner(QPainter& p);

    VirtualDeck* _deck;
    QImage _knobCap;
    int _knobPx = 0;
    QTimer* _anim;
    QVector<double> _shownAngle;   // eased toward the dial's real angle
    QVector<double> _keyGlow;      // a key's frame light, 1 = pressed, fading to 0 after the release
    QVector<bool> _keyGlowRemote;  // its colour: the real deck (blue-violet) or this window (cyan)
    DialDrag _drag;
    Hit _pressed, _hover;
    QPointF _stripStart;
    QElapsedTimer _stripTimer;
    double _wheelAccum = 0;
    QString _banner;
};
