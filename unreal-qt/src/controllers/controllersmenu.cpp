#include "controllersmenu.h"

#include <QActionGroup>
#include <QMenu>

#include "controllers/connectorview.h"
#include "controllers/hostcontrollers.h"
#include "emulator/emulatorcontext.h"

#ifdef ENABLE_CONTROLLERS
#include "devicedatabase.h"
#include "emulator/io/controllers/controllercontrol.h"
#include "emulator/io/controllers/controllermanager.h"
#include "router/inputrouter.h"

using namespace controllers;
#endif

namespace controllersmenu
{

void Fill(QMenu* menu, HostControllers* host)
{
    menu->clear();
#ifdef ENABLE_CONTROLLERS
    EmulatorContext* context = host != nullptr ? host->BoundContext() : nullptr;
    ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
    if (manager == nullptr)
    {
        menu->addAction(QMenu::tr("No machine"))->setEnabled(false);
        return;
    }
    const std::vector<Connector> connectors = manager->Connectors();
    std::vector<std::string> joysticks;
    for (const Connector& connector : connectors)
    {
        const connectorview::Style style = connectorview::StyleFor(connector);
        const QString port = connectorview::PortText(connector);
        const QString title = (style.tag.isEmpty() ? QString() : style.tag + QStringLiteral("  ")) +
                              connectorview::DisplayName(connector) +
                              (port.isEmpty() ? QString() : QStringLiteral("  ") + port);
        if (connector.device != RetroDeviceKind::None)
            joysticks.push_back(connector.id);
        if (connector.fixed)
        {
            // Part of the board: shown, not changed
            menu->addAction(title + QStringLiteral(": ") + QString::fromLatin1(RetroDeviceKindName(connector.device)))
                ->setEnabled(false);
            continue;
        }
        QMenu* sub = menu->addMenu(title);
        auto* group = new QActionGroup(sub);
        const auto add = [&](const QString& text, RetroDeviceKind kind) {
            QAction* action = sub->addAction(text);
            action->setCheckable(true);
            action->setChecked(connector.device == kind);
            group->addAction(action);
            const std::string id = connector.id;
            QObject::connect(action, &QAction::triggered, host, [host, id, kind] {
                EmulatorContext* bound = host->BoundContext();
                if (bound == nullptr)
                    return;
                ControllerRequest request;
                request.verb = kind == RetroDeviceKind::None ? "unplug" : "plug";
                request.selector = id;
                request.device = kind == RetroDeviceKind::None ? std::string() : RetroDeviceKindName(kind);
                request.options["async"] = "";
                ControllerControl(bound).Execute(request);
                host->Reapply();
            });
        };
        add(QMenu::tr("(empty)"), RetroDeviceKind::None);
        for (RetroDeviceKind kind : connector.accepts)
            add(QString::fromLatin1(RetroDeviceKindName(kind)), kind);
    }
    menu->addSeparator();

    // The player pads and what each drives now
    std::vector<std::pair<std::string, std::string>> pads;
    for (const HostDeviceInfo& device : host->Service().Router().Devices())
        if (device.family != "keyboard" && device.family != "mouse" &&
            DeviceDatabase::Active()->ForFamily(device.family, device.layout).player)
            pads.emplace_back(device.id, host->Service().Router().PlayerConnector(device.id));
    const auto swap = connectorview::SwapPorts(pads, joysticks);
    QAction* swapAction = menu->addAction(QMenu::tr("&Swap Joystick Ports"));
    swapAction->setStatusTip(QMenu::tr("Two pads trade their connectors; one pad moves on to the next joystick"));
    swapAction->setEnabled(!swap.empty());
    QObject::connect(swapAction, &QAction::triggered, host, [host, swap] {
        std::string message;
        for (const auto& [device, connector] : swap)
            host->ChoosePlayerConnector(device, connector, message);
    });
    QAction* reset = menu->addAction(QMenu::tr("&Reset Bindings to Default"));
    reset->setStatusTip(QMenu::tr("Drop the plugs made by hand: the profiles and the machine choose each pad's "
                                  "connector again"));
    QObject::connect(reset, &QAction::triggered, host, [host] { host->ResetPlayerChoices(); });
#else
    (void)host;
    menu->addAction(QMenu::tr("No controllers in this build"))->setEnabled(false);
#endif
}

}  // namespace controllersmenu
