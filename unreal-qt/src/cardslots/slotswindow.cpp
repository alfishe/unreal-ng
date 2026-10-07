/**
 * @file slotswindow.cpp
 * @brief The Slots window: DeviceState::Slots, SlotControl::Catalog, SlotManager::PlanChange, SlotChangeController.
 */

#include "slotswindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <map>
#include <sstream>

#include "cardslots/slotchangecontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/slots/slotplanner.h"
#include "emulator/state/devicestate.h"
#include "emulator/state/statenodejson.h"

// Qt defines `slots` as a macro; the code below names the slots namespace
#undef slots

namespace
{
    QString Q(const std::string& text)
    {
        return QString::fromStdString(text);
    }

    std::string Text(const StateNode* node)
    {
        return node != nullptr && node->kind == StateNode::Kind::String ? node->s : std::string();
    }

    /// "dip=ym,saa gsRam=1m" -> {dip: [ym, saa], gsRam: [1m]}
    std::map<std::string, std::vector<std::string>> ParseOptions(const std::string& text)
    {
        std::map<std::string, std::vector<std::string>> out;
        std::istringstream words(text);
        for (std::string word; words >> word;)
        {
            const size_t equals = word.find('=');
            if (equals == std::string::npos)
                continue;
            std::vector<std::string>& values = out[word.substr(0, equals)];
            std::istringstream list(word.substr(equals + 1));
            for (std::string value; std::getline(list, value, ',');)
            {
                if (!value.empty() && value != "none")
                    values.push_back(value);
            }
        }
        return out;
    }

    /// Notes for people next to a card's options
    QString CardNote(const std::string& card)
    {
        if (card == "multisound")
            return QObject::tr("DIP: which parts of the card answer (YM = TurboSound FM + MIDI, SAA, GS, SD = SounDrive). "
                               "ctrlMask classic: an unofficial firmware patch (issue #11), pro: the official firmware.");
        if (card == "gs-lw")
            return QObject::tr("An emulator-only personality of the General Sound: a player, no card firmware.");
        return {};
    }
}  // namespace

SlotsWindow::SlotsWindow(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(tr("Slots"));
    buildUi();
    _timer = new QTimer(this);
    _timer->setInterval(1000);
    connect(_timer, &QTimer::timeout, this, &SlotsWindow::refresh);
}

void SlotsWindow::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    _machine = new QLabel(this);
    _machine->setWordWrap(true);
    layout->addWidget(_machine);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    layout->addWidget(splitter, 1);

    _tree = new QTreeWidget(splitter);
    _tree->setColumnCount(5);
    _tree->setHeaderLabels({tr("Bus / slot"), tr("Card"), tr("Options"), tr("Fit"), tr("State")});
    _tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    connect(_tree, &QTreeWidget::itemSelectionChanged, this, &SlotsWindow::onTreeSelection);

    auto* change = new QGroupBox(tr("Change"), splitter);
    auto* changeLayout = new QVBoxLayout(change);
    auto* form = new QFormLayout();
    _slot = new QComboBox(change);
    _slot->setEditable(true);
    _slot->setToolTip(tr("A slot of the machine: ay-socket, zxbus.1, ... or <bus>.next for the next free one"));
    form->addRow(tr("Slot"), _slot);
    _card = new QComboBox(change);
    form->addRow(tr("Card"), _card);
    _optionsBox = new QWidget(change);
    _optionsLayout = new QVBoxLayout(_optionsBox);
    _optionsLayout->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Options"), _optionsBox);
    _cardNote = new QLabel(change);
    _cardNote->setWordWrap(true);
    form->addRow(QString(), _cardNote);
    changeLayout->addLayout(form);

    auto* buttons = new QHBoxLayout();
    _plug = new QPushButton(tr("Plug In"), change);
    _remove = new QPushButton(tr("Remove"), change);
    _setOptions = new QPushButton(tr("Set Options"), change);
    _undo = new QPushButton(tr("Undo"), change);
    _undo->setEnabled(false);
    buttons->addWidget(_plug);
    buttons->addWidget(_remove);
    buttons->addWidget(_setOptions);
    buttons->addStretch(1);
    buttons->addWidget(_undo);
    changeLayout->addLayout(buttons);

    _preview = new QPlainTextEdit(change);
    _preview->setReadOnly(true);
    _preview->setPlaceholderText(tr("The plan of the change: what it removes, shadows and releases"));
    changeLayout->addWidget(_preview, 1);

    connect(_plug, &QPushButton::clicked, this, &SlotsWindow::plugChosen);
    connect(_remove, &QPushButton::clicked, this, &SlotsWindow::removeChosen);
    connect(_setOptions, &QPushButton::clicked, this, &SlotsWindow::setChosenOptions);
    connect(_undo, &QPushButton::clicked, this, &SlotsWindow::undoLast);
    connect(_slot, &QComboBox::currentTextChanged, this, [this]() {
        if (!_loading)
            updatePreview();
    });
    connect(_card, &QComboBox::currentIndexChanged, this, [this]() {
        if (_loading)
            return;
        rebuildOptionEditors();
        updatePreview();
    });
    resize(760, 680);
}

void SlotsWindow::setBinding(EmulatorBinding* binding)
{
    _binding = binding;
    if (_binding)
    {
        connect(_binding, &EmulatorBinding::bound, this, [this]() {
            _shownReport.clear();
            refresh();
        });
        connect(_binding, &EmulatorBinding::unbound, this, [this]() {
            _shownReport.clear();
            refresh();
        });
    }
}

void SlotsWindow::setController(SlotChangeController* controller)
{
    _controller = controller;
    if (_controller)
    {
        connect(_controller, &SlotChangeController::undoAvailable, this, [this](bool available) {
            _undo->setEnabled(available);
            _undo->setToolTip(available ? tr("Undo: %1").arg(_controller->UndoText()) : QString());
        });
        connect(_controller, &SlotChangeController::applied, this, [this]() {
            _shownReport.clear();
            refresh();
        });
    }
}

void SlotsWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    _shownReport.clear();
    refresh();
    _timer->start();
    emit visibilityChanged(true);
}

void SlotsWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    _timer->stop();
    emit visibilityChanged(false);
}

std::string SlotsWindow::currentEmulatorId() const
{
    Emulator* emulator = _binding ? _binding->emulator() : nullptr;
    return emulator ? emulator->GetId() : std::string();
}

void SlotsWindow::refresh()
{
    Emulator* emulator = _binding ? _binding->emulator() : nullptr;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    const StateNode report = DeviceState::Slots(context);
    const std::string json = StateNodeToJsonText(report);
    if (json == _shownReport)
        return;
    _shownReport = json;
    _report = report;

    const StateNode* available = report.find("available");
    const bool ok = available != nullptr && available->b;
    _plug->setEnabled(ok);
    _remove->setEnabled(ok);
    _setOptions->setEnabled(ok);
    if (!ok)
    {
        _machine->setText(tr("No machine: %1").arg(Q(Text(report.find("description")))));
        _tree->clear();
        _card->clear();
        _slot->clear();
        _preview->clear();
        return;
    }
    _machine->setText(tr("<b>%1</b> - %2; cards from %3")
                          .arg(Q(Text(report.find("model"))), Q(Text(report.find("board"))), Q(Text(report.find("source")))));
    fillTree(report);
    fillSlotChoices(report);
    SlotManager* manager = context->pSlotManager;
    fillCatalog(manager ? SlotControl::Catalog(manager->Snapshot()) : StateNode::Array());
    updatePreview();
}

void SlotsWindow::fillTree(const StateNode& report)
{
    _tree->clear();
    std::map<std::string, QTreeWidgetItem*> buses;
    auto addBus = [&](const StateNode& bus, QTreeWidgetItem* parent) {
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(_tree);
        const std::string id = Text(bus.find("id"));
        item->setText(0, Q(id));
        item->setText(1, Q(Text(bus.find("kind"))));
        item->setText(4, tr("arbitration %1").arg(Q(Text(bus.find("arbitration")))));
        const StateNode* retrofit = bus.find("retrofit");
        if (retrofit != nullptr && retrofit->b)
        {
            item->setText(3, tr("retrofitted"));
            item->setToolTip(0, Q(Text(bus.find("retrofitNote"))));
        }
        else if (!Text(bus.find("note")).empty())
        {
            item->setToolTip(0, Q(Text(bus.find("note"))));
        }
        item->setExpanded(true);
        buses[id] = item;
    };
    // The machine's buses first, then the board's own cards in its own slots (the Sprinter's ISA slots, SL-8), then
    // the buses those cards host (the ZX-bus adapter's) under them
    if (const StateNode* list = report.find("buses"))
    {
        for (const StateNode& bus : list->items)
        {
            if (Text(bus.find("host")).empty())
                addBus(bus, nullptr);
        }
    }
    std::map<std::string, QTreeWidgetItem*> machineSlots;
    if (const StateNode* list = report.find("machineSlots"))
    {
        for (const StateNode& slot : list->items)
        {
            const std::string busId = Text(slot.find("bus"));
            QTreeWidgetItem* parent = buses.count(busId) ? buses[busId] : nullptr;
            auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(_tree);
            const std::string id = Text(slot.find("slot"));
            item->setText(0, Q(id));
            item->setText(1, Q(Text(slot.find("card"))));
            item->setToolTip(1, Q(Text(slot.find("name"))));
            item->setText(2, Q(Text(slot.find("details"))));
            item->setText(3, tr("board card"));
            item->setText(4, tr("from %1").arg(Q(Text(slot.find("source")))));
            item->setToolTip(4, tr("Set in the machine configuration (%1); not changed from this window")
                                    .arg(Q(Text(slot.find("source")))));
            item->setExpanded(true);
            machineSlots[id] = item;
        }
    }
    if (const StateNode* list = report.find("buses"))
    {
        for (const StateNode& bus : list->items)
        {
            const std::string host = Text(bus.find("host"));
            if (!host.empty())
                addBus(bus, machineSlots.count(host) ? machineSlots[host] : nullptr);
        }
    }
    if (const StateNode* list = report.find("slots"))
    {
        for (const StateNode& slot : list->items)
        {
            const std::string id = Text(slot.find("slot"));
            std::string busId = Text(slot.find("bus"));
            if (!buses.count(busId))
                busId = id.substr(0, id.find('.'));
            QTreeWidgetItem* parent = buses.count(busId) ? buses[busId] : nullptr;
            auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(_tree);
            item->setText(0, Q(id));
            item->setText(1, Q(Text(slot.find("card"))));
            item->setToolTip(1, Q(Text(slot.find("name"))));
            item->setText(2, Q(Text(slot.find("options"))));
            const std::string adapter = Text(slot.find("adapter"));
            item->setText(3, Q(Text(slot.find("fit"))) + (adapter.empty() ? QString() : tr(" behind %1").arg(Q(adapter))));
            item->setText(4, Q(Text(slot.find("state"))));
            const std::string reason = Text(slot.find("reason"));
            if (!reason.empty())
                item->setToolTip(4, Q(reason));
            item->setData(0, Qt::UserRole, Q(id));
        }
    }
    auto* builtIns = new QTreeWidgetItem(_tree);
    builtIns->setText(0, tr("Built-in devices"));
    builtIns->setExpanded(true);
    if (const StateNode* list = report.find("builtIns"))
    {
        for (const StateNode& builtIn : list->items)
        {
            auto* item = new QTreeWidgetItem(builtIns);
            item->setText(0, Q(Text(builtIn.find("id"))));
            item->setText(1, Q(Text(builtIn.find("name"))));
            item->setText(3, Q(Text(builtIn.find("kind"))));
            item->setText(4, Q(Text(builtIn.find("state"))));
        }
    }
}

void SlotsWindow::fillSlotChoices(const StateNode& report)
{
    const QString current = _slot->currentText();
    _loading = true;
    _slot->clear();
    QStringList choices;
    if (const StateNode* list = report.find("slots"))
    {
        for (const StateNode& slot : list->items)
            choices << Q(Text(slot.find("slot")));
    }
    if (const StateNode* list = report.find("buses"))
    {
        for (const StateNode& bus : list->items)
        {
            const std::string id = Text(bus.find("id"));
            const std::string host = Text(bus.find("host"));
            // A hosted bus (the ZX-bus adapter's) has one place: the slot of the card that hosts it
            choices << (!host.empty() ? Q(host) : id == "ay-socket" ? Q(id) : Q(id + ".next"));
        }
    }
    choices.removeDuplicates();
    _slot->addItems(choices);
    _slot->setCurrentText(current.isEmpty() && !choices.isEmpty() ? choices.first() : current);
    _loading = false;
}

void SlotsWindow::fillCatalog(const StateNode& cards)
{
    const QString current = _card->currentData().toString();
    _loading = true;
    _catalog = cards;
    _card->clear();
    for (const StateNode& card : cards.items)
    {
        const StateNode* here = card.find("thisMachine");
        const std::string outcome = here ? Text(here->find("outcome")) : std::string();
        QString label = Q(Text(card.find("id"))) + " - " + Q(Text(card.find("name")));
        if (outcome == "needs-replace")
            label += tr("  (replaces cards)");
        else if (outcome == "refused")
            label += tr("  (does not fit)");
        const StateNode* emulated = card.find("emulated");
        if (emulated != nullptr && !emulated->b)
            label += tr("  [not emulated]");
        _card->addItem(label, Q(Text(card.find("id"))));
        QStringList tip;
        if (here)
        {
            tip << tr("Here: %1 in %2, fit %3").arg(Q(outcome), Q(Text(here->find("slot"))), Q(Text(here->find("fit"))));
            for (const StateNode& reason : here->find("reasons")->items)
                tip << Q(reason.s);
        }
        _card->setItemData(_card->count() - 1, tip.join("\n"), Qt::ToolTipRole);
    }
    const int index = _card->findData(current);
    _card->setCurrentIndex(index >= 0 ? index : 0);
    _loading = false;
    rebuildOptionEditors();
}

void SlotsWindow::rebuildOptionEditors()
{
    while (QLayoutItem* item = _optionsLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    _editors.clear();
    const std::string cardId = _card->currentData().toString().toStdString();
    _cardNote->setText(CardNote(cardId));
    _cardNote->setVisible(!_cardNote->text().isEmpty());
    const StateNode* card = nullptr;
    for (const StateNode& c : _catalog.items)
    {
        if (Text(c.find("id")) == cardId)
            card = &c;
    }
    if (card == nullptr)
        return;
    // The slot's own options when it holds this card, else the defaults
    std::map<std::string, std::vector<std::string>> given;
    if (const StateNode* slots = _report.find("slots"))
    {
        for (const StateNode& slot : slots->items)
        {
            if (Q(Text(slot.find("slot"))) == _slot->currentText() && Text(slot.find("card")) == cardId)
                given = ParseOptions(Text(slot.find("options")));
        }
    }
    for (const StateNode& option : card->find("options")->items)
    {
        OptionEditor editor;
        editor.name = Text(option.find("name"));
        editor.set = Text(option.find("kind")) == "set";
        std::vector<std::string> values = given.count(editor.name) ? given[editor.name] : std::vector<std::string>{};
        if (!given.count(editor.name))
        {
            std::istringstream defaults(Text(option.find("default")));
            for (std::string value; std::getline(defaults, value, ',');)
                values.push_back(value);
        }
        auto* row = new QWidget(_optionsBox);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(Q(editor.name), row);
        label->setToolTip(Q(Text(option.find("description"))));
        label->setMinimumWidth(70);
        rowLayout->addWidget(label);
        if (editor.set)
        {
            for (const StateNode& value : option.find("values")->items)
            {
                const std::string id = Text(value.find("id"));
                auto* box = new QCheckBox(Q(id), row);
                box->setChecked(std::find(values.begin(), values.end(), id) != values.end());
                connect(box, &QCheckBox::toggled, this, &SlotsWindow::updatePreview);
                rowLayout->addWidget(box);
                editor.boxes.emplace_back(id, box);
            }
        }
        else
        {
            editor.combo = new QComboBox(row);
            for (const StateNode& value : option.find("values")->items)
                editor.combo->addItem(Q(Text(value.find("label"))).remove('`'), Q(Text(value.find("id"))));
            const int index = values.empty() ? -1 : editor.combo->findData(Q(values.front()));
            if (index >= 0)
                editor.combo->setCurrentIndex(index);
            connect(editor.combo, &QComboBox::currentIndexChanged, this, &SlotsWindow::updatePreview);
            rowLayout->addWidget(editor.combo);
        }
        rowLayout->addStretch(1);
        _optionsLayout->addWidget(row);
        _editors.push_back(std::move(editor));
    }
}

QString SlotsWindow::optionsText() const
{
    QStringList words;
    for (const OptionEditor& editor : _editors)
    {
        QStringList values;
        if (editor.set)
        {
            for (const auto& [id, box] : editor.boxes)
            {
                if (box->isChecked())
                    values << Q(id);
            }
        }
        else if (editor.combo)
        {
            values << editor.combo->currentData().toString();
        }
        words << Q(editor.name) + "=" + (values.isEmpty() ? QStringLiteral("none") : values.join(","));
    }
    return words.join(" ");
}

void SlotsWindow::choose(const QString& slot, const QString& card, const QString& options)
{
    _slot->setCurrentText(slot);
    const int index = _card->findData(card);
    if (index >= 0)
        _card->setCurrentIndex(index);
    rebuildOptionEditors();
    if (!options.isEmpty())
    {
        const auto given = ParseOptions(options.toStdString());
        for (OptionEditor& editor : _editors)
        {
            auto it = given.find(editor.name);
            if (it == given.end())
                continue;
            for (auto& [id, box] : editor.boxes)
                box->setChecked(std::find(it->second.begin(), it->second.end(), id) != it->second.end());
            if (editor.combo && !it->second.empty())
                editor.combo->setCurrentIndex(std::max(0, editor.combo->findData(Q(it->second.front()))));
        }
    }
    updatePreview();
}

QString SlotsWindow::treeText() const
{
    QStringList lines;
    std::function<void(QTreeWidgetItem*, int)> walk = [&](QTreeWidgetItem* item, int depth) {
        lines << QString(depth * 2, ' ') + item->text(0) + " | " + item->text(1);
        for (int i = 0; i < item->childCount(); i++)
            walk(item->child(i), depth + 1);
    };
    for (int i = 0; i < _tree->topLevelItemCount(); i++)
        walk(_tree->topLevelItem(i), 0);
    return lines.join('\n');
}

QString SlotsWindow::previewText() const
{
    return _preview->toPlainText();
}

void SlotsWindow::updatePreview()
{
    Emulator* emulator = _binding ? _binding->emulator() : nullptr;
    SlotManager* manager = emulator && emulator->GetContext() ? emulator->GetContext()->pSlotManager : nullptr;
    const std::string cardId = _card->currentData().toString().toStdString();
    static const slots::SlotPlanner planner;
    const slots::CardDef* card = planner.FindCard(cardId);
    if (!manager || !card)
    {
        _preview->clear();
        return;
    }
    // The slot holds this card: the change is its options; else a plug
    slots::SlotRequest request;
    request.replaceIfIncompatible = true;   // the GUI replaces (Q1); the plan names what goes
    request.slot = _slot->currentText().trimmed().toStdString();
    const SlotManager::Result current = manager->Snapshot();
    const SlotManager::Slot* fitted = current.FindSlot(request.slot);
    std::string error;
    if (fitted != nullptr && fitted->entry.card == cardId)
    {
        request.op = slots::SlotRequest::Op::SetOptions;
        request.card = cardId;
    }
    else
    {
        request.op = slots::SlotRequest::Op::Plug;
        request.card = cardId;
    }
    if (!slots::ParseCardOptions(*card, optionsText().toStdString(), request.options, &error))
    {
        _preview->setPlainText(tr("Options: %1").arg(Q(error)));
        return;
    }
    _preview->setPlainText(SlotChangeController::PlanText(manager->PlanChange(request)).join("\n"));
}

void SlotsWindow::onTreeSelection()
{
    const QList<QTreeWidgetItem*> selected = _tree->selectedItems();
    if (selected.isEmpty())
        return;
    const QString slot = selected.first()->data(0, Qt::UserRole).toString();
    if (slot.isEmpty())
        return;
    choose(slot, selected.first()->text(1), selected.first()->text(2));
}

void SlotsWindow::plugChosen()
{
    static const slots::SlotPlanner planner;
    const std::string cardId = _card->currentData().toString().toStdString();
    const slots::CardDef* card = planner.FindCard(cardId);
    if (!_controller || !card)
        return;
    slots::SlotRequest request;
    request.op = slots::SlotRequest::Op::Plug;
    request.slot = _slot->currentText().trimmed().toStdString();
    request.card = cardId;
    if (!slots::ParseCardOptions(*card, optionsText().toStdString(), request.options))
        return;
    _controller->Apply(currentEmulatorId(), request, this);
}

void SlotsWindow::removeChosen()
{
    if (!_controller)
        return;
    slots::SlotRequest request;
    request.op = slots::SlotRequest::Op::Remove;
    request.slot = _slot->currentText().trimmed().toStdString();
    _controller->Apply(currentEmulatorId(), request, this);
}

void SlotsWindow::setChosenOptions()
{
    static const slots::SlotPlanner planner;
    const slots::CardDef* card = planner.FindCard(_card->currentData().toString().toStdString());
    if (!_controller || !card)
        return;
    slots::SlotRequest request;
    request.op = slots::SlotRequest::Op::SetOptions;
    request.slot = _slot->currentText().trimmed().toStdString();
    request.card = card->id;
    if (!slots::ParseCardOptions(*card, optionsText().toStdString(), request.options))
        return;
    _controller->Apply(currentEmulatorId(), request, this);
}

void SlotsWindow::undoLast()
{
    if (_controller)
        _controller->Undo(this);
}
