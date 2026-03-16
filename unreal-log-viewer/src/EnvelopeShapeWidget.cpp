#include "EnvelopeShapeWidget.h"

#include <QPainter>

// AY-8910/12 envelope waveform color (matches sparkline gray)
static const QColor ENVELOPE_COLOR(140, 140, 140);

// Envelope shape waveform definitions (16 shapes from AY-8910 datasheet)
// Each shape is a sequence of (x, y) points normalized to [0..1]
// R13 bits: CONT(3) | ATT(2) | ALT(1) | HOLD(0)

struct ShapePoint { float x, y; };

struct ShapeDef
{
    ShapePoint pts[10];
    int count;
};

static const ShapeDef SHAPES[16] =
{
    // 0-3: CONT=0, ATT=0 — decay to 0, hold low
    {{{0,1}, {0.33f,0}, {1,0}}, 3},
    {{{0,1}, {0.33f,0}, {1,0}}, 3},
    {{{0,1}, {0.33f,0}, {1,0}}, 3},
    {{{0,1}, {0.33f,0}, {1,0}}, 3},

    // 4-7: CONT=0, ATT=1 — attack to max, drop to 0, hold low
    {{{0,0}, {0.33f,1}, {0.33f,0}, {1,0}}, 4},
    {{{0,0}, {0.33f,1}, {0.33f,0}, {1,0}}, 4},
    {{{0,0}, {0.33f,1}, {0.33f,0}, {1,0}}, 4},
    {{{0,0}, {0.33f,1}, {0.33f,0}, {1,0}}, 4},

    // 8: CONT=1, ATT=0, ALT=0, HOLD=0 — repeating sawtooth down  \\\\
    //
    {{{0,1}, {0.5f,0}, {0.5f,1}, {1,0}}, 4},

    // 9: CONT=1, ATT=0, ALT=0, HOLD=1 — decay, hold low  \___
    {{{0,1}, {0.33f,0}, {1,0}}, 3},

    // 10: CONT=1, ATT=0, ALT=1, HOLD=0 — triangle down-up  \/\/
    {{{0,1}, {0.25f,0}, {0.5f,1}, {0.75f,0}, {1,1}}, 5},

    // 11: CONT=1, ATT=0, ALT=1, HOLD=1 — decay, hold high  \‾‾‾
    {{{0,1}, {0.33f,0}, {0.33f,1}, {1,1}}, 4},

    // 12: CONT=1, ATT=1, ALT=0, HOLD=0 — repeating sawtooth up  ////
    {{{0,0}, {0.5f,1}, {0.5f,0}, {1,1}}, 4},

    // 13: CONT=1, ATT=1, ALT=0, HOLD=1 — attack, hold high  /‾‾‾
    {{{0,0}, {0.33f,1}, {1,1}}, 3},

    // 14: CONT=1, ATT=1, ALT=1, HOLD=0 — triangle up-down  /\/\.
    {{{0,0}, {0.25f,1}, {0.5f,0}, {0.75f,1}, {1,0}}, 5},

    // 15: CONT=1, ATT=1, ALT=1, HOLD=1 — attack, drop to 0  /|__
    {{{0,0}, {0.33f,1}, {0.33f,0}, {1,0}}, 4},
};

EnvelopeShapeWidget::EnvelopeShapeWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(18);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void EnvelopeShapeWidget::setShape(int shape)
{
    if (_shape != shape)
    {
        _shape = shape;
        update();
    }
}

void EnvelopeShapeWidget::clear()
{
    _shape = -1;
    update();
}

void EnvelopeShapeWidget::paintEvent(QPaintEvent*)
{
    if (_shape < 0 || _shape > 15)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    int w = width();
    int h = height();
    float marginY = 2.0f;
    float marginL = 10.0f;  // ~1 char gap from indicator
    float drawW = w - marginL - 2.0f;
    float drawH = h - 2 * marginY;

    const ShapeDef& shape = SHAPES[_shape];

    painter.setPen(QPen(ENVELOPE_COLOR, 1.5));

    for (int i = 1; i < shape.count; i++)
    {
        float x0 = marginL + shape.pts[i - 1].x * drawW;
        float y0 = marginY + (1.0f - shape.pts[i - 1].y) * drawH;
        float x1 = marginL + shape.pts[i].x * drawW;
        float y1 = marginY + (1.0f - shape.pts[i].y) * drawH;
        painter.drawLine(QPointF(x0, y0), QPointF(x1, y1));
    }
}
