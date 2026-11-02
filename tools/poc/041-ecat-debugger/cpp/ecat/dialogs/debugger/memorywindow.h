// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The memory dump of one device in a window of its own, header

#pragma once

#include "dialogs/genericdbgwnd.h"
#include "emulator/emulator.h"

class MemoryPanel;
class MemoryTabs;

//The debugger's dump, opened from the Devices menu for any device with an
//address space: a memory, a mapper, the registers of a controller
class MemoryWindow : public GenericDbgWnd
{
    Q_OBJECT
public:
    MemoryWindow(QWidget *parent, Emulator * e, AddressableDevice * d);

public slots:
    void update_view() override;

private:
    MemoryPanel * m_panel;
};

GenericDbgWnd * CreateMemoryWindow(QWidget *parent, Emulator * e, ComputerDevice * d);

//Debug > Dumps: the debugger's dump tabs in a window of their own, one for
//the machine
class DumpsWindow : public GenericDbgWnd
{
    Q_OBJECT
public:
    DumpsWindow(QWidget *parent, Emulator * e);

public slots:
    void update_view() override;

private:
    MemoryTabs * m_tabs;
};
