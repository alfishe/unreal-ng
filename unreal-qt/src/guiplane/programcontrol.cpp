#include "programcontrol.h"

#include "guicontrol/host.h"
#include "guicontrol/node.h"

using guicontrol::Call;
using guicontrol::Result;
namespace errors = guicontrol::errors;

ProgramControl::ProgramControl(guicontrol::Host& host, Hooks hooks, QObject* parent)
    : QObject(parent), _host(&host), _hooks(std::move(hooks))
{
    guicontrol::Node& root = host.Root();
    root.AddInterface(QStringLiteral("unrealqt"));
    const QPointer<ProgramControl> self(this);
    root.SetCommand(QStringLiteral("unrealqt.osd_keyboard"), [self](const Call& call) {
        if (!self || !self->_hooks.showKeyboard || !self->_hooks.keyboardShown)
            return Result::Failure(errors::kNotFound, QStringLiteral("the on-screen keyboard is not available"));
        bool on = false;
        Result failure;
        if (!guicontrol::params::Toggle(call.params, self->_hooks.keyboardShown(), on, failure))
            return failure;
        self->_hooks.showKeyboard(on);
        self->Publish();
        return Result::Success(QJsonObject{{"on", on}});
    });
    root.SetCommand(QStringLiteral("unrealqt.osd_keyboard_nav"), [self](const Call& call) {
        if (!self || !self->_hooks.keyboardNav || !self->_hooks.keyboardShown)
            return Result::Failure(errors::kNotFound, QStringLiteral("the on-screen keyboard is not available"));
        if (!self->_hooks.keyboardShown())
            return Result::Failure(errors::kFailed, QStringLiteral("the on-screen keyboard is hidden"));
        int dx = 0;
        int dy = 0;
        Result failure;
        if (!guicontrol::params::Direction(call.params, dx, dy, failure))
            return failure;
        self->_hooks.keyboardNav(dx, dy);
        self->Publish();
        return Result::Success(self->_hooks.keyboardState ? self->_hooks.keyboardState() : QJsonObject{});
    });
    root.SetCommand(QStringLiteral("unrealqt.osd_keyboard_tap"), [self](const Call&) {
        if (!self || !self->_hooks.keyboardTap || !self->_hooks.keyboardShown)
            return Result::Failure(errors::kNotFound, QStringLiteral("the on-screen keyboard is not available"));
        if (!self->_hooks.keyboardShown())
            return Result::Failure(errors::kFailed, QStringLiteral("the on-screen keyboard is hidden"));
        if (!self->_hooks.keyboardTap())
            return Result::Failure(errors::kFailed, QStringLiteral("no key under the cursor: move it first"));
        return Result::Success(self->_hooks.keyboardState ? self->_hooks.keyboardState() : QJsonObject{});
    });
    root.SetCommand(QStringLiteral("unrealqt.osd_keyboard_force"), [self](const Call& call) {
        if (!self || !self->_hooks.forceKeyboard)
            return Result::Failure(errors::kNotFound, QStringLiteral("the on-screen keyboard is not available"));
        const QString why = self->_hooks.forceKeyboard(call.params.value(QStringLiteral("keyboard")).toString().toStdString());
        if (!why.isEmpty())
            return Result::Failure(errors::kBadRequest, why);
        self->Publish();
        return Result::Success(self->_hooks.keyboardState ? self->_hooks.keyboardState() : QJsonObject{});
    });
    root.SetCommand(QStringLiteral("unrealqt.controllers_open"), [self](const Call&) {
        if (!self || !self->_hooks.showControllers)
            return Result::Failure(errors::kNotFound, QStringLiteral("the Controllers window is not available"));
        self->_hooks.showControllers();
        return Result::Success();
    });
    root.SetCommand(QStringLiteral("unrealqt.controllers_learn"), [self](const Call& call) {
        if (!self || !self->_hooks.learn)
            return Result::Failure(errors::kNotFound, QStringLiteral("the Controllers window is not available"));
        const QString device = call.params.value(QStringLiteral("device")).toString();
        if (device.isEmpty())
            return Result::Failure(errors::kBadRequest,
                                   QStringLiteral("device: a host device id (controllers devices lists them)"));
        const QString why = self->_hooks.learn(device.toStdString());
        return why.isEmpty() ? Result::Success() : Result::Failure(errors::kFailed, why);
    });
    Publish();
}

void ProgramControl::Publish()
{
    if (_host && _hooks.keyboardShown)
        _host->Root().SetState(QStringLiteral("osd_keyboard"), _hooks.keyboardShown());
    if (_host && _hooks.keyboardState)
        _host->Root().SetState(QStringLiteral("osd_keyboard_state"), _hooks.keyboardState());
}
