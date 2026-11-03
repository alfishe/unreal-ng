// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Fonts and colours shared by the debugger's views

#pragma once

#include <QColor>
#include <QFont>
#include <algorithm>
#include <cmath>

#include <QFontDatabase>
#include <QFontMetrics>
#include <QPalette>
#include <QString>

namespace dbg {

//The window's scale (not in ecat): the views' font, and with it
//every row, column and margin they measure in it
inline double &zoom_value()
{
    static double z = 1.0;
    return z;
}
inline double zoom() { return zoom_value(); }
inline void set_zoom(double z) { zoom_value() = z; }

//What follows the scale, and how fast. The text read closely (listing, dump,
//registers) grows with it; the frame around it, glanced at, grows slower and
//within limits: a size is clamp(zoom ^ exponent, min, max) times its 100 % one
enum class Zoom {
    Content,    // the views' text, their rows and columns
    Frame,      // tabs, buttons, labels, column titles: one size in the window
    Icons,      // toolbar and button icons
};

struct ZoomCurve {
    double exponent;
    double min;
    double max;
};

inline ZoomCurve zoom_curve(Zoom what)
{
    switch (what) {
    case Zoom::Content: return {1.0, 0.5, 3.0};
    case Zoom::Frame: return {0.5, 0.85, 1.6};
    case Zoom::Icons: return {0.75, 0.75, 2.0};
    }
    return {1.0, 1.0, 1.0};
}

inline double zoom(Zoom what, double z)
{
    const ZoomCurve c = zoom_curve(what);
    return std::clamp(std::pow(z, c.exponent), c.min, c.max);
}
inline double zoom(Zoom what) { return zoom(what, zoom()); }

//The views are text in columns: one fixed-pitch font for all of them, the
//Consolas the main window registers from the resources
inline QFont font()
{
    QFont f("Consolas");
    f.setPointSizeF(10.0 * zoom(Zoom::Content));
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    return f;
}

//horizontalAdvance came with Qt 5.11, the XP build is on 5.6
inline int text_width(const QFontMetrics &fm, const QString &s)
{
#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
    return fm.width(s);
#else
    return fm.horizontalAdvance(s);
#endif
}

inline int row_height(const QFontMetrics &fm)
{
    return fm.height() + 2;
}

//The colours, derived from the palette so that a dark theme stays dark
struct Colors {
    QColor back;
    QColor text;
    QColor dim;             // addresses, bytes
    QColor selection;       // the row or the cell of the cursor
    QColor selection_text;
    QColor pc_back;         // the instruction PC points at
    QColor breakpoint;
    QColor changed;         // a value that changed since the last stop
    QColor jump;
    QColor call;
    QColor ret;
    QColor arrow;
    QColor arrow_active;    // the jump at PC
    QColor gutter;
    QColor address;         // addresses of the listing
    QColor reg_name;        // register names
    QColor separator;       // lines between the columns
};

inline Colors colors(const QPalette &p)
{
    Colors c;
    const bool dark = p.color(QPalette::Base).lightness() < 128;
    c.back = p.color(QPalette::Base);
    c.text = p.color(QPalette::Text);
    c.dim = dark ? QColor(150, 150, 150) : QColor(96, 96, 96);
    c.selection = p.color(QPalette::Highlight);
    c.selection_text = p.color(QPalette::HighlightedText);
    c.pc_back = dark ? QColor(70, 70, 20) : QColor(255, 250, 190);
    c.breakpoint = dark ? QColor(200, 60, 60) : QColor(220, 0, 0);
    c.changed = dark ? QColor(255, 110, 110) : QColor(210, 0, 0);
    c.jump = dark ? QColor(230, 180, 90) : QColor(160, 90, 0);
    c.call = dark ? QColor(120, 170, 255) : QColor(0, 70, 200);
    c.ret = dark ? QColor(120, 220, 120) : QColor(0, 130, 0);
    c.arrow = dark ? QColor(170, 170, 170) : QColor(70, 70, 70);
    c.arrow_active = dark ? QColor(255, 120, 120) : QColor(220, 0, 0);
    c.gutter = dark ? c.back.lighter(115) : QColor(245, 245, 245);
    c.address = dark ? QColor(130, 160, 255) : QColor(0, 0, 139);
    c.reg_name = dark ? QColor(110, 200, 110) : QColor(0, 100, 0);
    c.separator = dark ? QColor(80, 80, 80) : QColor(210, 210, 210);
    return c;
}

} // namespace dbg
