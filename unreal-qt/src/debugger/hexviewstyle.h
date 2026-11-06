#pragma once

#include <QFontDatabase>
#include <QLabel>
#include <QString>

#include "QHexView/qhexview.h"

/// One look for the debugger's byte editors (device memory, disk sector): the system monospace font, 16 bytes a
/// line, separators, and an address column as wide as the last offset needs (at least 4 digits)
inline void StyleHexView(QHexView* view, quint64 length)
{
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QHexOptions options = view->options();
    options.linelength = 16;
    options.addresswidth = 4;
    for (quint64 last = length > 0 ? length - 1 : 0; last > 0xFFFF; last >>= 4)
        ++options.addresswidth;
    options.flags = QHexFlags::HSeparator | QHexFlags::VSeparator;
    view->setOptions(options);
}

/// The note shown where a byte editor has nothing to show: centred, wrapped, in the window's disabled text colour
inline QLabel* MakeEmptyNote(QWidget* parent)
{
    QLabel* note = new QLabel(parent);
    note->setAlignment(Qt::AlignCenter);
    note->setWordWrap(true);
    note->setMargin(24);
    note->setForegroundRole(QPalette::PlaceholderText);
    return note;
}
