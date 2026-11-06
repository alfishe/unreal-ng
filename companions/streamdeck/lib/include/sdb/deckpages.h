// Where each deck was (the scenario's name and the menu stack's ids, per serial), kept in a small file so a
// restarted daemon opens every deck on the page it showed (requirements.md ACC-6). Only a page of the same scenario
// is given back; a scenario changed underneath keeps the longest prefix of ids that still exists (Navigator::Rebind).
#pragma once
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace sdb
{

class DeckPages
{
public:
    // `file` "" = nothing is kept (tests, --no-lock rigs that ask for it)
    explicit DeckPages(std::string file);

    // The ids the deck showed last with this scenario; empty: none (another scenario, never seen, no file)
    [[nodiscard]] std::vector<std::string> Get(const std::string& serial, const std::string& scenario) const;
    // Stores and writes the file when the page changed (beside it, then moved over it)
    void Set(const std::string& serial, const std::string& scenario, const std::vector<std::string>& ids);

    // ~/.config/unreal-streamdeck/deck-pages.json (beside the companion file)
    [[nodiscard]] static std::string DefaultFile();

private:
    std::string _file;
    mutable std::mutex _mu;
    nlohmann::json _pages = nlohmann::json::object();
};

}  // namespace sdb
