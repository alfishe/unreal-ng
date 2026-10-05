/**
 * @file slotchangecontroller.cpp
 * @brief Plan, confirm, apply (a restart), warn with Undo - see slotchangecontroller.h.
 */

#include "slotchangecontroller.h"

#include <QMessageBox>
#include <QPushButton>
#include <QWidget>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

// Qt defines `slots` as a macro; the code below names the slots namespace
#undef slots

namespace
{
    QString Q(const std::string& text)
    {
        return QString::fromStdString(text);
    }

    /// "zxbus.1 = gs [ram=128k rom=1.05]" per removed card, one per line
    QString RemovedText(const slots::SlotPlan& plan)
    {
        QStringList lines;
        for (const slots::RemovedCard& card : plan.removed)
        {
            lines << QString("%1 = %2%3").arg(Q(card.slot), Q(card.card),
                                              card.optionsText.empty() ? QString() : QString(" [%1]").arg(Q(card.optionsText)));
        }
        for (const slots::RemovedFromSocket& chip : plan.removedFromSocket)
            lines << QObject::tr("the %1 out of %2").arg(Q(chip.chip), Q(chip.socket));
        for (const slots::SwitchedOffBuiltIn& off : plan.builtInSwitchedOff)
            lines << QObject::tr("the built-in %1 switched off").arg(Q(off.builtIn));
        return lines.join("\n");
    }
}  // namespace

SlotChangeController::SlotChangeController(Hooks hooks, QObject* parent) : QObject(parent), _hooks(std::move(hooks))
{
}

int SlotChangeController::Ask(QWidget* parent, const QString& title, const QString& text, const QStringList& buttons)
{
    if (_hooks.ask)
        return _hooks.ask(parent, title, text, buttons);
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, parent);
    QList<QAbstractButton*> added;
    for (int i = 0; i < buttons.size(); i++)
        added << box.addButton(buttons[i], i == 0 ? QMessageBox::AcceptRole : QMessageBox::RejectRole);
    box.exec();
    return added.indexOf(box.clickedButton());
}

QStringList SlotChangeController::PlanText(const SlotManager::ChangePlan& plan)
{
    QStringList lines;
    if (!plan.refusal.empty())
        lines << Q(plan.refusal);
    for (const std::string& line : SlotControl::PlanLines(plan.plan))
        lines << Q(line);
    return lines;
}

SlotChangeResult SlotChangeController::Run(SlotChangeRequest request)
{
    request.beforeRelease = _hooks.beforeRelease;
    if (_hooks.restarting)
        _hooks.restarting(true);
    SlotChangeResult result = SlotChange::Run(request);
    if (_hooks.restarting)
        _hooks.restarting(false);
    if (result.Applied() && _hooks.adopt)
        _hooks.adopt(result.emulator, result.wasRunning);
    return result;
}

bool SlotChangeController::Apply(const std::string& emulatorId, slots::SlotRequest change, QWidget* parent)
{
    return ApplyChanges(emulatorId, {std::move(change)}, parent);
}

bool SlotChangeController::ApplyChanges(const std::string& emulatorId, std::vector<slots::SlotRequest> changes,
                                        QWidget* parent)
{
    if (changes.empty())
        return true;
    const QString title = tr("Change the slots");
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(emulatorId);
    SlotManager* manager = emulator && emulator->GetContext() ? emulator->GetContext()->pSlotManager : nullptr;
    if (!manager)
    {
        Ask(parent, title, tr("No machine with a slot set"), {tr("OK")});
        return false;
    }

    // The GUI replaces what does not fit (Q1) and names it afterwards
    for (slots::SlotRequest& change : changes)
    {
        change.replaceIfIncompatible = true;
        change.dryRun = false;
    }
    const SlotManager::Result before = manager->Snapshot();
    SlotManager::ChangePlan plan = manager->PlanChanges(changes);

    // A removed card's medium with unsaved writes needs a decision (R-OP-6)
    bool dirty = false;
    for (const slots::MediaRelease& media : plan.plan.media)
        dirty = dirty || (media.dirty && media.disposition == slots::MediaDisposition::None);
    if (!plan.Allowed() && dirty && !plan.recording)
    {
        QStringList media;
        for (const slots::MediaRelease& release : plan.plan.media)
        {
            if (release.dirty)
                media << QString("%1 (%2 = %3)").arg(Q(release.mediaSlot), Q(release.slot), Q(release.card));
        }
        const int choice = Ask(parent, title,
                               tr("The card that goes holds media with unsaved writes:\n\n%1\n\nSave them into their "
                                  "files, or discard them?")
                                   .arg(media.join("\n")),
                               {tr("Save"), tr("Discard"), tr("Cancel")});
        if (choice != 0 && choice != 1)
            return false;
        for (slots::SlotRequest& change : changes)
            change.mediaDisposition = choice == 0 ? slots::MediaDisposition::Save : slots::MediaDisposition::Discard;
        plan = manager->PlanChanges(changes);
    }
    if (!plan.Allowed())
    {
        Ask(parent, title, Q(plan.refusal), {tr("OK")});
        return false;
    }

    const QStringList lines = PlanText(plan);
    QString text = tr("%1\n\nThe machine restarts with the new slot set: its state is lost; disks, tapes and the "
                      "cards' media stay in.")
                       .arg(lines.isEmpty() ? QString() : lines.first());
    const QString removed = RemovedText(plan.plan);
    if (!removed.isEmpty())
        text += tr("\n\nThis removes:\n%1").arg(removed);
    if (Ask(parent, title, text, {tr("Restart"), tr("Cancel")}) != 0)
        return false;

    manager = nullptr;
    emulator.reset();   // nothing here may keep the old machine alive across the restart
    SlotChangeRequest request;
    request.emulatorId = emulatorId;
    request.change = changes.front();   // the flags (dry run, media disposition)
    if (changes.size() > 1)
        request.changes = changes;
    const SlotChangeResult result = Run(request);
    if (!result.Applied())
    {
        Ask(parent, title, tr("The slots did not change: %1").arg(Q(result.message)), {tr("OK")});
        return false;
    }

    _undo = SlotManager::ConfigOf(before);
    _undoEmulatorId = result.emulator->GetId();
    _lastEmulatorId = _undoEmulatorId;
    _undoText = lines.isEmpty() ? tr("the last slot change") : lines.first();
    emit applied(Q(_undoEmulatorId));
    emit undoAvailable(true);

    // Q1: say what went, with Undo
    if (!removed.isEmpty() &&
        Ask(parent, tr("Cards removed"), tr("The change removed:\n%1").arg(removed), {tr("OK"), tr("Undo")}) == 1)
    {
        Undo(parent);
    }
    return true;
}

bool SlotChangeController::ApplyGeneralSound(const std::string& emulatorId, int gsTypeKind, QWidget* parent)
{
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(emulatorId);
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pSlotManager)
        return false;
    slots::SlotRequest change;
    std::string why;
    if (!SlotManager::GeneralSoundRequest(context->pSlotManager->Snapshot(), context->config,
                                          static_cast<GSTypeKind>(gsTypeKind), change, &why))
    {
        Ask(parent, tr("General Sound card"), Q(why), {tr("OK")});
        return false;
    }
    context = nullptr;
    emulator.reset();
    return Apply(emulatorId, change, parent);
}

bool SlotChangeController::ApplyNetworkCards(const std::string& emulatorId, uint8_t zxBusCards, QWidget* parent)
{
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(emulatorId);
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!context || !context->pSlotManager)
        return false;
    std::vector<slots::SlotRequest> changes;
    std::string why;
    if (!SlotManager::NetworkRequests(context->pSlotManager->Snapshot(), zxBusCards, changes, &why))
    {
        Ask(parent, tr("Network cards"), Q(why), {tr("OK")});
        return false;
    }
    context = nullptr;
    emulator.reset();
    if (changes.empty())
    {
        _lastEmulatorId = emulatorId;   // nothing to change: no restart
        return true;
    }
    return ApplyChanges(emulatorId, std::move(changes), parent);
}

bool SlotChangeController::Undo(QWidget* parent)
{
    if (!_undo)
        return false;
    const QString title = tr("Undo the slot change");
    if (!EmulatorManager::GetInstance()->HasEmulator(_undoEmulatorId))
    {
        Ask(parent, title, tr("The machine of that change is gone: nothing to undo"), {tr("OK")});
        _undo.reset();
        emit undoAvailable(false);
        return false;
    }
    SlotChangeRequest request;
    request.emulatorId = _undoEmulatorId;
    request.slotSet = *_undo;
    const SlotChangeResult result = Run(request);
    if (!result.Applied())
    {
        Ask(parent, title, tr("Not undone: %1").arg(Q(result.message)), {tr("OK")});
        return false;
    }
    _undo.reset();
    _undoText.clear();
    _lastEmulatorId = result.emulator->GetId();
    emit applied(Q(result.emulator->GetId()));
    emit undoAvailable(false);
    return true;
}
