#include "actioncatalogue.h"

#include <cctype>

using nlohmann::json;

EditorCatalogue& EditorCatalogue::Instance()
{
    static EditorCatalogue c;
    return c;
}

void EditorCatalogue::LoadShipped()
{
    _loaded = true;
    const auto file = sdb::cat::DefaultFile();
    if (file.empty())
        return;
    if (auto c = sdb::cat::Load(file))
    {
        _c = std::move(*c);
        _source = QStringLiteral("the shipped file (%1)").arg(QString::fromStdString(file.generic_string()));
    }
}

const sdb::cat::Catalogue& EditorCatalogue::Get()
{
    if (!_loaded)
        LoadShipped();
    return _c;
}

bool EditorCatalogue::Refresh(const std::string& webapi, QString* message)
{
    std::string err;
    auto c = sdb::cat::Fetch(webapi, &err);
    if (!c)
    {
        if (message)
            *message = QStringLiteral("catalogue: %1; keeping %2").arg(QString::fromStdString(err), Source());
        return false;
    }
    _loaded = true;
    _c = std::move(*c);
    _source = QStringLiteral("the emulator at %1").arg(QString::fromStdString(webapi));
    if (message)
        *message = QStringLiteral("catalogue: %1 actions from %2").arg(_c.actions.size()).arg(_source);
    return true;
}

void EditorCatalogue::Set(sdb::cat::Catalogue c, const QString& source)
{
    _loaded = true;
    _c = std::move(c);
    _source = source;
}

const QString& EditorCatalogue::Source()
{
    (void)Get();
    return _source;
}

json CatalogueFace(const sdb::cat::Entry& e)
{
    static const std::pair<const char*, const char*> kIcons[] = {
        {"Run", "play"},       {"Time travel", "rewind"}, {"Debugger", "bug"}, {"Input", "gear"},
        {"Media", "tape"},     {"Audio", "speaker"},      {"Video", "eye"},    {"Settings", "gear"},
        {"BASIC", "chip"},     {"Context", "flag"},       {"Capture and machine", "chip"}};
    std::string icon = "chip";
    for (const auto& [group, name] : kIcons)
        if (e.group == group)
            icon = name;
    std::string label;
    for (char c : e.label)
        label += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (label.size() > 10)
        label.resize(10);
    while (!label.empty() && label.back() == ' ')
        label.pop_back();
    return {{"label", label}, {"icon", icon}, {"bg", "1A1A10"}, {"fg", "FFD200"}};
}

json CatalogueStep(const sdb::cat::Entry& e)
{
    json step{{"kind", "action"}, {"id", e.id}};
    if (e.held && e.params.find("hold?") == std::string::npos)
        step["hold"] = true;
    if (e.kind == "text")
        step["value"] = "";
    return step;
}
