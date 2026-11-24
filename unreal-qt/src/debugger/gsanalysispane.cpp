#include "gsanalysispane.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>

#include <algorithm>
#include <cstring>
#include <vector>

#include "debugger/cardpanes.h"
#include "debugger/debugmanager.h"
#include "debugger/gsmonitorpane.h"
#include "debugger/history/historycontrol.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/reportpane.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareobjects.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/gs/gsslotstats.h"
#include "emulator/state/devicestate.h"

namespace
{
// The report panes' change colour (reportpane.cpp); a patched dispatch entry amber, an unknown one greyed
const QColor kChanged(255, 220, 90);
const QColor kPatched(230, 150, 40, 90);
const QColor kUnknownText(128, 128, 128);

const StateNode* Member(const StateNode& node, const char* key)
{
    return node.isObject() ? node.find(key) : nullptr;
}

int64_t Int(const StateNode& node, const char* key, int64_t none = 0)
{
    const StateNode* v = Member(node, key);
    if (v && v->kind == StateNode::Kind::Int)
        return v->i;
    if (v && v->kind == StateNode::Kind::Bool)
        return v->b ? 1 : 0;
    return none;
}

bool Flag(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    return v && v->kind == StateNode::Kind::Bool && v->b;
}

QString Str(const StateNode& node, const char* key)
{
    const StateNode* v = Member(node, key);
    return v && v->kind == StateNode::Kind::String ? QString::fromStdString(v->s) : QString();
}

const std::vector<StateNode>& Items(const StateNode& node, const char* key)
{
    static const std::vector<StateNode> none;
    const StateNode* v = Member(node, key);
    return v && v->isArray() ? v->items : none;
}

QString Hex(int64_t value, int digits)
{
    return QLatin1Char('#') +
           QString::number(static_cast<qulonglong>(value), 16).toUpper().rightJustified(digits, QLatin1Char('0'));
}

/// The `gs` report of the card shown: the GS slot's, or the `multisound` report's `gs` member
StateNode GsReport(EmulatorContext* context, CardPanes::Target target)
{
    if (target != CardPanes::Target::MultiSound)
        return DeviceState::Gs(context);
    const StateNode ms = DeviceState::MultiSound(context);
    const StateNode* gs = ms.find("gs");
    return gs && gs->isObject() ? *gs : ms;
}

DebugCpuId CardCpu(CardPanes::Target target)
{
    return target == CardPanes::Target::MultiSound ? DebugCpuId::MultiSoundGS : DebugCpuId::GS;
}

bool DeclaresDispatch(const GSFirmwareProfile* profile)
{
    return profile && (profile->dispatch.kind != GSDispatchKind::None || profile->comtab);
}

/// The profile stack of the card's code (M5), as the command log saw it: the ROM's profile, then each upload a #13
/// started (an extension stacks, other code replaces what was below); the log's decoding profile is the top. Without
/// the log only the ROM's
std::vector<const GSFirmwareProfile*> ProfileStack(const GeneralSoundCard& card, const StateNode* protocol)
{
    std::vector<const GSFirmwareProfile*> stack;
    if (const GSFirmwareProfile* rom = GSFirmwareProfileOf(card))
        stack.push_back(rom);
    if (!protocol || !Flag(*protocol, "on"))
        return stack;
    for (const StateNode& upload : Items(*protocol, "uploads"))
    {
        const QString id = Str(upload, "profile");
        const GSFirmwareProfile* p = id.isEmpty() ? nullptr : FindGSFirmwareProfileById(id.toStdString());
        if (!p || !Flag(upload, "started"))
            continue;
        if (!p->extends)
            stack.clear();
        if (stack.empty() || stack.back() != p)
            stack.push_back(p);
    }
    const QString topId = Str(*protocol, "profile");
    const GSFirmwareProfile* top = topId.isEmpty() ? nullptr : FindGSFirmwareProfileById(topId.toStdString());
    if (top)
    {
        const auto at = std::find(stack.begin(), stack.end(), top);
        if (at != stack.end())
            stack.erase(at + 1, stack.end());  // a reset or an exit returned below
        else if (top->extends)
            stack.push_back(top);
        else
            stack = {top};
    }
    return stack;
}

QString StackText(const std::vector<const GSFirmwareProfile*>& stack)
{
    QStringList ids;
    for (const GSFirmwareProfile* p : stack)
        ids << QString::fromLatin1(p->id);
    return ids.isEmpty() ? QObject::tr("no profile (an unknown ROM: commands by number)") : ids.join(QStringLiteral(" ▸ "));
}

QString StackTooltip(const std::vector<const GSFirmwareProfile*>& stack)
{
    QStringList lines;
    for (const GSFirmwareProfile* p : stack)
        lines << QStringLiteral("%1: %2%3")
                     .arg(QString::fromLatin1(p->id), QString::fromLatin1(p->title),
                          p->extends ? QObject::tr(" (extends the code below)") : QString());
    return lines.join(QLatin1Char('\n'));
}

/// The source label of `code`'s handler by the stack (the nearest profile naming the command with a handler)
QString ProfileHandler(const std::vector<const GSFirmwareProfile*>& stack, uint8_t code)
{
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    {
        const GSCommandInfo& info = (*it)->Command(code);
        if (info.name && info.handler)
            return QString::fromLatin1(info.handler);
        if (!(*it)->extends)
            break;
    }
    return QString();
}

/// The card-side port's register name (GS: #00-#0B; NeoGS adds the rest, FPGA ports.inc)
QString CardPortName(int port)
{
    static const char* const kNames[0x24] = {
        "MPAG",    "LED",      "DATRG",  "OUTRG",  "ZXSTAT", "RSCOM",  "VOL1",   "VOL2",   "VOL3",
        "VOL4",    "DAMNPORT1", "DAMNPORT2", "INTENA", "INTREQ", "TIM_FREQ", "GSCFG0", "MPAGEX", "SCTRL",
        "SSTAT",   "SD_SEND",  "MD_SEND", "MC_SEND", "VOL5",   "VOL6",   "VOL7",   "VOL8",   nullptr,
        "DMA_MOD", "DMA_HAD",  nullptr,  nullptr,  "DMA_CST", "PG0",    "PG1",    "PG2",    "PG3",
    };
    return port >= 0 && port < 0x24 && kNames[port] ? QString::fromLatin1(kNames[port]) : QString();
}
}  // namespace

GsCardLabels::GsCardLabels(EmulatorContext* context, DebugCpuId cpu)
{
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    _labels = debug ? debug->GetLabelManager(cpu) : nullptr;
    _anyPage = cpu != DebugCpuId::Main;
}

QString GsCardLabels::at(uint16_t address)
{
    if (!_labels)
        return QString();
    if (const std::shared_ptr<Label> label = _labels->GetLabelByZ80Address(address))
        return QString::fromStdString(label->name);
    if (!_anyPage)
        return QString();
    if (!_built)
    {
        _built = true;
        for (const std::shared_ptr<Label>& label : _labels->GetLabelsInRange(0x0000, 0xFFFF))
            if (label && label->active)
            {
                std::string& name = _byAddress[label->address];
                if (name.empty() || label->name < name)
                    name = label->name;  // the same one each refresh
            }
    }
    const auto it = _byAddress.find(address);
    return it != _byAddress.end() ? QString::fromStdString(it->second) : QString();
}

GsAnalysisPane::GsAnalysisPane(View view, std::shared_ptr<DebugCpuId> follow, QWidget* parent)
    : QWidget(parent), _view(view), _follow(std::move(follow))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(3);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _status->setVisible(false);
    layout->addWidget(_status);

    _body = new QWidget(this);
    auto* body = new QVBoxLayout(_body);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(3);
    layout->addWidget(_body, 1);

    auto* top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    _headline = new QLabel(_body);
    _headline->setWordWrap(true);
    _headline->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(_headline, 1);
    if (view == View::Unknown)
    {
        _clear = new QToolButton(_body);
        _clear->setText(tr("Clear"));
        _clear->setToolTip(tr("Count the unknown commands from now on (the command log and its counts stay: this view's "
                              "baseline)"));
        connect(_clear, &QToolButton::clicked, this, &GsAnalysisPane::clearUnknown);
        top->addWidget(_clear, 0, Qt::AlignTop);
    }
    body->addLayout(top);

    if (view == View::Objects)
    {
        auto* pick = new QHBoxLayout();
        pick->setContentsMargins(0, 0, 0, 0);
        _profiles = new QComboBox(_body);
        _profiles->setToolTip(tr("The profile whose layout is read on the card: the active one (the command log's top, "
                                 "else the ROM's), or any known profile"));
        _profiles->addItem(tr("Active profile"), QString());
        for (const GSFirmwareProfile* p : GSFirmwareProfiles())
        {
            const QString id = QString::fromLatin1(p->id);
            _profiles->addItem(id, id);
            _profiles->setItemData(_profiles->count() - 1, QString::fromLatin1(p->title), Qt::ToolTipRole);
        }
        connect(_profiles, &QComboBox::activated, this, [this](int index) {
            _profile = _profiles->itemData(index).toString().toStdString();
        });
        pick->addWidget(_profiles, 1);
        _objects = new QComboBox(_body);
        _objects->setToolTip(tr("The object shown: a struct instance (a row each), the profile's variables, the fixed RAM "
                                "window's variables"));
        connect(_objects, &QComboBox::activated, this, [this](int index) {
            _object = _objects->itemData(index).toString().toStdString();
        });
        pick->addWidget(_objects, 1);
        body->addLayout(pick);
    }

    if (view == View::Unknown || view == View::Writers)
    {
        _offBar = new QWidget(_body);
        auto* off = new QHBoxLayout(_offBar);
        off->setContentsMargins(0, 0, 0, 0);
        _offText = new QLabel(_offBar);
        _offText->setWordWrap(true);
        off->addWidget(_offText, 1);
        _turnOn = new QPushButton(tr("Turn on"), _offBar);
        _turnOn->setToolTip(view == View::Writers
                                ? tr("Record the card's command log (the host's writers) and its port trace (the card's): "
                                     "history mode ring (costs the emulation a little on each access)")
                                : tr("Record the card's command log: history <slot>.commands mode ring"));
        connect(_turnOn, &QPushButton::clicked, this, &GsAnalysisPane::turnOnRequested);
        off->addWidget(_turnOn, 0, Qt::AlignTop);
        _offBar->setVisible(false);
        body->addWidget(_offBar);
    }

    _table = new QTableWidget(0, 0, _body);
    _table->verticalHeader()->setVisible(false);
    _table->verticalHeader()->setDefaultSectionSize(_table->fontMetrics().height() + 4);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setWordWrap(false);
    body->addWidget(_table, 1);
}

void GsAnalysisPane::reset()
{
    _lastStop.clear();
    _marked.clear();
    _unknown.clear();
    _unknownAtClear.clear();
}

void GsAnalysisPane::refresh(EmulatorContext* context, bool live)
{
    const DebugCpuId front = _follow ? *_follow : DebugCpuId::Main;
    const CardPanes::Target target = CardPanes::Front(context, front);
    if (target == CardPanes::Target::None)
    {
        showUnavailable(tr("No General Sound card fitted (plug one: slots plug zxbus.next gs | gs-lw | neogs | multisound)"));
        return;
    }
    const std::string card = target == CardPanes::Target::MultiSound ? "ms" : "gs";
    if (card != _card)
    {
        _card = card;  // another card: its own counts and marks
        reset();
    }
    _now.clear();
    _marked.clear();
    switch (_view)
    {
        case View::Dispatch: refreshDispatch(context, static_cast<int>(target)); break;
        case View::Unknown: refreshUnknown(context, static_cast<int>(target)); break;
        case View::Objects: refreshObjects(context, static_cast<int>(target)); break;
        case View::Writers: refreshWriters(context, static_cast<int>(target)); break;
    }
    finishMarks(live);
}

void GsAnalysisPane::refreshDispatch(EmulatorContext* context, int targetValue)
{
    const auto target = static_cast<CardPanes::Target>(targetValue);
    const std::string cpu = target == CardPanes::Target::MultiSound ? "ms" : "";
    GeneralSoundCard* card = GSCardForCpu(context, cpu);
    if (!card)
    {
        showUnavailable(tr("No General Sound card fitted"));
        return;
    }
    if (!card->hasCoprocessor())
    {
        showUnavailable(tr("The lightweight player runs no firmware code: it has no dispatch table (its interpreter takes "
                           "the commands the gs105a profile names; GS interpreter shows it)"));
        return;
    }
    const StateNode gs = GsReport(context, target);
    const std::vector<const GSFirmwareProfile*> stack = ProfileStack(*card, Member(gs, "protocol"));
    // The table the code on the card dispatches through: the nearest profile on the stack that declares one
    const GSFirmwareProfile* dispatcher = nullptr;
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    {
        if (DeclaresDispatch(*it))
        {
            dispatcher = *it;
            break;
        }
        if (!(*it)->extends)
            break;
    }
    if (!dispatcher)
    {
        showUnavailable(stack.empty() ? tr("The card's firmware has no profile: no declared dispatch table")
                                      : tr("Profiles %1: none declares the command dispatch").arg(StackText(stack)));
        return;
    }
    GSFirmwareObjectsQuery query;
    query.profile = dispatcher->id;
    query.object = "dispatch";
    query.cpu = cpu;
    const GSFirmwareObjectsReply reply = GSFirmwareObjectsRead(context, query);
    const StateNode* dispatch = Member(reply.body, "dispatch");
    if (reply.status != 200 || !dispatch)
    {
        showUnavailable(reply.status != 200 ? Str(reply.body, "error") : tr("Profile %1 declares no command dispatch").arg(dispatcher->id));
        return;
    }
    const std::vector<StateNode>& uploads = [&]() -> const std::vector<StateNode>& {
        static const std::vector<StateNode> none;
        const StateNode* protocol = Member(gs, "protocol");
        return protocol ? Items(*protocol, "uploads") : none;
    }();

    QString table = Str(*dispatch, "table");
    if (const QString high = Str(*dispatch, "table_high"); !high.isEmpty())
        table += QStringLiteral(" / ") + high;
    // The stock split's COMTABH (#20-#EF) is read through window 3 as mapped now; the firmware maps its table page
    // there while it dispatches, so another page in window 3 reads no table: most entries then differ from the ROM's
    // (a patch changes a few). Those entries show the ROM's table (its handler, its label), not "patched"
    // The ROM baseline: the core reads it from the card's ROM pages 0-1. When most COMTAB words (window 0) differ from
    // it, those pages do not hold this table (a NeoGS keeps its firmware in flash pages 4-5 and runs a RAM copy): a
    // patch changes a few entries, not most. Then nothing is "patched"
    int words = 0, wordsDiffer = 0;
    for (const StateNode& e : Items(*dispatch, "entries"))
        if (const int64_t code = Int(e, "command"); (code < 0x20 || code >= 0xF0) && Member(e, "baseline"))
        {
            words++;
            wordsDiffer += Flag(e, "patched") ? 1 : 0;
        }
    const bool noBaseline = words && wordsDiffer * 2 > words;
    QString highStale;
    if (!noBaseline && dispatcher->comtabh && dispatcher->dispatch.kind != GSDispatchKind::Words)
    {
        int high = 0, differ = 0;
        for (const StateNode& e : Items(*dispatch, "entries"))
            if (const int64_t code = Int(e, "command"); code >= 0x20 && code < 0xF0 && Member(e, "baseline"))
            {
                high++;
                differ += Flag(e, "patched") ? 1 : 0;
            }
        if (high && differ * 2 > high)
        {
            const GeneralSoundCard::CardPage where = card->mapCardAddress(dispatcher->comtabh);
            highStale = tr("window 3 maps %1 page %2 now, not the table's: #20-#EF show the ROM's table")
                            .arg(where.kind == GeneralSoundCard::CardPage::Ram   ? tr("RAM")
                                 : where.kind == GeneralSoundCard::CardPage::Rom ? tr("ROM")
                                                                                 : tr("no"))
                            .arg(where.page);
        }
    }
    int64_t patchedCount = 0;
    for (const StateNode& e : Items(*dispatch, "entries"))
    {
        const int64_t code = Int(e, "command");
        if (Flag(e, "patched") && !noBaseline && (highStale.isEmpty() || code < 0x20 || code >= 0xF0))
            patchedCount++;
    }
    const QString stackLine = tr("Profiles: %1").arg(StackText(stack));
    showBody(stackLine + QLatin1Char('\n') +
             tr("table %1 (%2) · %3 patched · %4 unknown")
                 .arg(table, QString::fromLatin1(dispatcher->id))
                 .arg(patchedCount)
                 .arg(Int(*dispatch, "unknown")) +
             (highStale.isEmpty() ? QString() : QStringLiteral(" · ") + highStale) +
             (noBaseline ? tr(" · no ROM baseline (most entries differ from the card's ROM pages 0-1: the firmware runs from a copy)")
                         : QString()));
    _headline->setToolTip(StackTooltip(stack));
    _scope = QStringLiteral("dispatch:") + QString::fromLatin1(dispatcher->id);

    setColumns({tr("Cmd"), tr("Name"), tr("Handler"), tr("Label"), tr("ROM"), tr("Patched by")});
    const std::vector<StateNode>& entries = Items(*dispatch, "entries");
    setRowCount(static_cast<int>(entries.size()));
    GsCardLabels cardLabels(context, CardCpu(target));
    for (size_t i = 0; i < entries.size(); i++)
    {
        const StateNode& e = entries[i];
        const int row = static_cast<int>(i);
        const auto code = static_cast<uint8_t>(Int(e, "command"));
        setCell(row, 0, Hex(code, 2));
        setCell(row, 1, Str(e, "name"));
        if (!Flag(e, "readable") && Member(e, "readable"))
        {
            setCell(row, 2, tr("unreadable"));
            continue;
        }
        const bool stale = !highStale.isEmpty() && code >= 0x20 && code < 0xF0;
        const QString handlerText = stale ? Str(e, "baseline") : Str(e, "handler");
        setCell(row, 2, handlerText, stale ? tr("the ROM's entry (%1); read through the window now: %2").arg(highStale, Str(e, "handler"))
                                           : QString());
        const auto handler = static_cast<uint16_t>(handlerText.mid(1).toUInt(nullptr, 16));
        // The handler's label: the profile's source label (the patching code's when patched), else the card's labels
        QString label;
        const QString by = Str(e, "patched_by");
        if (const GSFirmwareProfile* patcher = by.isEmpty() ? nullptr : FindGSFirmwareProfileById(by.toStdString()))
            if (const char* h = patcher->Command(code).handler)
                label = QString::fromLatin1(h);
        if (label.isEmpty())
            label = ProfileHandler(stack, code);
        if (label.isEmpty())
            label = cardLabels.at(handler);
        setCell(row, 3, label);
        setCell(row, 4, noBaseline ? QString() : Str(e, "baseline"));
        if (Flag(e, "patched") && !stale && !noBaseline)
        {
            QString patched = by.isEmpty() ? tr("code no profile names") : by;
            QString tooltip;
            // The upload of that profile (the newest): where it landed, its size, its frame
            for (auto it = uploads.rbegin(); !by.isEmpty() && it != uploads.rend(); ++it)
                if (Str(*it, "profile") == by)
                {
                    patched += tr(" · upload %1, %2 bytes, frame %3")
                                   .arg(Hex(Int(*it, "address"), 4))
                                   .arg(Int(*it, "length"))
                                   .arg(Int(*it, "frame"));
                    tooltip = tr("sha256 %1").arg(Str(*it, "sha256"));
                    break;
                }
            setCell(row, 5, patched, tooltip);
            tintRow(row, kPatched);
        }
        else
            setCell(row, 5, QString());
        if (Flag(e, "unknown"))
            for (int column = 0; column < _table->columnCount(); column++)
                if (QTableWidgetItem* item = _table->item(row, column))
                    item->setForeground(kUnknownText);
    }
}

void GsAnalysisPane::refreshUnknown(EmulatorContext* context, int targetValue)
{
    const auto target = static_cast<CardPanes::Target>(targetValue);
    const std::string cpu = target == CardPanes::Target::MultiSound ? "ms" : "";
    GSFirmwareObjectsQuery query;
    query.object = "unknown_commands";
    query.cpu = cpu;
    const GSFirmwareObjectsReply reply = GSFirmwareObjectsRead(context, query);
    if (reply.status != 200)
    {
        showUnavailable(Str(reply.body, "error") + tr(": with no profile every command is by number, none is unknown"));
        return;
    }
    const StateNode* list = Member(reply.body, "unknown_commands");
    if (!list)
    {
        showUnavailable(tr("This card keeps no command log: unknown commands are not counted"));
        return;
    }
    const StateNode gs = GsReport(context, target);
    const StateNode* protocol = Member(gs, "protocol");
    const bool on = protocol && Flag(*protocol, "on");
    _offBar->setVisible(!on);
    _turnOn->setVisible(!on);
    if (!on)
        _offText->setText(tr("The command log is off: the commands the firmware does not know are counted while it records"));

    // The log's counts; fewer than at Clear: a new recording started, the baseline goes
    std::map<int, int64_t> counts;
    for (const StateNode& entry : list->items)
        counts[static_cast<int>(Int(entry, "command"))] = Int(entry, "count");
    for (const auto& [code, base] : _unknownAtClear)
        if (counts[code] < base)
        {
            _unknownAtClear.clear();
            break;
        }
    _unknown = counts;

    // The handler each takes on the card: the dispatch entry (the unknown-command stub, or a handler no command owns)
    std::map<int, QString> handlers;
    GSFirmwareObjectsQuery dispatchQuery;
    dispatchQuery.object = "dispatch";
    dispatchQuery.cpu = cpu;
    const GSFirmwareObjectsReply dispatch = GSFirmwareObjectsRead(context, dispatchQuery);
    if (const StateNode* d = Member(dispatch.body, "dispatch"))
        for (const StateNode& e : Items(*d, "entries"))
            handlers[static_cast<int>(Int(e, "command"))] = Str(e, "handler");

    int64_t total = 0;
    std::vector<std::pair<int, int64_t>> since;
    for (const auto& [code, count] : _unknown)
    {
        const auto base = _unknownAtClear.find(code);
        const int64_t n = count - (base == _unknownAtClear.end() ? 0 : base->second);
        if (n > 0)
        {
            since.emplace_back(code, n);
            total += n;
        }
    }
    GeneralSoundCard* card = GSCardForCpu(context, cpu);
    const bool lightweight = card && !card->hasCoprocessor();
    QString headline = tr("%n unknown commands since Clear", nullptr, static_cast<int>(total)) + tr(" · profile %1")
                                                                                                    .arg(Str(reply.body, "profile"));
    if (lightweight)
        headline += tr(" · the lightweight player answers as gs105a: the list is what that profile does not name (the "
                       "commands LW itself lacks are not tracked)");
    showBody(headline);
    _scope = QStringLiteral("unknown");
    setColumns({tr("Cmd"), tr("Since Clear"), tr("Total"), tr("Handler"), tr("Label")});
    setRowCount(static_cast<int>(since.size()));
    GsCardLabels cardLabels(context, CardCpu(target));
    for (size_t i = 0; i < since.size(); i++)
    {
        const int row = static_cast<int>(i);
        const int code = since[i].first;
        setCell(row, 0, Hex(code, 2));
        setCell(row, 1, QString::number(since[i].second));
        setCell(row, 2, QString::number(_unknown[code]));
        const QString handler = handlers.count(code) ? handlers[code] : QString();
        setCell(row, 3, handler);
        setCell(row, 4, handler.isEmpty() ? QString()
                                          : cardLabels.at(static_cast<uint16_t>(handler.mid(1).toUInt(nullptr, 16))));
    }
}

void GsAnalysisPane::clearUnknown()
{
    _unknownAtClear = _unknown;
    _table->setRowCount(0);
    if (_view == View::Unknown)
        _headline->setText(tr("0 unknown commands since Clear"));
}

void GsAnalysisPane::refreshObjects(EmulatorContext* context, int targetValue)
{
    const auto target = static_cast<CardPanes::Target>(targetValue);
    GSFirmwareObjectsQuery query;
    query.profile = _profile;
    query.cpu = target == CardPanes::Target::MultiSound ? "ms" : "";
    const GSFirmwareObjectsReply reply = GSFirmwareObjectsRead(context, query);
    if (reply.status != 200)
    {
        showUnavailable(Str(reply.body, "error"));
        return;
    }
    const StateNode& body = reply.body;
    const std::vector<StateNode>& instances = Items(body, "instances");
    const std::vector<StateNode>& variables = Items(body, "variables");
    const std::vector<StateNode>& window = Items(body, "window_variables");

    // The object picker: the instances, then the variables; kept by name across refreshes
    QStringList keys;
    QStringList labels;
    for (const StateNode& instance : instances)
    {
        keys << Str(instance, "name");
        labels << tr("%1 [%2] (struct %3)").arg(Str(instance, "name")).arg(Int(instance, "count")).arg(Str(instance, "struct"));
    }
    if (!variables.empty())
    {
        keys << QStringLiteral(":variables");
        labels << tr("Variables (%1)").arg(static_cast<int>(variables.size()));
    }
    if (!window.empty())
    {
        keys << QStringLiteral(":window");
        labels << tr("Window variables (%1)").arg(static_cast<int>(window.size()));
    }
    QStringList shown;
    for (int i = 0; i < _objects->count(); i++)
        shown << _objects->itemData(i).toString();
    if (shown != keys)
    {
        const QSignalBlocker block(_objects);
        _objects->clear();
        for (int i = 0; i < keys.size(); i++)
            _objects->addItem(labels[i], keys[i]);
    }
    if (!keys.contains(QString::fromStdString(_object)))
        _object = keys.isEmpty() ? std::string() : keys.front().toStdString();
    {
        const QSignalBlocker block(_objects);
        _objects->setCurrentIndex(static_cast<int>(keys.indexOf(QString::fromStdString(_object))));
    }
    {
        const QSignalBlocker block(_profiles);
        const int index = _profiles->findData(QString::fromStdString(_profile));
        _profiles->setCurrentIndex(index < 0 ? 0 : index);
    }

    const QString id = Str(body, "profile");
    const GSFirmwareProfile* profile = FindGSFirmwareProfileById(id.toStdString());
    QString headline = tr("%1 — %2 · cpu %3 · %4 instances, %5 variables, %6 window variables")
                           .arg(id, Str(body, "title"), Str(body, "cpu"))
                           .arg(static_cast<int>(instances.size()))
                           .arg(static_cast<int>(variables.size()))
                           .arg(static_cast<int>(window.size()));
    GeneralSoundCard* card = GSCardForCpu(context, query.cpu);
    if (card && !card->hasCoprocessor())
        headline += tr("\nThe lightweight player has no card RAM: the window variables it keeps are its interpreter's "
                       "mirror (source mirror); structs in RAM read nothing");
    showBody(headline);
    _scope = QStringLiteral("objects:") + id + QLatin1Char(':') + QString::fromStdString(_object);

    const QString object = QString::fromStdString(_object);
    if (object == QLatin1String(":variables"))
    {
        setColumns({tr("Name"), tr("Kind"), tr("At"), tr("Value"), tr("Meaning")});
        setRowCount(static_cast<int>(variables.size()));
        for (size_t i = 0; i < variables.size(); i++)
        {
            const StateNode& v = variables[i];
            const int row = static_cast<int>(i);
            setCell(row, 0, Str(v, "name"));
            setCell(row, 1, Str(v, "kind"));
            QString at = Str(v, "at");
            if (at.isEmpty())
                at = Str(v, "register") + QStringLiteral(" @ PC ") + Str(v, "pc_range");
            setCell(row, 2, at);
            const StateNode* value = Member(v, "value");
            setCell(row, 3, value ? Hex(value->i, 2) + QStringLiteral(" (") + QString::number(value->i) + QLatin1Char(')')
                                  : tr("not valid"),
                    Str(v, "reason"));
            setCell(row, 4, Str(v, "meaning"));
        }
        return;
    }
    if (object == QLatin1String(":window"))
    {
        setColumns({tr("Name"), tr("At"), tr("Size"), tr("Value"), tr("Meaning")});
        setRowCount(static_cast<int>(window.size()));
        for (size_t i = 0; i < window.size(); i++)
        {
            const StateNode& v = window[i];
            const int row = static_cast<int>(i);
            setCell(row, 0, Str(v, "name"));
            setCell(row, 1, Str(v, "at"));
            setCell(row, 2, QString::number(Int(v, "size")));
            const StateNode* value = Member(v, "value");
            QString text = value ? QString::number(value->i) : QString();
            if (value && Str(v, "source") == QLatin1String("mirror"))
                text += tr(" (mirror)");
            setCell(row, 3, text);
            setCell(row, 4, Str(v, "meaning"));
        }
        return;
    }
    for (const StateNode& instance : instances)
    {
        if (Str(instance, "name") != object)
            continue;
        const std::vector<StateNode>& rows = Items(instance, "rows");
        // The columns: the struct's fields in the schema's order (the first row's), their meanings as tooltips
        QStringList columns{tr("#"), tr("At")};
        std::vector<std::string> fields;
        if (!rows.empty())
            if (const StateNode* first = Member(rows.front(), "fields"); first && first->isObject())
                for (const auto& [name, value] : first->members)
                {
                    columns << QString::fromStdString(name);
                    fields.push_back(name);
                }
        setColumns(columns);
        if (const GSStructInfo* info = profile ? profile->Struct(Str(instance, "struct").toStdString()) : nullptr)
            for (size_t f = 0; f < fields.size(); f++)
                for (const GSStructField& field : info->fields)
                    if (field.name == fields[f] && !field.meaning.empty())
                        if (QTableWidgetItem* header = _table->horizontalHeaderItem(static_cast<int>(f) + 2))
                            header->setToolTip(QString::fromStdString(field.meaning));
        setRowCount(static_cast<int>(rows.size()));
        for (size_t r = 0; r < rows.size(); r++)
        {
            const int row = static_cast<int>(r);
            setCell(row, 0, QString::number(Int(rows[r], "index")));
            setCell(row, 1, Str(rows[r], "at"));
            const StateNode* values = Member(rows[r], "fields");
            for (size_t f = 0; f < fields.size(); f++)
            {
                const StateNode* value = values ? values->find(fields[f]) : nullptr;
                setCell(row, static_cast<int>(f) + 2, value ? ReportPane::ValueText(*value) : QString());
            }
        }
        return;
    }
    setColumns({});
    setRowCount(0);
}

void GsAnalysisPane::refreshWriters(EmulatorContext* context, int targetValue)
{
    const auto target = static_cast<CardPanes::Target>(targetValue);
    const std::string cpu = target == CardPanes::Target::MultiSound ? "ms" : "";
    GeneralSoundCard* card = GSCardForCpu(context, cpu);
    if (!card)
    {
        showUnavailable(tr("No General Sound card fitted"));
        return;
    }
    struct Writer
    {
        bool seen = false;
        int value = 0;
        int pc = -1;   ///< -1: not recorded
        int64_t frame = 0;
        int64_t writes = 0;
        QString source;
        QString note;
    };
    // The card's OUTs from its port trace (the card CPU's PC with each); the host's #33 from the trace's host side
    std::map<int, Writer> cardPorts;
    Writer control;
    const bool tracing = card->isPortTraceCapturing();
    if (tracing)
    {
        size_t matched = 0;
        uint64_t lastSeq = 0, evicted = 0;
        if (card->hasCoprocessor())
            for (const auto& [seq, e] : card->getPortTraceSide(GSTraceSide::GsInternal, 0, kTraceRows, matched, lastSeq, evicted))
                if (e.isOut())
                {
                    Writer& w = cardPorts[e.port];
                    w = {true, e.value, e.pc, int64_t(e.frameNumber), w.writes + 1, tr("port trace"), QString()};
                }
        for (const auto& [seq, e] : card->getPortTraceSide(GSTraceSide::Host, 0, kTraceRows, matched, lastSeq, evicted))
            if (e.isOut() && (e.port & 0xFF) == 0x33)
                control = {true, e.value, -1, int64_t(e.frameNumber), control.writes + 1, tr("port trace"),
                           tr("the trace keeps the card's PC, not the host's")};
    }
    // The host's #B3 / #BB writes from the command log (the main CPU's PC with each); a card reset marks #33 bit 7
    Writer data, command;
    const DebugCpuId front = _follow ? *_follow : DebugCpuId::Main;
    const std::string device = GsMonitorPane::HistoryDevice(context, front);
    HistoryRequest read;
    read.verb = "read";
    read.device = device;
    read.last = kLogRows;
    const HistoryReply log = HistoryControl::Execute(context, read);
    const bool logging = log.httpStatus == 200 && Str(log.body, "mode") != QLatin1String("off");
    if (logging)
        for (const StateNode& r : Items(log.body, "records"))
        {
            const QString kind = Str(r, "kind");
            const QString source = Flag(r, "automation") ? tr("command log (automation)") : tr("command log");
            if (kind == QLatin1String("command"))
            {
                const QString name = Str(r, "name");
                command = {true, static_cast<int>(Int(r, "command")), static_cast<int>(Int(r, "pc")), Int(r, "frame"),
                           command.writes + 1, source, name};
            }
            else if (kind == QLatin1String("param") || kind == QLatin1String("data"))
                data = {true, static_cast<int>(Int(r, "value")), static_cast<int>(Int(r, "pc")), Int(r, "frame"),
                        data.writes + 1, source, Str(r, "param")};
            else if (kind == QLatin1String("block"))
                data.note = tr("then %1 bytes of a block (frame %2)").arg(Int(r, "bytes")).arg(Int(r, "frame"));
            else if (kind == QLatin1String("reset") && (!control.seen || control.frame <= Int(r, "frame")))
            {
                // The reset's host PC; the value from the trace when it saw the same write
                const bool same = control.seen && control.frame == Int(r, "frame");
                control = {true, same ? control.value : 0x80, static_cast<int>(Int(r, "pc")), Int(r, "frame"),
                           std::max<int64_t>(control.writes, 1), source,
                           same ? QString() : tr("bit 7 (reset) seen by the command log; other bits not recorded")};
            }
        }

    QStringList off;
    if (!logging)
        off << tr("the command log is off (the host's #B3 / #BB writers)");
    if (!tracing)
        off << tr("the port trace is off (the card ports and #33)");
    _offBar->setVisible(!off.isEmpty());
    _turnOn->setVisible(!off.isEmpty() && log.httpStatus == 200);
    if (!off.isEmpty())
        _offText->setText(off.join(QStringLiteral("; ")));
    QString headline = tr("%1 card ports written · host #B3 / #BB / #33").arg(static_cast<int>(cardPorts.size()));
    if (!card->hasCoprocessor())
        headline += tr(" · the lightweight player has no card CPU: no card port writes");
    showBody(headline);
    _scope = QStringLiteral("writers");

    setColumns({tr("Port"), tr("Register"), tr("Value"), tr("PC"), tr("Label"), tr("Frame"), tr("Writes"), tr("Source")});
    setRowCount(static_cast<int>(cardPorts.size()) + 3);
    GsCardLabels cardLabels(context, CardCpu(target));
    GsCardLabels hostLabels(context, DebugCpuId::Main);
    int row = 0;
    const auto put = [&](const QString& port, const QString& reg, const Writer& w, bool host) {
        setCell(row, 0, port);
        setCell(row, 1, reg);
        if (!w.seen)
        {
            for (int column = 2; column < 8; column++)
                setCell(row, column, QString());
            row++;
            return;
        }
        setCell(row, 2, Hex(w.value, 2), w.note);
        setCell(row, 3, w.pc >= 0 ? Hex(w.pc, 4) : QString(), w.pc < 0 ? w.note : QString());
        setCell(row, 4, w.pc >= 0 ? (host ? hostLabels : cardLabels).at(static_cast<uint16_t>(w.pc)) : QString());
        setCell(row, 5, QString::number(w.frame));
        setCell(row, 6, QString::number(w.writes));
        setCell(row, 7, w.note.isEmpty() ? w.source : w.source + QStringLiteral(": ") + w.note);
        row++;
    };
    for (const auto& [port, w] : cardPorts)
        put(Hex(port, 2), CardPortName(port), w, false);
    put(tr("host #B3"), tr("data"), data, true);
    put(tr("host #BB"), tr("command"), command, true);
    put(tr("host #33"), tr("control"), control, true);
}

bool GsAnalysisPane::turnOn(EmulatorContext* context, QString* error)
{
    const DebugCpuId front = _follow ? *_follow : DebugCpuId::Main;
    const std::string commands = GsMonitorPane::HistoryDevice(context, front);
    if (commands.empty())
    {
        if (error)
            *error = tr("No General Sound card fitted");
        return false;
    }
    std::vector<std::string> devices{commands};
    if (_view == View::Writers)
    {
        // The port trace's history: the command log's id without `.commands` ("zxbus.1", "zxbus.2.gs")
        const std::string suffix = ".commands";
        devices.push_back(commands.substr(0, commands.size() - suffix.size()));
    }
    for (const std::string& device : devices)
    {
        HistoryRequest ring;
        ring.verb = "config";
        ring.device = device;
        ring.mode = "ring";
        const HistoryReply reply = HistoryControl::Execute(context, ring);
        if (reply.httpStatus != 200)
        {
            if (error)
                *error = Str(reply.body, "error");
            return false;
        }
    }
    return true;
}

void GsAnalysisPane::selectProfile(const QString& id)
{
    _profile = id.toStdString();
}

void GsAnalysisPane::selectObject(const QString& name)
{
    _object = name.toStdString();
}

void GsAnalysisPane::showUnavailable(const QString& why)
{
    _status->setText(why);
    _status->setVisible(true);
    _body->setVisible(false);
    _table->setRowCount(0);
}

void GsAnalysisPane::showBody(const QString& headline)
{
    _status->clear();
    _status->setVisible(false);
    _body->setVisible(true);
    _headline->setText(headline);
}

void GsAnalysisPane::setColumns(const QStringList& columns)
{
    QStringList now;
    for (int i = 0; i < _table->columnCount(); i++)
        now << (_table->horizontalHeaderItem(i) ? _table->horizontalHeaderItem(i)->text() : QString());
    if (now == columns)
        return;
    _table->setRowCount(0);
    _table->setColumnCount(static_cast<int>(columns.size()));
    _table->setHorizontalHeaderLabels(columns);
    for (int i = 0; i + 1 < static_cast<int>(columns.size()); i++)
        _table->setColumnWidth(i, i == 0 ? 56 : 80);
}

void GsAnalysisPane::setRowCount(int rows)
{
    _table->setRowCount(rows);
}

void GsAnalysisPane::setCell(int row, int column, const QString& text, const QString& tooltip)
{
    QTableWidgetItem* item = _table->item(row, column);
    if (!item)
    {
        item = new QTableWidgetItem();
        _table->setItem(row, column, item);
    }
    item->setText(text);
    item->setToolTip(tooltip);
    item->setData(Qt::BackgroundRole, QVariant());
    item->setData(Qt::ForegroundRole, QVariant());
    QFont font = item->font();
    font.setBold(false);
    item->setFont(font);
    // The cell's identity for the change marks: the row's first cell (the command, the index, the port) and the column
    const QTableWidgetItem* first = _table->item(row, 0);
    const QString key = _scope + QLatin1Char('|') + (first ? first->text() : QString::number(row)) + QLatin1Char('|') +
                        QString::number(column);
    _now[key] = text;
    if (const auto last = _lastStop.find(key); last != _lastStop.end() && last->second != text)
        _marked[{row, column}] = true;
}

void GsAnalysisPane::tintRow(int row, const QColor& color)
{
    for (int column = 0; column < _table->columnCount(); column++)
        if (QTableWidgetItem* item = _table->item(row, column))
            item->setBackground(color);
}

void GsAnalysisPane::finishMarks(bool live)
{
    // A stop marks what differs from the last stop; a live refresh shows values without marks, the last stop's stay
    // the reference (rules.md section 4, as the report panes)
    if (live)
    {
        _marked.clear();
        return;
    }
    for (const auto& [at, on] : _marked)
        if (QTableWidgetItem* item = _table->item(at.first, at.second))
        {
            item->setBackground(kChanged);
            item->setForeground(QColor(Qt::black));
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
    for (const auto& [key, text] : _now)
        _lastStop[key] = text;
}

QString GsAnalysisPane::status() const
{
    return _status->text();
}

QString GsAnalysisPane::headline() const
{
    return _headline->text();
}

QString GsAnalysisPane::cell(int row, const QString& column) const
{
    for (int c = 0; c < _table->columnCount(); c++)
        if (_table->horizontalHeaderItem(c) && _table->horizontalHeaderItem(c)->text() == column)
        {
            const QTableWidgetItem* item = _table->item(row, c);
            return item ? item->text() : QString();
        }
    return QString();
}

int GsAnalysisPane::findRow(const QString& column, const QString& text) const
{
    for (int row = 0; row < _table->rowCount(); row++)
        if (cell(row, column) == text)
            return row;
    return -1;
}

bool GsAnalysisPane::isMarked(int row, int column) const
{
    return _marked.count({row, column}) != 0;
}
