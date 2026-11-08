#pragma once

/// @file windowbinding.h
/// @brief The `window` interface on a top-level widget (design §4.3): a window node under the host's root with
/// raise / show / hide / move / screenshot, its title, geometry, visibility and activity as state, and the host's
/// `window` focus following the window the user activates. The binding is a child of the widget: it goes with it,
/// and the node goes with the binding.

#include <QObject>
#include <QPointer>
#include <QString>

class QWidget;

namespace guicontrol
{

class Host;
class Node;

class WindowBinding : public QObject
{
    Q_OBJECT

public:
    /// Binds `window` as the window node `id` ("main", "debugger")
    WindowBinding(Host& host, QWidget* window, const QString& id);
    ~WindowBinding() override;

    /// The window's node; null when the host is gone
    Node* GetNode() const;
    const QString& Id() const { return _id; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void Publish();

    QPointer<Host> _host;
    QPointer<QWidget> _window;
    QString _id;
};

}  // namespace guicontrol
