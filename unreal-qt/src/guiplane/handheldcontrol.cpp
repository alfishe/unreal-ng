#include "handheldcontrol.h"

#include <QPointer>

#include "guicontrol/host.h"
#include "guicontrol/node.h"
#include "handheld/handheldmenu.h"

using guicontrol::Call;
using guicontrol::Result;
namespace errors = guicontrol::errors;

void RegisterHandheldControl(guicontrol::Host& host, HandheldMenu* menu)
{
    guicontrol::Node& root = host.Root();
    root.AddInterface(QStringLiteral("handheld"));
    const QPointer<HandheldMenu> target(menu);
    const QPointer<guicontrol::Host> plane(&host);
    const auto missing = [] {
        return Result::Failure(errors::kNotFound,
                               QStringLiteral("the handheld menu is off: Steam Deck Game Mode or UNREAL_HANDHELD=1"));
    };
    const auto closed = [] { return Result::Failure(errors::kFailed, QStringLiteral("the handheld menu is closed")); };
    const auto publish = [plane, target] {
        if (plane && target)
            plane->Root().SetState(QStringLiteral("handheld_menu"), target->State());
    };
    root.SetCommand(QStringLiteral("handheld.menu"), [target, missing](const Call& call) {
        if (!target)
            return missing();
        bool on = false;
        Result failure;
        if (!guicontrol::params::Toggle(call.params, target->IsOpen(), on, failure))
            return failure;
        if (on)
            target->Open();
        else
            target->Close();
        return Result::Success(QJsonObject{{"on", on}});
    });
    root.SetCommand(QStringLiteral("handheld.nav"), [target, missing, closed](const Call& call) {
        if (!target)
            return missing();
        if (!target->IsOpen())
            return closed();
        int dx = 0;
        int dy = 0;
        Result failure;
        if (!guicontrol::params::Direction(call.params, dx, dy, failure))
            return failure;
        target->Nav(dx, dy);
        return Result::Success(target->State());
    });
    root.SetCommand(QStringLiteral("handheld.accept"), [target, missing, closed](const Call&) {
        if (!target)
            return missing();
        if (!target->IsOpen())
            return closed();
        target->Accept();
        return Result::Success(target->State());
    });
    root.SetCommand(QStringLiteral("handheld.back"), [target, missing, closed](const Call&) {
        if (!target)
            return missing();
        if (!target->IsOpen())
            return closed();
        target->Back();
        return Result::Success(target->State());
    });
    root.SetCommand(QStringLiteral("handheld.option"), [target, missing, closed](const Call& call) {
        if (!target)
            return missing();
        if (!target->IsOpen())
            return closed();
        const QString button = call.params.value(QStringLiteral("button")).toString();
        if (button != QLatin1String("x") && button != QLatin1String("y"))
            return Result::Failure(errors::kBadRequest, QStringLiteral("button: x or y"));
        target->Option(button.at(0).toLatin1());
        return Result::Success(target->State());
    });
    root.SetCommand(QStringLiteral("handheld.section"), [target, missing, closed](const Call& call) {
        if (!target)
            return missing();
        if (!target->IsOpen())
            return closed();
        const QString dir = call.params.value(QStringLiteral("dir")).toString();
        if (dir != QLatin1String("prev") && dir != QLatin1String("next"))
            return Result::Failure(errors::kBadRequest, QStringLiteral("dir: prev or next"));
        target->Section(dir == QLatin1String("next") ? 1 : -1);
        return Result::Success(target->State());
    });
    if (menu != nullptr)
        QObject::connect(menu, &HandheldMenu::stateChanged, &host, publish);
    root.SetState(QStringLiteral("handheld_menu"), menu != nullptr ? menu->State() : QJsonObject{{"open", false}});
}
