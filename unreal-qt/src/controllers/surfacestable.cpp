#include "surfacestable.h"

#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "surfacetargets.h"

QString SurfaceInstanceLabel(const std::string& instanceId)
{
    if (instanceId.empty())
        return QObject::tr("no machine");
    EmulatorManager* manager = EmulatorManager::GetInstance();
    const std::vector<std::string> ids = manager->GetEmulatorIds();
    const auto at = std::find(ids.begin(), ids.end(), instanceId);
    std::shared_ptr<Emulator> emulator = manager->GetEmulator(instanceId);
    QString label = at != ids.end() ? QStringLiteral("#%1").arg(at - ids.begin()) : QStringLiteral("#?");
    if (emulator)
    {
        const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);
        if (identity.Valid)
            label += QStringLiteral(" %1 %2K").arg(QString::fromStdString(identity.Model)).arg(identity.RamKb);
    }
    return label;
}

SurfacesTable::SurfacesTable(SurfaceTargets* targets, QWidget* parent) : QWidget(parent), _targets(targets)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(tr("Control surfaces"), this);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    layout->addWidget(title);
    _empty = new QLabel(tr("No surface is connected (a Stream Deck joins through the WebAPI)."), this);
    layout->addWidget(_empty);
    _table = new QTableWidget(0, 4, this);
    _table->setHorizontalHeaderLabels({tr("Surface"), tr("Kind"), tr("Drives"), tr("Target")});
    _table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->verticalHeader()->hide();
    _table->setSelectionMode(QAbstractItemView::NoSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setMaximumHeight(140);
    layout->addWidget(_table);
    if (_targets)
        connect(_targets, &SurfaceTargets::changed, this, &SurfacesTable::refresh);
    refresh();
}

void SurfacesTable::refresh()
{
    if (!_targets)
        return;
    const std::vector<actions::SurfaceSessionInfo> sessions = _targets->sessions();
    const std::vector<actions::ViewInfo> views = _targets->views();
    const std::vector<std::string> instances = EmulatorManager::GetInstance()->GetEmulatorIds();
    _empty->setVisible(sessions.empty());
    _table->setVisible(!sessions.empty());
    _table->setRowCount(static_cast<int>(sessions.size()));
    for (int row = 0; row < static_cast<int>(sessions.size()); row++)
    {
        const actions::SurfaceSessionInfo& session = sessions[static_cast<size_t>(row)];
        auto* name = new QTableWidgetItem(QString::fromStdString(session.spec.name));
        if (!session.notice.empty())
            name->setToolTip(QString::fromStdString(session.notice));
        _table->setItem(row, 0, name);
        _table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(session.spec.kind)));
        QString drives = SurfaceInstanceLabel(session.target.instance);
        if (!session.target.view.empty())
            drives += QStringLiteral(" - %1").arg(QString::fromStdString(session.target.viewName));
        _table->setItem(row, 2, new QTableWidgetItem(drives));

        auto* combo = new QComboBox(_table);
        combo->addItem(tr("Follow the focus"), QStringLiteral("follow"));
        for (const actions::ViewInfo& view : views)
            combo->addItem(tr("Window: %1").arg(QString::fromStdString(view.name)),
                           QString::fromStdString(actions::Pin::OnView(view.id).ToText()));
        for (const std::string& instance : instances)
            combo->addItem(tr("Instance: %1").arg(SurfaceInstanceLabel(instance)),
                           QString::fromStdString(actions::Pin::OnInstance(instance).ToText()));
        const int current = combo->findData(QString::fromStdString(session.pin.ToText()));
        combo->setCurrentIndex(current >= 0 ? current : 0);
        const QString id = QString::fromStdString(session.id);
        connect(combo, &QComboBox::activated, this, [this, combo, id](int index) {
            if (!_targets)
                return;
            actions::Pin pin;
            std::string error;
            if (!actions::Pin::FromText(combo->itemData(index).toString().toStdString(), pin, error))
                return;
            const QString why = _targets->pin(id, pin);
            if (!why.isEmpty())
                combo->setToolTip(why);
        });
        _table->setCellWidget(row, 3, combo);
    }
}
