// The shipped controller profiles and the GUI control plane (design docs/inprogress/2026-11-07-gui-control-plane
// §4.7, phases 3-4): every action binding names a core action or a declared plane command with parameters it takes;
// the GUI verbs are no core actions.

#include "emulator/actions/actionregistry.h"
#include "guicontrol/host.h"
#include "guicontrol/interfaces.h"
#include "guiplane/unrealqtinterface.h"
#include "profiles/profilestore.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

#include <cctype>
#include <optional>
#include <string>
#include <vector>

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

/// The parameters an action's params line names without `?` ("addr, seek? (true), ..." -> addr); parentheses are notes
std::vector<std::string> RequiredParams(const std::string& line)
{
    std::vector<std::string> required;
    int depth = 0;
    std::string token;
    const auto take = [&]() {
        size_t at = token.find_first_not_of(' ');
        size_t end = at;
        while (end < token.size() && (std::islower(static_cast<unsigned char>(token[end])) || token[end] == '_'))
            ++end;
        if (at != std::string::npos && end > at && (end == token.size() || token[end] != '?'))
            required.push_back(token.substr(at, end - at));
        token.clear();
    };
    for (char c : line)
    {
        depth += c == '(' ? 1 : c == ')' ? -1 : 0;
        if (c == ',' && depth == 0)
            take();
        else if (depth == 0 && c != ')')
            token += c;
    }
    take();
    return required;
}

}  // namespace

TEST(GuiPlaneProfiles_Test, ShippedBindingsUseThePlane)
{
    DeclareUnrealQtInterface();
    const actions::ActionRegistry& core = actions::ActionRegistry::Instance();

    const std::vector<ActionBinding> bindings = ShippedActionBindings();
    ASSERT_FALSE(bindings.empty());
    size_t planeBindings = 0;
    for (const ActionBinding& binding : bindings)
    {
        const QString method = QString::fromStdString(binding.id);
        if (guicontrol::FindInterface(guicontrol::InterfaceOf(method)) == nullptr)
        {
            // Not a plane command: a core action, or nothing (a GUI action id the core had: its plane command now)
            const std::optional<actions::ActionInfo> info = core.Find(binding.id);
            EXPECT_TRUE(info.has_value())
                << binding.where << ": " << binding.id << " is neither a core action nor a GUI control plane command";
            // A core action the GUI composes (run.to_cursor {addr}) is bound through its plane command
            const QJsonObject params = QJsonDocument::fromJson(QByteArray::fromStdString(binding.params)).object();
            for (const std::string& name : info ? RequiredParams(info->params) : std::vector<std::string>())
                EXPECT_TRUE(params.contains(QString::fromStdString(name)))
                    << binding.where << ": " << binding.id << " needs " << name << " (" << info->params << ")";
            continue;
        }
        ++planeBindings;
        ASSERT_NE(guicontrol::FindCommand(method), nullptr) << binding.where << ": no plane command " << binding.id;
        const QJsonObject params = QJsonDocument::fromJson(QByteArray::fromStdString(binding.params)).object();
        const guicontrol::Result checked = guicontrol::Host::CheckParams(method, params);
        EXPECT_TRUE(checked.ok) << binding.where << ": " << checked.message.toStdString();
    }
    EXPECT_GE(planeBindings, 9u);  // debug/pad-default 6, debug/shuttle-pro 2, play/pad-default 1
}
