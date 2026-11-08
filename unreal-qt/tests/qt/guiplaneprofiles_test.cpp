// The shipped controller profiles and the GUI control plane (design docs/inprogress/2026-11-07-gui-control-plane
// §4.7, phase 3): a binding of a GUI verb names a declared plane command with parameters it takes, never one of the
// core's GUI actions.

#include "emulator/actions/catalogue.h"
#include "guicontrol/host.h"
#include "guicontrol/interfaces.h"
#include "guiplane/unrealqtinterface.h"
#include "profiles/profilestore.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace
{

/// Every `action` target of every shipped profile: "<profile> <layer>.<control>" -> {id, params as JSON text}
struct ActionBinding
{
    std::string where;
    std::string id;
    std::string params;
};

std::vector<ActionBinding> ShippedActionBindings()
{
    std::vector<ActionBinding> bindings;
    controllers::ProfileStore store;  // the compiled-in profiles
    for (const controllers::Profile* profile : store.Profiles())
        for (const auto& [layer, controls] : profile->layers)
            for (const auto& [control, spec] : controls)
            {
                const controllers::JsonValue* action = spec.IsObject() ? spec.Find("action") : nullptr;
                if (action == nullptr || !action->IsString())
                    continue;
                const controllers::JsonValue* params = spec.Find("params");
                bindings.push_back({profile->id + " " + layer + "." + control, action->AsString(),
                                    params != nullptr ? params->Dump() : std::string("{}")});
            }
    return bindings;
}

}  // namespace

TEST(GuiPlaneProfiles_Test, ShippedBindingsUseThePlane)
{
    DeclareUnrealQtInterface();
    std::set<std::string> coreGui;
    for (const actions::GuiActionDeclaration& declaration : actions::GuiActionDeclarations())
        coreGui.insert(declaration.id);

    const std::vector<ActionBinding> bindings = ShippedActionBindings();
    ASSERT_FALSE(bindings.empty());
    size_t planeBindings = 0;
    for (const ActionBinding& binding : bindings)
    {
        EXPECT_EQ(coreGui.count(binding.id), 0u)
            << binding.where << ": " << binding.id << " is a GUI action of the core; bind its GUI control plane command";
        const QString method = QString::fromStdString(binding.id);
        if (guicontrol::FindInterface(guicontrol::InterfaceOf(method)) == nullptr)
            continue;  // a core action
        ++planeBindings;
        ASSERT_NE(guicontrol::FindCommand(method), nullptr) << binding.where << ": no plane command " << binding.id;
        const QJsonObject params = QJsonDocument::fromJson(QByteArray::fromStdString(binding.params)).object();
        const guicontrol::Result checked = guicontrol::Host::CheckParams(method, params);
        EXPECT_TRUE(checked.ok) << binding.where << ": " << checked.message.toStdString();
    }
    EXPECT_GE(planeBindings, 9u);  // debug/pad-default 6, debug/shuttle-pro 2, play/pad-default 1
}
