// The action catalogue the editor offers (scenarios.md §6): the shipped one ($data/streamdeck/catalogue.json) from
// the start, the running emulator's (GET /api/v1/controllers/actions) once asked for. Feeds the palette's catalogue
// presets, the inspector's action picker and the editor's "not in the catalogue" warnings.
#pragma once
#include <QString>
#include <string>

#include "sdb/catalogue.h"

class EditorCatalogue
{
public:
    static EditorCatalogue& Instance();
    // The catalogue now (the shipped file on first use; empty when there is none)
    [[nodiscard]] const sdb::cat::Catalogue& Get();
    // From a running emulator; false with `message` (the catalogue stays as it was)
    bool Refresh(const std::string& webapi, QString* message = nullptr);
    void Set(sdb::cat::Catalogue c, const QString& source);  // tests
    [[nodiscard]] const QString& Source();                   // "the emulator at ...", "the shipped file", "none"

private:
    void LoadShipped();
    bool _loaded = false;
    sdb::cat::Catalogue _c;
    QString _source = QStringLiteral("none");
};

// A palette face for a catalogue action: its short label and the icon of its group
[[nodiscard]] nlohmann::json CatalogueFace(const sdb::cat::Entry& e);
// A step for a catalogue action: {kind: action, id} (+ hold for actions whose release matters and that have no
// "hold?" param)
[[nodiscard]] nlohmann::json CatalogueStep(const sdb::cat::Entry& e);
