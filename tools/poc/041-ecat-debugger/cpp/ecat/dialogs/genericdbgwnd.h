// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Common ancestor foe debug windows, header

#pragma once

#include <QCoreApplication>
#include <QDialog>

inline QString translateResultMessage(const std::string &message)
{
    QString msg = QString::fromStdString(message);
    int open = msg.indexOf('{');
    int close = msg.indexOf('}');
    if (open >= 0 && close > open) {
        QString inner = msg.mid(open + 1, close - open - 1);
        int sep = inner.indexOf('|');
        if (sep >= 0) {
            QByteArray context = inner.left(sep).toUtf8();
            QByteArray source = inner.mid(sep + 1).toUtf8();
            QString translated = QCoreApplication::translate(context.constData(), source.constData());
            msg = translated + msg.mid(close + 1);
        }
    }
    return msg;
}

class GenericDbgWnd : public QDialog
{
    Q_OBJECT
public:
    GenericDbgWnd(QWidget *parent);

signals:
    void data_changed(GenericDbgWnd * src);

public slots:
    virtual void update_view();
};
