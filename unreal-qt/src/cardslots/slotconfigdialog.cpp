/**
 * @file slotconfigdialog.cpp
 * @brief SlotConfigDialog implementation.
 */

#include "slotconfigdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
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
}  // namespace

SlotConfigDialog::SlotConfigDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Configure Slot"));
    setMinimumSize(600, 400);
    buildUi();
}

void SlotConfigDialog::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(splitter, 1);

    auto* leftPanel = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    auto* leftHeader = new QLabel(tr("Compatible Cards"), leftPanel);
    leftHeader->setStyleSheet("font-weight: bold;");
    leftLayout->addWidget(leftHeader);

    _cardList = new QListWidget(leftPanel);
    _cardList->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(_cardList, 1);

    auto* rightPanel = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    _selectedCardLabel = new QLabel(tr("No card selected"), rightPanel);
    _selectedCardLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    rightLayout->addWidget(_selectedCardLabel);

    _cardDescription = new QLabel(rightPanel);
    _cardDescription->setWordWrap(true);
    _cardDescription->setStyleSheet("color: gray;");
    rightLayout->addWidget(_cardDescription);

    rightLayout->addSpacing(10);

    auto* optionsHeader = new QLabel(tr("Options"), rightPanel);
    optionsHeader->setStyleSheet("font-weight: bold;");
    rightLayout->addWidget(optionsHeader);

    _optionsContainer = new QWidget(rightPanel);
    _optionsLayout = new QVBoxLayout(_optionsContainer);
    _optionsLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(_optionsContainer);

    rightLayout->addStretch(1);

    splitter->setSizes({200, 400});

    auto* buttonLayout = new QHBoxLayout();
    _clearButton = new QPushButton(tr("Clear Slot"), this);
    _clearButton->setToolTip(tr("Remove the card from this slot"));
    buttonLayout->addWidget(_clearButton);
    buttonLayout->addStretch(1);
    _cancelButton = new QPushButton(tr("Cancel"), this);
    _saveButton = new QPushButton(tr("Save"), this);
    _saveButton->setDefault(true);
    buttonLayout->addWidget(_cancelButton);
    buttonLayout->addWidget(_saveButton);
    mainLayout->addLayout(buttonLayout);

    connect(_cardList, &QListWidget::currentItemChanged, this, &SlotConfigDialog::onCardSelected);
    connect(_saveButton, &QPushButton::clicked, this, &SlotConfigDialog::onSave);
    connect(_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(_clearButton, &QPushButton::clicked, this, [this]() {
        _cardList->clearSelection();
        rebuildOptionEditors();
        emit accepted(_slotId, "", "");
        accept();
    });
}

void SlotConfigDialog::setSlotId(const std::string& slotId)
{
    _slotId = slotId;
    setWindowTitle(tr("Configure: %1").arg(Q(slotId)));
}

void SlotConfigDialog::setCatalog(const StateNode& catalog)
{
    _catalog = catalog;
    _cardList->clear();

    for (const StateNode& card : catalog.items)
    {
        const StateNode* here = card.find("thisMachine");
        const std::string outcome = here ? Text(here->find("outcome")) : "";
        if (outcome == "refused")
            continue;

        const StateNode* emulated = card.find("emulated");
        if (emulated && !emulated->b)
            continue;

        const std::string id = Text(card.find("id"));
        const std::string name = Text(card.find("name"));

        auto* item = new QListWidgetItem(_cardList);
        item->setText(Q(name));
        item->setData(Qt::UserRole, Q(id));
        item->setToolTip(Q(Text(card.find("description"))));

        if (outcome == "needs-replace")
        {
            item->setText(Q(name) + tr(" (replaces)"));
            item->setForeground(QColor(180, 120, 0));
        }
    }
}

void SlotConfigDialog::setCurrentCard(const std::string& cardId, const std::string& options)
{
    _currentCardId = cardId;
    _currentOptions = options;

    for (int i = 0; i < _cardList->count(); ++i)
    {
        if (_cardList->item(i)->data(Qt::UserRole).toString().toStdString() == cardId)
        {
            _cardList->setCurrentRow(i);
            break;
        }
    }
    rebuildOptionEditors();
}

void SlotConfigDialog::setConflictProvider(ConflictProvider provider)
{
    _conflictProvider = std::move(provider);
}

std::string SlotConfigDialog::selectedCard() const
{
    auto* item = _cardList->currentItem();
    return item ? item->data(Qt::UserRole).toString().toStdString() : "";
}

std::string SlotConfigDialog::selectedOptions() const
{
    return formatOptions().toStdString();
}

SlotConfigDialog::ConflictInfo SlotConfigDialog::conflicts() const
{
    return _lastConflicts;
}

void SlotConfigDialog::onCardSelected(QListWidgetItem* current, QListWidgetItem* /*previous*/)
{
    if (!current)
    {
        _selectedCardLabel->setText(tr("No card selected"));
        _cardDescription->clear();
        rebuildOptionEditors();
        return;
    }

    const std::string cardId = current->data(Qt::UserRole).toString().toStdString();

    for (const StateNode& card : _catalog.items)
    {
        if (Text(card.find("id")) == cardId)
        {
            _selectedCardLabel->setText(Q(Text(card.find("name"))));
            _cardDescription->setText(Q(Text(card.find("description"))));
            break;
        }
    }

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
        return;

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
        return;

    std::map<std::string, std::vector<std::string>> given;
    if (cardId == _currentCardId)
        given = ParseOptions(_currentOptions);

    const StateNode* options = card->find("options");
    if (!options)
        return;

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

        auto* label = new QLabel(Q(editor.name), row);
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

void SlotConfigDialog::onSave()
{
    const std::string cardId = selectedCard();
    const std::string options = selectedOptions();

    if (!_lastConflicts.disabledBuiltIns.empty() || !_lastConflicts.removedCards.empty())
    {
        QStringList warnings;
        for (const auto& b : _lastConflicts.disabledBuiltIns)
            warnings << tr("• %1 (built-in) — disabled").arg(Q(b));
        for (const auto& c : _lastConflicts.removedCards)
            warnings << tr("• %1 — removed").arg(Q(c));

        QString msg = tr("This will replace:\n%1").arg(warnings.join("\n"));
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
