#include "presets.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <set>
#include <string>

#include "actioncatalogue.h"

using nlohmann::json;

namespace
{
json Http(const char* url, const char* method = "GET")
{
    return {{"kind", "http"}, {"method", method}, {"url", url}};
}
json FaceOf(const char* label, const char* icon, const char* bg)
{
    return {{"label", label}, {"icon", icon}, {"bg", bg}};
}
json ActionNode(const char* title, json f, json d)
{
    return {{"type", "action"}, {"title", title}, {"face", std::move(f)}, {"do", std::move(d)}};
}

std::vector<Preset> Build()
{
    std::vector<Preset> v;
    auto add = [&](const char* cat, const char* name, const char* desc, json node) {
        v.push_back({QString::fromUtf8(cat), QString::fromUtf8(name), QString::fromUtf8(desc), std::move(node)});
    };
    add("Menu", "Menu", "A submenu: its own keys, dials and strip",
        {{"type", "menu"}, {"title", "Menu"}, {"face", FaceOf("MENU", "gear", "202030")}, {"items", json::array()}});
    add("Menu", "Menu (back key)", "A submenu with a back key at the bottom-left",
        {{"type", "menu"},
         {"title", "Menu"},
         {"face", FaceOf("MENU", "gear", "202030")},
         {"nav", {{"breadcrumbs", "key"}, {"back", {3, 0}}}},
         {"items", json::array()}});

    add("HTTP", "HTTP request", "Any URL",
        ActionNode("HTTP", FaceOf("HTTP", "chip", "203040"), Http("http://127.0.0.1:8080/")));
    add("Multi / Toggle", "Multi action", "Steps in order with delays (nestable)",
        ActionNode("Multi", FaceOf("MULTI", "forward", "003040"),
                   {{"kind", "multi"},
                    {"steps",
                     {Http("http://127.0.0.1:8080/start", "POST"),
                      {{"kind", "delay"}, {"ms", 300}},
                      Http("http://127.0.0.1:8080/stop", "POST")}}}));
    add("Multi / Toggle", "Toggle", "Alternates two actions",
        {{"type", "action"},
         {"title", "Toggle"},
         {"face", FaceOf("OFF", "check", "202020")},
         {"face_on", FaceOf("ON", "check", "006020")},
         {"do",
          {{"kind", "toggle"},
           {"on", Http("http://127.0.0.1:8080/on", "POST")},
           {"off", Http("http://127.0.0.1:8080/off", "POST")}}}});
    add("Multi / Toggle", "Delay", "Wait (inside multi actions)",
        ActionNode("Delay", FaceOf("WAIT", "pause", "202020"), {{"kind", "delay"}, {"ms", 500}}));

    add("System", "Shell command", "/bin/sh -c (10 s timeout)",
        ActionNode("Shell", FaceOf("SHELL", "chip", "203020"), {{"kind", "shell"}, {"command", "echo hello"}}));
    add("System", "Open URL / file", "With the system handler",
        ActionNode("Open", FaceOf("OPEN", "eye", "202030"),
                   {{"kind", "open"}, {"target", "https://github.com/alfishe/unreal-ng"}}));
    add("System", "Keystroke", "A key combination (macOS: Accessibility permission)",
        ActionNode("Keys", FaceOf("KEYS", "gear", "302020"), {{"kind", "keys"}, {"combo", "cmd+shift+4"}}));
    add("System", "Type text", "Types text",
        ActionNode("Type", FaceOf("TYPE", "gear", "302020"), {{"kind", "keys"}, {"text", "hello"}}));

    add("Navigation", "Back", "One level up",
        ActionNode("Back", FaceOf("BACK", "stepback", "202030"), {{"kind", "nav"}, {"to", "back"}}));
    add("Navigation", "Home", "To the root menu",
        ActionNode("Home", FaceOf("HOME", "flag", "202030"), {{"kind", "nav"}, {"to", "home"}}));
    add("Navigation", "Go to menu", "Opens a menu anywhere (set the node id)",
        ActionNode("Go to", FaceOf("GO", "forward", "202030"), {{"kind", "nav"}, {"to", "node"}, {"node", "home"}}));
    add("Device", "Brighter", "+10 %",
        ActionNode("Brighter", FaceOf("BRIGHT +", "eye", "303000"), {{"kind", "brightness"}, {"delta", 10}}));
    add("Device", "Dimmer", "-10 %",
        ActionNode("Dimmer", FaceOf("BRIGHT -", "eye", "202000"), {{"kind", "brightness"}, {"delta", -10}}));

    // unreal-ng: one per catalogue action (scenarios.md §6), from the shipped catalogue (the inspector's picker can
    // ask the running emulator); the name carries the id, so two actions with one label stay apart
    std::set<std::string> seen;
    for (const auto& e : EditorCatalogue::Instance().Get().actions)
    {
        if (!seen.insert(e.id).second)
            continue;
        json node{{"type", "action"}, {"title", e.label}, {"face", CatalogueFace(e)}, {"do", CatalogueStep(e)}};
        std::string desc = e.id + " (" + e.kind + (e.held ? ", held" : "") + (e.gui ? ", unreal-qt only" : "") + ")";
        if (!e.params.empty())
            desc += "; params: " + e.params;
        add(("unreal-ng: " + (e.group.empty() ? std::string("Other") : e.group)).c_str(),
            (e.label + "  [" + e.id + "]").c_str(), desc.c_str(), std::move(node));
    }

    // the user's own
    auto load = [&](const QString& file) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            return;
        const json j = json::parse(f.readAll().toStdString(), nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return;
        for (const auto& p : j.value("presets", json::array()))
            if (p.is_object() && p.contains("node"))
                add(p.value("category", "User").c_str(), p.value("name", "").c_str(),
                    p.value("description", "").c_str(), p["node"]);
    };
    const QDir user(UserPresetDir());
    for (const QString& f : user.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        load(user.filePath(f));
    return v;
}
}  // namespace

QString UserPresetDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/unreal-ng/streamdeck/presets");
}

const std::vector<Preset>& Presets()
{
    static const std::vector<Preset> v = Build();
    return v;
}

const Preset* FindPreset(const QString& name)
{
    for (const auto& p : Presets())
        if (p.name.compare(name, Qt::CaseInsensitive) == 0)
            return &p;
    return nullptr;
}
