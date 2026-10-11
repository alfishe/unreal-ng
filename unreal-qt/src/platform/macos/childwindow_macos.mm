#include "platform/childwindow.h"

#import <AppKit/AppKit.h>

#include <QGuiApplication>
#include <QWidget>

namespace
{
NSWindow* NativeWindow(QWidget* widget)
{
    // Only the Cocoa platform's window ids are NSViews: offscreen (the sanitizer stress, CI) hands out its own ids,
    // and messaging one crashed docking the first tool window
    if (!widget || QGuiApplication::platformName() != QLatin1String("cocoa"))
        return nil;
    // winId() creates the native window when needed; on Cocoa it is the NSView
    NSView* view = reinterpret_cast<NSView*>(widget->window()->winId());
    return view ? [view window] : nil;
}
}  // namespace

bool ChildWindow::Attach(QWidget* parent, QWidget* child)
{
    NSWindow* parentWindow = NativeWindow(parent);
    NSWindow* childWindow = NativeWindow(child);
    if (!parentWindow || !childWindow || parentWindow == childWindow)
        return false;
    if ([childWindow parentWindow] != parentWindow)
        [parentWindow addChildWindow:childWindow ordered:NSWindowAbove];
    return true;
}

void ChildWindow::Detach(QWidget* parent, QWidget* child)
{
    NSWindow* parentWindow = NativeWindow(parent);
    NSWindow* childWindow = NativeWindow(child);
    if (parentWindow && childWindow && [childWindow parentWindow] == parentWindow)
        [parentWindow removeChildWindow:childWindow];
}
