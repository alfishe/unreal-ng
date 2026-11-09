#include "guicontrol/screenbinding.h"

#include "guicontrol/host.h"
#include "guicontrol/node.h"
#include "guicontrol/protocol.h"

#include <QEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace guicontrol
{

ScreenBinding::ScreenBinding(Host& host, QWidget* window, QWidget* screen, const QString& windowId, const QString& id,
                             Hooks hooks)
    : QObject(screen), _host(&host), _window(window), _screen(screen), _windowId(windowId), _id(id),
      _hooks(std::move(hooks))
{
    Node& node = host.Root().AddChild(windowId).AddChild(id);
    node.AddInterface(QStringLiteral("screen"));
    const QPointer<ScreenBinding> self(this);

    node.SetCommand(QStringLiteral("screen.zoom"), [self](const Call& call) {
        if (!self || !self->_window || !self->_hooks.zoom)
            return Result::Failure(errors::kNotFound, QStringLiteral("the screen is gone"));
        const QJsonValue value = call.params.value(QStringLiteral("factor"));
        const double factor = value.toDouble(0);
        if (!value.isDouble() || factor != std::floor(factor) || factor < 1 || factor > 4)
            return Result::Failure(errors::kBadRequest, QStringLiteral("factor: a whole scale, 1..4"));
        const QString refused = self->_hooks.zoom(static_cast<int>(factor));
        if (!refused.isEmpty())
            return Result::Failure(errors::kNotNow, refused);
        self->Publish();
        return Result::Success(QJsonObject{{"zoom", static_cast<int>(factor)}});
    });
    node.SetCommand(QStringLiteral("screen.fullscreen"), [self](const Call& call) {
        if (!self || !self->_window)
            return Result::Failure(errors::kNotFound, QStringLiteral("the screen is gone"));
        bool on = false;
        Result failure;
        if (!params::Toggle(call.params, self->_window->isFullScreen(), on, failure))
            return failure;
        if (on != self->_window->isFullScreen())
        {
            if (self->_hooks.fullscreen)
                self->_hooks.fullscreen(on);
            else if (on)
                self->_window->showFullScreen();
            else
                self->_window->showNormal();
        }
        self->Publish();
        return Result::Success(QJsonObject{{"fullscreen", on}});
    });

    window->installEventFilter(this);
    screen->installEventFilter(this);
    Publish();
}

ScreenBinding::~ScreenBinding()
{
    if (!_host)
        return;
    if (Node* window = _host->Root().Child(_windowId))
        window->RemoveChild(_id);
}

Node* ScreenBinding::GetNode() const
{
    Node* window = _host ? _host->Root().Child(_windowId) : nullptr;
    return window != nullptr ? window->Child(_id) : nullptr;
}

bool ScreenBinding::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == _screen && event->type() == QEvent::Resize) ||
        (watched == _window && event->type() == QEvent::WindowStateChange))
        Publish();
    return QObject::eventFilter(watched, event);
}

void ScreenBinding::Publish()
{
    Node* node = GetNode();
    if (node == nullptr || !_window || !_screen)
        return;
    const QSize native = _hooks.nativeSize ? _hooks.nativeSize() : QSize();
    QJsonValue zoom;
    if (native.width() > 0 && native.height() > 0)
    {
        // The picture keeps its aspect: the smaller of the two ratios, to the hundredth
        const double scale = std::min(static_cast<double>(_screen->width()) / native.width(),
                                      static_cast<double>(_screen->height()) / native.height());
        zoom = std::round(scale * 100.0) / 100.0;
    }
    node->SetState(QJsonObject{{"zoom", zoom}, {"fullscreen", _window->isFullScreen()}});
    const QJsonObject instance = _hooks.instance ? _hooks.instance() : QJsonObject();
    node->SetInstance(instance.value(QStringLiteral("endpoint")).toString(), instance.value(QStringLiteral("id")).toString());
}

}  // namespace guicontrol
