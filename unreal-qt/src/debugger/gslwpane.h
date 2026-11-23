#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;
class QFormLayout;
class QLabel;
class QProgressBar;
class QTableWidget;

/// @file gslwpane.h
/// @brief The lightweight General Sound player's read-only inspector panes (gui-card-debugger.md A10, gs-debugger
/// requirements W1-W5): the player has no CPU, so instead of registers and disassembly the debugger shows its command
/// interpreter and its MOD player, each from the `gs` report every plane reads (`interpreter`, `player`). No device code
/// here: the pane draws the report. It never reads by itself: its owner calls refresh with a context it holds a lease on;
/// a paused machine shows the same report on every refresh (the view freezes).
class GsLwPane : public QWidget
{
    Q_OBJECT

public:
    enum class View
    {
        Interpreter,  ///< the state diagram, the command, the byte counts, the two queues, the upload
        Player,       ///< the song position, the four channels, the module row, the samples
    };

    explicit GsLwPane(View view, QWidget* parent = nullptr);

    /// Build the `gs` report on `context` and show its part; `live`: the machine runs (no difference here)
    void refresh(EmulatorContext* context, bool live);
    /// Show `part` (the report's `interpreter` or `player`); null: `why` in the status line
    void display(const StateNode* part, const QString& why);

    /// The interpreter's states in the diagram's order (the report's `state` values)
    static const QStringList& States();

    /// region <For the tests>
    View view() const { return _view; }
    QString status() const;
    /// Interpreter: the lit state ("upload"); empty when none
    QString litState() const;
    /// Interpreter: the ready line ("Upload, 2,000 of 3,196 bytes"); Player: the song line
    QString headline() const;
    /// The shown text of field `name` ("Command", "Params queue", "Position")
    QString field(const QString& name) const;
    QProgressBar* uploadBar() const { return _upload; }
    QProgressBar* paramsBar() const { return _params; }
    QProgressBar* repliesBar() const { return _replies; }
    QTableWidget* channels() const { return _channels; }
    QTableWidget* modules() const { return _modules; }
    QTableWidget* samples() const { return _samples; }
    /// endregion

private:
    void BuildInterpreter();
    void BuildPlayer();
    void ShowInterpreter(const StateNode& in);
    void ShowPlayer(const StateNode& player);
    QLabel* AddField(const QString& name);
    void SetField(const QString& name, const QString& text);

    View _view;
    QLabel* _status = nullptr;
    QLabel* _headline = nullptr;
    QWidget* _body = nullptr;
    QFormLayout* _form = nullptr;
    std::vector<std::pair<QString, QLabel*>> _fields;
    std::vector<QLabel*> _chips;  ///< Interpreter: one per state of States()
    QString _lit;
    QProgressBar* _params = nullptr;
    QProgressBar* _replies = nullptr;
    QProgressBar* _upload = nullptr;
    QTableWidget* _channels = nullptr;
    QTableWidget* _modules = nullptr;
    QTableWidget* _samples = nullptr;
};
