/**
 * @file slotswindow.cpp
 * @brief SlotsWindow - simplified slot management UI.
 */

#include "slotswindow.h"

#include <map>
#include <sstream>

#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "cardslots/slotchangecontroller.h"
#include "cardslots/slotconfigdialog.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/slots/slotplanner.h"
#include "emulator/state/devicestate.h"
#include "emulator/state/statenodejson.h"

using ConflictInfo = SlotConfigDialog::ConflictInfo;

#undef slots

namespace
{
    QString Q(const std::string& text)
    {
        return QString::fromStdString(text);
    }

    std::string Text(const StateNode* node)
    {
        return node && node->kind == StateNode::Kind::String ? node->s : std::string();
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
    layout->setSpacing(12);

    _machineLabel = new QLabel(this);
    _machineLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    layout->addWidget(_machineLabel);

    auto* builtInsHeader = new QHBoxLayout();
    _builtInsToggle = new QToolButton(this);
    _builtInsToggle->setArrowType(Qt::DownArrow);
    _builtInsToggle->setAutoRaise(true);
    _builtInsToggle->setToolTip(tr("Show/hide built-in devices"));
    builtInsHeader->addWidget(_builtInsToggle);
    auto* builtInsLabel = new QLabel(tr("Built-in Devices"), this);
    builtInsLabel->setStyleSheet("font-weight: bold;");
    builtInsHeader->addWidget(builtInsLabel);
    builtInsHeader->addStretch(1);
    layout->addLayout(builtInsHeader);

    _builtInsTree = new QTreeWidget(this);
    _builtInsTree->setHeaderHidden(true);
    _builtInsTree->setColumnCount(2);
    _builtInsTree->header()->setStretchLastSection(true);
    _builtInsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _builtInsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _builtInsTree->setRootIsDecorated(false);
    _builtInsTree->setMaximumHeight(120);
    layout->addWidget(_builtInsTree);

    connect(_builtInsToggle, &QToolButton::clicked, this, [this]() {
        bool visible = !_builtInsTree->isVisible();
        _builtInsTree->setVisible(visible);
        _builtInsToggle->setArrowType(visible ? Qt::DownArrow : Qt::RightArrow);
    });

    // The machine's own slots (SL-8: the Sprinter's ISA slots, filled by [ISA]): read-only; the ZX-bus adapter's
    // card is an expansion slot below, on the adapter's ZX-bus
    _machineSlotsLabel = new QLabel(tr("Board Slots"), this);
    _machineSlotsLabel->setStyleSheet("font-weight: bold;");
    _machineSlotsLabel->setToolTip(tr("Cards of the machine's own slots, set in its configuration ([ISA]); not changed "
                                      "from this window"));
    layout->addWidget(_machineSlotsLabel);
    _machineSlotsTree = new QTreeWidget(this);
    _machineSlotsTree->setHeaderHidden(true);
    _machineSlotsTree->setColumnCount(3);
    _machineSlotsTree->setRootIsDecorated(false);
    _machineSlotsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _machineSlotsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _machineSlotsTree->header()->setStretchLastSection(true);
    _machineSlotsTree->setMaximumHeight(80);
    layout->addWidget(_machineSlotsTree);
    _machineSlotsLabel->hide();
    _machineSlotsTree->hide();

    auto* slotsHeader = new QHBoxLayout();
    auto* slotsLabel = new QLabel(tr("Expansion Slots"), this);
    slotsLabel->setStyleSheet("font-weight: bold;");
    slotsHeader->addWidget(slotsLabel);
    slotsHeader->addStretch(1);
    _addButton = new QPushButton("+", this);
    _addButton->setFixedWidth(30);
    _addButton->setToolTip(tr("Add a new slot"));
    _removeButton = new QPushButton("-", this);
    _removeButton->setFixedWidth(30);
    _removeButton->setToolTip(tr("Remove selected slot"));
    _configButton = new QPushButton("...", this);
    _configButton->setFixedWidth(30);
    _configButton->setToolTip(tr("Configure selected slot"));
    slotsHeader->addWidget(_addButton);
    slotsHeader->addWidget(_removeButton);
    slotsHeader->addWidget(_configButton);
    layout->addLayout(slotsHeader);

    _slotsList = new QListWidget(this);
    _slotsList->setSelectionMode(QAbstractItemView::SingleSelection);
    _slotsList->setAlternatingRowColors(true);
    layout->addWidget(_slotsList, 1);

    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch(1);
    _undoButton = new QPushButton(tr("Undo"), this);
    _undoButton->setEnabled(false);
    bottomLayout->addWidget(_undoButton);
    layout->addLayout(bottomLayout);

    connect(_addButton, &QPushButton::clicked, this, &SlotsWindow::onAddSlot);
    connect(_removeButton, &QPushButton::clicked, this, &SlotsWindow::onRemoveSlot);
    connect(_configButton, &QPushButton::clicked, this, [this]() {
        onConfigureSlot(_slotsList->currentRow());
    });
    connect(_undoButton, &QPushButton::clicked, this, &SlotsWindow::undoLast);
    connect(_slotsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        onConfigureSlot(_slotsList->row(item));
    });

    resize(400, 500);
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
            _undoButton->setEnabled(available);
            _undoButton->setToolTip(available ? tr("Undo: %1").arg(_controller->UndoText()) : QString());
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
    const bool ok = available && available->b;
    _addButton->setEnabled(ok);
    _removeButton->setEnabled(ok);

    if (!ok)
    {
        _machineLabel->setText(tr("No machine"));
        _builtInsTree->clear();
        _slotsList->clear();
        _machineSlotsTree->clear();
        _machineSlotsLabel->hide();
        _machineSlotsTree->hide();
        return;
    }

    _machineLabel->setText(Q(Text(report.find("model"))));
    fillBuiltIns(report);
    fillMachineSlots(report);
    fillSlots(report);

    SlotManager* manager = context->pSlotManager;
    _catalog = manager ? SlotControl::Catalog(manager->Snapshot()) : StateNode::Array();
}

void SlotsWindow::fillBuiltIns(const StateNode& report)
{
    _builtInsTree->clear();

    std::map<std::string, std::string> replacements;
    if (const StateNode* slotsNode = report.find("slots"))
    {
        for (const StateNode& slot : slotsNode->items)
        {
            const StateNode* replaces = slot.find("replaces");
            if (replaces)
            {
                for (const StateNode& r : replaces->items)
                {
                    const std::string replacedId = Text(&r);
                    const std::string cardName = Text(slot.find("name"));
                    const std::string slotId = Text(slot.find("slot"));
                    replacements[replacedId] = cardName + " (" + slotId + ")";
                }
            }
        }
    }

    if (const StateNode* list = report.find("builtIns"))
    {
        for (const StateNode& builtIn : list->items)
        {
            auto* item = new QTreeWidgetItem(_builtInsTree);
            const std::string id = Text(builtIn.find("id"));
            const std::string name = Text(builtIn.find("name"));
            const std::string state = Text(builtIn.find("state"));

            item->setText(0, Q(name.empty() ? id : name));

            if (replacements.count(id))
            {
                item->setText(1, tr("replaced by %1").arg(Q(replacements[id])));
                item->setForeground(1, QColor(180, 120, 0));
                item->setIcon(0, style()->standardIcon(QStyle::SP_MessageBoxWarning));
            }
            else if (state == "active")
            {
                item->setText(1, tr("active"));
                item->setForeground(1, QColor(0, 128, 0));
            }
            else
            {
                item->setText(1, Q(state));
                item->setForeground(1, QColor(128, 128, 128));
            }
        }
    }
}

void SlotsWindow::fillSlots(const StateNode& report)
{
    _slotsList->clear();

    if (const StateNode* list = report.find("slots"))
    {
        for (const StateNode& slot : list->items)
        {
            const std::string slotId = Text(slot.find("slot"));
            const std::string cardId = Text(slot.find("card"));
            const std::string cardName = Text(slot.find("name"));
            const std::string options = Text(slot.find("options"));

            auto* item = new QListWidgetItem(_slotsList);
            item->setData(Qt::UserRole, Q(slotId));
            item->setData(Qt::UserRole + 1, Q(cardId));
            item->setData(Qt::UserRole + 2, Q(options));

            if (cardId.empty())
            {
                item->setText(Q(slotId) + ": " + tr("(empty)"));
                item->setForeground(QColor(128, 128, 128));
            }
            else
            {
                QString display = Q(slotId) + ": " + Q(cardName.empty() ? cardId : cardName);
                // Behind an adapter the card is on the adapter's bus, hosted by the slot (SL-8: isa.1.zxbus)
                const std::string host = Text(slot.find("host"));
                if (!host.empty())
                    display += tr(" (on %1)").arg(Q(Text(slot.find("bus"))));
                item->setText(display);
                if (!options.empty())
                    item->setToolTip(Q(options));
            }
        }
    }
}

void SlotsWindow::fillMachineSlots(const StateNode& report)
{
    _machineSlotsTree->clear();
    const StateNode* list = report.find("machineSlots");
    const bool any = list != nullptr && list->size() > 0;
    _machineSlotsLabel->setVisible(any);
    _machineSlotsTree->setVisible(any);
    if (!any)
        return;
    for (const StateNode& slot : list->items)
    {
        auto* item = new QTreeWidgetItem(_machineSlotsTree);
        item->setText(0, Q(Text(slot.find("slot"))));
        item->setText(1, Q(Text(slot.find("name"))));
        const std::string hosts = Text(slot.find("hostsBus"));
        const std::string details = Text(slot.find("details"));
        item->setText(2, !hosts.empty() ? tr("hosts %1: %2").arg(Q(hosts), Q(Text(slot.find("hostedCard"))))
                                        : Q(details));
        item->setToolTip(0, tr("from %1").arg(Q(Text(slot.find("source")))));
    }
}

QString SlotsWindow::slotsText() const
{
    QStringList lines;
    for (int i = 0; i < _slotsList->count(); i++)
        lines << _slotsList->item(i)->text();
    for (int i = 0; i < _machineSlotsTree->topLevelItemCount(); i++)
    {
        const QTreeWidgetItem* item = _machineSlotsTree->topLevelItem(i);
        lines << "board " + item->text(0) + " | " + item->text(1) + " | " + item->text(2);
    }
    return lines.join('\n');
}

void SlotsWindow::onAddSlot()
{
    const std::string slotId = nextFreeSlot();
    if (slotId.empty())
        return;

    if (!_controller)
        return;

    slots::SlotRequest request;
    request.op = slots::SlotRequest::Op::Plug;
    request.slot = slotId;
    request.card = "";
    _controller->Apply(currentEmulatorId(), request, this);

    QTimer::singleShot(200, this, [this]() {
        refresh();
        if (_slotsList->count() > 0)
        {
            _slotsList->setCurrentRow(_slotsList->count() - 1);
            onConfigureSlot(_slotsList->count() - 1);
        }
    });
}

void SlotsWindow::onRemoveSlot()
{
    auto* item = _slotsList->currentItem();
    if (!item || !_controller)
        return;

    const std::string slotId = item->data(Qt::UserRole).toString().toStdString();
    slots::SlotRequest request;
    request.op = slots::SlotRequest::Op::Remove;
    request.slot = slotId;
    _controller->Apply(currentEmulatorId(), request, this);
}

void SlotsWindow::onConfigureSlot(int row)
{
    if (row < 0 || row >= _slotsList->count())
        return;

    auto* item = _slotsList->item(row);
    const std::string slotId = item->data(Qt::UserRole).toString().toStdString();
    const std::string cardId = item->data(Qt::UserRole + 1).toString().toStdString();
    const std::string options = item->data(Qt::UserRole + 2).toString().toStdString();

    auto* dialog = new SlotConfigDialog(this);
    dialog->setSlotId(slotId);
    dialog->setCatalog(_catalog);
    dialog->setCurrentCard(cardId, options);

    Emulator* emulator = _binding ? _binding->emulator() : nullptr;
    SlotManager* manager = emulator && emulator->GetContext() ? emulator->GetContext()->pSlotManager : nullptr;
    if (manager)
    {
        dialog->setConflictProvider([manager, slotId](const std::string& cardId) -> SlotConfigDialog::ConflictInfo {
            SlotConfigDialog::ConflictInfo info;
            slots::SlotRequest req;
            req.op = slots::SlotRequest::Op::Plug;
            req.slot = slotId;
            req.card = cardId;
            req.replaceIfIncompatible = true;
            const SlotManager::ChangePlan plan = manager->PlanChange(req);
            for (const auto& r : plan.plan.removed)
                info.removedCards.push_back(r.card + " (" + r.slot + ")");
            for (const auto& b : plan.plan.builtInSwitchedOff)
                info.disabledBuiltIns.push_back(b.builtIn);
            info.needsRestart = true;
            return info;
        });
    }

    connect(dialog, &SlotConfigDialog::accepted, this, &SlotsWindow::onSlotDialogAccepted);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void SlotsWindow::onSlotDialogAccepted(const std::string& slotId, const std::string& cardId, const std::string& options)
{
    if (!_controller)
        return;

    static const slots::SlotPlanner planner;
    slots::SlotRequest request;
    request.slot = slotId;
    request.replaceIfIncompatible = true;

    if (cardId.empty())
    {
        request.op = slots::SlotRequest::Op::Remove;
    }
    else
    {
        const slots::CardDef* card = planner.FindCard(cardId);
        if (!card)
            return;

        const StateNode* currentSlot = nullptr;
        if (const StateNode* slotsNode = _report.find("slots"))
        {
            for (const StateNode& s : slotsNode->items)
            {
                if (Text(s.find("slot")) == slotId)
                {
                    currentSlot = &s;
                    break;
                }
            }
        }

        if (currentSlot && Text(currentSlot->find("card")) == cardId)
        {
            request.op = slots::SlotRequest::Op::SetOptions;
        }
        else
        {
            request.op = slots::SlotRequest::Op::Plug;
        }

        request.card = cardId;
        slots::ParseCardOptions(*card, options, request.options);
    }

    _controller->Apply(currentEmulatorId(), request, this);
}

void SlotsWindow::undoLast()
{
    if (_controller)
        _controller->Undo(this);
}

std::string SlotsWindow::nextFreeSlot() const
{
    if (const StateNode* buses = _report.find("buses"))
    {
        for (const StateNode& bus : buses->items)
        {
            const std::string id = Text(bus.find("id"));
            // A hosted bus (the ZX-bus adapter's) is reached through its host slot, not as "<bus>.next"
            if (id != "ay-socket" && Text(bus.find("host")).empty())
                return id + ".next";
        }
    }
    return "zxbus.next";
}

void SlotsWindow::choose(const QString& slot, const QString& card, const QString& options)
{
    _testSlot = slot;
    _testCard = card;
    _testOptions = mergeOptionsWithDefaults(card.toStdString(), options.toStdString());
}

QString SlotsWindow::previewText() const
{
    Emulator* emulator = _binding ? _binding->emulator() : nullptr;
    SlotManager* manager = emulator && emulator->GetContext() ? emulator->GetContext()->pSlotManager : nullptr;
    if (!manager || _testCard.isEmpty())
        return {};

    static const slots::SlotPlanner planner;
    const slots::CardDef* card = planner.FindCard(_testCard.toStdString());
    if (!card)
        return {};

    const SlotManager::Result snapshot = manager->Snapshot();
    const SlotManager::Slot* fitted = nullptr;
    for (const SlotManager::Slot& s : snapshot.entries)
    {
        if (s.entry.slot == _testSlot.toStdString())
        {
            fitted = &s;
            break;
        }
    }

    slots::SlotRequest req;
    req.replaceIfIncompatible = true;
    req.slot = _testSlot.toStdString();
    req.card = _testCard.toStdString();
    slots::ParseCardOptions(*card, _testOptions.toStdString(), req.options);

    if (fitted && fitted->entry.card == _testCard.toStdString())
        req.op = slots::SlotRequest::Op::SetOptions;
    else
        req.op = slots::SlotRequest::Op::Plug;

    return SlotChangeController::PlanText(manager->PlanChange(req)).join("\n");
}

QString SlotsWindow::optionsText() const
{
    return _testOptions;
}

void SlotsWindow::plugChosen()
{
    if (!_controller || _testCard.isEmpty())
        return;

    static const slots::SlotPlanner planner;
    const slots::CardDef* card = planner.FindCard(_testCard.toStdString());
    if (!card)
        return;

    slots::SlotRequest req;
    req.op = slots::SlotRequest::Op::Plug;
    req.slot = _testSlot.toStdString();
    req.card = _testCard.toStdString();
    slots::ParseCardOptions(*card, _testOptions.toStdString(), req.options);
    _controller->Apply(currentEmulatorId(), req, this);
}

void SlotsWindow::removeChosen()
{
    if (!_controller)
        return;

    slots::SlotRequest req;
    req.op = slots::SlotRequest::Op::Remove;
    req.slot = _testSlot.toStdString();
    _controller->Apply(currentEmulatorId(), req, this);
}

void SlotsWindow::setChosenOptions()
{
    if (!_controller || _testCard.isEmpty())
        return;

    static const slots::SlotPlanner planner;
    const slots::CardDef* card = planner.FindCard(_testCard.toStdString());
    if (!card)
        return;

    slots::SlotRequest req;
    req.op = slots::SlotRequest::Op::SetOptions;
    req.slot = _testSlot.toStdString();
    req.card = _testCard.toStdString();
    slots::ParseCardOptions(*card, _testOptions.toStdString(), req.options);
    _controller->Apply(currentEmulatorId(), req, this);
}

QString SlotsWindow::defaultOptionsFor(const std::string& cardId) const
{
    for (const StateNode& card : _catalog.items)
    {
        if (Text(card.find("id")) != cardId)
            continue;
        const StateNode* options = card.find("options");
        if (!options)
            return {};
        QStringList parts;
        for (const StateNode& opt : options->items)
        {
            parts << Q(Text(opt.find("name"))) + "=" + Q(Text(opt.find("default")));
        }
        return parts.join(" ");
    }
    return {};
}

QString SlotsWindow::mergeOptionsWithDefaults(const std::string& cardId, const std::string& given) const
{
    std::vector<std::pair<std::string, std::string>> merged;
    for (const StateNode& card : _catalog.items)
    {
        if (Text(card.find("id")) != cardId)
            continue;
        const StateNode* options = card.find("options");
        if (!options)
            break;
        for (const StateNode& opt : options->items)
        {
            const std::string name = Text(opt.find("name"));
            merged.emplace_back(name, Text(opt.find("default")));
        }
        break;
    }
    std::map<std::string, std::string> givenMap;
    std::istringstream words(given);
    for (std::string word; words >> word;)
    {
        const size_t eq = word.find('=');
        if (eq != std::string::npos)
            givenMap[word.substr(0, eq)] = word.substr(eq + 1);
    }
    QStringList parts;
    for (auto& [name, value] : merged)
    {
        if (givenMap.count(name))
            value = givenMap[name];
        parts << Q(name) + "=" + Q(value);
    }
    return parts.join(" ");
}
