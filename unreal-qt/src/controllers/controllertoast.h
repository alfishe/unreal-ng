#pragma once

/// @file controllertoast.h
/// @brief The auto-profile toast (ui-ux.md §3.8): "Kempston on kempston (port reads ...)" with Keep, Change… and ×,
/// at the bottom of the main window's screen area. Like DropTargetOverlay a frameless translucent window of its own:
/// the GPU screen is a native window a child widget would not draw over. Hides itself after 8 s.

#include <QTimer>
#include <QWidget>

class QLabel;

class ControllerToast : public QWidget
{
    Q_OBJECT

public:
    explicit ControllerToast(QWidget* owner);

    /// Show @p text over the bottom of @p area (global coordinates)
    void showText(const QString& text, const QRect& area);

signals:
    void keepRequested();
    void changeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* _text = nullptr;
    QTimer _hide;
};
