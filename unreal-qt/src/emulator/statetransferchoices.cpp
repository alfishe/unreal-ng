#include "emulator/statetransferchoices.h"

#include <QAction>
#include <QCoreApplication>
#include <QMenu>

#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

namespace StateTransferChoices
{
namespace
{
QString Tr(const char* text)
{
    return QCoreApplication::translate("StateTransferChoices", text);
}
}  // namespace

std::vector<Target> OpenInstances(const std::string& sourceId)
{
    std::vector<Target> out;
    EmulatorManager* manager = EmulatorManager::GetInstance();
    for (const std::string& id : manager->GetEmulatorIds())
    {
        if (id == sourceId)
            continue;
        std::shared_ptr<Emulator> emulator = manager->GetEmulator(id);
        if (!emulator || emulator->IsDestroying())
            continue;
        const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);
        const std::string symbolic = emulator->GetSymbolicId();
        QString label = QString::fromStdString(symbolic.empty() ? id.substr(0, 8) : symbolic);
        if (identity.Valid)
            label += QString(" - %1 %2K").arg(QString::fromStdString(identity.ModelFullName)).arg(identity.RamKb);
        if (!symbolic.empty())
            label += QString(" (%1)").arg(QString::fromStdString(id.substr(0, 8)));
        out.push_back({id, label});
    }
    return out;
}

std::vector<Model> Models()
{
    std::vector<Model> out;
    static const uint32_t kRamSizes[] = {48, 128, 256, 512, 1024, 2048, 4096};
    for (const TMemModel& model : EmulatorManager::GetInstance()->GetAvailableModels())
    {
        const QString name = QString::fromUtf8(model.FullName);
        if (!Config::IsModelCreatable(model) || name.isEmpty() || model.ShortName == nullptr || !*model.ShortName)
            continue;
        std::vector<uint32_t> sizes;
        for (uint32_t ram : kRamSizes)
            if (model.AvailRAMs & ram)
                sizes.push_back(ram);
        for (uint32_t ram : sizes)
            out.push_back({model.ShortName, sizes.size() == 1 ? model.defaultRAM : ram,
                           sizes.size() == 1 ? name : QString("%1 %2K").arg(name).arg(ram)});
    }
    return out;
}

void Fill(QMenu* menu, const std::string& sourceId, const std::function<void(const StateTransferRequest&)>& pick)
{
    menu->clear();
    if (sourceId.empty())
    {
        menu->addAction(Tr("(no machine)"))->setEnabled(false);
        return;
    }
    const std::vector<Target> targets = OpenInstances(sourceId);
    if (targets.empty())
        menu->addAction(Tr("(no other instance open)"))->setEnabled(false);
    for (const Target& target : targets)
    {
        QAction* action = menu->addAction(target.label);
        action->setStatusTip(Tr("Check, then move this machine's running state into %1").arg(target.label));
        action->setData(QString::fromStdString(target.id));
        QObject::connect(action, &QAction::triggered, menu, [pick, sourceId, id = target.id]() {
            StateTransferRequest request;
            request.sourceId = sourceId;
            request.to = id;
            pick(request);
        });
    }
    menu->addSeparator();
    QMenu* fresh = menu->addMenu(Tr("&New Machine"));
    fresh->setStatusTip(Tr("Move this machine's running state into a new machine of the chosen model"));
    for (const Model& model : Models())
    {
        QAction* action = fresh->addAction(model.label);
        action->setData(QString("%1:%2").arg(QString::fromStdString(model.shortName)).arg(model.ramKb));
        QObject::connect(action, &QAction::triggered, menu, [pick, sourceId, model]() {
            StateTransferRequest request;
            request.sourceId = sourceId;
            request.model = model.shortName;
            request.ramSizeKB = model.ramKb;
            pick(request);
        });
    }
}

QString ReportText(const StateTransferReply& reply)
{
    return QString::fromStdString(reply.ToText()).trimmed();
}
}  // namespace StateTransferChoices
