#pragma once

/// @file statetransferchoices.h
/// @brief Machine > Transfer State To (model what-if W0b): the bound machine's running state into another open
/// instance or into a new machine of a chosen model, through StateTransferControl - the verb the WebAPI, MCP, CLI,
/// Lua and Python call, so the menu shows the same report (copied / dropped / refused, mode clone / cross-model).
/// Filled each time the menu opens, so it lists the instances open right now. No dialogs in here: MainWindow asks
/// and shows the report, a test reads the menu.

#include <QString>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "loaders/snapshot/statetransfercontrol.h"

class QMenu;

namespace StateTransferChoices
{
struct Target
{
    std::string id;
    QString label;   ///< "w0b-fork - Pentagon 512K (1a2b3c4d)"
};

struct Model
{
    std::string shortName;
    uint32_t ramKb = 0;
    QString label;   ///< as the Machine menu names it ("Pentagon 128K 512K", "ZX-Spectrum 48k")
};

/// The open instances other than @p sourceId (removed or retiring ones left out), in the manager's order
std::vector<Target> OpenInstances(const std::string& sourceId);

/// The models a new machine can be (Config::IsModelCreatable), one entry per RAM size, in the Machine menu's order
std::vector<Model> Models();

/// Clear @p menu and fill it: one item per other open instance (a disabled note when there is none), then the New
/// Machine submenu of models. @p pick gets the request of the chosen item (sourceId, and `to` or `model` + `ramSizeKB`)
void Fill(QMenu* menu, const std::string& sourceId, const std::function<void(const StateTransferRequest&)>& pick);

/// The report box's text: the verb's own text report (StateTransferReply::ToText)
QString ReportText(const StateTransferReply& reply);
}  // namespace StateTransferChoices
