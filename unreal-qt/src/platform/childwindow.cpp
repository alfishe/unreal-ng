#include "platform/childwindow.h"

#include <QtGlobal>

// macOS has its own implementation (platform/macos/childwindow_macos.mm)
#ifndef Q_OS_MACOS

bool ChildWindow::Attach(QWidget* parent, QWidget* child)
{
    (void)parent;
    (void)child;
    return false;
}

void ChildWindow::Detach(QWidget* parent, QWidget* child)
{
    (void)parent;
    (void)child;
}

#endif
