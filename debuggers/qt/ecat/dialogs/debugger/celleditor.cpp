// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: In-place editor of a value in the debugger's views

#include "celleditor.h"

#include <QKeyEvent>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

#include "debugstyle.h"

CellEditor::CellEditor(QWidget *parent):
    QLineEdit(parent)
{
    setFont(dbg::font());
    setFrame(false);
    setStyleSheet("QLineEdit { border: 1px solid palette(highlight); padding: 0; margin: 0; }");
    hide();
}

void CellEditor::set_digits(int base, int digits)
{
    const QValidator * old = validator();
    if (base == 0) {
        setValidator(nullptr);
        setMaxLength(digits > 0 ? digits : 32767);
    } else {
        const QString chars = (base == 2) ? "[01 ]" : (base == 8) ? "[0-7 ]" : "[0-9A-Fa-f ]";
        setValidator(new QRegularExpressionValidator(QRegularExpression(QString("%1*").arg(chars)), this));
        setMaxLength(digits);
    }
    delete old;
}

void CellEditor::open(const QRect &r, const QString &text)
{
    setGeometry(r);
    setText(text);
    selectAll();
    show();
    setFocus();
}

void CellEditor::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit committed();
        return;
    case Qt::Key_Escape:
        emit cancelled();
        return;
    default:
        QLineEdit::keyPressEvent(event);
    }
}

bool CellEditor::event(QEvent *event)
{
    //Tab would move the focus to the next widget before keyPressEvent()
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Tab) {
        emit tab_pressed();
        return true;
    }
    return QLineEdit::event(event);
}

void CellEditor::focusOutEvent(QFocusEvent *event)
{
    QLineEdit::focusOutEvent(event);
    if (isVisible()) emit cancelled();
}
