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
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
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
    layout->setSpacing(8);

    _machineLabel = new QLabel(this);
    _machineLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    layout->addWidget(_machineLabel);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    layout->addWidget(splitter, 1);

    _infoTree = new QTreeWidget(splitter);
    _infoTree->setColumnCount(5);
    _infoTree->setHeaderLabels({tr("Bus / Slot"), tr("Card"), tr("Options"), tr("Fit"), tr("State")});
    _infoTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _infoTree->header()->setStretchLastSection(true);
    _infoTree->setRootIsDecorated(true);
    _infoTree->setIndentation(16);
    _infoTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _infoTree->setSelectionMode(QAbstractItemView::NoSelection);

    auto* bottomPanel = new QWidget(splitter);
    auto* bottomLayout = new QVBoxLayout(bottomPanel);
    bottomLayout->setContentsMargins(0, 8, 0, 0);
    bottomLayout->setSpacing(8);

    _machineSlotsLabel = new QLabel(tr("Board Slots"), bottomPanel);
    _machineSlotsLabel->setStyleSheet("font-weight: bold;");
    _machineSlotsLabel->setToolTip(tr("Cards of the machine's own slots, set in its configuration ([ISA]); not changed "
                                      "from this window"));
    bottomLayout->addWidget(_machineSlotsLabel);
    _machineSlotsTree = new QTreeWidget(bottomPanel);
    _machineSlotsTree->setHeaderHidden(true);
    _machineSlotsTree->setColumnCount(3);
    _machineSlotsTree->setRootIsDecorated(false);
    _machineSlotsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _machineSlotsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _machineSlotsTree->header()->setStretchLastSection(true);
    _machineSlotsTree->setMaximumHeight(80);
    bottomLayout->addWidget(_machineSlotsTree);
    _machineSlotsLabel->hide();
    _machineSlotsTree->hide();

    auto* slotsHeader = new QHBoxLayout();
    auto* slotsLabel = new QLabel(tr("Expansion Slots"), bottomPanel);
    slotsLabel->setStyleSheet("font-weight: bold;");
    slotsHeader->addWidget(slotsLabel);
    slotsHeader->addStretch(1);
    _addButton = new QPushButton("+", bottomPanel);
    _addButton->setFixedWidth(30);
    _addButton->setToolTip(tr("Add a new slot"));
    _removeButton = new QPushButton("-", bottomPanel);
    _removeButton->setFixedWidth(30);
    _removeButton->setToolTip(tr("Remove selected slot"));
    slotsHeader->addWidget(_addButton);
    slotsHeader->addWidget(_removeButton);
    bottomLayout->addLayout(slotsHeader);

    _slotsTable = new QTableWidget(bottomPanel);
    _slotsTable->setColumnCount(3);
    _slotsTable->setHorizontalHeaderLabels({tr("Slot"), tr("Card"), QString()});
    _slotsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _slotsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    _slotsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    _slotsTable->horizontalHeader()->resizeSection(2, 36);
    _slotsTable->verticalHeader()->setVisible(false);
    _slotsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _slotsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _slotsTable->setAlternatingRowColors(true);
    _slotsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bottomLayout->addWidget(_slotsTable, 1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    _undoButton = new QPushButton(tr("Undo"), bottomPanel);
    _undoButton->setEnabled(false);
    buttonRow->addWidget(_undoButton);
    bottomLayout->addLayout(buttonRow);

    splitter->setSizes({300, 200});

    connect(_addButton, &QPushButton::clicked, this, &SlotsWindow::onAddSlot);
    connect(_removeButton, &QPushButton::clicked, this, &SlotsWindow::onRemoveSlot);
    connect(_undoButton, &QPushButton::clicked, this, &SlotsWindow::undoLast);
    connect(_slotsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        onConfigureSlot(row);
    });

    resize(760, 600);
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
        _infoTree->clear();
        _slotsTable->setRowCount(0);
        _machineSlotsTree->clear();
        _machineSlotsLabel->hide();
        _machineSlotsTree->hide();
        return;
    }

    _machineLabel->setText(Q(Text(report.find("model"))));
    fillInfoTree(report);
    fillMachineSlots(report);
    fillSlots(report);

    SlotManager* manager = context->pSlotManager;
    _catalog = manager ? SlotControl::Catalog(manager->Snapshot()) : StateNode::Array();
}

void SlotsWindow::fillInfoTree(const StateNode& report)
{
    _infoTree->clear();
    std::map<std::string, QTreeWidgetItem*> buses;

    if (const StateNode* list = report.find("buses"))
    {
        for (const StateNode& bus : list->items)
        {
            auto* item = new QTreeWidgetItem(_infoTree);
            const std::string id = Text(bus.find("id"));
            item->setText(0, Q(id));
            item->setText(1, Q(Text(bus.find("kind"))));
            item->setText(4, tr("arbitration %1").arg(Q(Text(bus.find("arbitration")))));
            const StateNode* retrofit = bus.find("retrofit");
            if (retrofit && retrofit->b)
            {
                item->setText(3, tr("retrofitted"));
                item->setToolTip(0, Q(Text(bus.find("retrofitNote"))));
            }
            item->setExpanded(true);
            buses[id] = item;
        }
    }

    if (const StateNode* list = report.find("slots"))
    {
        for (const StateNode& slot : list->items)
        {
            const std::string id = Text(slot.find("slot"));
            const std::string busId = id.substr(0, id.find('.'));
            QTreeWidgetItem* parent = buses.count(busId) ? buses[busId] : nullptr;
            auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(_infoTree);
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
        }
    }

    auto* builtIns = new QTreeWidgetItem(_infoTree);
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

void SlotsWindow::fillSlots(const StateNode& report)
{
    _slotsTable->setRowCount(0);

    if (const StateNode* list = report.find("slots"))
    {
        for (const StateNode& slot : list->items)
        {
            const std::string slotId = Text(slot.find("slot"));
            const std::string cardId = Text(slot.find("card"));
            const std::string cardName = Text(slot.find("name"));
            const std::string options = Text(slot.find("options"));

            const int row = _slotsTable->rowCount();
            _slotsTable->insertRow(row);

            auto* slotItem = new QTableWidgetItem(Q(slotId));
            slotItem->setData(Qt::UserRole, Q(slotId));
            slotItem->setData(Qt::UserRole + 1, Q(cardId));
            slotItem->setData(Qt::UserRole + 2, Q(options));
            _slotsTable->setItem(row, 0, slotItem);

            QString cardText;
            if (cardId.empty())
            {
                cardText = tr("(empty)");
            }
            else
            {
                cardText = Q(cardName.empty() ? cardId : cardName);
                const std::string host = Text(slot.find("host"));
                if (!host.empty())
                    cardText += tr(" (on %1)").arg(Q(Text(slot.find("bus"))));
            }

            auto* cardItem = new QTableWidgetItem(cardText);
            if (cardId.empty())
                cardItem->setForeground(QColor(128, 128, 128));
            if (!options.empty())
                cardItem->setToolTip(Q(options));
            _slotsTable->setItem(row, 1, cardItem);

            auto* configBtn = new QPushButton("...", _slotsTable);
            configBtn->setFixedSize(30, 24);
            connect(configBtn, &QPushButton::clicked, this, [this, row]() {
                onConfigureSlot(row);
            });
            _slotsTable->setCellWidget(row, 2, configBtn);
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
    for (int i = 0; i < _slotsTable->rowCount(); i++)
    {
        const QString slot = _slotsTable->item(i, 0)->text();
        const QString card = _slotsTable->item(i, 1)->text();
        lines << slot + ": " + card;
    }
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
        if (_slotsTable->rowCount() > 0)
        {
            const int lastRow = _slotsTable->rowCount() - 1;
            _slotsTable->selectRow(lastRow);
            onConfigureSlot(lastRow);
        }
    });
}

void SlotsWindow::onRemoveSlot()
{
    const int row = _slotsTable->currentRow();
    if (row < 0 || !_controller)
        return;

    auto* item = _slotsTable->item(row, 0);
    if (!item)
        return;

    const std::string slotId = item->data(Qt::UserRole).toString().toStdString();
    slots::SlotRequest request;
    request.op = slots::SlotRequest::Op::Remove;
    request.slot = slotId;
    _controller->Apply(currentEmulatorId(), request, this);
}

void SlotsWindow::onConfigureSlot(int row)
{
    if (row < 0 || row >= _slotsTable->rowCount())
        return;

    auto* item = _slotsTable->item(row, 0);
    if (!item)
        return;

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
