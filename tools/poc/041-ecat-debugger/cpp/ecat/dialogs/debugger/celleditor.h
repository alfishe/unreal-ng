// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: In-place editor of a value in the debugger's views

#pragma once

#include <QLineEdit>

//A line put over the value being changed. Enter commits, Esc cancels, Tab
//commits and goes on to the next value; losing the focus cancels
class CellEditor : public QLineEdit
{
    Q_OBJECT
public:
    explicit CellEditor(QWidget *parent);

    //Digits of the base only (2, 8, 16), at most that many; 0 for any text
    void set_digits(int base, int digits);
    //Opens over the rectangle with the text, all of it selected
    void open(const QRect &r, const QString &text);

signals:
    void committed();
    void cancelled();
    void tab_pressed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
};
