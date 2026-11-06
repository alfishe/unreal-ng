#include "sdb/deckpages.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace sdb
{

namespace fs = std::filesystem;
using json = nlohmann::json;

DeckPages::DeckPages(std::string file) : _file(std::move(file))
{
    if (_file.empty())
        return;
    std::ifstream in(_file, std::ios::binary);
    if (!in)
        return;
    const json j = json::parse(in, nullptr, false);
    if (j.is_object())
        _pages = j;
}

std::vector<std::string> DeckPages::Get(const std::string& serial, const std::string& scenario) const
{
    std::lock_guard l(_mu);
    const auto it = _pages.find(serial);
    if (it == _pages.end() || !it->is_object() || it->value("scenario", "") != scenario)
        return {};
    std::vector<std::string> ids;
    if (const auto p = it->find("path"); p != it->end() && p->is_array())
        for (const json& id : *p)
            if (id.is_string())
                ids.push_back(id.get<std::string>());
    return ids;
}

void DeckPages::Set(const std::string& serial, const std::string& scenario, const std::vector<std::string>& ids)
{
    std::lock_guard l(_mu);
    const json entry{{"scenario", scenario}, {"path", ids}};
    if (const auto it = _pages.find(serial); it != _pages.end() && *it == entry)
        return;
    _pages[serial] = entry;
    if (_file.empty())
        return;
    std::error_code ec;
    if (const fs::path parent = fs::path(_file).parent_path(); !parent.empty())
        fs::create_directories(parent, ec);
    // written beside it, then moved over it: a daemon killed meanwhile leaves the last whole file
    const std::string temp = _file + ".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out << _pages.dump(2);
    }
    fs::rename(temp, _file, ec);
}

std::string DeckPages::DefaultFile()
{
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/unreal-streamdeck/deck-pages.json";
}

}  // namespace sdb
