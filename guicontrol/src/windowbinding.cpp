#include "guicontrol/windowbinding.h"

#include "guicontrol/host.h"
#include "guicontrol/node.h"

#include <QClipboard>
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QRunnable>
#include <QThreadPool>
#include <QWidget>

namespace guicontrol
{

WindowBinding::WindowBinding(Host& host, QWidget* window, const QString& id)
    : QObject(window), _host(&host), _window(window), _id(id)
{
    Node& node = host.Root().AddChild(id);
    node.AddInterface(QStringLiteral("window"));
    const QPointer<WindowBinding> self(this);

    node.SetCommand(QStringLiteral("window.raise"), [self](const Call&) {
        if (!self || !self->_window)
            return Result::Failure(errors::kNotFound, QStringLiteral("the window is gone"));
        QWidget* window = self->_window;
        if (window->isMinimized())
            window->showNormal();
        window->show();
        window->raise();
        window->activateWindow();
        return Result::Success();
    });
    node.SetCommand(QStringLiteral("window.show"), [self](const Call&) {
        if (!self || !self->_window)
            return Result::Failure(errors::kNotFound, QStringLiteral("the window is gone"));
        self->_window->show();
        return Result::Success();
    });
    node.SetCommand(QStringLiteral("window.hide"), [self](const Call&) {
        if (!self || !self->_window)
            return Result::Failure(errors::kNotFound, QStringLiteral("the window is gone"));
        self->_window->hide();
        return Result::Success();
    });
    node.SetCommand(QStringLiteral("window.move"), [self](const Call& call) {
        if (!self || !self->_window)
            return Result::Failure(errors::kNotFound, QStringLiteral("the window is gone"));
        QWidget* window = self->_window;
        QRect geometry = window->geometry();
        for (const char* key : {"x", "y", "w", "h"})
            if (call.params.contains(key) && !call.params.value(key).isDouble())
                return Result::Failure(errors::kBadRequest, QStringLiteral("%1: a number").arg(key));
        if (call.params.contains("w") || call.params.contains("h"))
        {
            const int w = call.params.value("w").toInt(geometry.width());
            const int h = call.params.value("h").toInt(geometry.height());
            if (w <= 0 || h <= 0)
                return Result::Failure(errors::kBadRequest, QStringLiteral("w, h: above 0"));
            window->resize(w, h);
        }
        if (call.params.contains("x") || call.params.contains("y"))
            window->move(call.params.value("x").toInt(window->x()), call.params.value("y").toInt(window->y()));
        self->Publish();
        return Result::Success(self->GetNode() != nullptr ? self->GetNode()->State() : QJsonObject());
    });
    node.SetAsyncCommand(QStringLiteral("window.screenshot"), [self](const Call& call, Done done) {
        if (!self || !self->_window)
        {
            done(Result::Failure(errors::kNotFound, QStringLiteral("the window is gone")));
            return;
        }
        const QPixmap picture = self->_window->grab();
        const QJsonObject size{{"width", picture.width()}, {"height", picture.height()}};
        const QString path = call.params.value("path").toString();
        if (path.isEmpty())
        {
            QGuiApplication::clipboard()->setPixmap(picture);
            QJsonObject body = size;
            body["clipboard"] = true;
            done(Result::Success(body));
            return;
        }
        // Grabbed here, encoded and written on a pool thread; the answer comes back on the host's thread
        const QImage image = picture.toImage();
        const QPointer<Host> host = self->_host;
        QThreadPool::globalInstance()->start(QRunnable::create([image, path, size, host, done]() {
            const bool saved = image.save(path, "PNG");
            if (!host)
                return;
            QMetaObject::invokeMethod(host.data(), [saved, path, size, done]() {
                if (!saved)
                {
                    done(Result::Failure(errors::kFailed, QStringLiteral("cannot write %1").arg(path)));
                    return;
                }
                QJsonObject body = size;
                body["path"] = path;
                done(Result::Success(body));
            });
        }));
    });

    window->installEventFilter(this);
    Publish();
}

WindowBinding::~WindowBinding()
{
    if (_host)
        _host->Root().RemoveChild(_id);
}

Node* WindowBinding::GetNode() const
{
    return _host ? _host->Root().Child(_id) : nullptr;
}

bool WindowBinding::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _window)
        switch (event->type())
        {
            case QEvent::Move:
            case QEvent::Resize:
            case QEvent::Show:
            case QEvent::Hide:
            case QEvent::WindowTitleChange:
            case QEvent::WindowDeactivate:
                Publish();
                break;
            case QEvent::WindowActivate:
                Publish();
                if (_host)
                    _host->SetFocus(QStringLiteral("window"), GetNode());
                break;
            default:
                break;
        }
    return QObject::eventFilter(watched, event);
}

void WindowBinding::Publish()
{
    Node* node = GetNode();
    if (node == nullptr || !_window)
        return;
    const QRect geometry = _window->geometry();
    node->SetState(QJsonObject{
        {"title", _window->windowTitle()},
        {"geometry", QJsonObject{{"x", geometry.x()}, {"y", geometry.y()}, {"w", geometry.width()}, {"h", geometry.height()}}},
        {"visible", _window->isVisible()},
        {"active", _window->isActiveWindow()}});
}

}  // namespace guicontrol
