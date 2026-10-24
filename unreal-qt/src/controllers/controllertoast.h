#pragma once

/// @file controllertoast.h
/// @brief The controllers' toast (ui-ux.md §3.8, §8): the auto-profile decision "Kempston on kempston (port reads ...)"
/// with Keep, Change… and ×; a device's first connection "Xbox Elite 2 connected → Kempston" with Open controllers;
/// a device gone while the machine runs ("disconnected: paused" with the pause-on-disconnect option), × alone. At the
/// bottom of the main window's screen area, one at a time (a new one replaces the shown one). Like DropTargetOverlay a frameless translucent window of its own:
/// the GPU screen is a native window a child widget would not draw over. Hides itself after 8 s.

#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;

class ControllerToast : public QWidget
{
    Q_OBJECT

public:
    explicit ControllerToast(QWidget* owner);

    enum class Kind
    {
        AutoProfile,   ///< Keep, Change…
        Connected,     ///< Open controllers
        Disconnected,  ///< × alone
    };

    /// Show @p text over the bottom of @p area (global coordinates)
    void showText(const QString& text, const QRect& area, Kind kind = Kind::AutoProfile);
    Kind kind() const
    {
        return _kind;
    }
    QString text() const;

signals:
    void keepRequested();
    void changeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* _text = nullptr;
    QPushButton* _keep = nullptr;
    QPushButton* _change = nullptr;
    QPushButton* _open = nullptr;
    Kind _kind = Kind::AutoProfile;
    QTimer _hide;
};
