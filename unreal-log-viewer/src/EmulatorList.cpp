#include "EmulatorList.h"

#include <QJsonObject>
#include <QDebug>

EmulatorList::EmulatorList(QWidget* parent)
    : QListWidget(parent)
{
    setSelectionMode(QAbstractItemView::SingleSelection);
    setAlternatingRowColors(true);

    connect(this, &QListWidget::itemSelectionChanged,
            this, &EmulatorList::onSelectionChanged);
}

void EmulatorList::updateEmulatorList(const QJsonArray& emulators)
{
    QString previousSelection = _lastSelectedId;

    blockSignals(true);
    clear();

    for (const QJsonValue& value : emulators)
    {
        QJsonObject emu = value.toObject();
        QString id = emu.value("id").toString();
        QString model = emu.value("model").toString();
        QString state = emu.value("state").toString();

        QJsonObject features = emu.value("features").toObject();
        bool shmActive = features.value("sharedmemory").toBool(false);

        addEmulatorItem(id, model, state, shmActive);
    }

    bool selectionRestored = false;
    if (!previousSelection.isEmpty())
    {
        for (int i = 0; i < count(); ++i)
        {
            QListWidgetItem* itm = this->item(i);
            if (itm->data(Qt::UserRole).toString() == previousSelection)
            {
                setCurrentItem(itm);
                selectionRestored = true;
                break;
            }
        }
    }

    blockSignals(false);

    if (count() == 0)
    {
        if (!_lastSelectedId.isEmpty())
        {
            _lastSelectedId.clear();
            emit emulatorDeselected();
        }
        return;
    }

    if (count() == 1 && !selectionRestored)
    {
        setCurrentRow(0);
    }
    else if (!selectionRestored && !previousSelection.isEmpty())
    {
        _lastSelectedId.clear();
        emit emulatorDeselected();
    }
}

void EmulatorList::addEmulatorItem(const QString& id, const QString& model,
                                    const QString& state, bool shmActive)
{
    QString shortId = id.left(8);

    QString secondLine;
    if (!model.isEmpty() && model.toLower() != "unknown")
        secondLine = QString("%1 | %2").arg(model, state);
    else
        secondLine = state;

    QString displayText = QString("%1 %2\n%3")
        .arg(shmActive ? "S" : "o")
        .arg(shortId)
        .arg(secondLine);

    QListWidgetItem* item = new QListWidgetItem(displayText, this);
    item->setData(Qt::UserRole, id);

    QFont font = item->font();
    font.setFamily("Menlo, Monaco, Consolas, monospace");
    item->setFont(font);
}

void EmulatorList::onSelectionChanged()
{
    QList<QListWidgetItem*> selected = selectedItems();

    if (selected.isEmpty())
    {
        if (!_lastSelectedId.isEmpty())
        {
            _lastSelectedId.clear();
            emit emulatorDeselected();
        }
        return;
    }

    QString emulatorId = selected.first()->data(Qt::UserRole).toString();

    if (emulatorId != _lastSelectedId)
    {
        _lastSelectedId = emulatorId;
        emit emulatorSelected(emulatorId);
    }
}
