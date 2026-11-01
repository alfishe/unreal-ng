// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Common ancestor foe debug windows, source

#include "genericdbgwnd.h"

GenericDbgWnd::GenericDbgWnd(QWidget *parent):
    QDialog(parent)
{}

void GenericDbgWnd::update_view()
{
    update();
}
