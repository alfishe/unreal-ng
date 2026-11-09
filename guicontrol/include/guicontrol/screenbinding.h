#pragma once

/// @file screenbinding.h
/// @brief The `screen` interface on a view of an emulated screen (design §4.3): `screen.zoom {factor}` and
/// `screen.fullscreen {on?}`, the scale the picture is shown at and whether its window is full screen as state, and
/// the machine it shows as the node's instance. The program gives what only it knows through hooks: the picture's
/// native size, how its window fits a whole scale, how it goes full screen. The binding is a child of the screen
/// widget: it goes with it, and the node goes with the binding.

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>

#include <functional>

class QWidget;

namespace guicontrol
{

class Host;
class Node;

class ScreenBinding : public QObject
{
    Q_OBJECT

public:
    struct Hooks
    {
        /// The picture at 1x: the frame the screen shows now
        std::function<QSize()> nativeSize;
        /// Fit the window to the picture at the whole scale `factor` (1..4); "" done, else why not
        std::function<QString(int factor)> zoom;
        /// Full screen on or off; absent: the window's showFullScreen / showNormal
        std::function<void(bool on)> fullscreen;
        /// The machine shown: {endpoint, id}, empty when none
        std::function<QJsonObject()> instance;
    };

    /// Binds `screen` (inside `window`) as the view `id` under the window node `windowId` ("main/screen")
    ScreenBinding(Host& host, QWidget* window, QWidget* screen, const QString& windowId, const QString& id,
                  Hooks hooks);
    ~ScreenBinding() override;

    Node* GetNode() const;
    /// The state and the instance again (another machine shown, another frame size)
    void Publish();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<Host> _host;
    QPointer<QWidget> _window;
    QPointer<QWidget> _screen;
    QString _windowId;
    QString _id;
    Hooks _hooks;
};

}  // namespace guicontrol
