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
    // Remember current selection - don't clear _lastSelectedId
    QString previousSelection = _lastSelectedId;
    
    // Block signals during list update to prevent spurious selection events
    blockSignals(true);
    
    // Clear and rebuild list
    clear();
    
    for (const QJsonValue& value : emulators)
    {
        QJsonObject emu = value.toObject();
        QString id = emu.value("id").toString();
        QString model = emu.value("model").toString();
        QString state = emu.value("state").toString();
        
        // Check if shared memory is active
        QJsonObject features = emu.value("features").toObject();
        bool shmActive = features.value("sharedmemory").toBool(false);
        
        addEmulatorItem(id, model, state, shmActive);
    }
    
    // Restore previous selection if still present (without triggering signals)
    bool selectionRestored = false;
    if (!previousSelection.isEmpty())
    {
        for (int i = 0; i < count(); ++i)
        {
            QListWidgetItem* item = this->item(i);
            if (item->data(Qt::UserRole).toString() == previousSelection)
            {
                setCurrentItem(item);
                selectionRestored = true;
                break;
            }
        }
    }
    
    // Re-enable signals
    blockSignals(false);
    
    // Auto-select if only one emulator is available AND no previous selection
    if (count() == 1 && !selectionRestored && previousSelection.isEmpty())
    {
        setCurrentRow(0);  // This will trigger onSelectionChanged
        qDebug() << "EmulatorList: Auto-selecting single emulator";
    }
}

void EmulatorList::addEmulatorItem(const QString& id, const QString& model, 
                                    const QString& state, bool shmActive)
{
    // Use short ID as primary identifier
    QString shortId = id.left(8);
    
    // Only include model if it's meaningful (not "unknown")
    QString secondLine;
    if (!model.isEmpty() && model.toLower() != "unknown")
    {
        secondLine = QString("%1 • %2").arg(model, state);
    }
    else
    {
        secondLine = state;
    }
    
    QString displayText = QString("%1 %2\n%3")
        .arg(shmActive ? "●" : "○")
        .arg(shortId)
        .arg(secondLine);
    
    QListWidgetItem* item = new QListWidgetItem(displayText, this);
    item->setData(Qt::UserRole, id);  // Store full ID in item data
    
    // Visual styling
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
        qDebug() << "EmulatorList: Selected emulator:" << emulatorId;
        emit emulatorSelected(emulatorId);
    }
}
