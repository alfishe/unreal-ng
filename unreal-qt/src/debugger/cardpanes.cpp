#include "cardpanes.h"

#include <QObject>

#include <cstring>

#include "debugger/reportpane.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicecatalog.h"
#include "emulator/state/devicestate.h"

namespace CardPanes
{
namespace
{
/// The report of the catalog entry `id` (the one every plane reads by the entry's route and commands)
StateNode Catalogued(EmulatorContext* context, const char* id)
{
    for (const DeviceState::ReportEntry& e : DeviceState::CatalogEntries())
        if (std::strcmp(e.id, id) == 0)
            return e.build(context);
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = std::string("no report ") + id;
    return n;
}

/// What the host's conversation with the card is at: the `gs` report's protocol (the command log's decoder) and the
/// debugger's last send, with the mailbox they act on
StateNode Protocol(EmulatorContext* context)
{
    const StateNode gs = DeviceState::Gs(context);
    if (const StateNode* available = gs.find("available"); !available || available->kind != StateNode::Kind::Bool || !available->b)
        return gs;
    StateNode out = StateNode::Object();
    out["available"] = true;
    for (const char* key : {"protocol", "last_send", "firmware_profile", "status", "command_pending", "data_pending",
                            "command_from_host", "data_from_host", "data_to_host"})
        if (const StateNode* value = gs.find(key))
            out[key] = *value;
    if (!out.find("protocol"))
        out["protocol"] = std::string("off: the command log does not record (history <slot>.commands mode ring)");
    return out;
}

bool Anomaly(const StateNode& row)
{
    const StateNode* kind = row.find("kind");
    return kind && kind->kind == StateNode::Kind::String && kind->s == "anomaly";
}
}  // namespace

std::vector<Pane> Create(QWidget* parent)
{
    std::vector<Pane> panes;

    ReportPane* log = new ReportPane([](EmulatorContext* c) { return Catalogued(c, "gs-commands"); }, "records",
                                     {"frame", "t", "kind", "command", "name", "text"}, parent);
    log->setRowMark(&Anomaly);
    panes.push_back({QObject::tr("GS log"),
                     QObject::tr("The card's command log decoded: the newest commands, data, replies and anomalies (anomalies tinted)"), log});

    ReportPane* protocol = new ReportPane(&Protocol, std::string(), {}, parent);
    panes.push_back({QObject::tr("GS protocol"),
                     QObject::tr("Where the host's conversation with the card is: the decoder's state, the last send, the mailbox"),
                     protocol});

    ReportPane* firmware = new ReportPane([](EmulatorContext* c) { return Catalogued(c, "gs-firmware"); }, "window_variables",
                                          {"name", "at", "size", "value", "meaning"}, parent);
    panes.push_back({QObject::tr("GS firmware"),
                     QObject::tr("The firmware's objects by its profile: instances, variables, the dispatch table, the unknown commands"),
                     firmware});

    ReportPane* hardware = new ReportPane([](EmulatorContext* c) { return DeviceState::Gs(c); }, "channels", {}, parent);
    panes.push_back({QObject::tr("GS hardware"), QObject::tr("The card: ROM, MPAG, the mailbox, the four DAC channels, its CPU"), hardware});

    ReportPane* stats = new ReportPane([](EmulatorContext* c) { return Catalogued(c, "gs-stats"); }, std::string(), {}, parent);
    panes.push_back({QObject::tr("GS stats"), QObject::tr("The card's statistics block: the mailbox, the CPU's load, the channels"), stats});
    return panes;
}

bool Fitted(EmulatorContext* context)
{
    return context && context->pSoundManager && context->pSoundManager->getGeneralSound();
}
}  // namespace CardPanes
