// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Helpers of the debugger's views

#pragma once

#include <vector>

#include <QAbstractButton>
#include <QPaintEvent>
#include <QRegion>
#include <QString>
#include <QStringList>
#include <QToolBar>
#include <QVariant>

#include "emulator/core.h"
#include "emulator/devices/cpu/i8086.h"
#include "emulator/devices/cpu/k1801vm1.h"
#include "dialogs/numfmt.h"

//The addresses a processor's code lives at, as its debugger sees them: the
//К1801 family 64 KB of its own addresses (the ВМ3 translates them through its
//MMU), anything else its mapper
inline unsigned int code_space(CPU * cpu)
{
    if (dynamic_cast<k1801vm1*>(cpu) != nullptr) return 0x10000;
    const unsigned int s = (cpu->mm != nullptr) ? cpu->mm->get_size() : 0;
    return (s > 0) ? s : 0x10000;
}

//A register by name, 0 when there is none
inline unsigned int register_value(CPU * cpu, const std::string &name)
{
    for (const CpuRegister &r : cpu->get_register_values())
        if (r.name == name) return r.value;
    return 0;
}

//An address of code as a listing writes it: CS:IP on the 8086 when it lies in
//the current code segment, the plain number otherwise
inline QString code_address(CPU * cpu, unsigned int address, int base, int digits)
{
    if (dynamic_cast<i8086*>(cpu) != nullptr) {
        const unsigned int cs = register_value(cpu, "CS");
        const unsigned int from = cs << 4;
        if (address >= from && address - from <= 0xFFFF)
            return fmt_num(cs, 16, 4) + ":" + fmt_num(address - from, 16, 4);
        return fmt_num(address, 16, 5) + "    ";
    }
    return fmt_num(address, base, digits);
}

//Space-separated words, empty ones dropped (Qt::SkipEmptyParts came with 5.14)
inline QStringList split_words(const QString &s)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    return s.split(' ', Qt::SkipEmptyParts);
#else
    return s.split(' ', QString::SkipEmptyParts);
#endif
}

//The icons of the buttons under a widget at the window's scale (POC 041, not in
//ecat), from the size each one had at 100 %; a toolbar scales its own
inline void scale_icons(QWidget * root, double zoom)
{
    for (QAbstractButton * b : root->findChildren<QAbstractButton*>()) {
        if (qobject_cast<QToolBar*>(b->parentWidget()) != nullptr) continue;
        QVariant base = b->property("dbg_icon_size");
        if (!base.isValid()) {
            base = b->iconSize();
            b->setProperty("dbg_icon_size", base);
        }
        b->setIconSize(base.toSize() * zoom);
    }
}

//What each row of a view was last painted with. The 200 ms timer refreshes the
//views while the machine runs; repainting them whole every time costs more
//than everything else the window does, so a refresh repaints only the rows
//whose key (what the row shows, as a string) differs from the painted one.
//A row counts as painted when the paint covered it whole, or when it was
//painted already with the same key: then the part left out still shows it.
//Otherwise the jump arrows, a column over all the rows, would leave every row
//half painted and the next refresh would repaint the whole listing
class PaintedRows
{
public:
    void painted(const QPaintEvent *event, int row, const QRect &rect, const QString &key)
    {
        if (row >= (int)m_rows.size()) m_rows.resize(row + 1);
        Row &r = m_rows[row];
        r.valid = (QRegion(rect) - event->region()).isEmpty() || (r.valid && r.key == key);
        r.key = key;
    }
    bool same(int row, const QString &key) const
    {
        return row < (int)m_rows.size() && m_rows[row].valid && m_rows[row].key == key;
    }
    //The rows moved (another font): none is on the screen as known
    void clear() { m_rows.clear(); }

private:
    struct Row {
        bool valid = false;
        QString key;
    };
    std::vector<Row> m_rows;
};
