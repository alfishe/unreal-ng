#include "nextpanes.h"

#include <QObject>

#include <algorithm>

#include "debugger/reportpane.h"
#include "emulator/io/z80n/nextreportquery.h"
#include "emulator/state/devicestate.h"

namespace NextPanes
{
namespace
{
bool BoolMember(const StateNode& row, const char* key)
{
    const StateNode* value = row.find(key);
    return value && value->kind == StateNode::Kind::Bool && value->b;
}

/// The whole copper list up to its last word in use (or the copper's PC, if that is further)
StateNode CopperList(EmulatorContext* context)
{
    NextCopperQuery query;
    query.count = 1024;
    StateNode report = DeviceState::NextCopper(context, query);
    const StateNode* length = report.find("list_length");
    const StateNode* pc = report.find("pc");
    if (StateNode* list = const_cast<StateNode*>(report.find("instructions")); list && length && pc)
    {
        const size_t keep = static_cast<size_t>(std::max<int64_t>(length->i, pc->i + 1));
        if (list->items.size() > keep)
            list->items.resize(keep);
    }
    return report;
}

/// The interrupt controller and the CTC of the `next` report, alone
StateNode CtcAndIm2(EmulatorContext* context)
{
    const StateNode next = DeviceState::Next(context);
    StateNode out = StateNode::Object();
    if (const StateNode* available = next.find("available"); available && available->kind == StateNode::Kind::Bool && !available->b)
        return next;
    out["available"] = true;
    if (const StateNode* interrupts = next.find("interrupts"))
        out["interrupts"] = *interrupts;
    if (const StateNode* ctc = next.find("ctc"))
        out["ctc"] = *ctc;
    return out;
}
}  // namespace

std::vector<Pane> Create(QWidget* parent)
{
    std::vector<Pane> panes;

    ReportPane* regs = new ReportPane(&DeviceState::NextRegs, "registers", {"nr", "value", "reset", "access", "name"}, parent);
    regs->setRowMark([](const StateNode& row) {
        const StateNode* value = row.find("value");
        const StateNode* reset = row.find("reset");
        return value && reset && value->s != reset->s;
    });
    panes.push_back({QObject::tr("NextREG"), QObject::tr("Every NextREG: value and reset value (rows off their reset value tinted)"), regs});

    ReportPane* mmu = new ReportPane(&DeviceState::NextMmu, "slots", {"slot", "address_range", "mmu", "kind", "buffer_offset"}, parent);
    panes.push_back({QObject::tr("MMU"), QObject::tr("The eight 8K slots: MMU page, what they map, the paging latches"), mmu});

    ReportPane* copper = new ReportPane(&CopperList, "instructions", {"index", "word", "text"}, parent);
    copper->setRowMark([](const StateNode& row) { return BoolMember(row, "current"); });
    panes.push_back({QObject::tr("Copper"), QObject::tr("The copper's control, PC and list (the word at its PC tinted)"), copper});

    ReportPane* sprites = new ReportPane(
        [](EmulatorContext* context) {
            NextSpritesQuery query;
            query.all = true;
            return DeviceState::NextSprites(context, query);
        },
        "sprites", {}, parent);
    sprites->setRowMark([](const StateNode& row) { return BoolMember(row, "visible"); });
    panes.push_back({QObject::tr("Sprites"), QObject::tr("The 128 sprites decoded, relative ones resolved (visible ones tinted)"), sprites});

    ReportPane* dma = new ReportPane(&DeviceState::NextDma, std::string(), {}, parent);
    panes.push_back({QObject::tr("DMA"), QObject::tr("The zxnDMA: ports A and B, mode, counter, prescaler, the bus"), dma});

    ReportPane* ctc = new ReportPane(&CtcAndIm2, "ctc", {"channel", "control", "time_constant", "running", "zero_counts"}, parent);
    panes.push_back({QObject::tr("CTC / IM2"), QObject::tr("The hardware IM2 controller (pending, in service, enables) and the four CTC channels"), ctc});
    return panes;
}
}  // namespace NextPanes
