/**
 * @file slotconfigdialog.cpp
 * @brief SlotConfigDialog implementation.
 */

#include "slotconfigdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <sstream>

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

    std::map<std::string, std::vector<std::string>> ParseOptions(const std::string& text)
    {
        std::map<std::string, std::vector<std::string>> out;
        std::istringstream words(text);
        for (std::string word; words >> word;)
        {
            const size_t eq = word.find('=');
            if (eq == std::string::npos)
                continue;
            auto& values = out[word.substr(0, eq)];
            std::istringstream list(word.substr(eq + 1));
            for (std::string v; std::getline(list, v, ',');)
            {
                if (!v.empty() && v != "none")
                    values.push_back(v);
            }
        }
        return out;
    }

    QString categoryName(const std::string& cat)
    {
        static const std::map<std::string, QString> names = {
            {"sound", QObject::tr("Sound")},
            {"storage", QObject::tr("Storage")},
            {"io", QObject::tr("I/O")},
            {"network", QObject::tr("Network")},
        };
        auto it = names.find(cat);
        return it != names.end() ? it->second : Q(cat);
    }

    int categoryOrder(const std::string& cat)
    {
        static const std::map<std::string, int> order = {
            {"sound", 0}, {"storage", 1}, {"io", 2}, {"network", 3},
        };
        auto it = order.find(cat);
        return it != order.end() ? it->second : 50;
    }

    std::string inferCategory(const StateNode& card)
    {
        const StateNode* functions = card.find("functions");
        if (!functions || functions->items.empty())
            return "io";

        const std::string func = Text(&functions->items.front());

        if (func.find("ay") != std::string::npos || func.find("gs") != std::string::npos ||
            func.find("saa") != std::string::npos || func.find("soundrive") != std::string::npos ||
            func.find("covox") != std::string::npos || func.find("opl") != std::string::npos ||
            func.find("midi") != std::string::npos || func.find("moon") != std::string::npos)
            return "sound";

        if (func.find("beta") != std::string::npos || func.find("ide") != std::string::npos ||
            func.find("divide") != std::string::npos || func.find("divmmc") != std::string::npos ||
            func.find("sd") != std::string::npos || func.find("fdc") != std::string::npos)
            return "storage";

        if (func.find("net") != std::string::npos)
            return "network";

        return "io";
    }

    QString joinReasons(const StateNode* here)
    {
        if (!here)
            return QString();
        const StateNode* reasons = here->find("reasons");
        if (!reasons)
            return QString();
        QStringList list;
        for (const StateNode& r : reasons->items)
            list << Q(Text(&r));
        return list.join("\n");
    }
}  // namespace

SlotConfigDialog::SlotConfigDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Configure Slot"));
    setMinimumWidth(500);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    buildUi();
}

void SlotConfigDialog::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    auto* currentGroup = new QGroupBox(tr("Current"), this);
    auto* currentLayout = new QHBoxLayout(currentGroup);
    _currentCardLabel = new QLabel(tr("(empty)"), currentGroup);
    _currentCardLabel->setStyleSheet("font-weight: bold;");
    currentLayout->addWidget(_currentCardLabel);
    currentLayout->addStretch(1);
    mainLayout->addWidget(currentGroup);

    auto* cardsGroup = new QGroupBox(tr("Available Cards"), this);
    auto* cardsLayout = new QVBoxLayout(cardsGroup);
    cardsLayout->setContentsMargins(4, 8, 4, 4);
    _cardTree = new QTreeWidget(cardsGroup);
    _cardTree->setHeaderHidden(true);
    _cardTree->setRootIsDecorated(true);
    _cardTree->setIndentation(16);
    _cardTree->setSelectionMode(QAbstractItemView::SingleSelection);
    _cardTree->setFixedHeight(250);
    cardsLayout->addWidget(_cardTree);
    mainLayout->addWidget(cardsGroup);

    _optionsGroup = new QGroupBox(tr("Options"), this);
    auto* optGroupLayout = new QVBoxLayout(_optionsGroup);
    optGroupLayout->setSpacing(8);

    auto* optHeader = new QHBoxLayout();
    _optionsCardLabel = new QLabel(_optionsGroup);
    _optionsCardLabel->setStyleSheet("font-weight: bold;");
    optHeader->addWidget(_optionsCardLabel);
    _optionsStatusLabel = new QLabel(_optionsGroup);
    _optionsStatusLabel->setStyleSheet("color: gray; font-style: italic;");
    optHeader->addWidget(_optionsStatusLabel);
    optHeader->addStretch(1);
    optGroupLayout->addLayout(optHeader);

    _optionsContainer = new QWidget(_optionsGroup);
    _optionsLayout = new QVBoxLayout(_optionsContainer);
    _optionsLayout->setContentsMargins(0, 0, 0, 0);
    _optionsLayout->setSpacing(6);
    optGroupLayout->addWidget(_optionsContainer);

    _optionsGroup->setVisible(false);
    mainLayout->addWidget(_optionsGroup);

    auto* buttonLayout = new QHBoxLayout();
    _clearButton = new QPushButton(tr("Clear Slot"), this);
    _clearButton->setToolTip(tr("Remove the card from this slot"));
    buttonLayout->addWidget(_clearButton);
    buttonLayout->addStretch(1);
    _cancelButton = new QPushButton(tr("Cancel"), this);
    _saveButton = new QPushButton(tr("Apply"), this);
    _saveButton->setDefault(true);
    buttonLayout->addWidget(_cancelButton);
    buttonLayout->addWidget(_saveButton);
    mainLayout->addLayout(buttonLayout);

    connect(_cardTree, &QTreeWidget::currentItemChanged, this, &SlotConfigDialog::onCardSelected);
    connect(_saveButton, &QPushButton::clicked, this, &SlotConfigDialog::onSave);
    connect(_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(_clearButton, &QPushButton::clicked, this, &SlotConfigDialog::onClearSlot);
}

void SlotConfigDialog::setSlotId(const std::string& slotId)
{
    _slotId = slotId;
    setWindowTitle(tr("Configure: %1").arg(Q(slotId)));
}

void SlotConfigDialog::setCatalog(const StateNode& catalog)
{
    _catalog = catalog;
    populateCardTree();
}

void SlotConfigDialog::populateCardTree()
{
    _cardTree->clear();
    _categoryItems.clear();

    struct CardEntry
    {
        std::string id;
        std::string name;
        std::string description;
        std::string category;
        std::string outcome;
        QString reasons;
        bool isCurrent = false;
    };

    std::vector<CardEntry> cards;
    for (const StateNode& card : _catalog.items)
    {
        const StateNode* here = card.find("thisMachine");
        const std::string outcome = here ? Text(here->find("outcome")) : "";
        if (outcome == "refused")
            continue;

        const StateNode* emulated = card.find("emulated");
        if (emulated && !emulated->b)
            continue;

        CardEntry entry;
        entry.id = Text(card.find("id"));
        entry.name = Text(card.find("name"));
        entry.description = Text(card.find("description"));
        entry.category = inferCategory(card);
        entry.outcome = outcome;
        entry.reasons = joinReasons(here);
        entry.isCurrent = (entry.id == _currentCardId);

        cards.push_back(entry);
    }

    std::sort(cards.begin(), cards.end(), [](const CardEntry& a, const CardEntry& b) {
        const int catA = categoryOrder(a.category);
        const int catB = categoryOrder(b.category);
        if (catA != catB)
            return catA < catB;
        return a.name < b.name;
    });

    for (const CardEntry& card : cards)
    {
        QTreeWidgetItem* catItem = nullptr;
        auto it = _categoryItems.find(card.category);
        if (it == _categoryItems.end())
        {
            catItem = new QTreeWidgetItem(_cardTree);
            catItem->setText(0, categoryName(card.category));
            catItem->setFlags(catItem->flags() & ~Qt::ItemIsSelectable);
            catItem->setExpanded(true);
            _categoryItems[card.category] = catItem;
        }
        else
        {
            catItem = it->second;
        }

        auto* item = new QTreeWidgetItem(catItem);
        QString display = Q(card.name);
        QString tooltip = Q(card.description);

        if (card.isCurrent)
        {
            display = QString::fromUtf8("✓ ") + display;
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        }
        else if (card.outcome == "needs-replace")
        {
            display = QString::fromUtf8("• ") + display;
            item->setForeground(0, QColor(100, 100, 100));
            if (!card.reasons.isEmpty())
                tooltip = card.reasons + (tooltip.isEmpty() ? "" : "\n\n" + tooltip);
        }

        item->setText(0, display);
        item->setData(0, Qt::UserRole, Q(card.id));
        item->setData(0, Qt::UserRole + 1, card.reasons);
        if (!tooltip.isEmpty())
            item->setToolTip(0, tooltip);
    }
}

void SlotConfigDialog::setCurrentCard(const std::string& cardId, const std::string& options)
{
    _currentCardId = cardId;
    _currentOptions = options;

    if (cardId.empty())
    {
        _currentCardLabel->setText(tr("(empty)"));
        _currentCardLabel->setStyleSheet("color: gray; font-style: italic;");
    }
    else
    {
        QString cardName = Q(cardId);
        for (const StateNode& card : _catalog.items)
        {
            if (Text(card.find("id")) == cardId)
            {
                cardName = Q(Text(card.find("name")));
                break;
            }
        }
        QString display = QString::fromUtf8("✓ ") + cardName;
        if (!options.empty())
            display += QString(" (%1)").arg(Q(options));
        _currentCardLabel->setText(display);
        _currentCardLabel->setStyleSheet("font-weight: bold; color: #2e7d32;");
    }

    populateCardTree();

    QTreeWidgetItemIterator iter(_cardTree);
    while (*iter)
    {
        if ((*iter)->data(0, Qt::UserRole).toString().toStdString() == cardId)
        {
            _cardTree->setCurrentItem(*iter);
            break;
        }
        ++iter;
    }

    rebuildOptionEditors();
}

void SlotConfigDialog::setConflictProvider(ConflictProvider provider)
{
    _conflictProvider = std::move(provider);
}

std::string SlotConfigDialog::selectedCard() const
{
    auto* item = _cardTree->currentItem();
    if (!item)
        return "";
    return item->data(0, Qt::UserRole).toString().toStdString();
}

std::string SlotConfigDialog::selectedOptions() const
{
    return formatOptions().toStdString();
}

SlotConfigDialog::ConflictInfo SlotConfigDialog::conflicts() const
{
    return _lastConflicts;
}

void SlotConfigDialog::onCardSelected()
{
    rebuildOptionEditors();
    updatePlan();
}

void SlotConfigDialog::rebuildOptionEditors()
{
    while (QLayoutItem* item = _optionsLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    _editors.clear();

    const std::string cardId = selectedCard();
    if (cardId.empty())
    {
        _optionsGroup->setVisible(false);
        adjustSize();
        return;
    }

    const StateNode* card = nullptr;
    for (const StateNode& c : _catalog.items)
    {
        if (Text(c.find("id")) == cardId)
        {
            card = &c;
            break;
        }
    }
    if (!card)
    {
        _optionsGroup->setVisible(false);
        adjustSize();
        return;
    }

    _optionsCardLabel->setText(Q(Text(card->find("name"))));

    const bool isCurrent = (cardId == _currentCardId);
    if (isCurrent)
    {
        _optionsStatusLabel->setText(tr("(current)"));
        _optionsStatusLabel->setStyleSheet("color: #2e7d32; font-style: italic;");
    }
    else
    {
        _optionsStatusLabel->setText(tr("(new)"));
        _optionsStatusLabel->setStyleSheet("color: #1565c0; font-style: italic;");
    }

    auto* treeItem = _cardTree->currentItem();
    const QString reasons = treeItem ? treeItem->data(0, Qt::UserRole + 1).toString() : QString();
    if (!reasons.isEmpty())
    {
        auto* reasonsLabel = new QLabel(reasons, _optionsContainer);
        reasonsLabel->setWordWrap(true);
        reasonsLabel->setStyleSheet("color: #666; font-size: 11px; padding: 4px; background: #f5f5f5; border-radius: 4px;");
        _optionsLayout->addWidget(reasonsLabel);
    }

    std::map<std::string, std::vector<std::string>> given;
    if (isCurrent)
        given = ParseOptions(_currentOptions);

    const StateNode* options = card->find("options");
    if (!options || options->items.empty())
    {
        if (reasons.isEmpty())
        {
            auto* noOptions = new QLabel(tr("No configurable options"), _optionsContainer);
            noOptions->setStyleSheet("color: gray;");
            _optionsLayout->addWidget(noOptions);
        }
        _optionsGroup->setVisible(true);
        adjustSize();
        return;
    }

    for (const StateNode& option : options->items)
    {
        OptionEditor editor;
        editor.name = Text(option.find("name"));
        editor.isSet = Text(option.find("kind")) == "set";

        std::vector<std::string> values;
        if (given.count(editor.name))
        {
            values = given[editor.name];
        }
        else
        {
            std::istringstream defaults(Text(option.find("default")));
            for (std::string v; std::getline(defaults, v, ',');)
                values.push_back(v);
        }

        auto* row = new QWidget(_optionsContainer);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 2, 0, 2);

        auto* label = new QLabel(Q(editor.name) + ":", row);
        label->setToolTip(Q(Text(option.find("description"))));
        label->setMinimumWidth(80);
        rowLayout->addWidget(label);

        const StateNode* valuesNode = option.find("values");
        if (editor.isSet && valuesNode)
        {
            for (const StateNode& val : valuesNode->items)
            {
                const std::string id = Text(val.find("id"));
                auto* box = new QCheckBox(Q(id), row);
                box->setChecked(std::find(values.begin(), values.end(), id) != values.end());
                connect(box, &QCheckBox::toggled, this, &SlotConfigDialog::updatePlan);
                rowLayout->addWidget(box);
                editor.boxes.emplace_back(id, box);
            }
        }
        else if (valuesNode)
        {
            editor.combo = new QComboBox(row);
            for (const StateNode& val : valuesNode->items)
                editor.combo->addItem(Q(Text(val.find("label"))).remove('`'), Q(Text(val.find("id"))));
            if (!values.empty())
            {
                int idx = editor.combo->findData(Q(values.front()));
                if (idx >= 0)
                    editor.combo->setCurrentIndex(idx);
            }
            connect(editor.combo, &QComboBox::currentIndexChanged, this, &SlotConfigDialog::updatePlan);
            rowLayout->addWidget(editor.combo);
        }

        rowLayout->addStretch(1);
        _optionsLayout->addWidget(row);
        _editors.push_back(std::move(editor));
    }

    _optionsGroup->setVisible(true);
    adjustSize();
}

QString SlotConfigDialog::formatOptions() const
{
    QStringList parts;
    for (const OptionEditor& ed : _editors)
    {
        QStringList values;
        if (ed.isSet)
        {
            for (const auto& [id, box] : ed.boxes)
            {
                if (box->isChecked())
                    values << Q(id);
            }
        }
        else if (ed.combo)
        {
            values << ed.combo->currentData().toString();
        }
        parts << Q(ed.name) + "=" + (values.isEmpty() ? "none" : values.join(","));
    }
    return parts.join(" ");
}

void SlotConfigDialog::updatePlan()
{
    _lastConflicts = {};
    const std::string cardId = selectedCard();
    if (cardId.empty() || !_conflictProvider)
        return;

    _lastConflicts = _conflictProvider(cardId);
}

void SlotConfigDialog::onClearSlot()
{
    emit accepted(_slotId, "", "");
    accept();
}

void SlotConfigDialog::onSave()
{
    const std::string cardId = selectedCard();
    const std::string options = selectedOptions();

    if (!_lastConflicts.disabledBuiltIns.empty() || !_lastConflicts.removedCards.empty())
    {
        QStringList warnings;
        for (const auto& b : _lastConflicts.disabledBuiltIns)
            warnings << tr("• %1 (built-in) will be disabled").arg(Q(b));
        for (const auto& c : _lastConflicts.removedCards)
            warnings << tr("• %1 will be removed").arg(Q(c));

        QString msg = tr("Installing this card will:\n%1").arg(warnings.join("\n"));
        if (_lastConflicts.needsRestart)
            msg += tr("\n\nThe machine will restart.");

        QMessageBox::StandardButton result = QMessageBox::warning(
            this, tr("Confirm Changes"), msg, QMessageBox::Cancel | QMessageBox::Ok, QMessageBox::Cancel);

        if (result != QMessageBox::Ok)
            return;
    }

    emit accepted(_slotId, cardId, options);
    accept();
}
