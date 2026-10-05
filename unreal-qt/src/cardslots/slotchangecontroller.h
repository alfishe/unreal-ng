/**
 * @file slotchangecontroller.h
 * @brief The Qt side of a slot change (ZX-bus slots SL-7, architecture.md §9): plans the change, asks for the
 *        confirmation the restart needs, applies it, then names the cards it removed with an Undo.
 *
 * Owner decisions: Q1 - the GUI replaces incompatible cards (it plans with replaceIfIncompatible) and warns naming
 * every card it removed, with Undo; Q6 - every change is applied by a restart of the machine (a new emulator id, the
 * machine state lost, the media kept). The core does the work (SlotManager::PlanChange, SlotChange::Run); this class
 * only asks and shows. The slot window, the audio settings' General Sound choice and the Undo all come through here.
 *
 * Questions go through the `ask` hook (a QMessageBox by default) so a test can answer them.
 */

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <QObject>
#include <QString>
#include <QStringList>

#include "emulator/slots/slotchange.h"

class Emulator;
class QWidget;

// Qt defines `slots` as a macro; this header names the slots namespace (no `public slots:` section here)
#pragma push_macro("slots")
#undef slots

class SlotChangeController : public QObject
{
    Q_OBJECT

public:
    struct Hooks
    {
        /// The old machine stopped, before it goes: the window unbinds its views
        std::function<void(Emulator& old)> beforeRelease;
        /// The restarted machine: the window adopts it and starts it when the old one ran
        std::function<void(std::shared_ptr<Emulator> emulator, bool start)> adopt;
        /// A restart is in progress (the window ignores the instance notifications meanwhile)
        std::function<void(bool on)> restarting;
        /// A question with buttons: returns the index of the button clicked, -1 for none (closed). Default: QMessageBox
        std::function<int(QWidget* parent, const QString& title, const QString& text, const QStringList& buttons)> ask;
    };

    explicit SlotChangeController(Hooks hooks, QObject* parent = nullptr);

    /// Plans `change` for the instance (with replaceIfIncompatible: the GUI replaces, Q1), asks to restart the machine
    /// (naming what the plan removes; unsaved media of a removed card: save / discard), applies it and then names the
    /// removed cards with an Undo button. False when refused, cancelled or failed (a message says why)
    bool Apply(const std::string& emulatorId, slots::SlotRequest change, QWidget* parent);

    /// The General Sound personality (owner decision Q10): the card in the GS slot replaced, the same flow
    bool ApplyGeneralSound(const std::string& emulatorId, int gsTypeKind, QWidget* parent);

    /// Puts the slot set from before the last applied change back (another restart). False when there is nothing to
    /// undo or it is refused
    bool Undo(QWidget* parent);
    bool CanUndo() const
    {
        return _undo.has_value();
    }
    /// The last change in words, for the Undo button's tooltip
    QString UndoText() const
    {
        return _undoText;
    }

    /// The plan of a change as lines for people (the slot window's preview): the refusal first when there is one
    static QStringList PlanText(const SlotManager::ChangePlan& plan);

signals:
    /// A change (or an undo) was applied: the new emulator id
    void applied(const QString& emulatorId);
    void undoAvailable(bool available);

private:
    SlotChangeResult Run(SlotChangeRequest request);
    int Ask(QWidget* parent, const QString& title, const QString& text, const QStringList& buttons);

    Hooks _hooks;
    std::optional<SlotConfig> _undo;   ///< the slot set before the last change
    std::string _undoEmulatorId;       ///< the machine the last change created
    QString _undoText;
};

#pragma pop_macro("slots")
