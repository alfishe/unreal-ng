// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The memory dump of one device in a window of its own

#include "memorywindow.h"

#include <QTimer>
#include <QVBoxLayout>

#include "memoryview.h"

MemoryWindow::MemoryWindow(QWidget *parent, Emulator * e, AddressableDevice * d):
    GenericDbgWnd(parent)
{
    setWindowTitle(QString::fromStdString(d->name + " : " + d->type));
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    resize(720, 480);
    QVBoxLayout * layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    m_panel = new MemoryPanel(e, memory_of_device(d), this);
    layout->addWidget(m_panel);
    connect(m_panel->view(), &MemoryView::memory_changed, this, [this]() { emit data_changed(this); });

    //The machine runs meanwhile: what it writes shows up
    QTimer * timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MemoryWindow::update_view);
    timer->start(500);
}

void MemoryWindow::update_view()
{
    m_panel->refresh();
}

DumpsWindow::DumpsWindow(QWidget *parent, Emulator * e):
    GenericDbgWnd(parent)
{
    setWindowTitle(tr("Dumps"));
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    resize(760, 520);
    QVBoxLayout * layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    m_tabs = new MemoryTabs(e, this);
    layout->addWidget(m_tabs);
    connect(m_tabs, &MemoryTabs::memory_changed, this, [this]() { emit data_changed(this); });

    QTimer * timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &DumpsWindow::update_view);
    timer->start(500);
}

void DumpsWindow::update_view()
{
    m_tabs->refresh();
}

GenericDbgWnd * CreateMemoryWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    AddressableDevice * a = dynamic_cast<AddressableDevice*>(d);
    if (a == nullptr) return nullptr;
    return new MemoryWindow(parent, e, a);
}
